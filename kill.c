// SPDX-License-Identifier: MIT

/* Kill the most memory-hungy process */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h> // for PATH_MAX
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "globals.h"
#include "kill.h"
#include "meminfo.h"
#include "msg.h"

#define BADNESS_PREFER 300
#define BADNESS_AVOID -300

static int isnumeric(char* str)
{
    int i = 0;

    // Empty string is not numeric
    if (str[0] == 0)
        return 0;

    while (1) {
        if (str[i] == 0) // End of string
            return 1;

        if (isdigit(str[i]) == 0)
            return 0;

        i++;
    }
}

static void notify(const char* summary, const char* body)
{
    int pid = fork();
    if (pid > 0) {
        // parent
        return;
    }
    char summary2[1024] = { 0 };
    snprintf(summary2, sizeof(summary2), "string:%s", summary);
    char body2[1024] = "string:";
    if (body != NULL) {
        snprintf(body2, sizeof(body2), "string:%s", body);
    }
    // Complete command line looks like this:
    // dbus-send --system / net.nuetzlich.SystemNotifications.Notify 'string:summary text' 'string:and body text'
    execl("/usr/bin/dbus-send", "dbus-send", "--system", "/", "net.nuetzlich.SystemNotifications.Notify",
        summary2, body2, NULL);
    warn("notify: exec failed: %s\n", strerror(errno));
    exit(1);
}

/*
 * Send the selected signal to "pid" and wait for the process to exit
 * (max 10 seconds)
 */
int kill_wait(const poll_loop_args_t* args, pid_t pid, int sig)
{
    if (args->dryrun && sig != 0) {
        warn("dryrun, not actually sending any signal\n");
        return 0;
    }
    meminfo_t m = { 0 };
    const unsigned poll_ms = 100;
    int res = kill(pid, sig);
    if (res != 0) {
        return res;
    }
    /* signal 0 does not kill the process. Don't wait for it to exit */
    if (sig == 0) {
        return 0;
    }
    for (unsigned i = 0; i < 100; i++) {
        float secs = (float)(i * poll_ms) / 1000;
        // We have sent SIGTERM but now have dropped below SIGKILL limits.
        // Escalate to SIGKILL.
        if (sig != SIGKILL) {
            m = parse_meminfo();
            print_mem_stats(debug, m);
            if (m.MemAvailablePercent <= args->mem_kill_percent && m.SwapFreePercent <= args->swap_kill_percent) {
                sig = SIGKILL;
                res = kill(pid, sig);
                // kill first, print after
                warn("escalating to SIGKILL after %.1f seconds\n", secs);
                if (res != 0) {
                    return res;
                }
            }
        } else if (enable_debug) {
            m = parse_meminfo();
            print_mem_stats(printf, m);
        }
        if (!is_alive(pid)) {
            warn("process exited after %.1f seconds\n", secs);
            return 0;
        }
        struct timespec req = { .tv_sec = (time_t)(poll_ms / 1000), .tv_nsec = (poll_ms % 1000) * 1000000 };
        nanosleep(&req, NULL);
    }
    errno = ETIME;
    return -1;
}

/*
 * Find the process with the largest oom_score.
 */
procinfo_t find_largest_process(const poll_loop_args_t* args)
{
    DIR* procdir = opendir("/proc");
    if (procdir == NULL) {
        fatal(5, "%s: could not open /proc: %s", __func__, strerror(errno));
    }

    struct timespec t0 = { 0 }, t1 = { 0 };
    if (enable_debug) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
    }

    procinfo_t victim = { 0 };
    while (1) {
        errno = 0;
        struct dirent* d = readdir(procdir);
        if (d == NULL) {
            if (errno != 0)
                warn("%s: readdir error: %s", __func__, strerror(errno));
            break;
        }

        // proc contains lots of directories not related to processes,
        // skip them
        if (!isnumeric(d->d_name))
            continue;

        procinfo_t cur = {
            .pid = (int)strtol(d->d_name, NULL, 10),
            .uid = -1,
            .badness = -1,
            .VmRSSkiB = -1,
        };

        int skip_reason = 0;

        if (cur.pid <= 1)
            // Let's not kill init.
            skip_reason = -1;

        {
            int res = get_oom_score(cur.pid);
            if (res < 0) {
                warn("pid %5d: error reading oom_score: %s\n", cur.pid, strerror(-res));
                continue;
            }
            cur.badness = res;
        }
        if (args->ignore_oom_score_adj) {
            int oom_score_adj = 0;
            int res = get_oom_score_adj(cur.pid, &oom_score_adj);
            if (res < 0) {
                warn("pid %5d: error reading oom_score_adj: %s\n", cur.pid, strerror(-res));
                continue;
            }
            if (oom_score_adj > 0) {
                cur.badness -= oom_score_adj;
            }
        }
        {
            int res = get_comm(cur.pid, cur.name, sizeof(cur.name));
            if (res < 0) {
                warn("pid %5d: error reading process name: %s\n", cur.pid, strerror(-res));
                continue;
            }
        }
        {
            int res = get_uid(cur.pid);
            if (res < 0) {
                warn("pid %5d: error reading uid: %s\n", cur.pid, strerror(-res));
                continue;
            }
            cur.uid = res;
        }
        {
            long long res = get_vm_rss_kib(cur.pid);
            if (res < 0) {
                warn("pid %5d: error reading rss: %s\n", cur.pid, strerror((int)-res));
                continue;
            }
            cur.VmRSSkiB = res;
        }

        if ((args->prefer_regex || args->avoid_regex)) {
            if (args->prefer_regex && regexec(args->prefer_regex, cur.name, (size_t)0, NULL, 0) == 0) {
                cur.badness += BADNESS_PREFER;
            }
            if (args->avoid_regex && regexec(args->avoid_regex, cur.name, (size_t)0, NULL, 0) == 0) {
                cur.badness += BADNESS_AVOID;
            }
        }

        int oom_score_adj = 0;
        {
            int res = get_oom_score_adj(cur.pid, &oom_score_adj);
            if (res < 0) {
                warn("pid %5d: error reading oom_score_adj: %s\n", cur.pid, strerror(-res));
                continue;
            }
        }

        if (skip_reason == 0 && cur.badness < victim.badness) {
            skip_reason = 1;
        }
        if (skip_reason == 0 && cur.VmRSSkiB == 0) {
            // Kernel threads have zero rss
            skip_reason = 2;
        }
        if (skip_reason == 0 && cur.badness == victim.badness && cur.VmRSSkiB <= victim.VmRSSkiB) {
            skip_reason = 3;
        }
        if (skip_reason == 0 && oom_score_adj == -1000) {
            // Skip processes with oom_score_adj = -1000, like the
            // kernel oom killer would.
            skip_reason = 4;
        }

        if (enable_debug) {
            debug("pid %5d: badness %3d vm_rss %7llu uid %4d oom_score_adj %4d \"%s\" debug %d\n",
                 cur.pid, cur.badness, cur.VmRSSkiB, cur.uid, oom_score_adj, cur.name, skip_reason);
        } else {
            info("pid %5d: badness %3d vm_rss %7llu uid %4d oom_score_adj %4d \"%s\"\n",
                 cur.pid, cur.badness, cur.VmRSSkiB, cur.uid, oom_score_adj, cur.name);
        }

        if (skip_reason == 0) {
            // Save new victim
            victim = cur;
        }
    } // end of while(1) loop
    closedir(procdir);

    if (enable_debug) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long delta = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
        debug("selecting victim took %ld.%03ld ms\n", delta / 1000, delta % 1000);
    }

    if (victim.pid == getpid()) {
        warn("%s: selected myself (pid %d). Do you use hidpid? See https://github.com/rfjakob/earlyoom/wiki/proc-hidepid\n",
            __func__, victim.pid);
        // zeroize victim struct
        victim = (const procinfo_t) { 0 };
    }

    return victim;
}

/*
 * Kill the victim process, wait for it to exit, send a gui notification
 * (if enabled).
 */
void kill_process(const poll_loop_args_t* args, int sig, const procinfo_t victim)
{
    if (victim.pid <= 0) {
        warn("Could not find a process to kill. Sleeping 1 second.\n");
        if (args->notify) {
            notify("earlyoom", "Error: Could not find a process to kill. Sleeping 1 second.");
        }
        sleep(1);
        return;
    }

    char* sig_name = "?";
    if (sig == SIGTERM) {
        sig_name = "SIGTERM";
    } else if (sig == SIGKILL) {
        sig_name = "SIGKILL";
    } else if (sig == 0) {
        sig_name = "0 (no-op signal)";
    }
    // sig == 0 is used as a self-test during startup. Don't notifiy the user.
    if (sig != 0 || enable_debug) {
        warn("sending %s to process %d uid %d \"%s\": badness %d, VmRSS %lld MiB\n",
            sig_name, victim.pid, victim.uid, victim.name, victim.badness, victim.VmRSSkiB / 1024);
    }

    int res = kill_wait(args, victim.pid, sig);
    int saved_errno = errno;

    // Send the GUI notification AFTER killing a process. This makes it more likely
    // that there is enough memory to spawn the notification helper.
    if (sig != 0) {
        char notif_args[PATH_MAX + 1000];
        snprintf(notif_args, sizeof(notif_args),
            "Low memory! Killing process %d %s", victim.pid, victim.name);
        if (args->notify) {
            notify("earlyoom", notif_args);
        }
    }

    if (sig == 0) {
        return;
    }

    if (res != 0) {
        warn("kill failed: %s\n", strerror(saved_errno));
        if (args->notify) {
            notify("earlyoom", "Error: Failed to kill process");
        }
        // Killing the process may have failed because we are not running as root.
        // In that case, trying again in 100ms will just yield the same error.
        // Throttle ourselves to not spam the log.
        if (saved_errno == EPERM) {
            warn("sleeping 1 second\n");
            sleep(1);
        }
    }
}

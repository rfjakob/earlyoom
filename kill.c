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

static void maybe_notify(char* notif_command, char* notif_args)
{
    if (!notif_command)
        return;

    char notif[PATH_MAX + 2000];
    snprintf(notif, sizeof(notif), "%s %s", notif_command, notif_args);
    if (system(notif) != 0)
        warn("system('%s') failed: %s\n", notif, strerror(errno));
}

/*
 * Send the selected signal to "pid" and wait for the process to exit
 * (max 10 seconds)
 */
int kill_wait(const poll_loop_args_t args, pid_t pid, int sig)
{
    if (dry_run) {
        return 0;
    }
    meminfo_t m = { 0 };
    const int poll_ms = 100;
    int res = kill(pid, sig);
    if (res != 0) {
        return res;
    }
    /* signal 0 does not kill the process. Don't wait for it to exit */
    if (sig == 0) {
        return 0;
    }
    for (int i = 0; i < 100; i++) {
        float secs = ((float)i) * poll_ms / 1000;
        // We have sent SIGTERM but now have dropped below SIGKILL limits.
        // Escalate to SIGKILL.
        if (sig != SIGKILL) {
            m = parse_meminfo();
            print_mem_stats(debug, m);
            if (m.MemAvailablePercent <= args.mem_kill_percent && m.SwapFreePercent <= args.swap_kill_percent) {
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
        usleep(poll_ms * 1000);
    }
    errno = ETIME;
    return -1;
}

/*
 * Find the process with the largest oom_score and kill it.
 */
void kill_largest_process(const poll_loop_args_t args, int sig)
{
    struct procinfo victim = { 0 };
    struct timespec t0 = { 0 }, t1 = { 0 };

    if (enable_debug) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
    }

    DIR* procdir = opendir("/proc");
    if (procdir == NULL) {
        fatal(5, "Could not open /proc: %s", strerror(errno));
    }

    while (1) {
        errno = 0;
        struct dirent* d = readdir(procdir);
        if (d == NULL) {
            if (errno != 0)
                warn("userspace_kill: readdir error: %s", strerror(errno));
            break;
        }

        // proc contains lots of directories not related to processes,
        // skip them
        if (!isnumeric(d->d_name))
            continue;

        struct procinfo cur = {
            .pid = strtoul(d->d_name, NULL, 10),
            .uid = -1,
            .badness = -1,
            .VmRSSkiB = -1,
        };

        if (cur.pid <= 1)
            // Let's not kill init.
            continue;

        debug("pid %5d:", cur.pid);

        {
            int res = get_oom_score(cur.pid);
            if (res < 0) {
                debug(" error reading oom_score: %s\n", strerror(-res));
                continue;
            }
            cur.badness = res;
        }
        if (args.ignore_oom_score_adj) {
            int oom_score_adj = 0;
            int res = get_oom_score_adj(cur.pid, &oom_score_adj);
            if (res < 0) {
                debug(" error reading oom_score_adj: %s\n", strerror(-res));
                continue;
            }
            if (oom_score_adj > 0) {
                cur.badness -= oom_score_adj;
            }
        }

        if ((args.prefer_regex || args.avoid_regex)) {
            int res = get_comm(cur.pid, cur.name, sizeof(cur.name));
            if (res < 0) {
                debug(" error reading process name: %s\n", strerror(-res));
                continue;
            }
            if (args.prefer_regex && regexec(args.prefer_regex, cur.name, (size_t)0, NULL, 0) == 0) {
                cur.badness += BADNESS_PREFER;
            }
            if (args.avoid_regex && regexec(args.avoid_regex, cur.name, (size_t)0, NULL, 0) == 0) {
                cur.badness += BADNESS_AVOID;
            }
        }

        debug(" badness %3d", cur.badness);

        if (cur.badness < victim.badness) {
            // skip "type 1", encoded as 1 space
            debug(" \n");
            continue;
        }

        {
            long res = get_vm_rss_kib(cur.pid);
            if (res < 0) {
                debug(" error reading rss: %s\n", strerror(-res));
                continue;
            }
            cur.VmRSSkiB = res;
        }
        debug(" vm_rss %7lu", cur.VmRSSkiB);
        if (cur.VmRSSkiB == 0) {
            // Kernel threads have zero rss
            // skip "type 2", encoded as 2 spaces
            debug("  \n");
            continue;
        }
        if (cur.badness == victim.badness && cur.VmRSSkiB <= victim.VmRSSkiB) {
            // skip "type 3", encoded as 3 spaces
            debug("   \n");
            continue;
        }

        // Fill out remaining fields
        if (strlen(cur.name) == 0) {
            int res = get_comm(cur.pid, cur.name, sizeof(cur.name));
            if (res < 0) {
                debug(" error reading process name: %s\n", strerror(-res));
                continue;
            }
        }
        {
            int res = get_uid(cur.pid);
            if (res < 0) {
                debug(" error reading uid: %s\n", strerror(-res));
                continue;
            }
            cur.uid = res;
        }

        // Save new victim
        victim = cur;
        debug(" uid %4d \"%s\" <--- new victim\n", victim.uid, victim.name);

    } // end of while(1) loop
    closedir(procdir);

    if (victim.pid <= 0) {
        warn("Could not find a process to kill. Sleeping 1 second.\n");
        maybe_notify(args.notif_command,
            "-i dialog-error 'earlyoom' 'Error: Could not find a process to kill. Sleeping 1 second.'");
        sleep(1);
        return;
    }

    if (enable_debug) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long delta = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
        debug("selecting victim took %ld.%03ld ms\n", delta / 1000, delta % 1000);
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
        warn("sending %s to process %d uid %d \"%s\": badness %d, VmRSS %lu MiB\n",
            sig_name, victim.pid, victim.uid, victim.name, victim.badness, victim.VmRSSkiB / 1024);
    }

    int res = kill_wait(args, victim.pid, sig);
    int saved_errno = errno;

    // Send the GUI notification AFTER killing a process. This makes it more likely
    // that there is enough memory to spawn the notification helper.
    if (sig != 0) {
        char notif_args[PATH_MAX + 1000];
        // maybe_notify() calls system(). We must sanitize the strings we pass.
        sanitize(victim.name);
        snprintf(notif_args, sizeof(notif_args),
            "-i dialog-warning 'earlyoom' 'Low memory! Killing process %d %s'", victim.pid, victim.name);
        maybe_notify(args.notif_command, notif_args);
    }

    if (sig == 0) {
        return;
    }

    if (res != 0) {
        warn("kill failed: %s\n", strerror(saved_errno));
        maybe_notify(args.notif_command,
            "-i dialog-error 'earlyoom' 'Error: Failed to kill process'");
        // Killing the process may have failed because we are not running as root.
        // In that case, trying again in 100ms will just yield the same error.
        // Throttle ourselves to not spam the log.
        if (saved_errno == EPERM) {
            warn("sleeping 1 second\n");
            sleep(1);
        }
    }
}

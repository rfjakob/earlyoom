// SPDX-License-Identifier: MIT

/* Kill the most memory-hungy process */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h> /* Definition of SYS_* constants */
#include <time.h>
#include <unistd.h>

#include "globals.h"
#include "kill.h"
#include "meminfo.h"
#include "msg.h"

// Processes matching "--prefer REGEX" get BADNESS_PREFER added to their badness
#define BADNESS_PREFER 300
// Processes matching "--avoid REGEX" get BADNESS_AVOID added to their badness
#define BADNESS_AVOID -300

// Buffer size for UID/GID/PID string conversion
#define UID_BUFSIZ 128
// At most 1 notification per second when --dryrun is active
#define NOTIFY_RATELIMIT 1

static bool isnumeric(char* str)
{
    int i = 0;

    // Empty string is not numeric
    if (str[0] == 0)
        return false;

    while (1) {
        if (str[i] == 0) // End of string
            return true;

        if (isdigit(str[i]) == 0)
            return false;

        i++;
    }
}

static void notify_dbus(const char* summary, const char* body)
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
    warn("%s: exec failed: %s\n", __func__, strerror(errno));
    exit(1);
}

static void notify_ext(const char* script, const procinfo_t* victim)
{
    pid_t pid1 = fork();

    if (pid1 == -1) {
        warn("notify_ext: fork() returned -1: %s\n", strerror(errno));
        return;
    } else if (pid1 != 0) {
        return;
    }

    char pid_str[UID_BUFSIZ] = { 0 };
    char uid_str[UID_BUFSIZ] = { 0 };

    snprintf(pid_str, UID_BUFSIZ, "%d", victim->pid);
    snprintf(uid_str, UID_BUFSIZ, "%d", victim->uid);

    setenv("EARLYOOM_PID", pid_str, 1);
    setenv("EARLYOOM_UID", uid_str, 1);
    setenv("EARLYOOM_NAME", victim->name, 1);
    setenv("EARLYOOM_CMDLINE", victim->cmdline, 1);

    execl(script, script, NULL);
    warn("%s: exec %s failed: %s\n", __func__, script, strerror(errno));
    exit(1);
}

static void notify_process_killed(const poll_loop_args_t* args, const procinfo_t* victim)
{
    // Dry run can cause the notify function to be called on each poll as
    // nothing is immediately done to change the situation we don't know how
    // heavy the notify script is so avoid spamming it
    if (args->dryrun) {
        static struct timespec prev_notify = { 0 };
        struct timespec cur_time = { 0 };

        int ret = clock_gettime(CLOCK_MONOTONIC, &cur_time);
        if (ret == -1) {
            warn("%s: clock_gettime failed: %s\n", __func__, strerror(errno));
            return;
        }
        // Ignores nanoseconds, but good enough here
        if (cur_time.tv_sec - prev_notify.tv_sec < NOTIFY_RATELIMIT) {
            // Too soon
            debug("%s: rate limit hit, skipping notifications this time\n", __func__);
            return;
        }
        prev_notify = cur_time;
    }

    if (args->notify) {
        char notif_args[PATH_MAX + 1000];
        snprintf(notif_args, sizeof(notif_args),
            "Low memory! Killing process %d %s", victim->pid, victim->name);
        notify_dbus("earlyoom", notif_args);
    }
    if (args->notify_ext) {
        notify_ext(args->notify_ext, victim);
    }
}

#if defined(__NR_pidfd_open) && defined(__NR_process_mrelease)
void mrelease(const pid_t pid)
{
    int pidfd = (int)syscall(__NR_pidfd_open, pid, 0);
    if (pidfd < 0) {
        // can happen if process has already exited
        debug("mrelease: pid %d: error opening pidfd: %s\n", pid, strerror(errno));
        return;
    }
    int res = (int)syscall(__NR_process_mrelease, pidfd, 0);
    if (res != 0) {
        warn("mrelease: pid=%d pidfd=%d failed: %s\n", pid, pidfd, strerror(errno));
    } else {
        debug("mrelease: pid=%d pidfd=%d success\n", pid, pidfd);
    }
}
#else
void mrelease(__attribute__((unused)) const pid_t pid)
{
    debug("mrelease: process_mrelease() and/or pidfd_open() not available\n");
}
#ifndef __NR_pidfd_open
#warning "__NR_pidfd_open is undefined, cannot use process_mrelease"
#endif
#ifndef __NR_process_mrelease
#warning "__NR_process_mrelease is undefined, cannot use process_mrelease"
#endif
#endif

/*
 * Send the selected signal to "pid" and wait for the process to exit
 * (max 10 seconds)
 */
int kill_wait(const poll_loop_args_t* args, pid_t pid, int sig)
{
    int pidfd = -1;

    if (args->dryrun && sig != 0) {
        warn("dryrun, not actually sending any signal\n");
        return 0;
    }
    const unsigned poll_ms = 100;
    if (args->kill_process_group) {
        int res = getpgid(pid);
        if (res < 0) {
            return res;
        }
        pid = -res;
        warn("killing whole process group %d (-g flag is active)\n", res);
    }

#if defined(__NR_pidfd_open) && defined(__NR_process_mrelease)
    // Open the pidfd *before* calling kill().
    // Otherwise process_mrelease() fails in 50% of cases with ESRCH.
    if (!args->kill_process_group && sig != 0) {
        pidfd = (int)syscall(__NR_pidfd_open, pid, 0);
        if (pidfd < 0) {
            warn("%s pid %d: error opening pidfd: %s\n", __func__, pid, strerror(errno));
        }
    }
#endif

    int res = kill(pid, sig);
    if (res != 0) {
        if (pidfd >= 0) {
            close(pidfd);
        }
        return res;
    }
    /* signal 0 does not kill the process. Don't wait for it to exit */
    if (sig == 0) {
        return 0;
    }

    struct timespec t0 = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &t0);

#if defined(__NR_pidfd_open) && defined(__NR_process_mrelease)
    // Call the process_mrelease() syscall to release all the memory of
    // the killed process as quickly as possible - see https://lwn.net/Articles/864184/
    // for details.
    if (pidfd >= 0) {
        int res = (int)syscall(__NR_process_mrelease, pidfd, 0);
        if (res != 0) {
            warn("%s pid=%d: process_mrelease pidfd=%d failed: %s\n", __func__, pid, pidfd, strerror(errno));
        } else {
            debug("%s pid=%d: process_mrelease pidfd=%d success\n", __func__, pid, pidfd);
        }
        close(pidfd);
    }
#endif

    for (unsigned i = 0; i < 100; i++) {
        struct timespec t1 = { 0 };
        clock_gettime(CLOCK_MONOTONIC, &t1);
        float secs = (float)(t1.tv_sec - t0.tv_sec) + (float)(t1.tv_nsec - t0.tv_nsec) / (float)1e9;

        // We have sent SIGTERM but now have dropped below SIGKILL limits.
        // Escalate to SIGKILL.
        if (sig != SIGKILL) {
            meminfo_t m = parse_meminfo();
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
            meminfo_t m = parse_meminfo();
            print_mem_stats(info, m);
        }
        if (!is_alive(pid)) {
            warn("process %d exited after %.3f seconds\n", pid, secs);
            return 0;
        }
        struct timespec req = { .tv_sec = (time_t)(poll_ms / 1000), .tv_nsec = (poll_ms % 1000) * 1000000 };
        nanosleep(&req, NULL);
    }
    errno = ETIME;
    return -1;
}

// is_larger finds out if the process with pid `cur->pid` uses more memory
// than our current `victim`.
// In the process, it fills the `cur` structure. It does so lazily, meaning
// it only fills the fields it needs to make a decision.
bool is_larger(const poll_loop_args_t* args, const procinfo_t* victim, procinfo_t* cur)
{
    if (cur->pid <= 1) {
        // Let's not kill init.
        return false;
    }

    {
        int res = get_uid(cur->pid);
        if (res < 0) {
            debug("pid %d: error reading uid: %s\n", cur->pid, strerror(-res));
            return false;
        }
        cur->uid = res;
    }
    if (cur->uid == 0 && args->ignore_root_user) {
        // Ignore processes owned by root user.
        return false;
    }

    {
        int res = get_oom_score(cur->pid);
        if (res < 0) {
            debug("pid %d: error reading oom_score: %s\n", cur->pid, strerror(-res));
            return false;
        }
        cur->badness = res;
    }

    if ((args->prefer_regex || args->avoid_regex || args->ignore_regex)) {
        int res = get_comm(cur->pid, cur->name, sizeof(cur->name));
        if (res < 0) {
            debug("pid %d: error reading process name: %s\n", cur->pid, strerror(-res));
            return false;
        }
        res = get_cmdline(cur->pid, cur->cmdline, sizeof(cur->cmdline));
        if (res < 0) {
            debug("pid %d: error reading process cmdline: %s\n", cur->pid, strerror(-res));
            return false;
        }
        if (args->prefer_regex && regexec(args->prefer_regex, cur->name, (size_t)0, NULL, 0) == 0) {
            cur->badness += BADNESS_PREFER;
        }
        if (args->avoid_regex && regexec(args->avoid_regex, cur->name, (size_t)0, NULL, 0) == 0) {
            cur->badness += BADNESS_AVOID;
        }
        if (args->ignore_regex && regexec(args->ignore_regex, cur->name, (size_t)0, NULL, 0) == 0) {
            return false;
        }
    }

    if (cur->badness < victim->badness) {
        return false;
    }

    {
        long long res = get_vm_rss_kib(cur->pid);
        if (res < 0) {
            debug("pid %d: error reading rss: %s\n", cur->pid, strerror((int)-res));
            return false;
        }
        cur->VmRSSkiB = res;
    }

    if (cur->VmRSSkiB == 0) {
        // Kernel threads have zero rss
        return false;
    }
    if (cur->badness == victim->badness && cur->VmRSSkiB <= victim->VmRSSkiB) {
        return false;
    }

    // Skip processes with oom_score_adj = -1000, like the
    // kernel oom killer would.
    {
        int res = get_oom_score_adj(cur->pid, &cur->oom_score_adj);
        if (res < 0) {
            debug("pid %d: error reading oom_score_adj: %s\n", cur->pid, strerror(-res));
            return false;
        }
        if (cur->oom_score_adj == -1000) {
            return false;
        }
    }

    // Looks like we have a new victim. Fill out remaining fields
    if (strlen(cur->name) == 0) {
        int res = get_comm(cur->pid, cur->name, sizeof(cur->name));
        if (res < 0) {
            debug("pid %d: error reading process name: %s\n", cur->pid, strerror(-res));
            return false;
        }
        res = get_cmdline(cur->pid, cur->cmdline, sizeof(cur->cmdline));
        if (res < 0) {
            debug("pid %d: error reading process cmdline: %s\n", cur->pid, strerror(-res));
            return false;
        }
    }
    return true;
}

// debug_print_procinfo pretty-prints the process information in `cur`.
void debug_print_procinfo(const procinfo_t* cur)
{
    if (!enable_debug) {
        return;
    }
    debug("pid %5d: badness %3d VmRSS %7lld uid %4d oom_score_adj %4d \"%s\"",
        cur->pid, cur->badness, cur->VmRSSkiB, cur->uid, cur->oom_score_adj, cur->name);
}

/*
 * Find the process with the largest oom_score.
 */
procinfo_t find_largest_process(const poll_loop_args_t* args)
{
    DIR* procdir = opendir(procdir_path);
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
            .oom_score_adj = -1,
            /* omitted fields are set to zero */
        };

        bool larger = is_larger(args, &victim, &cur);

        debug_print_procinfo(&cur);

        if (larger) {
            debug(" <--- new victim\n");
            victim = cur;
        } else {
            debug("\n");
        }
    }
    closedir(procdir);

    if (enable_debug) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long delta = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
        debug("selecting victim took %ld.%03ld ms\n", delta / 1000, delta % 1000);
    }

    if (victim.pid == getpid()) {
        warn("%s: selected myself (pid %d). Do you use hidpid? See https://github.com/rfjakob/earlyoom/wiki/proc-hidepid\n",
            __func__, victim.pid);
        // zero victim struct
        victim = (const procinfo_t) { 0 };
    }

    return victim;
}

/*
 * Kill the victim process, wait for it to exit, send a gui notification
 * (if enabled).
 */
void kill_process(const poll_loop_args_t* args, int sig, const procinfo_t* victim)
{
    if (victim->pid <= 0) {
        warn("Could not find a process to kill. Sleeping 1 second.\n");
        if (args->notify) {
            notify_dbus("earlyoom", "Error: Could not find a process to kill. Sleeping 1 second.");
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
    // sig == 0 is used as a self-test during startup. Don't notify the user.
    if (sig != 0 || enable_debug) {
        warn("sending %s to process %d uid %d \"%s\": badness %d, VmRSS %lld MiB\n",
            sig_name, victim->pid, victim->uid, victim->name, victim->badness, victim->VmRSSkiB / 1024);
        warn("process %d cmdline \"%s\"\n", victim->pid, victim->cmdline);
    }

    int res = kill_wait(args, victim->pid, sig);
    int saved_errno = errno;

    // Send the GUI notification AFTER killing a process. This makes it more likely
    // that there is enough memory to spawn the notification helper.
    if (sig != 0) {
        notify_process_killed(args, victim);
    }

    if (sig == 0) {
        return;
    }

    if (res != 0) {
        warn("kill failed: %s\n", strerror(saved_errno));
        if (args->notify) {
            notify_dbus("earlyoom", "Error: Failed to kill process");
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

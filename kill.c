// SPDX-License-Identifier: MIT

/* Kill the most memory-hungy process */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h> // for PATH_MAX
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "kill.h"
#include "meminfo.h"
#include "msg.h"

#define BADNESS_PREFER 300
#define BADNESS_AVOID -300

extern int enable_debug;
extern long page_size;
void sanitize(char* s);

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
        usleep(poll_ms * 1000);
        if (!is_alive(pid)) {
            printf("process %d exited after %.1f seconds\n",
                pid, ((float)i) * poll_ms / 1000);
            return 0;
        }
        /* abort wait for SIGTERM response if we drop below the SIGKILL limits */
        if (sig != SIGKILL) {
            m = parse_meminfo();
            if (m.MemAvailablePercent <= args.mem_kill_percent && m.SwapFreePercent <= args.swap_kill_percent) {
                break;
            }
        }
    }
    errno = ETIME;
    return -1;
}

/*
 * Find the process with the largest oom_score and kill it.
 */
void kill_largest_process(const poll_loop_args_t args, int sig)
{
    struct dirent* d;
    char buf[256];
    int pid;
    int victim_pid = 0;
    int victim_badness = 0;
    unsigned long victim_vm_rss = 0;
    char victim_name[256] = { 0 };
    struct procinfo p;
    int badness;
    struct timespec t0 = { 0 }, t1 = { 0 };

    if (enable_debug) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
    }

    // main() makes sure that we are in /proc
    DIR* procdir = opendir(".");
    if (procdir == NULL) {
        fatal(5, "Could not open /proc: %s", strerror(errno));
    }

    while (1) {
        errno = 0;
        d = readdir(procdir);
        if (d == NULL) {
            if (errno != 0)
                warn("userspace_kill: readdir error: %s", strerror(errno));

            break;
        }

        // proc contains lots of directories not related to processes,
        // skip them
        if (!isnumeric(d->d_name))
            continue;

        pid = strtoul(d->d_name, NULL, 10);

        if (pid <= 1)
            // Let's not kill init.
            continue;

        p = get_process_stats(pid);

        if (p.exited == 1)
            // Process may have died in the meantime
            continue;

        if (p.VmRSSkiB == 0)
            // Skip kernel threads
            continue;

        badness = p.oom_score;
        if (args.ignore_oom_score_adj && p.oom_score_adj > 0)
            badness -= p.oom_score_adj;

        char name[256] = { 0 };
        snprintf(buf, sizeof(buf), "%d/comm", pid);
        FILE* comm = fopen(buf, "r");
        if (comm) {
            const int TASK_COMM_LEN = 16;
            int n = fread(name, 1, TASK_COMM_LEN, comm);
            // Strip trailing newline
            if (n > 1) {
                name[n - 1] = 0;
            } else {
                warn("reading %s failed: %s", buf, strerror(errno));
            }
            fclose(comm);
        } else {
            warn("could not open %s: %s", buf, strerror(errno));
        }

        if (args.prefer_regex && regexec(args.prefer_regex, name, (size_t)0, NULL, 0) == 0) {
            badness += BADNESS_PREFER;
        }
        if (args.avoid_regex && regexec(args.avoid_regex, name, (size_t)0, NULL, 0) == 0) {
            badness += BADNESS_AVOID;
        }

        if (enable_debug)
            printf("pid %5d: badness %3d vm_rss %6lu %s\n", pid, badness, p.VmRSSkiB, name);

        if (badness > victim_badness) {
            victim_pid = pid;
            victim_badness = badness;
            victim_vm_rss = p.VmRSSkiB;
            strncpy(victim_name, name, sizeof(victim_name));
            if (enable_debug)
                printf("    ^ new victim (higher badness)\n");
        } else if (badness == victim_badness && p.VmRSSkiB > victim_vm_rss) {
            victim_pid = pid;
            victim_vm_rss = p.VmRSSkiB;
            strncpy(victim_name, name, sizeof(victim_name));
            if (enable_debug)
                printf("    ^ new victim (higher vm_rss)\n");
        }
    } // end of while(1) loop
    closedir(procdir);

    if (victim_pid == 0) {
        warn("Could not find a process to kill. Sleeping 1 second.\n");
        maybe_notify(args.notif_command,
            "-i dialog-error 'earlyoom' 'Error: Could not find a process to kill. Sleeping 1 second.'");
        sleep(1);
        return;
    }

    char* sig_name = "?";
    if (sig == SIGTERM) {
        sig_name = "SIGTERM";
    } else if (sig == SIGKILL) {
        sig_name = "SIGKILL";
    }
    // sig == 0 is used as a self-test during startup. Don't notifiy the user.
    if (sig != 0) {
        warn("sending %s to process %d \"%s\": badness %d, VmRSS %lu MiB\n",
            sig_name, victim_pid, victim_name, victim_badness, victim_vm_rss / 1024);
    }

    int res = kill_wait(args, victim_pid, sig);
    int saved_errno = errno;

    if (enable_debug) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long delta = (t1.tv_sec - t0.tv_sec) * 1000000 + (t1.tv_nsec - t0.tv_nsec) / 1000;
        printf("selecting victim and sending signal took %ld.%03ld ms\n", delta / 1000, delta % 1000);
    }

    // Send the GUI notification AFTER killing a process. This makes it more likely
    // that there is enough memory to spawn the notification helper.
    if (sig != 0) {
        char notif_args[PATH_MAX + 1000];
        // maybe_notify() calls system(). We must sanitize the strings we pass.
        sanitize(victim_name);
        snprintf(notif_args, sizeof(notif_args),
            "-i dialog-warning 'earlyoom' 'Low memory! Killing process %d %s'", victim_pid, victim_name);
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

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
#include <unistd.h>

#include "kill.h"

#define BADNESS_PREFER 300
#define BADNESS_AVOID -300

extern int enable_debug;

struct procinfo {
    int oom_score;
    int oom_score_adj;
    unsigned long vm_rss;
    int exited;
};

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

    char notif[600];
    snprintf(notif, sizeof(notif), "%s %s", notif_command, notif_args);
    if (system(notif) != 0)
        fprintf(stderr, "system(%s) failed: %d: %s\n", notif, errno, strerror(errno));
}

const char* const fopen_msg = "fopen %s failed: %s\n";

/* Read /proc/pid/{oom_score, oom_score_adj, statm}
 * Caller must ensure that we are already in the /proc/ directory
 */
static struct procinfo get_process_stats(int pid)
{
    char buf[256];
    FILE* f;
    struct procinfo p = { 0, 0, 0, 0 };

    // Read /proc/[pid]/oom_score
    snprintf(buf, sizeof(buf), "%d/oom_score", pid);
    f = fopen(buf, "r");
    if (f == NULL) {
        printf(fopen_msg, buf, strerror(errno));
        p.exited = 1;
        return p;
    }
    if (fscanf(f, "%d", &(p.oom_score)) < 1)
        fprintf(stderr, "fscanf() oom_score failed: %d: %s\n", errno, strerror(errno));
    fclose(f);

    // Read /proc/[pid]/oom_score_adj
    snprintf(buf, sizeof(buf), "%d/oom_score_adj", pid);
    f = fopen(buf, "r");
    if (f == NULL) {
        printf(fopen_msg, buf, strerror(errno));
        p.exited = 1;
        return p;
    }
    if (fscanf(f, "%d", &(p.oom_score_adj)) < 1)
        fprintf(stderr, "fscanf() oom_score_adj failed: %d: %s\n", errno, strerror(errno));

    fclose(f);

    // Read VmRss from /proc/[pid]/statm
    snprintf(buf, sizeof(buf), "%d/statm", pid);
    f = fopen(buf, "r");
    if (f == NULL) {
        printf(fopen_msg, buf, strerror(errno));
        p.exited = 1;
        return p;
    }
    if (fscanf(f, "%*u %lu", &(p.vm_rss)) < 1)
        fprintf(stderr, "fscanf() vm_rss failed: %d: %s\n", errno, strerror(errno));

    fclose(f);

    return p;
}

/*
 * Find the process with the largest oom_score and kill it.
 * See trigger_kernel_oom() for the reason why this is done in userspace.
 */
static void userspace_kill(DIR* procdir, int sig, int ignore_oom_score_adj,
    char* notif_command, regex_t* prefer_regex, regex_t* avoid_regex)
{
    struct dirent* d;
    char buf[256];
    int pid;
    int victim_pid = 0;
    int victim_badness = 0;
    unsigned long victim_vm_rss = 0;
    char name[PATH_MAX];
    char victim_name[PATH_MAX] = { 0 };
    struct procinfo p;
    int badness;

    rewinddir(procdir);
    while (1) {
        errno = 0;
        d = readdir(procdir);
        if (d == NULL) {
            if (errno != 0)
                perror("userspace_kill: readdir error");

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

        if (p.vm_rss == 0)
            // Skip kernel threads
            continue;

        badness = p.oom_score;
        if (ignore_oom_score_adj && p.oom_score_adj > 0)
            badness -= p.oom_score_adj;

        name[0] = 0;
        snprintf(buf, sizeof(buf), "%d/stat", pid);
        FILE* stat = fopen(buf, "r");
        if (stat) {
            if (fscanf(stat, "%*d (%[^)]s", name) < 1)
                fprintf(stderr, "fscanf() stat name failed: %d: %s\n", errno, strerror(errno));

            fclose(stat);
        } else {
            perror("could not read process name");
        }

        if (prefer_regex && regexec(prefer_regex, name, (size_t)0, NULL, 0) == 0) {
            badness += BADNESS_PREFER;
        }
        if (avoid_regex && regexec(avoid_regex, name, (size_t)0, NULL, 0) == 0) {
            badness += BADNESS_AVOID;
        }

        if (enable_debug)
            printf("pid %5d: badness %3d vm_rss %6lu %s\n", pid, badness, p.vm_rss, name);

        if (badness > victim_badness) {
            victim_pid = pid;
            victim_badness = badness;
            victim_vm_rss = p.vm_rss;
            strncpy(victim_name, name, sizeof(victim_name) - 1);
            if (enable_debug)
                printf("    ^ new victim (higher badness)\n");
        } else if (badness == victim_badness && p.vm_rss > victim_vm_rss) {
            victim_pid = pid;
            victim_vm_rss = p.vm_rss;
            strncpy(victim_name, name, sizeof(victim_name) - 1);
            if (enable_debug)
                printf("    ^ new victim (higher vm_rss)\n");
        }
    } // end of while(1) loop

    if (victim_pid == 0) {
        fprintf(stderr, "Error: Could not find a process to kill. Sleeping 1 second.\n");
        maybe_notify(notif_command, "-i dialog-error 'earlyoom' 'Error: Could not find a process to kill. Sleeping 1 second.'");
        sleep(1);
        return;
    }

    if (sig != 0) {
        fprintf(stderr, "Killing process: %s, pid: %d, badness: %d, VmRSS: %lu MiB\n",
            victim_name, victim_pid, victim_badness, victim_vm_rss / 1024);
    }

    int res = kill(victim_pid, sig);

    // Send the GUI notification AFTER killing a process. This makes it more likely
    // that there is enough memory to spawn the notification helper.
    if (sig != 0) {
        char notif_args[PATH_MAX + 1000];
        snprintf(notif_args, sizeof(notif_args), "-i dialog-warning 'earlyoom' 'Killing process %d %s'", victim_pid, victim_name);
        maybe_notify(notif_command, notif_args);
    }

    // Killing the process may have failed because we are not running as root.
    // In that case, trying again in 100ms will just yield the same error.
    // Throttle ourselves to not spam the log.
    if (res != 0) {
        perror("userspace_kill: kill() failed, sleeping 1 second");
        maybe_notify(notif_command, "-i dialog-error 'earlyoom' 'Error: Failed to kill process. Sleeping 1 second.'");
        sleep(1);
    }
}

/*
 * Invoke the kernel oom killer by writing "f" into /proc/sysrq-trigger
 *
 * This approach has a few problems:
 * 1) It is disallowed by default (even for root) on Fedora 20.
 *    You have to first write "1" into /proc/sys/kernel/sysrq to enable the "f"
 *    trigger.
 * 2) The Chrome web browser assigns a penalty of 300 onto its own tab renderer
 *    processes. On an 8GB RAM machine, this means 2400MB, and will lead to every
 *    tab being killed before the actual memory hog
 *    See https://code.google.com/p/chromium/issues/detail?id=333617 for more info
 * 3) It is broken in 4.0.5 - see
 *    https://github.com/rfjakob/earlyoom/commit/f7e2cedce8e9605c688d0c6d7dc26b7e81817f02
 * Because of these issues, kill_by_rss() is used instead by default.
 */
void trigger_kernel_oom(int sig, char* notif_command)
{
    FILE* trig_fd;
    trig_fd = fopen("sysrq-trigger", "w");
    if (trig_fd == NULL) {
        perror("Could not open /proc/sysrq-trigger");
        exit(7);
    }
    if (sig == 9) {
        fprintf(stderr, "Invoking oom killer: ");
        maybe_notify(notif_command, "-i dialog-warning 'earlyoom' 'Invoking OOM killer'");

        if (fprintf(trig_fd, "f\n") != 2) {
            perror("failed");
            maybe_notify(notif_command, "-i dialog-error 'earlyoom' 'Error: Failed to invoke OOM killer'");
        } else
            fprintf(stderr, "done\n");
    }
    fclose(trig_fd);
}

void handle_oom(DIR* procdir, int sig, int kernel_oom_killer, int ignore_oom_score_adj,
    char* notif_command, regex_t* prefer_regex, regex_t* avoid_regex)
{
    if (kernel_oom_killer)
        trigger_kernel_oom(sig, notif_command);
    else
        userspace_kill(procdir, sig, ignore_oom_score_adj, notif_command, prefer_regex, avoid_regex);
}

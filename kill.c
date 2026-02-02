// SPDX-License-Identifier: MIT

/* Kill the most memory-hungy process */

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h> /* Definition of SYS_* constants */
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "globals.h"
#include "kill.h"
#include "meminfo.h"
#include "msg.h"

// Processes matching "--prefer REGEX" get OOM_SCORE_PREFER added to their oom_score
#define OOM_SCORE_PREFER 300
// Processes matching "--avoid REGEX" get OOM_SCORE_AVOID added to their oom_score
#define OOM_SCORE_AVOID -300

// Processes matching "--prefer REGEX" get VMRSS_PREFER added to their VmRSSkiB
#define VMRSS_PREFER 3145728
// Processes matching "--avoid REGEX" get VMRSS_AVOID added to their VmRSSkiB
#define VMRSS_AVOID -3145728

// Buffer size for UID/GID/PID string conversion
#define UID_BUFSIZ 128
// At most 1 notification per second when --dryrun is active
#define NOTIFY_RATELIMIT 1

// Wait for at most this amount of milliseconds when invoking the pre-hook (otherwise
// when the pre-hook gets spawned, it doesn't have time to act)
#define PREHOOK_STARTUP_SLEEP_MS 200

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

#ifndef SYS_pidfd_open
// It's 434 on all architectures except Alpha. Sorry, Alpha users.
#warning SYS_pidfd_open is not defined. Assuming 434.
#define SYS_pidfd_open 434
#endif

static int pidfd_open(pid_t pid, unsigned int flags)
{
    return (int)syscall(SYS_pidfd_open, pid, flags);
}

#ifndef SYS_process_mrelease
// It's 448 on all architectures except Alpha. Sorry, Alpha users.
#warning SYS_process_mrelease is not defined. Assuming 448.
#define SYS_process_mrelease 448
#endif

static int process_mrelease(int pidfd, unsigned int flags)
{
    return (int)syscall(SYS_process_mrelease, pidfd, flags);
}

static void notify_spawn_subprocess(const char* script, char* const argv[], const procinfo_t* victim, int timeout_ms)
{
    // Prevent our SIGCHLD handler from reaping
    // children before we can
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &set, NULL);

    pid_t pid1 = fork();

    if (pid1 == -1) {
        warn("%s: fork error: %s\n", __func__, strerror(errno));
        goto out_unblock;
    } else if (pid1 != 0) {
        // we are the parent
        int pidfd = pidfd_open(pid1, 0);
        if (pidfd == -1) {
            warn("%s: pidfd_open error: %s\n", __func__, strerror(errno));
            goto out_unblock;
        }
        struct pollfd pollfd = { 0 };
        pollfd.fd = pidfd;
        pollfd.events = POLLIN;

        int ready = poll(&pollfd, 1, timeout_ms);
        if (ready == -1) {
            warn("%s: poll error: %s\n", __func__, strerror(errno));
        } else if (ready == 0) {
            // child is still running. Ignore unless a timeout was set.
            if (timeout_ms > 0)
                warn("%s: timeout waiting for process %s\n", __func__, script);
        } else {
            // child has exited
            int ret = 0, wstatus = 0;
            ret = waitpid(pid1, &wstatus, WNOHANG);
            if (ret <= 0) {
                warn("%s: waitpid error: %s\n", __func__, strerror(errno));
            } else {
                if (WIFEXITED(wstatus)) {
                    debug("%s: child exited, status=%d\n", __func__, WEXITSTATUS(wstatus));
                } else if (WIFSIGNALED(wstatus)) {
                    debug("%s: child killed by signal %d\n", __func__, WTERMSIG(wstatus));
                } else {
                    warn("%s: unknown child status 0x%x\n", __func__, wstatus);
                }
            }
        }
        close(pidfd);
    out_unblock:
        sigprocmask(SIG_UNBLOCK, &set, NULL);
        return;
    }

    // we are the child
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    if (victim) {
        char pid_str[UID_BUFSIZ] = { 0 };
        char uid_str[UID_BUFSIZ] = { 0 };

        snprintf(pid_str, UID_BUFSIZ, "%d", victim->pid);
        snprintf(uid_str, UID_BUFSIZ, "%d", victim->uid);

        setenv("EARLYOOM_PID", pid_str, 1);
        setenv("EARLYOOM_UID", uid_str, 1);
        setenv("EARLYOOM_NAME", victim->name, 1);
        setenv("EARLYOOM_CMDLINE", victim->cmdline, 1);
    }

    debug("%s: exec %s\n", __func__, script);
    execv(script, argv);
    warn("%s: exec %s failed: %s\n", __func__, script, strerror(errno));
    exit(1);
}

// "-n" option
static void notify_dbus(const char* body)
{
    char body2[1024] = "string:";
    if (body != NULL) {
        snprintf(body2, sizeof(body2), "string:%s", body);
    }

    // Complete command line looks like this:
    // dbus-send --system / net.nuetzlich.SystemNotifications.Notify 'string:earlyoom' 'string:and body text'
    char* const argv[] = {
        "dbus-send",
        "--system",
        "/",
        "net.nuetzlich.SystemNotifications.Notify",
        "string:earlyoom",
        body2,
        NULL
    };
    const char* dbus_send_path = "/usr/bin/dbus-send";
    notify_spawn_subprocess(dbus_send_path, argv, NULL, 0);
}

// "-N" option
static void notify_ext(char* const script, const procinfo_t* victim)
{
    char* const argv[] = {
        script,
        NULL
    };
    notify_spawn_subprocess(script, argv, victim, 0);
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
        notify_dbus(notif_args);
    }
    if (args->notify_ext) {
        notify_ext(args->notify_ext, victim);
    }
}

// "-P" option
static void kill_process_prehook(const poll_loop_args_t* args, const procinfo_t* victim)
{
    char* const argv[] = {
        args->kill_process_prehook,
        NULL,
    };
    notify_spawn_subprocess(args->kill_process_prehook, argv, victim, PREHOOK_STARTUP_SLEEP_MS);
}

/*
 * Trigger the kernel OOM killer via /proc/sysrq-trigger
 * This requires Linux v5.17+ to work correctly. OOM sysrq will always kill a process
 * The victim passed to hooks is dummy. OOM killer will select its own victim
 *
 * See https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=f530243a172d2ff03f88d0056f838928d6445c6d
 * for details about this feature.
 */
int trigger_kernel_oom(const poll_loop_args_t* args)
{
    const char* sysrq_path = "/proc/sysrq-trigger";
    const char trigger = 'f';

    // Check if we have permission to write to /proc/sysrq-trigger
    // This check is also done in dryrun mode to warn about permission issues early
    if (access(sysrq_path, W_OK) != 0) {
        warn("%s: no permission to write to %s: %s\n", __func__, sysrq_path, strerror(errno));
        return -1;
    }

    if (args->dryrun) {
        return 0;
    }

    int fd = open(sysrq_path, O_WRONLY);
    if (fd < 0) {
        warn("%s: failed to open %s: %s\n", __func__, sysrq_path, strerror(errno));
        return -1;
    }

    ssize_t written = write(fd, &trigger, 1);
    if (written != 1) {
        warn("%s: failed to write to %s: %s\n", __func__, sysrq_path, strerror(errno));
        close(fd);
        return -1;
    }

    close(fd);
    info("%s: successfully triggered kernel OOM killer\n", __func__);

    if (args->notify) {
        notify_dbus("Low memory! Triggered kernel OOM killer");
    }

    return 0;
}

// kill_release kills a process and calls process_mrelease to
// release the memory as quickly as possible.
//
// See https://lwn.net/Articles/864184/ for details on process_mrelease.
int kill_release(const pid_t pid, const int pidfd, const int sig)
{
    int res = kill(pid, sig);
    if (res != 0) {
        return res;
    }
    // Can't do process_mrelease without a pidfd.
    if (pidfd < 0) {
        return 0;
    }

    res = process_mrelease(pidfd, 0);
    if (res != 0) {
        warn("%s: pid=%d: process_mrelease pidfd=%d failed: %s\n", __func__, pid, pidfd, strerror(errno));
    } else {
        info("%s: pid=%d: process_mrelease pidfd=%d success\n", __func__, pid, pidfd);
    }

    // Return 0 regardless of process_mrelease outcome
    return 0;
}

/*
 * Send the selected signal to "pid" and wait for the process to exit
 * (max kill_wait_timeout_secs)
 */
int kill_wait(const poll_loop_args_t* args, pid_t pid, int sig)
{
    const unsigned poll_ms = 100;
    // Calculate max_poll_iterations safely to avoid overflow
    // With poll_ms = 100, we get 10 iterations per second
    // Max safe value: UINT_MAX / 10 = ~429496729 seconds (~13.6 years)
    unsigned max_poll_iterations;
    if ((unsigned)args->kill_wait_timeout_secs > UINT_MAX / 10u) {
        // Cap at maximum to avoid overflow (should never happen in practice)
        max_poll_iterations = UINT_MAX;
    } else {
        max_poll_iterations = (unsigned)args->kill_wait_timeout_secs * 10u;
    }
    int pidfd = -1;

    if (args->dryrun && sig != 0) {
        warn("dryrun, not actually sending any signal\n");
        return 0;
    }

    if (args->kill_process_group) {
        int res = getpgid(pid);
        if (res < 0) {
            return res;
        }
        pid = -res;
        warn("killing whole process group %d (-g flag is active)\n", res);
    }

    // Open the pidfd *before* calling kill().
    if (!args->kill_process_group && sig != 0) {
        pidfd = pidfd_open(pid, 0);
        if (pidfd < 0) {
            warn("%s pid %d: error opening pidfd: %s\n", __func__, pid, strerror(errno));
        }
    }

    int res = kill_release(pid, pidfd, sig);
    if (res != 0) {
        goto out_close;
    }

    /* signal 0 does not kill the process. Don't wait for it to exit */
    if (sig == 0) {
        goto out_close;
    }

    struct timespec t0 = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (unsigned i = 0; i < max_poll_iterations; i++) {
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
                warn("escalating to SIGKILL after %.3f seconds\n", secs);
                res = kill_release(pid, pidfd, sig);
                if (res != 0) {
                    goto out_close;
                }
            }
        } else if (enable_debug) {
            meminfo_t m = parse_meminfo();
            print_mem_stats(info, m);
        }
        if (!is_alive(pid)) {
            warn("process %d exited after %.3f seconds\n", pid, secs);
            goto out_close;
        }
        struct timespec req = { .tv_sec = (time_t)(poll_ms / 1000), .tv_nsec = (poll_ms % 1000) * 1000000 };
        nanosleep(&req, NULL);
    }

    res = -1;
    errno = ETIME;
    warn("process %d did not exit\n", pid);

out_close:
    if (pidfd >= 0) {
        int saved_errno = errno;
        if (close(pidfd)) {
            warn("%s pid %d: error closing pidfd %d: %s\n", __func__, pid, pidfd, strerror(errno));
        }
        errno = saved_errno;
    }
    return res;
}

// is_larger finds out if the process with pid `cur->pid` uses more memory
// than our current `victim`.
// In the process, it fills the `cur` structure. It does so lazily, meaning
// it only fills the fields it needs to make a decision.
bool is_larger(const poll_loop_args_t* args, const procinfo_t* victim, procinfo_t* cur)
{
    if (cur->pid <= 2) {
        // Let's not kill init or kthreadd.
        return false;
    }

    // Ignore processes owned by root user?
    if (args->ignore_root_user) {
        int res = get_uid(cur->pid);
        if (res < 0) {
            debug("%s: pid %d: error reading uid: %s\n", __func__, cur->pid, strerror(-res));
            return false;
        }
        cur->uid = res;

        if (cur->uid == 0) {
            return false;
        }
    }

    {
        bool res = parse_proc_pid_stat(&cur->stat, cur->pid);
        if (!res) {
            debug("%s: pid %d: error reading stat\n", __func__, cur->pid);
            return false;
        }
        const long page_size = sysconf(_SC_PAGESIZE);
        cur->VmRSSkiB = cur->stat.rss * page_size / 1024;
    }

    // A pid is a kernel thread if it's pid or ppid is 2.
    // At least that's what procs does:
    // https://github.com/warmchang/procps/blob/d173f5d6db746e3f252a6182aa1906a292fc200f/library/readproc.c#L1325
    //
    // The check for pid == 2 has already been done at the top.
    if (cur->stat.ppid == 2) {
        return false;
    }

    {
        int res = get_oom_score(cur->pid);
        if (res < 0) {
            debug("%s: pid %d: error reading oom_score: %s\n", __func__, cur->pid, strerror(-res));
            return false;
        }
        cur->oom_score = res;
    }

    if ((args->prefer_regex || args->avoid_regex || args->ignore_regex)) {
        int res = get_comm(cur->pid, cur->name, sizeof(cur->name));
        if (res < 0) {
            debug("%s: pid %d: error reading process name: %s\n", __func__, cur->pid, strerror(-res));
            return false;
        }
        if (args->prefer_regex && regexec(args->prefer_regex, cur->name, (size_t)0, NULL, 0) == 0) {
            if (args->sort_by_rss) {
                cur->VmRSSkiB += VMRSS_PREFER;
            } else {
                cur->oom_score += OOM_SCORE_PREFER;
            }
        }
        if (args->avoid_regex && regexec(args->avoid_regex, cur->name, (size_t)0, NULL, 0) == 0) {
            if (args->sort_by_rss) {
                cur->VmRSSkiB += VMRSS_AVOID;
            } else {
                cur->oom_score += OOM_SCORE_AVOID;
            }
        }
        if (args->ignore_regex && regexec(args->ignore_regex, cur->name, (size_t)0, NULL, 0) == 0) {
            return false;
        }
    }

    // find process with the largest rss
    if (args->sort_by_rss) {
        // Case 1: neither victim nor cur have rss=0 (zombie main thread).
        // This is the usual case.
        if (cur->VmRSSkiB > 0 && victim->VmRSSkiB > 0) {
            if (cur->VmRSSkiB < victim->VmRSSkiB) {
                return false;
            }
            if (cur->VmRSSkiB == victim->VmRSSkiB && cur->oom_score <= victim->oom_score) {
                return false;
            }
        }
        // Case 2: one (or both) have rss=0 (zombie main thread)
        else {
            if (cur->VmRSSkiB == 0) {
                // only print the warning when the zombie is first seen, i.e. as "cur"
                get_comm(cur->pid, cur->name, sizeof(cur->name));
                warn("%s: pid %d \"%s\": rss=0 but oom_score=%d. Zombie main thread? Using oom_score for this process.\n",
                    __func__, cur->pid, cur->name, cur->oom_score);
            }
            if (cur->oom_score < victim->oom_score) {
                return false;
            }
            if (cur->oom_score == victim->oom_score && cur->VmRSSkiB <= victim->VmRSSkiB) {
                return false;
            }
        }
    } else {
        /* find process with the largest oom_score */
        if (cur->oom_score < victim->oom_score) {
            return false;
        }

        if (cur->oom_score == victim->oom_score && cur->VmRSSkiB <= victim->VmRSSkiB) {
            return false;
        }
    }

    // Skip processes with oom_score_adj = -1000, like the
    // kernel oom killer would.
    {
        int res = get_oom_score_adj(cur->pid, &cur->oom_score_adj);
        if (res < 0) {
            debug("%s: pid %d: error reading oom_score_adj: %s\n", __func__, cur->pid, strerror(-res));
            return false;
        }
        if (cur->oom_score_adj == -1000) {
            return false;
        }
    }
    return true;
}

// Fill the fields in `cur` that are not required for the kill decision.
// Used to log details about the selected process.
void fill_informative_fields(procinfo_t* cur)
{
    if (strlen(cur->name) == 0) {
        int res = get_comm(cur->pid, cur->name, sizeof(cur->name));
        if (res < 0) {
            debug("%s: pid %d: error reading process name: %s\n", __func__, cur->pid, strerror(-res));
        }
    }
    if (strlen(cur->cmdline) == 0) {
        int res = get_cmdline(cur->pid, cur->cmdline, sizeof(cur->cmdline));
        if (res < 0) {
            debug("%s: pid %d: error reading process cmdline: %s\n", __func__, cur->pid, strerror(-res));
        }
    }
    if (cur->uid == PROCINFO_FIELD_NOT_SET) {
        int res = get_uid(cur->pid);
        if (res < 0) {
            debug("%s: pid %d: error reading uid: %s\n", __func__, cur->pid, strerror(-res));
        } else {
            cur->uid = res;
        }
    }
}

// debug_print_procinfo pretty-prints the process information in `cur`.
void debug_print_procinfo(procinfo_t* cur)
{
    if (!enable_debug) {
        return;
    }
    fill_informative_fields(cur);
    debug("%5d %9d %7lld %5d %13d \"%s\"",
        cur->pid, cur->oom_score, cur->VmRSSkiB, cur->uid, cur->oom_score_adj, cur->name);
}

void debug_print_procinfo_header()
{
    debug("  PID OOM_SCORE  RSSkiB   UID OOM_SCORE_ADJ  COMM\n");
}

/*
 * Find the process with the largest oom_score or rss(when flag --sort-by-rss is set).
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

    debug_print_procinfo_header();

    const procinfo_t empty_procinfo = {
        .pid = PROCINFO_FIELD_NOT_SET,
        .uid = PROCINFO_FIELD_NOT_SET,
        .oom_score = PROCINFO_FIELD_NOT_SET,
        .oom_score_adj = PROCINFO_FIELD_NOT_SET,
        .VmRSSkiB = PROCINFO_FIELD_NOT_SET,
        /* omitted fields are set to zero */
    };

    procinfo_t victim = empty_procinfo;
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

        procinfo_t cur = empty_procinfo;
        cur.pid = (int)strtol(d->d_name, NULL, 10);

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

    if (victim.pid >= 0) {
        // We will pretty-print the victim later, so get all the info.
        fill_informative_fields(&victim);
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
            notify_dbus("Error: Could not find a process to kill. Sleeping 1 second.");
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
        warn("sending %s to process %d uid %d \"%s\": oom_score %d, oom_score_adj %d, VmRSS %lld MiB, cmdline \"%s\"\n",
            sig_name, victim->pid, victim->uid, victim->name, victim->oom_score, victim->oom_score_adj, victim->VmRSSkiB / 1024,
            victim->cmdline);
    }

    // Invoke program BEFORE killing a process. There is a small risk that there
    // is not enough memory to spawn it, warn; and a brief period of waiting to
    // let the program be able to start and do something meaningful.
    if (sig != 0 && args->kill_process_prehook) {
        debug("going to invoke program before killing: %s\n", args->kill_process_prehook);
        kill_process_prehook(args, victim);
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
            notify_dbus("Error: Failed to kill process");
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

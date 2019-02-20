// SPDX-License-Identifier: MIT

/* Check available memory and swap in a loop and start killing
 * processes if they get too low */

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include "kill.h"
#include "meminfo.h"
#include "msg.h"

/* Don't fail compilation if the user has an old glibc that
 * does not define MCL_ONFAULT. The kernel may still be recent
 * enough to support the flag.
 */
#ifndef MCL_ONFAULT
#define MCL_ONFAULT 4
#endif

/* Arbitrary identifiers for long options that do not have a short
 * version */
enum {
    LONG_OPT_PREFER = 513,
    LONG_OPT_AVOID,
};

static int set_oom_score_adj(int);
static void print_mem_stats(bool lowmem, const meminfo_t m);
static void poll_loop(const poll_loop_args_t args);

int enable_debug = 0;
long page_size = 0;

int main(int argc, char* argv[])
{
    poll_loop_args_t args = {
        .mem_term_percent = 10,
        .swap_term_percent = 10,
        .mem_kill_percent = 5,
        .swap_kill_percent = 5,
        .report_interval_ms = 1000,
        /* omitted fields are set to zero */
    };
    int set_my_priority = 0;
    char* prefer_cmds = NULL;
    char* avoid_cmds = NULL;
    regex_t _prefer_regex;
    regex_t _avoid_regex;
    page_size = sysconf(_SC_PAGESIZE);

    /* request line buffering for stdout - otherwise the output
     * may lag behind stderr */
    setlinebuf(stdout);

    fprintf(stderr, "earlyoom " VERSION "\n");

    if (chdir("/proc") != 0) {
        fatal(4, "Could not cd to /proc: %s", strerror(errno));
    }

    args.procdir = opendir(".");
    if (args.procdir == NULL) {
        fatal(5, "Could not open /proc: %s", strerror(errno));
    }

    meminfo_t m = parse_meminfo();

    int c;
    const char* short_opt = "m:s:M:S:kinN:dvr:ph";
    struct option long_opt[] = {
        { "prefer", required_argument, NULL, LONG_OPT_PREFER },
        { "avoid", required_argument, NULL, LONG_OPT_AVOID },
        { "help", no_argument, NULL, 'h' },
        { 0, 0, NULL, 0 } /* end-of-array marker */
    };
    bool have_m = 0, have_M = 0, have_s = 0, have_S = 0;

    while ((c = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {
        float report_interval_f = 0;
        term_kill_tuple_t tuple;

        switch (c) {
        case -1: /* no more arguments */
        case 0: /* long option toggles */
            break;
        case 'm':
            // Use 99 as upper limit. Passing "-m 100" makes no sense.
            tuple = parse_term_kill_tuple(optarg, 99);
            if (strlen(tuple.err)) {
                fatal(15, "-m: %s", tuple.err);
            }
            args.mem_term_percent = tuple.term;
            args.mem_kill_percent = tuple.kill;
            have_m = 1;
            break;
        case 's':
            // Using "-s 100" is a valid way to ignore swap usage
            tuple = parse_term_kill_tuple(optarg, 100);
            if (strlen(tuple.err)) {
                fatal(16, "-s: %s", tuple.err);
            }
            args.swap_term_percent = tuple.term;
            args.swap_kill_percent = tuple.kill;
            have_s = 1;
            break;
        case 'M':
            tuple = parse_term_kill_tuple(optarg, m.MemTotalKiB * 100 / 99);
            if (strlen(tuple.err)) {
                fatal(15, "-M: %s", tuple.err);
            }
            args.mem_term_percent = 100 * tuple.term / m.MemTotalKiB;
            args.mem_kill_percent = 100 * tuple.kill / m.MemTotalKiB;
            have_M = 1;
            break;
        case 'S':
            if (m.SwapTotalKiB == 0) {
                warn("warning: -S: total swap is zero, using default percentages\n");
                break;
            }
            tuple = parse_term_kill_tuple(optarg, m.SwapTotalKiB * 100 / 99);
            if (strlen(tuple.err)) {
                fatal(16, "-S: %s", tuple.err);
            }
            args.swap_term_percent = 100 * tuple.term / m.SwapTotalKiB;
            args.swap_kill_percent = 100 * tuple.kill / m.SwapTotalKiB;
            have_S = 1;
            break;
        case 'k':
            fprintf(stderr, "Option -k is ignored since earlyoom v1.2\n");
            break;
        case 'i':
            args.ignore_oom_score_adj = 1;
            fprintf(stderr, "Ignoring oom_score_adj\n");
            break;
        case 'n':
            args.notif_command = "notify-send";
            fprintf(stderr, "Notifying using '%s'\n", args.notif_command);
            break;
        case 'N':
            args.notif_command = optarg;
            fprintf(stderr, "Notifying using '%s'\n", args.notif_command);
            break;
        case 'd':
            enable_debug = 1;
            break;
        case 'v':
            // The version has already been printed above
            exit(0);
        case 'r':
            report_interval_f = strtof(optarg, NULL);
            if (report_interval_f < 0) {
                fatal(14, "-r: invalid interval '%s'\n", optarg);
            }
            args.report_interval_ms = report_interval_f * 1000;
            break;
        case 'p':
            set_my_priority = 1;
            break;
        case LONG_OPT_PREFER:
            prefer_cmds = optarg;
            break;
        case LONG_OPT_AVOID:
            avoid_cmds = optarg;
            break;
        case 'h':
            fprintf(stderr,
                "Usage: earlyoom [OPTION]...\n"
                "\n"
                "  -m PERCENT[,KILL_PERCENT] set available memory minimum to PERCENT of total (default 10 %%).\n"
                "                            earlyoom sends SIGTERM once below PERCENT, then SIGKILL once below\n"
                "                            KILL_PERCENT (default PERCENT/2).\n"
                "  -s PERCENT[,KILL_PERCENT] set free swap minimum to PERCENT of total (default 10 %%).\n"
                "                            Note: both memory and swap must be below minimum for earlyoom to act.\n"
                "  -M SIZE[,KILL_SIZE]       set available memory minimum to SIZE KiB\n"
                "  -S SIZE[,KILL_SIZE]       set free swap minimum to SIZE KiB\n"
                "  -i                        user-space oom killer should ignore positive oom_score_adj values\n"
                "  -n                        enable notifications using \"notify-send\"\n"
                "  -N COMMAND                enable notifications using COMMAND\n"
                "  -d                        enable debugging messages\n"
                "  -v                        print version information and exit\n"
                "  -r INTERVAL               memory report interval in seconds (default 1), set to 0 to\n"
                "                            disable completely\n"
                "  -p                        set niceness of earlyoom to -20 and oom_score_adj to -1000\n"
                "  --prefer REGEX            prefer killing processes matching REGEX\n"
                "  --avoid REGEX             avoid killing processes matching REGEX\n"
                "  -h, --help                this help text\n");
            exit(0);
        case '?':
            fprintf(stderr, "Try 'earlyoom --help' for more information.\n");
            exit(13);
        }
    } /* while getopt */

    if (optind < argc) {
        fatal(13, "extra argument not understood: '%s'\n", argv[optind]);
    }
    if (have_m && have_M) {
        fatal(2, "can't use both -m and -M\n");
    }
    if (have_s && have_S) {
        fatal(2, "can't use both -s and -S\n");
    }
    if (prefer_cmds) {
        args.prefer_regex = &_prefer_regex;
        if (regcomp(args.prefer_regex, prefer_cmds, REG_EXTENDED | REG_NOSUB) != 0) {
            fatal(6, "could not compile regexp '%s'\n", prefer_cmds);
        }
        fprintf(stderr, "Prefering to kill process names that match regex '%s'\n", prefer_cmds);
    }
    if (avoid_cmds) {
        args.avoid_regex = &_avoid_regex;
        if (regcomp(args.avoid_regex, avoid_cmds, REG_EXTENDED | REG_NOSUB) != 0) {
            fatal(6, "could not compile regexp '%s'\n", avoid_cmds);
        }
        fprintf(stderr, "Avoiding to kill process names that match regex '%s'\n", avoid_cmds);
    }
    if (set_my_priority) {
        bool fail = 0;
        if (setpriority(PRIO_PROCESS, 0, -20) != 0) {
            warn("Could not set priority: %s. Continuing anyway\n", strerror(errno));
            fail = 1;
        }
        int ret = set_oom_score_adj(-1000);
        if (ret != 0) {
            warn("Could not set oom_score_adj: %s. Continuing anyway\n", strerror(ret));
            fail = 1;
        }
        if (!fail) {
            fprintf(stderr, "Priority was raised successfully\n");
        }
    }

    // Print memory limits
    fprintf(stderr, "mem total: %4d MiB, swap total: %4d MiB\n",
        m.MemTotalMiB, m.SwapTotalMiB);
    fprintf(stderr, "Sending SIGTERM when mem <= %2d %% and swap <= %2d %%,\n",
        args.mem_term_percent, args.swap_term_percent);
    fprintf(stderr, "        SIGKILL when mem <= %2d %% and swap <= %2d %%\n",
        args.mem_kill_percent, args.swap_kill_percent);

    /* Dry-run oom kill to make sure stack grows to maximum size before
     * calling mlockall()
     */
    userspace_kill(args, 0);

    int err = mlockall(MCL_CURRENT | MCL_FUTURE | MCL_ONFAULT);
    // kernels older than 4.4 don't support MCL_ONFAULT. Retry without it.
    if (err != 0) {
        err = mlockall(MCL_CURRENT | MCL_FUTURE);
    }
    if (err != 0) {
        perror("Could not lock memory - continuing anyway");
    }

    // Jump into main poll loop
    poll_loop(args);
    return 0;
}

/* Print a status line like
 *   mem avail: 5259 MiB (67 %), swap free: 0 MiB (0 %)"
 * to the fd passed in out_fd.
 */
static void print_mem_stats(bool lowmem, const meminfo_t m)
{
    int (*out_func)(const char* fmt, ...) = &printf;
    if (lowmem) {
        out_func = &warn;
    }
    out_func("mem avail: %4d of %4d MiB (%2d %%), swap free: %4d of %4d MiB (%2d %%)\n",
        m.MemAvailableMiB,
        m.MemTotalMiB,
        m.MemAvailablePercent,
        m.SwapFreeMiB,
        m.SwapTotalMiB,
        m.SwapFreePercent);
}

// Returns errno (success = 0)
static int set_oom_score_adj(int oom_score_adj)
{
    char buf[256];
    pid_t pid = getpid();

    snprintf(buf, sizeof(buf), "%d/oom_score_adj", pid);
    FILE* f = fopen(buf, "w");
    if (f == NULL) {
        return -1;
    }

    // fprintf returns a negative error code on failure
    int ret1 = fprintf(f, "%d", oom_score_adj);
    // fclose returns a non-zero value on failure and errno contains the error code
    int ret2 = fclose(f);

    if (ret1 < 0) {
        return -ret1;
    }
    if (ret2) {
        return errno;
    }
    return 0;
}

/* Calculate the time we should sleep based upon how far away from the memory and swap
 * limits we are (headroom). Returns a millisecond value between 100 and 1000 (inclusive).
 * The idea is simple: if memory and swap can only fill up so fast, we know how long we can sleep
 * without risking to miss a low memory event.
 */
static int sleep_time_ms(const poll_loop_args_t* args, const meminfo_t* m)
{
    // Maximum expected memory/swap fill rate. In kiB per millisecond ==~ MiB per second.
    const int mem_fill_rate = 6000; // 6000MiB/s seen with "stress -m 4 --vm-bytes 4G"
    const int swap_fill_rate = 800; //  800MiB/s seen with membomb on ZRAM
    // Clamp calculated value to this range (milliseconds)
    const int min_sleep = 100;
    const int max_sleep = 1000;

    int mem_headroom_kib = (m->MemAvailablePercent - args->mem_term_percent) * 10 * m->MemTotalMiB;
    if (mem_headroom_kib < 0) {
        mem_headroom_kib = 0;
    }
    int swap_headroom_kib = (m->SwapFreePercent - args->swap_term_percent) * 10 * m->SwapTotalMiB;
    if (swap_headroom_kib < 0) {
        swap_headroom_kib = 0;
    }
    int ms = mem_headroom_kib / mem_fill_rate + swap_headroom_kib / swap_fill_rate;
    if (ms < min_sleep) {
        return min_sleep;
    }
    if (ms > max_sleep) {
        return max_sleep;
    }
    return ms;
}

static void poll_loop(const poll_loop_args_t args)
{
    meminfo_t m = { 0 };
    int report_countdown_ms = 0;
    // extra time to sleep after a kill
    const int cooldown_ms = 200;

    while (1) {
        int sig = 0;
        m = parse_meminfo();
        if (m.MemAvailablePercent <= args.mem_kill_percent && m.SwapFreePercent <= args.swap_kill_percent) {
            warn("Low memory! At or below SIGKILL limits (mem: %d %%, swap: %d %%)\n",
                args.mem_kill_percent, args.swap_kill_percent);
            sig = SIGKILL;
        } else if (m.MemAvailablePercent <= args.mem_term_percent && m.SwapFreePercent <= args.swap_term_percent) {
            warn("Low Memory! At or below SIGTERM limits (mem: %d %%, swap: %d %%)\n",
                args.mem_term_percent, args.swap_term_percent);
            sig = SIGTERM;
        }
        if (sig) {
            print_mem_stats(1, m);
            userspace_kill(args, sig);
            // With swap enabled, the kernel seems to need more than 100ms to free the memory
            // of the killed process. This means that earlyoom would immediately kill another
            // process. Sleep a little extra to give the kernel time to free the memory.
            // (Yes, this will sleep even if the kill has failed. Does no harm and keeps the
            // code simple.)
            if (m.SwapTotalMiB > 0) {
                usleep(cooldown_ms * 1000);
                report_countdown_ms -= cooldown_ms;
            }
        } else if (args.report_interval_ms && report_countdown_ms <= 0) {
            print_mem_stats(0, m);
            report_countdown_ms = args.report_interval_ms;
        }
        int sleep_ms = sleep_time_ms(&args, &m);
        if (enable_debug) {
            printf("adaptive sleep time: %d ms\n", sleep_ms);
        }
        usleep(sleep_ms * 1000);
        report_countdown_ms -= sleep_ms;
    }
}

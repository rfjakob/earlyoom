// SPDX-License-Identifier: MIT

/* Check available memory and swap in a loop and start killing
 * processes if they get too low */

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include "kill.h"
#include "meminfo.h"

/* Arbitrary identifiers for long options that do not have a short
 * version */
enum {
    LONG_OPT_PREFER=513,
    LONG_OPT_AVOID,
    LONG_OPT_HELP,
};

int set_oom_score_adj(int);
void print_mem_stats(FILE*, const struct meminfo);

int enable_debug = 0;

int main(int argc, char* argv[])
{
    int kernel_oom_killer = 0;
    unsigned long oom_cnt = 0;
    /* If the available memory goes below this percentage, we start killing
     * processes. 10 is a good start. */
    int mem_min_percent = 0, swap_min_percent = 0;
    long mem_min = 0, swap_min = 0; /* Same thing in KiB */
    int ignore_oom_score_adj = 0;
    char* notif_command = NULL;
    int report_interval = 1;
    int set_my_priority = 0;
    char* prefer_cmds = NULL;
    char* avoid_cmds = NULL;
    regex_t _prefer_regex;
    regex_t _avoid_regex;
    regex_t* prefer_regex = NULL;
    regex_t* avoid_regex = NULL;

    /* request line buffering for stdout - otherwise the output
     * may lag behind stderr */
    setlinebuf(stdout);

    fprintf(stderr, "earlyoom " VERSION "\n");

    if (chdir("/proc") != 0) {
        perror("Could not cd to /proc");
        exit(4);
    }

    DIR* procdir = opendir(".");
    if (procdir == NULL) {
        perror("Could not open /proc");
        exit(5);
    }

    int c;
    const char* short_opt = "m:s:M:S:kinN:dvr:ph";
    struct option long_opt[] = {
        { "prefer", required_argument, NULL, LONG_OPT_PREFER },
        { "avoid", required_argument, NULL, LONG_OPT_AVOID },
        { "help", no_argument, NULL, LONG_OPT_HELP },
        { 0, 0, NULL, 0 }
    };

    while ((c = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {
        switch (c) {
        case -1: /* no more arguments */
        case 0: /* long option toggles */
            break;
        case 'm':
            mem_min_percent = strtol(optarg, NULL, 10);
            if (mem_min_percent <= 0) {
                fprintf(stderr, "-m: Invalid percentage\n");
                exit(15);
            }
            break;
        case 's':
            swap_min_percent = strtol(optarg, NULL, 10);
            if (swap_min_percent <= 0 || swap_min_percent > 100) {
                fprintf(stderr, "-s: Invalid percentage\n");
                exit(16);
            }
            break;
        case 'M':
            mem_min = strtol(optarg, NULL, 10);
            if (mem_min <= 0) {
                fprintf(stderr, "-M: Invalid KiB value\n");
                exit(15);
            }
            break;
        case 'S':
            swap_min = strtol(optarg, NULL, 10);
            if (swap_min <= 0) {
                fprintf(stderr, "-S: Invalid KiB value\n");
                exit(16);
            }
            break;
        case 'k':
            kernel_oom_killer = 1;
            fprintf(stderr, "Using kernel oom killer\n");
            break;
        case 'i':
            ignore_oom_score_adj = 1;
            break;
        case 'n':
            notif_command = "notify-send";
            break;
        case 'N':
            notif_command = optarg;
            break;
        case 'd':
            enable_debug = 1;
            break;
        case 'v':
            // The version has already been printed above
            exit(0);
        case 'r':
            report_interval = strtol(optarg, NULL, 10);
            if (report_interval < 0) {
                fprintf(stderr, "-r: Invalid interval\n");
                exit(14);
            }
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
        case LONG_OPT_HELP:
            fprintf(stderr,
                "Usage: earlyoom [OPTION]...\n"
                "\n"
                "  -m PERCENT       set available memory minimum to PERCENT of total (default 10 %%)\n"
                "  -s PERCENT       set free swap minimum to PERCENT of total (default 10 %%)\n"
                "  -M SIZE          set available memory minimum to SIZE KiB\n"
                "  -S SIZE          set free swap minimum to SIZE KiB\n"
                "  -k               use kernel oom killer instead of own user-space implementation\n"
                "  -i               user-space oom killer should ignore positive oom_score_adj values\n"
                "  -n               enable notifications using \"notify-send\"\n"
                "  -N COMMAND       enable notifications using COMMAND\n"
                "  -d               enable debugging messages\n"
                "  -v               print version information and exit\n"
                "  -r INTERVAL      memory report interval in seconds (default 1), set to 0 to\n"
                "                   disable completely\n"
                "  -p               set niceness of earlyoom to -20 and oom_score_adj to -1000\n"
                "  --prefer REGEX   prefer killing processes matching REGEX\n"
                "  --avoid REGEX    avoid killing processes matching REGEX\n"
                "  -h, --help       this help text\n");
            exit(0);
        case '?':
            fprintf(stderr, "Try 'earlyoom --help' for more information.\n");
            exit(13);
        }
    }

    if (mem_min_percent && mem_min) {
        fprintf(stderr, "Can't use -m with -M\n");
        exit(2);
    }

    if (swap_min_percent && swap_min) {
        fprintf(stderr, "Can't use -s with -S\n");
        exit(2);
    }

    if (kernel_oom_killer && ignore_oom_score_adj) {
        fprintf(stderr, "Kernel oom killer does not support -i\n");
        exit(2);
    }

    if (kernel_oom_killer && (prefer_cmds || avoid_cmds)) {
        fprintf(stderr, "Kernel oom killer does not support --prefer/--avoid\n");
        exit(2);
    }

    if (prefer_cmds) {
        prefer_regex = &_prefer_regex;
        if (regcomp(prefer_regex, prefer_cmds, REG_EXTENDED | REG_NOSUB) != 0) {
            fprintf(stderr, "Could not compile regexp: %s\n", prefer_cmds);
            exit(6);
        }
    }

    if (avoid_cmds) {
        avoid_regex = &_avoid_regex;
        if (regcomp(avoid_regex, avoid_cmds, REG_EXTENDED | REG_NOSUB) != 0) {
            fprintf(stderr, "Could not compile regexp: %s\n", avoid_cmds);
            exit(6);
        }
    }

    struct meminfo m = parse_meminfo();

    if (mem_min) {
        mem_min_percent = 100 * mem_min / m.MemTotal;
    } else {
        if (!mem_min_percent) {
            mem_min_percent = 10;
        }
        mem_min = m.MemTotal * mem_min_percent / 100;
    }

    if (swap_min) {
        swap_min_percent = 100 * swap_min / m.SwapTotal;
    } else {
        if (!swap_min_percent) {
            swap_min_percent = 10;
        }
        swap_min = m.SwapTotal * swap_min_percent / 100;
    }

    fprintf(stderr, "mem total: %lu MiB, min: %lu MiB (%d %%)\n",
        m.MemTotal / 1024, mem_min / 1024, mem_min_percent);
    fprintf(stderr, "swap total: %lu MiB, min: %lu MiB (%d %%)\n",
        m.SwapTotal / 1024, swap_min / 1024, swap_min_percent);

    if (notif_command)
        fprintf(stderr, "notifications enabled using command: %s\n", notif_command);

    /* Dry-run oom kill to make sure stack grows to maximum size before
     * calling mlockall()
     */
    handle_oom(procdir, 0, kernel_oom_killer, ignore_oom_score_adj,
        notif_command, prefer_regex, avoid_regex);

    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
        perror("Could not lock memory - continuing anyway");

    if (set_my_priority) {
        if (setpriority(PRIO_PROCESS, 0, -20) != 0)
            perror("Could not set priority - continuing anyway");

        if (set_oom_score_adj(-1000) != 0)
            perror("Could not set oom_score_adj to -1000 for earlyoom process - continuing anyway");
    }

    c = 0; // loop counter
    report_interval = report_interval * 10; // loop runs at 10Hz
    while (1) {
        c++;
        m = parse_meminfo();

        if (m.MemAvailable <= mem_min && m.SwapFree <= swap_min) {
            print_mem_stats(stderr, m);
            fprintf(stderr, "Out of memory! mem min: %lu MiB, swap min: %lu MiB\n",
                mem_min / 1024, swap_min / 1024);
            handle_oom(procdir, 9, kernel_oom_killer, ignore_oom_score_adj,
                notif_command, prefer_regex, avoid_regex);
            oom_cnt++;
        } else if (report_interval > 0 && c % report_interval == 0) {
            print_mem_stats(stdout, m);
        }
        usleep(100000); // 100 ms <=> 10 Hz
    }
    return 0;
}

/* Print a status line like
 *   mem avail: 5259 MiB (67 %), swap free: 0 MiB (0 %)"
 * to the fd passed in out_fd.
 */
void print_mem_stats(FILE* out_fd, const struct meminfo m)
{
    long mem_mib = m.MemAvailable / 1024;
    long mem_percent = m.MemAvailable * 100 / m.MemTotal;
    long swap_mib = m.SwapFree / 1024;
    long swap_percent = 0;

    if (m.SwapTotal > 0)
        swap_percent = m.SwapFree * 100 / m.SwapTotal;

    fprintf(out_fd,
        "mem avail: %ld MiB (%ld %%), swap free: %ld MiB (%ld %%)\n",
        mem_mib, mem_percent, swap_mib, swap_percent);
}

int set_oom_score_adj(int oom_score_adj)
{
    char buf[256];
    pid_t pid = getpid();

    snprintf(buf, sizeof(buf), "%d/oom_score_adj", pid);
    FILE* f = fopen(buf, "w");
    if (f == NULL) {
        return -1;
    }

    fprintf(f, "%d", oom_score_adj);
    fclose(f);

    return 0;
}

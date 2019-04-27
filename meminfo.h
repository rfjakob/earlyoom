/* SPDX-License-Identifier: MIT */
#ifndef MEMINFO_H
#define MEMINFO_H

#include <stdbool.h>

typedef struct {
    // Values from /proc/meminfo, in KiB or converted to MiB.
    long MemTotalKiB;
    int MemTotalMiB;
    int MemAvailableMiB; // -1 means no data available
    int SwapTotalMiB;
    long SwapTotalKiB;
    int SwapFreeMiB;
    // Calculated percentages
    int MemAvailablePercent; // percent of total memory that is available
    int SwapFreePercent; // percent of total swap that is free
} meminfo_t;

struct procinfo {
    int oom_score;
    int oom_score_adj;
    unsigned long VmRSSkiB;
    int exited;
};

meminfo_t parse_meminfo();
bool is_alive(int pid);
struct procinfo get_process_stats(int pid);

#endif

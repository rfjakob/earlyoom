/* SPDX-License-Identifier: MIT */
#ifndef MEMINFO_H
#define MEMINFO_H

#define PATH_LEN 256

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
    int pid;
    int uid;
    int badness;
    unsigned long VmRSSkiB;
    char name[PATH_LEN];
    // remove later
    int exited;
    int oom_score;
    int oom_score_adj;
};

meminfo_t parse_meminfo();
bool is_alive(int pid);
struct procinfo get_process_stats(int pid);
void print_mem_stats(int (*out_func)(const char* fmt, ...), const meminfo_t m);
int get_oom_score(int pid);
int get_oom_score_adj(const int pid, int* out);
long get_vm_rss_kib(int pid);
int get_comm(int pid, char* out, int outlen);
int get_uid(int pid);

#endif

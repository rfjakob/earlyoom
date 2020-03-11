/* SPDX-License-Identifier: MIT */
#ifndef MEMINFO_H
#define MEMINFO_H

#define PATH_LEN 256

#include <stdbool.h>
#include <sys/types.h>

typedef struct {
    // Values from /proc/meminfo, in KiB or converted to MiB.
    long long MemTotalKiB;
    long long MemTotalMiB;
    long long MemAvailableMiB; // -1 means no data available
    long long SwapTotalMiB;
    long long SwapTotalKiB;
    long long SwapFreeMiB;
    // Calculated percentages
    int MemAvailablePercent; // percent of total memory that is available
    int SwapFreePercent; // percent of total swap that is free
} meminfo_t;

struct procinfo {
    pid_t pid;
    uid_t uid;
    int badness;
    unsigned long long VmRSSkiB;
    char name[PATH_LEN];
};

meminfo_t parse_meminfo();
bool is_alive(pid_t pid);
void print_mem_stats(int (*out_func)(const char* fmt, ...), const meminfo_t m);
int get_oom_score(pid_t pid);
int get_oom_score_adj(const pid_t pid, int* out);
long long get_vm_rss_kib(pid_t pid);
int get_comm(pid_t pid, char* out, int outlen);
int get_uid(pid_t pid);

#endif

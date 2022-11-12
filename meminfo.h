/* SPDX-License-Identifier: MIT */
#ifndef MEMINFO_H
#define MEMINFO_H

#define PATH_LEN 256

#include <stdbool.h>

typedef struct {
    // Values from /proc/meminfo, in KiB or converted to MiB.
    long long MemTotalKiB;
    long long MemAvailableKiB;
    long long SwapTotalKiB;
    long long SwapFreeKiB;
    long long AnonPagesKiB;
    // Calculated percentages
    double MemAvailablePercent; // percent of total memory that is available
    double SwapFreePercent; // percent of total swap that is free
    double AnonPagesPercent; // percent of total memory that is used by processes
} meminfo_t;

typedef struct procinfo {
    int pid;
    int uid;
    int badness;
    int oom_score_adj;
    long long VmRSSkiB;
    char name[PATH_LEN];
} procinfo_t;

meminfo_t parse_meminfo();
bool is_alive(int pid);
void print_mem_stats(int (*out_func)(const char* fmt, ...), const meminfo_t m);
int get_oom_score(int pid);
int get_oom_score_adj(const int pid, int* out);
long long get_vm_rss_kib(int pid);
int get_comm(int pid, char* out, size_t outlen);
int get_uid(int pid);

#endif

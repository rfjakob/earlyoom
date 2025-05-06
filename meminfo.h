/* SPDX-License-Identifier: MIT */
#ifndef MEMINFO_H
#define MEMINFO_H

#define PATH_LEN 256

#include "proc_pid.h"
#include <stdbool.h>

typedef struct {
    // Values from /proc/meminfo, in KiB
    long long MemTotalKiB;
    long long MemAvailableKiB;
    long long SwapTotalKiB;
    long long SwapFreeKiB;
    long long AnonPagesKiB;
    // Calculated values
    // UserMemTotalKiB = MemAvailableKiB + AnonPagesKiB.
    // Represents the total amount of memory that may be used by user processes.
    long long UserMemTotalKiB;
    // Calculated percentages
    double MemAvailablePercent; // percent of total memory that is available
    double SwapFreePercent; // percent of total swap that is free
} meminfo_t;

typedef struct procinfo {
    int pid;
    int uid;
    int oom_score;
    int oom_score_adj;
    long long VmRSSkiB;
    pid_stat_t stat;
    char name[PATH_LEN];
    char cmdline[PATH_LEN];
    char cgroup[PATH_LEN];
} procinfo_t;

// placeholder value for numeric fields
#define PROCINFO_FIELD_NOT_SET -9999

meminfo_t parse_meminfo();
bool is_alive(int pid);
void print_mem_stats(int (*out_func)(const char* fmt, ...), const meminfo_t m);
int get_oom_score(int pid);
int get_oom_score_adj(const int pid, int* out);
int get_comm(int pid, char* out, size_t outlen);
int get_uid(int pid);
int get_cmdline(int pid, char* out, size_t outlen);
int get_cgroup(int pid, char* out, size_t outlen);

#endif

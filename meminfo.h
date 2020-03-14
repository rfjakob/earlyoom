/* SPDX-License-Identifier: MIT */
#ifndef MEMINFO_H
#define MEMINFO_H

#define PATH_LEN 256

#include <stdbool.h>

typedef struct {
    long long Total; // KiB
    long long Available; // -1 means no data available
    int AvailablePercent; // percent of total memory that is available
} mem_capacity_t;

enum MEM_TYPE {
    MEM = 0,
    SWAP,
    MEM_TYPE_CNT
};

extern const char* const mem_type_name[MEM_TYPE_CNT];

typedef struct {
    // Values from /proc/meminfo, in KiB or converted to MiB.
    mem_capacity_t info[MEM_TYPE_CNT]; // Swap.Available not Swap.Available
} meminfo_t;

struct procinfo {
    int pid;
    int uid;
    int badness;
    long long VmRSSkiB;
    char name[PATH_LEN];
};

meminfo_t parse_meminfo();
bool is_alive(int pid);
void print_mem_stats(int (*out_func)(const char* fmt, ...), const meminfo_t* m);
int get_oom_score(int pid);
int get_oom_score_adj(const int pid, int* out);
long long get_vm_rss_kib(int pid);
int get_comm(int pid, char* out, size_t outlen);
int get_uid(int pid);

#endif

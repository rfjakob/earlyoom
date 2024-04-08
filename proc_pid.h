/* SPDX-License-Identifier: MIT */
#ifndef PROC_PID_H
#define PROC_PID_H

#include <stdbool.h>

typedef struct {
    char state;
    int ppid;
    long num_threads;
    long rss;
} pid_stat_t;

bool parse_proc_pid_stat_buf(pid_stat_t* out, char* buf);
bool parse_proc_pid_stat(pid_stat_t* out, int pid);

#endif

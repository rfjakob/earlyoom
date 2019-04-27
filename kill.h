/* SPDX-License-Identifier: MIT */
#ifndef KILL_H
#define KILL_H

#include <stdbool.h>

typedef struct {
    /* if the available memory AND swap goes below these percentages,
     * we start killing processes */
    int mem_term_percent;
    int mem_kill_percent;
    int swap_term_percent;
    int swap_kill_percent;
    /* ignore /proc/PID/oom_score_adj? */
    bool ignore_oom_score_adj;
    /* notifcation command to launch when killing something. NULL = no-op. */
    char* notif_command;
    /* prefer/avoid killing these processes. NULL = no-op. */
    regex_t* prefer_regex;
    regex_t* avoid_regex;
    /* memory report interval, in milliseconds */
    int report_interval_ms;
} poll_loop_args_t;

void kill_largest_process(poll_loop_args_t args, int sig);

#endif

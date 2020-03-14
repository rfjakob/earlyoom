/* SPDX-License-Identifier: MIT */
#ifndef KILL_H
#define KILL_H

#include <regex.h>
#include <stdbool.h>

typedef struct {
    /* if the available memory AND swap goes below these percentages,
     * we start killing processes */
    int mem_term_percent;
    int mem_kill_percent;
    int swap_term_percent;
    int swap_kill_percent;
    long long mem_term_size;
    long long mem_kill_size;
    long long swap_term_size;
    long long swap_kill_size;
    /* ignore /proc/PID/oom_score_adj? */
    bool ignore_oom_score_adj;
    /* notifcation command to launch when killing something. NULL = no-op. */
    char* notif_command;
    /* prefer/avoid killing these processes. NULL = no-op. */
    regex_t* prefer_regex;
    regex_t* avoid_regex;
    /* memory report interval, in milliseconds */
    int report_interval_ms;
    /* Flag --dryrun was passed */
    bool dryrun;
} poll_loop_args_t;

void kill_largest_process(poll_loop_args_t args, int sig);

#endif

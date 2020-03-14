/* SPDX-License-Identifier: MIT */
#ifndef KILL_H
#define KILL_H

#include <regex.h>
#include <stdbool.h>

typedef struct {
    /* if the available memory AND swap goes below these percentages,
     * and we also drop below the kiB values (if set),
     * we start killing processes */
    // -m x
    int mem_term_percent;
    // -m x,y
    int mem_kill_percent;
    // -s x
    int swap_term_percent;
    // -s x,y
    int swap_kill_percent;
    // -M x
    long long mem_term_kib;
    // -M x,y
    long long mem_kill_kib;
    // -S x
    long long swap_term_kib;
    // -S x,y
    long long swap_kill_kib;

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

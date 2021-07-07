/* SPDX-License-Identifier: MIT */
#ifndef KILL_H
#define KILL_H

#include <regex.h>
#include <stdbool.h>

#include "meminfo.h"

typedef struct {
    /* if the available memory AND swap goes below these percentages,
     * we start killing processes */
    double mem_term_percent;
    double mem_kill_percent;
    double swap_term_percent;
    double swap_kill_percent;
    /* send d-bus notifications? */
    bool notify;
    /* minimum time to wait before sending SIGTERM */
    int sigterm_delay_ms;
    /* kill all processes within a process group */
    bool kill_process_group;
    /* prefer/avoid killing these processes. NULL = no-op. */
    regex_t* prefer_regex;
    regex_t* avoid_regex;
    /* memory report interval, in milliseconds */
    int report_interval_ms;
    /* Flag --dryrun was passed */
    bool dryrun;
} poll_loop_args_t;

void kill_process(const poll_loop_args_t* args, int sig, procinfo_t victim);
procinfo_t find_largest_process(const poll_loop_args_t* args);

#endif

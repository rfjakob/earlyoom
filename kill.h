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
    /* Path to script for programmatic notifications after killing (or NULL) */
    char* notify_ext;
    /* Path to script/binary for to execute before killing (or NULL) */
    char* kill_process_prehook;
    /* kill all processes within a process group */
    bool kill_process_group;
    /* do not kill processes owned by root */
    bool ignore_root_user;
    /* find process with the largest rss */
    bool sort_by_rss;
    /* prefer/avoid killing these processes. NULL = no-op. */
    regex_t* prefer_regex;
    regex_t* avoid_regex;
    /* will ignore these processes. NULL = no-op. */
    regex_t* ignore_regex;
    /* memory report interval, in milliseconds */
    int report_interval_ms;
    /* Flag --dryrun was passed */
    bool dryrun;
    /* Flag --kernel-oom was passed, use kernel oom killer via /proc/sysrq-trigger */
    bool kernel_oom;
} poll_loop_args_t;

void kill_process(const poll_loop_args_t* args, int sig, const procinfo_t* victim);
procinfo_t find_largest_process(const poll_loop_args_t* args);
bool is_larger(const poll_loop_args_t* args, const procinfo_t* victim, procinfo_t* cur);
int trigger_kernel_oom(const poll_loop_args_t* args);

#endif

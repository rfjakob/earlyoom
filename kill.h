/* SPDX-License-Identifier: MIT */
#ifndef KILL_H
#define KILL_H

#include <regex.h>
#include <stdbool.h>

#include "meminfo.h"

typedef struct {
    int percent;
    long long size;
} limit_tuple_t;

enum LIMIT_TYPE {
    TERM = 0,
    KILL,
    LIMIT_TYPE_CNT
};

extern const char* const limit_type_name[LIMIT_TYPE_CNT];
extern const int limit_type_signal[LIMIT_TYPE_CNT];

typedef struct {
    /* if the available memory AND swap goes below these percentages,
     * we start killing processes */
    limit_tuple_t limits[LIMIT_TYPE_CNT][MEM_TYPE_CNT];
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

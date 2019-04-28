/* SPDX-License-Identifier: MIT */
#ifndef MSG_H
#define MSG_H

#include <stdbool.h>

void fatal(int code, char* fmt, ...);
int warn(const char* fmt, ...);

typedef struct {
    // If the conversion failed, err contains the error message.
    char err[255];
    // Parsed values.
    long term;
    long kill;
} term_kill_tuple_t;

term_kill_tuple_t parse_term_kill_tuple(char* optarg, long upper_limit);
void fix_truncated_utf8(char *str);

#endif

/* SPDX-License-Identifier: MIT */
#ifndef MSG_H
#define MSG_H

void fatal(int code, char* fmt, ...);
int warn(const char* fmt, ...);

typedef struct {
    long term;
    long kill;
} term_kill_tuple_t;

term_kill_tuple_t parse_term_kill_tuple(char* opt, char* optarg, long upper_limit, int exitcode);

#endif

// SPDX-License-Identifier: MIT

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "msg.h"

// Print message to stderr and exit with "code".
// Example: fatal(6, "could not compile regexp '%s'\n", regex_str);
void fatal(int code, char* fmt, ...)
{
    char* red = "";
    char* reset = "";
    if (isatty(fileno(stderr))) {
        red = "\033[31m";
        reset = "\033[0m";
    }
    char fmt2[100];
    snprintf(fmt2, sizeof(fmt2), "%sfatal: %s%s", red, fmt, reset);
    va_list args;
    va_start(args, fmt); // yes fmt, NOT fmt2!
    vfprintf(stderr, fmt2, args);
    va_end(args);
    exit(code);
}

// Print a yellow warning message to stderr.
int warn(const char* fmt, ...)
{
    int ret = 0;
    char* yellow = "";
    char* reset = "";
    if (isatty(fileno(stderr))) {
        yellow = "\033[33m";
        reset = "\033[0m";
    }
    char fmt2[100];
    snprintf(fmt2, sizeof(fmt2), "%s%s%s", yellow, fmt, reset);
    va_list args;
    va_start(args, fmt); // yes fmt, NOT fmt2!
    ret = vfprintf(stderr, fmt2, args);
    va_end(args);
    return ret;
}

term_kill_tuple_t parse_term_kill_tuple(char* opt, char* optarg, long upper_limit, int exitcode)
{
    term_kill_tuple_t tuple = { 0 };
    int n;

    n = sscanf(optarg, "%ld,%ld", &tuple.term, &tuple.kill);
    if (n == 0) {
        fatal(exitcode, "%s: could not parse '%s'\n", opt, optarg);
    }
    if (tuple.term == 0) {
        fatal(exitcode, "%s: zero sigterm value in '%s'\n", opt, optarg);
    }
    if (tuple.term < 0) {
        fatal(exitcode, "%s: negative sigterm value in '%s'\n", opt, optarg);
    }
    if (tuple.term > upper_limit) {
        fatal(exitcode, "%s: sigterm value in '%s' exceeds limit %ld\n",
            opt, optarg, upper_limit);
    }
    // User passed only "term" value
    if (n == 1) {
        tuple.kill = tuple.term / 2;
        return tuple;
    }
    // User passed "term,kill" values
    if (tuple.kill == 0) {
        fatal(exitcode, "%s: zero sigkill value in '%s'\n", opt, optarg);
    }
    if (tuple.kill < 0) {
        fatal(exitcode, "%s: negative sigkill value in '%s'\n", opt, optarg);
    }
    if (tuple.kill > tuple.term) {
        fatal(exitcode, "%s: sigkill value exceeds sigterm value in '%s'\n",
            opt, optarg);
    }
    return tuple;
}

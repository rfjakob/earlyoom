// SPDX-License-Identifier: MIT

#include <ctype.h> // need isdigit()
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // need strlen()
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

// Parse the "123[,456]" tuple in optarg.
term_kill_tuple_t parse_term_kill_tuple(char* optarg, long upper_limit)
{
    term_kill_tuple_t tuple = { 0 };
    int n = 0;

    // Arbitrary limit of 100 bytes to prevent snprintf truncation
    if (strlen(optarg) > 100) {
        snprintf(tuple.err, sizeof(tuple.err),
            "argument too long (%d bytes)\n", (int)strlen(optarg));
        return tuple;
    }

    for (size_t i = 0; i < strlen(optarg); i++) {
        if (isdigit(optarg[i])) {
            continue;
        }
        if (optarg[i] == ',') {
            n++;
            if (n == 1) {
                continue;
            }
            snprintf(tuple.err, sizeof(tuple.err),
                "found multiple ','\n");
            return tuple;
        }
        snprintf(tuple.err, sizeof(tuple.err),
            "found non-digit '%c'\n", optarg[i]);
        return tuple;
    }

    n = sscanf(optarg, "%ld,%ld", &tuple.term, &tuple.kill);
    if (n == 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "could not parse '%s'\n", optarg);
        return tuple;
    }
    if (tuple.term == 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "zero SIGTERM value in '%s'\n", optarg);
        return tuple;
    }
    if (tuple.term < 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "negative SIGTERM value in '%s'\n", optarg);
        return tuple;
    }
    if (tuple.term > upper_limit) {
        snprintf(tuple.err, sizeof(tuple.err),
            "SIGTERM value in '%s' exceeds limit %ld\n", optarg, upper_limit);
        return tuple;
    }
    // User passed only "term" value
    if (n == 1) {
        tuple.kill = tuple.term / 2;
        return tuple;
    }
    // User passed "term,kill" values
    if (tuple.kill == 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "zero SIGKILL value in '%s'\n", optarg);
        return tuple;
    }
    if (tuple.kill < 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "negative SIGKILL value in '%s'\n", optarg);
        return tuple;
    }
    if (tuple.kill > tuple.term) {
        snprintf(tuple.err, sizeof(tuple.err),
            "SIGKILL value exceeds SIGTERM value in '%s'\n", optarg);
        return tuple;
    }
    return tuple;
}

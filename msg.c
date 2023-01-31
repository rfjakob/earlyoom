// SPDX-License-Identifier: MIT

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // need strlen()
#include <unistd.h>

#include "globals.h"
#include "msg.h"

// color_log writes to `f`, prefixing the `color` code if `f` is a tty.
static void color_log(FILE* f, const char* color, const char* fmt, va_list vl)
{
    // Find out (and cache) if we should use color
    static int stdout_is_tty = -1;
    static int stderr_is_tty = -1;
    static int no_color = -1;
    bool is_tty = false;

    if (no_color == -1) {
        // https://no-color.org/
        if (getenv("NO_COLOR") != NULL) {
            no_color = 1;
        } else {
            no_color = 0;
        }
    }
    if (no_color == 0) {
        if (fileno(f) == fileno(stdout)) {
            if (stdout_is_tty == -1) {
                stdout_is_tty = isatty(fileno(stdout));
            }
            is_tty = stdout_is_tty;
        } else if (fileno(f) == fileno(stderr)) {
            if (stderr_is_tty == -1) {
                stderr_is_tty = isatty(fileno(stderr));
            }
            is_tty = stderr_is_tty;
        }
    }

    // fds other than stdout and stderr never get color
    const char* reset = "\033[0m";
    if (!is_tty) {
        color = "";
        reset = "";
    }

    fputs(color, f);
    vfprintf(f, fmt, vl);
    fputs(reset, f);
    // The `reset` control was not flushed out by the
    // newline as it was sent after. Manually flush
    // it now to prevent artifacts when stderr and stdout
    // mix.
    if (fmt[strlen(fmt) - 1] == '\n') {
        fflush(f);
    }
}

// Print message, prefixed with "fatal: ", to stderr and exit with "code".
// Example: fatal(6, "could not compile regexp '%s'\n", regex_str);
int fatal(int code, char* fmt, ...)
{
    const char* red = "\033[31m";
    char fmt2[MSG_LEN] = { 0 };
    snprintf(fmt2, sizeof(fmt2), "fatal: %s", fmt);
    va_list vl;
    va_start(vl, fmt);
    color_log(stderr, red, fmt2, vl);
    va_end(vl);
    exit(code);
}

// Print a yellow warning message to stderr. No "warning" prefix is added.
int warn(const char* fmt, ...)
{
    const char* yellow = "\033[33m";
    va_list vl;
    va_start(vl, fmt);
    color_log(stderr, yellow, fmt, vl);
    va_end(vl);
    return 0;
}

// Print a gray debug message to stdout. No prefix is added.
int debug(const char* fmt, ...)
{
    if (!enable_debug) {
        return 0;
    }
    const char* gray = "\033[2m";
    va_list vl;
    va_start(vl, fmt);
    color_log(stdout, gray, fmt, vl);
    va_end(vl);
    return 0;
}

// Parse a floating point value, check conversion errors and allowed range.
// Guaranteed value range: 0 <= val <= upper_limit.
// An error is indicated by storing an error message in tuple->err and returning 0.
static double parse_part(term_kill_tuple_t* tuple, const char* part, long long upper_limit)
{
    errno = 0;
    char* endptr = 0;
    double val = strtod(part, &endptr);
    if (*endptr != '\0') {
        snprintf(tuple->err, sizeof(tuple->err),
            "trailing garbage '%s'", endptr);
        return 0;
    }
    if (errno) {
        snprintf(tuple->err, sizeof(tuple->err),
            "conversion error: %s", strerror(errno));
        return 0;
    }
    if (val > (double)upper_limit) {
        snprintf(tuple->err, sizeof(tuple->err),
            "value %lf exceeds limit %lld", val, upper_limit);
        return 0;
    }
    if (val < 0) {
        snprintf(tuple->err, sizeof(tuple->err),
            "value %lf below zero", val);
        return 0;
    }
    return val;
}

// Parse the "term[,kill]" tuple in optarg, examples: "123", "123,456".
// Guaranteed value range: 0 <= term <= kill <= upper_limit.
term_kill_tuple_t parse_term_kill_tuple(const char* optarg, long long upper_limit)
{
    term_kill_tuple_t tuple = { 0 };
    // writable copy of optarg
    char buf[MSG_LEN] = { 0 };

    if (strlen(optarg) > (sizeof(buf) - 1)) {
        snprintf(tuple.err, sizeof(tuple.err),
            "argument too long (%zu bytes)", strlen(optarg));
        return tuple;
    }
    strncpy(buf, optarg, sizeof(buf) - 1);
    // Split string on "," into two parts
    char* part1 = buf;
    char* part2 = NULL;
    char* comma = strchr(buf, ',');
    if (comma) {
        // Zero-out the comma, truncates part1
        *comma = '\0';
        // part2 gets zero or more bytes after the comma
        part2 = comma + 1;
    }
    // Parse part1
    tuple.term = parse_part(&tuple, part1, upper_limit);
    if (strlen(tuple.err)) {
        return tuple;
    }
    if (part2) {
        // Parse part2
        tuple.kill = parse_part(&tuple, part2, upper_limit);
        if (strlen(tuple.err)) {
            return tuple;
        }
    } else {
        // User passed only the SIGTERM value: the SIGKILL value is calculated as
        // SIGTERM/2.
        tuple.kill = tuple.term / 2;
    }
    // Setting term < kill makes no sense
    if (tuple.term < tuple.kill) {
        warn("warning: SIGTERM value %.2lf is below SIGKILL value %.2lf, setting SIGTERM = SIGKILL = %.2lf\n",
            tuple.term, tuple.kill, tuple.kill);
        tuple.term = tuple.kill;
    }
    // Sanity checks
    if (tuple.kill == 0 && tuple.term == 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "both SIGTERM and SIGKILL values are zero");
        return tuple;
    }
    return tuple;
}

// Credit to https://gist.github.com/w-vi/67fe49106c62421992a2
// Only works for string of length 3 and up. This is good enough
// for our use case, which is fixing the 16-byte value we get
// from /proc/[pid]/comm.
//
// Tested in unit_test.go: Test_fix_truncated_utf8()
void fix_truncated_utf8(char* str)
{
    size_t len = strlen(str);
    if (len < 3) {
        return;
    }
    // We only need to look at the last three bytes
    char* b = str + len - 3;
    // Last byte is ascii
    if ((b[2] & 0x80) == 0) {
        return;
    }
    // Last byte is multi-byte sequence start
    if (b[2] & 0x40) {
        b[2] = 0;
    }
    // Truncated 3-byte sequence
    else if ((b[1] & 0xe0) == 0xe0) {
        b[1] = 0;
        // Truncated 4-byte sequence
    } else if ((b[0] & 0xf0) == 0xf0) {
        b[0] = 0;
    }
}

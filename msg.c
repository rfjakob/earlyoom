// SPDX-License-Identifier: MIT

#include <ctype.h> // need isdigit()
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // need strlen()
#include <unistd.h>

#include "globals.h"
#include "msg.h"

// color_log writed to `f`, prefixing the `color` code if `f` is a tty.
static void color_log(FILE* f, const char* color, const char* fmt, va_list vl)
{
    char* reset = "\033[0m";
    if (!isatty(fileno(stderr))) {
        color = "";
        reset = "";
    }
    fprintf(f, "%s", color);
    vfprintf(f, fmt, vl);
    fprintf(f, "%s", reset);
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
    return 0;
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
    // User passed only the SIGTERM value: the SIGKILL value is calculated as
    // SIGTERM/2.
    if (n == 1) {
        tuple.kill = tuple.term / 2;
    }
    // Would setting SIGTERM below SIGKILL ever make sense?
    if (tuple.term < tuple.kill) {
        warn("warning: SIGTERM value %ld is below SIGKILL value %ld, setting SIGTERM = SIGKILL = %ld\n",
            tuple.term, tuple.kill, tuple.kill);
        tuple.term = tuple.kill;
    }
    // Sanity checks
    if (tuple.term < 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "negative SIGTERM value in '%s'\n", optarg);
        return tuple;
    }
    if (tuple.term > upper_limit) {
        snprintf(tuple.err, sizeof(tuple.err),
            "SIGTERM value %ld exceeds limit %ld\n", tuple.term, upper_limit);
        return tuple;
    }
    if (tuple.kill < 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "negative SIGKILL value in '%s'\n", optarg);
        return tuple;
    }
    if (tuple.kill == 0 && tuple.term == 0) {
        snprintf(tuple.err, sizeof(tuple.err),
            "both SIGTERM and SIGKILL values are zero\n");
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
    int len = strlen(str);
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

/* sanitize replaces everything in string "s" that is not [a-zA-Z0-9]
 * with an underscore. The resulting string is safe to pass to a shell.
 */
void sanitize(char* s)
{
    char c;
    for (int i = 0; s[i] != 0; i++) {
        c = s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            continue;
        }
        s[i] = '_';
    }
}

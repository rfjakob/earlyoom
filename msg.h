/* SPDX-License-Identifier: MIT */
#ifndef MSG_H
#define MSG_H

#include <stdbool.h>

#define MSG_LEN 256

// printf format for percentages
#define PRIPCT "%5.2lf%%"

/* From https://gcc.gnu.org/onlinedocs/gcc-9.2.0/gcc/Common-Function-Attributes.html :
 * The format attribute specifies that a function takes printf
 * style arguments that should be type-checked against a format string.
 */
int fatal(int code, char* fmt, ...) __attribute__((noreturn, format(printf, 2, 3)));
int warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
int debug(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

typedef struct {
    // If the conversion failed, err contains the error message.
    char err[255];
    // Parsed values.
    double term;
    double kill;
} term_kill_tuple_t;

term_kill_tuple_t parse_term_kill_tuple(const char* optarg, long long upper_limit);
void fix_truncated_utf8(char* str);

#endif

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "globals.h"
#include "msg.h"
#include "proc_pid.h"

// Parse a buffer that contains the text from /proc/$pid/stat. Example:
// $ cat /proc/self/stat
// 551716 (cat) R 551087 551716 551087 34816 551716 4194304 94 0 0 0 0 0 0 0 20 0 1 0 5017160 227065856 448 18446744073709551615 94898152189952 94898152206609 140721104501216 0 0 0 0 0 0 0 0 0 17 0 0 0 0 0 0 94898152221328 94898152222824 94898185641984 140721104505828 140721104505848 140721104505848 140721104510955 0
bool parse_proc_pid_stat_buf(pid_stat_t* out, char* buf)
{
    char* closing_bracket = strrchr(buf, ')');
    if (!closing_bracket) {
        return false;
    }
    // If the string ends (i.e. has a null byte) after the closing bracket: bail out.
    if (!closing_bracket[1]) {
        return false;
    }
    // Because of the check above, there must be at least one more byte at
    // closing_bracket[2] (possibly a null byte, but sscanf will handle that).
    char* state_field = &closing_bracket[2];
    int ret = sscanf(state_field,
        "%c " // state
        "%d %*d %*d %*d %*d " // ppid, pgrp, sid, tty_nr, tty_pgrp
        "%*u %*u %*u %*u %*u " // flags, min_flt, cmin_flt, maj_flt, cmaj_flt
        "%*u %*u %*u %*u " // utime, stime, cutime, cstime
        "%*d %*d " // priority, nice
        "%ld " // num_threads
        "%*d %*d %*d" // itrealvalue, starttime, vsize
        "%ld ", // rss
        &out->state,
        &out->ppid,
        &out->num_threads,
        &out->rss);
    if (ret != 4) {
        return false;
    };
    return true;
};

// Read and parse /proc/$pid/stat. Returns true on success, false on error.
bool parse_proc_pid_stat(pid_stat_t* out, int pid)
{
    // Largest /proc/*/stat file here is 363 bytes acc. to:
    //   wc -c /proc/*/stat | sort
    // 512 seems safe given that we only need the first 20 fields.
    char buf[512] = { 0 };

    // Read /proc/$pid/stat
    snprintf(buf, sizeof(buf), "%s/%d/stat", procdir_path, pid);
    FILE* f = fopen(buf, "r");
    if (f == NULL) {
        // Process is gone - good.
        return false;
    }
    memset(buf, 0, sizeof(buf));

    // File content looks like this:
    // 10751 (cat) R 2663 10751 2663[...]
    // File may be bigger than 256 bytes, but we only need the first 20 or so.
    int len = (int)fread(buf, 1, sizeof(buf) - 1, f);
    bool read_error = ferror(f) || len == 0;
    fclose(f);
    if (read_error) {
        warn("%s: fread failed: %s\n", __func__, strerror(errno));
        return false;
    }
    // Terminate string at end of data
    buf[len] = 0;
    return parse_proc_pid_stat_buf(out, buf);
}

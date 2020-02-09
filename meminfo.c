// SPDX-License-Identifier: MIT

/* Parse /proc/meminfo
 * Returned values are in kiB */

#include <errno.h>
#include <stddef.h> // for size_t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "globals.h"
#include "meminfo.h"
#include "msg.h"

/* Parse the contents of /proc/meminfo (in buf), return value of "name"
 * (example: MemTotal) */
static long get_entry(const char* name, const char* buf)
{
    char* hit = strstr(buf, name);
    if (hit == NULL) {
        return -1;
    }

    errno = 0;
    long val = strtol(hit + strlen(name), NULL, 10);
    if (errno != 0) {
        perror("get_entry: strtol() failed");
        return -1;
    }
    return val;
}

/* Like get_entry(), but exit if the value cannot be found */
static long get_entry_fatal(const char* name, const char* buf)
{
    long val = get_entry(name, buf);
    if (val == -1) {
        fatal(104, "could not find entry '%s' in /proc/meminfo\n");
    }
    return val;
}

/* If the kernel does not provide MemAvailable (introduced in Linux 3.14),
 * approximate it using other data we can get */
static long available_guesstimate(const char* buf)
{
    long Cached = get_entry_fatal("Cached:", buf);
    long MemFree = get_entry_fatal("MemFree:", buf);
    long Buffers = get_entry_fatal("Buffers:", buf);
    long Shmem = get_entry_fatal("Shmem:", buf);

    return MemFree + Cached + Buffers - Shmem;
}

meminfo_t parse_meminfo()
{
    static FILE* fd;
    static int guesstimate_warned = 0;
    // On Linux 5.3, "wc -c /proc/meminfo" counts 1391 bytes.
    // 8192 should be enough for the foreseeable future.
    char buf[8192] = { 0 };
    meminfo_t m = { 0 };

    if (fd == NULL)
        fd = fopen("/proc/meminfo", "r");
    if (fd == NULL) {
        fatal(102, "could not open /proc/meminfo: %s\n", strerror(errno));
    }
    rewind(fd);

    size_t len = fread(buf, 1, sizeof(buf) - 1, fd);
    if (len == 0) {
        fatal(102, "could not read /proc/meminfo: %s\n", strerror(errno));
    }

    m.MemTotalKiB = get_entry_fatal("MemTotal:", buf);
    m.SwapTotalKiB = get_entry_fatal("SwapTotal:", buf);
    long SwapFree = get_entry_fatal("SwapFree:", buf);

    long MemAvailable = get_entry("MemAvailable:", buf);
    if (MemAvailable == -1) {
        MemAvailable = available_guesstimate(buf);
        if (guesstimate_warned == 0) {
            fprintf(stderr, "Warning: Your kernel does not provide MemAvailable data (needs 3.14+)\n"
                            "         Falling back to guesstimate\n");
            guesstimate_warned = 1;
        }
    }

    // Calculate percentages
    m.MemAvailablePercent = MemAvailable * 100 / m.MemTotalKiB;
    if (m.SwapTotalKiB > 0) {
        m.SwapFreePercent = SwapFree * 100 / m.SwapTotalKiB;
    } else {
        m.SwapFreePercent = 0;
    }

    // Convert kiB to MiB
    m.MemTotalMiB = m.MemTotalKiB / 1024;
    m.MemAvailableMiB = MemAvailable / 1024;
    m.SwapTotalMiB = m.SwapTotalKiB / 1024;
    m.SwapFreeMiB = SwapFree / 1024;

    return m;
}

bool is_alive(int pid)
{
    char buf[256];
    // Read /proc/[pid]/stat
    snprintf(buf, sizeof(buf), "/proc/%d/stat", pid);
    FILE* f = fopen(buf, "r");
    if (f == NULL) {
        // Process is gone - good.
        return false;
    }
    // File content looks like this:
    // 10751 (cat) R 2663 10751 2663[...]
    char state;
    int res = fscanf(f, "%*d %*s %c", &state);
    fclose(f);
    if (res < 1) {
        warn("is_alive: fscanf() failed: %s\n", strerror(errno));
        return false;
    }
    if (enable_debug)
        printf("process state: %c\n", state);
    if (state == 'Z') {
        // A zombie process does not use any memory. Consider it dead.
        return false;
    }
    return true;
}

/* Read /proc/pid/{oom_score, oom_score_adj, statm}
 * Caller must ensure that we are already in the /proc/ directory
 */
struct procinfo get_process_stats(int pid)
{
    const char* const fopen_msg = "fopen %s failed: %s\n";
    char buf[256];
    struct procinfo p = { 0 };

    // Read /proc/[pid]/oom_score
    snprintf(buf, sizeof(buf), "/proc/%d/oom_score", pid);
    FILE* f = fopen(buf, "r");
    if (f == NULL) {
        // ENOENT just means that process has already exited.
        // Not need to bug the user.
        if (errno != ENOENT) {
            printf(fopen_msg, buf, strerror(errno));
        }
        p.exited = 1;
        return p;
    }
    if (fscanf(f, "%d", &(p.oom_score)) < 1)
        warn("fscanf() oom_score failed: %s\n", strerror(errno));
    fclose(f);

    // Read /proc/[pid]/oom_score_adj
    snprintf(buf, sizeof(buf), "/proc/%d/oom_score_adj", pid);
    f = fopen(buf, "r");
    if (f == NULL) {
        printf(fopen_msg, buf, strerror(errno));
        p.exited = 1;
        return p;
    }
    if (fscanf(f, "%d", &(p.oom_score_adj)) < 1)
        warn("fscanf() oom_score_adj failed: %s\n", strerror(errno));

    fclose(f);

    // Read VmRSS from /proc/[pid]/statm (in pages)
    snprintf(buf, sizeof(buf), "/proc/%d/statm", pid);
    f = fopen(buf, "r");
    if (f == NULL) {
        printf(fopen_msg, buf, strerror(errno));
        p.exited = 1;
        return p;
    }
    if (fscanf(f, "%*u %lu", &(p.VmRSSkiB)) < 1) {
        warn("fscanf() vm_rss failed: %s\n", strerror(errno));
    }
    // Read and cache page size
    static int page_size;
    if (page_size == 0) {
        page_size = sysconf(_SC_PAGESIZE);
    }
    // Value is in pages. Convert to kiB.
    p.VmRSSkiB = p.VmRSSkiB * page_size / 1024;

    fclose(f);

    return p;
}

/* Print a status line like
 *   mem avail: 5259 MiB (67 %), swap free: 0 MiB (0 %)"
 * as an informational message to stdout (default), or
 * as a warning to stderr.
 */
void print_mem_stats(bool urgent, const meminfo_t m)
{
    int (*out_func)(const char* fmt, ...) = &printf;
    if (urgent) {
        out_func = &warn;
    }
    out_func("mem avail: %5d of %5d MiB (%2d %%), swap free: %4d of %4d MiB (%2d %%)\n",
        m.MemAvailableMiB,
        m.MemTotalMiB,
        m.MemAvailablePercent,
        m.SwapFreeMiB,
        m.SwapTotalMiB,
        m.SwapFreePercent);
}

// SPDX-License-Identifier: MIT

/* Parse /proc/meminfo
 * Returned values are in kiB */

#include <errno.h>
#include <stddef.h> // for size_t
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
    debug("process state: %c\n", state);
    if (state == 'Z') {
        // A zombie process does not use any memory. Consider it dead.
        return false;
    }
    return true;
}

/* Read /proc/[pid]/[name]
 * Returns the value or -1 on error.
 */
static int read_proc_file_integer(int pid, char* name)
{
    int out = -1;
    char path[PATH_LEN] = { 0 };
    snprintf(path, sizeof(path), "/proc/%d/%s", pid, name);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        // ENOENT just means that process has already exited.
        // Not need to bug the user.
        if (errno != ENOENT) {
            warn("fopen %s failed: %s", path, strerror(errno));
        }
        return -1;
    }
    int matches = fscanf(f, "%d", &out);
    fclose(f);
    if (matches != 1) {
        warn("fscanf() %s failed: %s\n", name, strerror(errno));
    }
    return out;
}

/* Read /proc/[pid]/oom_score.
 * Returns the value or -1 on error.
 */
int get_oom_score(int pid)
{
    return read_proc_file_integer(pid, "oom_score");
}

/* Read /proc/[pid]/oom_score_adj.
 * Returns the value or -1 on error.
 */
int get_oom_score_adj(int pid)
{
    return read_proc_file_integer(pid, "oom_score_adj");
}

/* Read /proc/[pid]/comm.
 * Returns -1 on error.
 */
int get_comm(int pid, char* out, int outlen)
{
    char path[PATH_LEN] = { 0 };
    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    int n = fread(out, 1, outlen - 1, f);
    fclose(f);
    // Strip trailing newline
    if (n > 1) {
        out[n - 1] = 0;
    }
    return 0;
}

int get_uid(int pid)
{
    char path[PATH_LEN] = { 0 };
    snprintf(path, sizeof(path), "/proc/%d", pid);
    struct stat st = { 0 };
    int res = stat(path, &st);
    if (res < 0) {
        perror("stat /proc/[pid]");
        return -1;
    }
    return st.st_uid;
}

// Read VmRSS from /proc/[pid]/statm and convert to kib
long get_vm_rss_kib(int pid)
{
    long vm_rss_kib = -1;
    char path[PATH_LEN] = { 0 };

    // Read VmRSS from /proc/[pid]/statm (in pages)
    snprintf(path, sizeof(path), "/proc/%d/statm", pid);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    int res = fscanf(f, "%*u %ld", &vm_rss_kib);
    fclose(f);
    if (res < 1) {
        warn("fscanf() vm_rss_kib failed: %s\n", strerror(errno));
        return -1;
    }

    // Read and cache page size
    static int page_size;
    if (page_size == 0) {
        page_size = sysconf(_SC_PAGESIZE);
    }

    // Convert to kiB
    vm_rss_kib = vm_rss_kib * page_size / 1024;
    return vm_rss_kib;
}

/* Read /proc/pid/{oom_score, oom_score_adj, statm}
 */
struct procinfo get_process_stats(int pid)
{
    struct procinfo p = { 0 };

    {
        int res = get_oom_score(pid);
        if (res < 0) {
            p.exited = 1;
            return p;
        }
        p.oom_score = res;
    }

    {
        int res = get_oom_score_adj(pid);
        if (res < 0) {
            p.exited = 1;
            return p;
        }
        p.oom_score_adj = res;
    }

    {
        long res = get_vm_rss_kib(pid);
        if (res < 0) {
            p.exited = 1;
            return p;
        }
        p.VmRSSkiB = res;
    }

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

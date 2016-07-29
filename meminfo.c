/* Parse /proc/meminfo
 * Returned values are in kiB */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>                     // for size_t

#include "meminfo.h"

/* Parse the contents of /proc/meminfo (in buf), return value of "*name" */
static long get_entry(const char *name, const char *buf) {
	char *hit = strstr(buf, name);
	if(hit == NULL) {
		return -1;
	}

	errno = 0;
	long val = strtol(hit + strlen(name), NULL, 10);
	if(errno != 0) {
		perror("Could not convert number");
		exit(105);
	}
	return val;
}

/* Like get_entry(), but exit if the value cannot not be found */
static long get_entry_fatal(const char *name, const char *buf) {
	long val = get_entry(name, buf);
	if(val == -1) {
		fprintf(stderr, "Could not find \"%s\"\n", name);
		exit(104);
	}
	return val;
}

/* If the kernel does not provide MemAvailable (introduced in Linux 3.14),
 * approximate it using other data we can get */
static long available_guesstimate(const char *buf) {
	long Cached = get_entry_fatal("Cached:", buf);
	long MemFree = get_entry_fatal("MemFree:", buf);
	long Buffers = get_entry_fatal("Buffers:", buf);
	long Shmem =  get_entry_fatal("Shmem:", buf);

    return MemFree + Cached + Buffers - Shmem;
}

struct meminfo parse_meminfo() {
	static FILE* fd;
	static char buf[8192];
    static int guesstimate_warned = 0;
	struct meminfo m;

	if(fd == NULL)
		fd = fopen("/proc/meminfo", "r");
	if(fd == NULL) {
		perror("Could not open /proc/meminfo");
		exit(102);
	}
	rewind(fd);

	size_t len = fread(buf, 1, sizeof(buf) - 1, fd);
	if( len == 0) {
		perror("Could not read /proc/meminfo");
		exit(103);
	}
	buf[len] = 0; // Make sure buf is zero-terminated

	m.MemTotal = get_entry_fatal("MemTotal:", buf);
	m.SwapTotal = get_entry_fatal("SwapTotal:", buf);
	m.SwapFree = get_entry_fatal("SwapFree:", buf);

	m.MemAvailable = get_entry("MemAvailable:", buf);
	if(m.MemAvailable == -1) {
		m.MemAvailable = available_guesstimate(buf);
        if(guesstimate_warned == 0) {
			fprintf(stderr, "Warning: Your kernel does not provide MemAvailable data (needs 3.14+)\n"
			                "         Falling back to guesstimate\n");
			guesstimate_warned = 1;
        }
    }

	return m;
}

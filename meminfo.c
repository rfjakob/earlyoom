/* Parse /proc/meminfo
 * Returned values are in kiB */

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>                     // for size_t

#include "meminfo.h"

static unsigned long get_entry(char *name, char *buf) {
	char *hit = strstr(buf, name);
	if(hit == NULL) {
		fprintf(stderr, "Could not find \"%s\"\n", name);
		exit(104);
	}

	errno = 0;
	unsigned long val = strtoul(hit + strlen(name), NULL, 10);
	if(errno != 0) {
		perror("Could not convert number");
		exit(105);
	}
	return val;
}

struct meminfo parse_meminfo() {
	static FILE* fd;
	static char buf[8192];
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

	m.MemTotal = get_entry("MemTotal:", buf);
	m.MemAvailable = get_entry("MemAvailable:", buf);
	m.SwapTotal = get_entry("SwapTotal:", buf);
	m.SwapFree = get_entry("SwapFree:", buf);

	return m;
}

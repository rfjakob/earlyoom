/* If the available memory goes below this percentage, we start killing
 * processes. 10 is a good start. */
#define MIN_AVAIL_PERCENT 10

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "meminfo.h"
#include "kill.h"

int main(int argc, char *argv[])
{
	unsigned long mem_min, swap_min; // kiB
	unsigned long oom_cnt=0;

	/* To be able to observe in real time what is happening when the
	 * output is redirected we have to explicitely request line
	 * buffering */
	setvbuf(stdout , NULL , _IOLBF , 80);

	fprintf(stderr, "earlyoom %s\n", GITVERSION);

	if(chdir("/proc")!=0)
	{
		fprintf(stderr, "Error: Could not cd to /proc: %s\n", strerror(errno));
		exit(4);
	}

	DIR *procdir = opendir(".");
	if(procdir==NULL)
	{
		fprintf(stderr, "Error: Could not open /proc: %s\n", strerror(errno));
		exit(5);
	}

	struct meminfo m = parse_meminfo();
	mem_min = m.MemTotal * MIN_AVAIL_PERCENT / 100;
	swap_min = m.SwapTotal * MIN_AVAIL_PERCENT / 100;

	fprintf(stderr, "mem total: %lu MiB, min: %lu MiB\n",
		m.MemTotal / 1024, mem_min / 1024);
	fprintf(stderr, "swap total: %lu MiB, min: %lu MiB\n",
		m.SwapTotal / 1024, swap_min / 1024);

	/* Dry-run oom kill to make sure stack grows to maximum size before
	 * calling mlockall()
	 */
	handle_oom(procdir, 0);

	if(mlockall(MCL_FUTURE)!=0)
	{
		perror("Could not lock memory");
		exit(10);
	}

	unsigned char c=1; // init to 1 so we do not print another status line immediately
	while(1)
	{
		m = parse_meminfo();

		if(c % 10 == 0)
		{
			printf("mem avail: %5lu MiB, swap free: %5lu MiB\n",
				m.MemAvailable / 1024, m.SwapFree / 1024);
			c=0;
		}
		c++;

		if(m.MemAvailable <= mem_min && m.SwapFree <= swap_min)
		{
			fprintf(stderr, "Out of memory! avail: %lu MiB < min: %lu MiB\n",
				m.MemAvailable / 1024, mem_min / 1024);
			handle_oom(procdir, 9);
			oom_cnt++;
		}
		
		usleep(100000); // 100ms
	}
	
	return 0;
}

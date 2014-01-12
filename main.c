#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "sysinfo.h"
// Physical RAM size
//kb_main_total

/* "free -/+ buffers/cache"
 * Memory that is actually available to applications */
unsigned long get_kb_avail(void)
{
	meminfo();
	return kb_main_free + kb_main_buffers + kb_main_cached;
}

int main(int argc, char *argv[])
{
	unsigned long kb_avail, kb_min, oom_cnt=0;
	int trig_fd = open("/proc/sysrq-trigger", O_WRONLY);
	char dryrun = 0;

	if(trig_fd == -1)
	{
		printf("Warning: Cannot open /proc/sysrq-trigger: %s. "
		"Continuing in dryrun mode.\n", strerror(errno));
		dryrun = 1;
	}
	
	kb_avail = get_kb_avail();
	kb_min = kb_main_total/10;
	
	printf("Physical RAM: %lu, currently available: %lu, minimum: %lu (kiB)\n",
		kb_main_total, kb_avail, kb_min);
	
	while(1)
	{
		kb_avail = get_kb_avail();
	
		//printf("kb_avail: %lu\n", kb_avail);
	
		if(kb_avail < kb_min)
		{
			if(dryrun)
				printf("Warning: Dryrun mode, not actually killing anything\n");
			printf("LOW\n");
			oom_cnt++;
		}
		
		usleep(100000);
	}
	
	return 0;
}

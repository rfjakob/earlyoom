/* Kill the most memory-hungy process */

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <limits.h>                     // for PATH_MAX

static int isnumeric(char* str)
{
	int i=0;

	// Empty string is not numeric
	if(str[0]==0)
		return 0;

	while(1)
	{
		if(str[i]==0) // End of string
			return 1;

		if(isdigit(str[i])==0)
			return 0;

		i++;
	}
}

/*
 * Find the process with the largest RSS and kill it.
 * See trigger_oom_killer() for the reason why this is done in userspace.
 */
static void kill_by_rss(DIR *procdir, int sig)
{
	struct dirent * d;
	char buf[PATH_MAX];
	int pid;
	int hog_pid=0;
	unsigned long hog_rss=0;
	char name[PATH_MAX];

	rewinddir(procdir);
	while(1)
	{
		d=readdir(procdir);
		if(d==NULL)
			break;

		if(!isnumeric(d->d_name))
			continue;

		pid=strtoul(d->d_name, NULL, 10);

		snprintf(buf, PATH_MAX, "%d/statm", pid);

		FILE * statm = fopen(buf, "r");
		if(statm == 0)
		{
			// Process may have died in the meantime
			continue;
		}

		unsigned long VmSize=0, VmRSS=0;
		if(fscanf(statm, "%lu %lu", &VmSize, &VmRSS) < 2)
		{
			fprintf(stderr, "Error: Could not parse %s\n", buf);
			exit(8);
		}
		fclose(statm);

		if(VmRSS > hog_rss)
		{
			hog_pid=pid;
			hog_rss=VmRSS;
		}
	}

	if(hog_pid==0)
	{
		fprintf(stderr, "Error: Could not find a process to kill\n");
		exit(9);
	}

	name[0]=0;
	snprintf(buf, PATH_MAX, "%d/stat", hog_pid);
	FILE * stat = fopen(buf, "r");
	fscanf(stat, "%d %s", &pid, name);
	fclose(stat);

	if(sig!=0)
		fprintf(stderr, "Killing process %d %s\n", hog_pid, name);

	if(kill(hog_pid, sig) != 0)
	{
		fprintf(stderr, "Warning: Could not kill process: %s\n", strerror(errno));
	}
}

/*
 * Invoke the kernel oom killer by writing "f" into /proc/sysrq-trigger
 *
 * This approach has a few problems:
 * 1) It is disallowed by default (even for root) on Fedora 20.
 *    You have to first write "1" into /proc/sys/kernel/sysrq to enable the "f"
 *    trigger.
 * 2) The Chrome web browser assigns a penalty of 300 onto its own tab renderer
 *    processes. On an 8GB RAM machine, this means 2400MB, and will lead to every
 *    tab being killed before the actual memory hog
 *    See https://code.google.com/p/chromium/issues/detail?id=333617 for more info
 * Because of these issues, kill_by_rss() is used instead by default.
 */
void trigger_kernel_oom(int sig)
{
	FILE * trig_fd;
	trig_fd = fopen("sysrq-trigger", "w");
	if(trig_fd == NULL)
	{
		perror("Cannot open /proc/sysrq-trigger");
		exit(7);
	}
	if(sig == 9)
	{
		fprintf(stderr, "Invoking oom killer: ");
		if(fprintf(trig_fd, "f\n") != 2)
			perror("failed");
		else
			fprintf(stderr, "done\n");
	}
	fclose(trig_fd);
}

void handle_oom(DIR * procdir, int sig, int kernel_oom_killer)
{
	if(kernel_oom_killer)
		trigger_kernel_oom(sig);
	else
		kill_by_rss(procdir, sig);
}

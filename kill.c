/* Kill the most memory-hungy process */

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <limits.h>                     // for PATH_MAX
#include <unistd.h>

#include "kill.h"

extern int enable_debug;

struct procinfo {
	int oom_score;
	int oom_score_adj;
	unsigned long vm_rss;
	int exited;
};

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

const char * const fopen_msg = "fopen %s failed: %s\n";

/* Read /proc/pid/{oom_score, oom_score_adj, statm}
 * Caller must ensure that we are already in the /proc/ directory
 */
static struct procinfo get_process_stats(int pid)
{
	char buf[256];
	FILE * f;
	struct procinfo p = {0, 0, 0, 0};

	// Read /proc/[pid]/oom_score
	snprintf(buf, sizeof(buf), "%d/oom_score", pid);
	f = fopen(buf, "r");
	if(f == NULL) {
		printf(fopen_msg, buf, strerror(errno));
		p.exited = 1;
		return p;
	}
	fscanf(f, "%d", &(p.oom_score));
	fclose(f);

	// Read /proc/[pid]/oom_score_adj
	snprintf(buf, sizeof(buf), "%d/oom_score_adj", pid);
	f = fopen(buf, "r");
	if(f == NULL) {
		printf(fopen_msg, buf, strerror(errno));
		p.exited = 1;
		return p;
	}
	fscanf(f, "%d", &(p.oom_score_adj));
	fclose(f);

	// Read VmRss from /proc/[pid]/statm
	snprintf(buf, sizeof(buf), "%d/statm", pid);
	f = fopen(buf, "r");
	if(f == NULL)
	{
		printf(fopen_msg, buf, strerror(errno));
		p.exited = 1;
		return p;
	}
	fscanf(f, "%*u %lu", &(p.vm_rss));
	fclose(f);

	return p;
}

/*
 * Find the process with the largest oom_score and kill it.
 * See trigger_kernel_oom() for the reason why this is done in userspace.
 */
static void userspace_kill(DIR *procdir, int sig, int ignore_oom_score_adj)
{
	struct dirent * d;
	char buf[256];
	int pid;
	int victim_pid = 0;
	int victim_badness = 0;
	unsigned long victim_vm_rss = 0;
	char name[PATH_MAX];
	struct procinfo p;
	int badness;

	rewinddir(procdir);
	while(1)
	{
		errno = 0;
		d = readdir(procdir);
		if(d == NULL)
		{
			if(errno != 0)
				perror("readdir returned error");

			break;
		}

		// proc contains lots of directories not related to processes,
		// skip them
		if(!isnumeric(d->d_name))
			continue;

		pid = strtoul(d->d_name, NULL, 10);

		if(pid == 1)
			// Let's not kill init.
			continue;

		p = get_process_stats(pid);

		if(p.exited == 1)
			// Process may have died in the meantime
			continue;

		badness = p.oom_score;
		if(ignore_oom_score_adj && p.oom_score_adj > 0)
			badness -= p.oom_score_adj;

		if(enable_debug)
			printf("pid %5d: badness %3d vm_rss %6lu\n", pid, badness, p.vm_rss);

		if(badness > victim_badness)
		{
			victim_pid = pid;
			victim_badness = badness;
			if(enable_debug)
				printf("    ^ new victim (higher badness)\n");
		} else if(badness == victim_badness && p.vm_rss > victim_vm_rss) {
			victim_pid = pid;
			victim_vm_rss = p.vm_rss;
			if(enable_debug)
				printf("    ^ new victim (higher vm_rss)\n");
		}
	}

	if(victim_pid == 0)
	{
		fprintf(stderr, "Error: Could not find a process to kill. Sleeping 10 seconds.\n");
		sleep(10);
		return;
	}

	name[0]=0;
	snprintf(buf, sizeof(buf), "%d/stat", victim_pid);
	FILE * stat = fopen(buf, "r");
	fscanf(stat, "%*d %s", name);
	fclose(stat);

	if(sig != 0)
		fprintf(stderr, "Killing process %d %s\n", victim_pid, name);

	if(kill(victim_pid, sig) != 0)
	{
		perror("Could not kill process");
		// Killing the process may have failed because we are not running as root.
		// In that case, trying again in 100ms will just yield the same error.
		// Throttle ourselves to not spam the log.
		fprintf(stderr, "Sleeping 10 seconds\n");
		sleep(10);
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
 * 3) It is broken in 4.0.5 - see
 *    https://github.com/rfjakob/earlyoom/commit/f7e2cedce8e9605c688d0c6d7dc26b7e81817f02
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

void handle_oom(DIR * procdir, int sig, int kernel_oom_killer, int ignore_oom_score_adj)
{
	if(kernel_oom_killer)
		trigger_kernel_oom(sig);
	else
		userspace_kill(procdir, sig, ignore_oom_score_adj);
}

/*
Creates a zombie child process.
Automatically exits after 10 minutes.

Should look like this in ps:

$ ps auxwwww | grep zombie
jakob     7513  0.0  0.0   2172   820 pts/1    S+   13:44   0:00 ./zombie
jakob     7514  0.0  0.0      0     0 pts/1    Z+   13:44   0:00 [zombie]
<defunct>
*/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(1);
    }
    if (pid > 0) {
        /* parent */
        printf("zombie created, pid %d. Sleeping 10 minutes.\n", pid);
        sleep(600);
        int wstatus;
        wait(&wstatus);
        exit(0);
    } else {
        /* child */
        exit(0);
    }
}

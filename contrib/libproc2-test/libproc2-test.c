#include <libproc2/pids.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[])
{
    enum pids_item items[] = { PIDS_STATE };
    struct pids_info* info;
    struct pids_stack* stack;
    struct pids_fetch* fetched;

    int ret = procps_pids_new(&info, items, 1);
    if (ret != 0) {
        printf("new failured, ret=%d\n", ret);
        exit(4);
    }

    unsigned pidlist[] = { 1 };
    fetched = procps_pids_select(info, pidlist, 1, PIDS_SELECT_PID_THREADS);
    if (!fetched) {
        printf("select error\n");
        exit(3);
    }
    if (fetched->counts->total != 1) {
        exit(2);
    }
    stack = fetched->stacks[0];
    char state = PIDS_VAL(0, s_ch, stack, info);
    printf("%c\n", state);
}
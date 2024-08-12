#ifndef OOMSCORE_H
#define OOMSCORE_H

extern int default_oom_score;

// Returns errno (success = 0)
int set_oom_score_adj(pid_t pid, int oom_score_adj);

void* proc_listen(void* oom_score_adj_arg);

#endif

#include <errno.h>
#include <inttypes.h>
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "globals.h"
#include "meminfo.h"

// this value will be set if the oom score is not explicitly set
// "foo:123 bar baz:-111" == "foo:123 bar:-1000 baz:-111"
int default_oom_score = -1000;

struct nlcn_send_msg {
    struct nlmsghdr nl_hdr;
    struct {
        struct cn_msg cn_msg;
        enum proc_cn_mcast_op cn_mcast;
    } __attribute__((__packed__));
} __attribute__((aligned(NLMSG_ALIGNTO)));

struct nlcn_recv_msg {
    struct nlmsghdr nl_hdr;
    struct {
        struct cn_msg cn_msg;
        struct proc_event proc_ev;
    } __attribute__((__packed__));
} __attribute__((aligned(NLMSG_ALIGNTO)));

// Returns errno (success = 0)
int set_oom_score_adj(pid_t pid, int oom_score_adj)
{
    char buf[PATH_LEN] = { 0 };

    snprintf(buf, sizeof(buf), "%s/%d/oom_score_adj", procdir_path, pid);
    FILE* f = fopen(buf, "w");
    if (f == NULL) {
        return -1;
    }

    // fprintf returns a negative error code on failure
    int ret1 = fprintf(f, "%d", oom_score_adj);
    // fclose returns a non-zero value on failure and errno contains the error
    // code
    int ret2 = fclose(f);

    if (ret1 < 0) {
        return -ret1;
    }
    if (ret2) {
        return errno;
    }
    return 0;
}

static int netlink_connect()
{
    int fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (fd < 0) {
        perror("Error opening the socket");
        return -1;
    }

    struct sockaddr_nl client;
    client.nl_family = AF_NETLINK;
    client.nl_groups = CN_IDX_PROC;
    client.nl_pid = (__u32)getpid();

    int rc = bind(fd, (struct sockaddr*)&client, sizeof(client));
    if (rc < 0) {
        perror("Binding error");
        close(fd);
        return -1;
    }

    return fd;
}

static int netlink_set_event_listen(int fd, bool enable)
{
    struct nlcn_send_msg message;

    memset(&message, 0, sizeof(message));
    message.nl_hdr.nlmsg_len = sizeof(message);
    message.nl_hdr.nlmsg_pid = (__u32)getpid();
    message.nl_hdr.nlmsg_type = NLMSG_DONE;

    message.cn_msg.id.idx = CN_IDX_PROC;
    message.cn_msg.id.val = CN_VAL_PROC;
    message.cn_msg.len = sizeof(enum proc_cn_mcast_op);

    message.cn_mcast = enable ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;

    ssize_t rc = send(fd, &message, sizeof(message), 0);
    if (rc == -1) {
        perror("Netlink send failed");
        return -1;
    }

    return 0;
}

static char* get_process_name_by_pid(int pid)
{
    size_t name_size = 1024;
    char* name = (char*)malloc(sizeof(char) * name_size);
    if (name) {
        snprintf(name, name_size, "%s/%d/comm", procdir_path, pid);
        FILE* f = fopen(name, "r");
        if (f) {
            size_t size;
            size = fread(name, sizeof(char), name_size, f);
            if (size > 0) {
                name[size - 1] = '\0';
            }
            fclose(f);
        }
    }
    return name;
}

static void separate_score_and_name(char* raw_data, char** name, int* score)
{
    char* substring_start = raw_data;
    char* substring_end = strchr(raw_data, ':');

    size_t substring_length;
    if (substring_end == NULL) {
        substring_length = strlen(raw_data);
    } else {
        substring_length = (size_t)(substring_end - substring_start);
    }

    *name = malloc(sizeof(char) * substring_length);
    memcpy(*name, substring_start, substring_length);

    if (substring_end == NULL) {
        *score = default_oom_score;
        return;
    }

    char* number_start = substring_end + 1;
    char* endptr;
    long result = strtol(number_start, &endptr, 10);

    if (result < -1000)
        result = -1000;
    else if (result > 1000)
        result = 1000;

    *score = (int)result;
}

static bool list_contains_candidate(
    char* oom_score_adj_arg, char* candidate, int* out_score)
{
    if (strlen(oom_score_adj_arg) == 0)
        return false;

    char* saveptr;
    char* arg_copy = malloc(sizeof(char) * (strlen(oom_score_adj_arg) + 1));
    strcpy(arg_copy, oom_score_adj_arg);
    char* name_with_score = strtok_r(arg_copy, ", ", &saveptr);
    while (name_with_score != NULL) {
        int new_score = 0;
        char* new_name;
        separate_score_and_name(name_with_score, &new_name, &new_score);
        if (strcmp(new_name, candidate) == 0) {
            free(arg_copy);
            free(new_name);
            *out_score = new_score;
            return true;
        }
        name_with_score = strtok_r(NULL, ", ", &saveptr);
        free(new_name);
    }
    free(arg_copy);
    return false;
}

static int netlink_handle_process_events(int fd, char* oom_score_adj_arg)
{
    while (1) {
        struct nlcn_recv_msg nlcn_msg;
        ssize_t rc = recv(fd, &nlcn_msg, sizeof(nlcn_msg), 0);
        if (rc == 0) {
            return 0;
        } else if (rc == -1) {
            if (errno == EINTR)
                continue;
            perror("Netlink receive failed");
            return -1;
        }
        if (nlcn_msg.proc_ev.what == PROC_EVENT_EXEC) {
            int proc_pid = nlcn_msg.proc_ev.event_data.exec.process_pid;
            char* proc_name = get_process_name_by_pid(proc_pid);
            int adjust_score = default_oom_score;
            if (list_contains_candidate(
                    oom_score_adj_arg, proc_name, &adjust_score)) {
                set_oom_score_adj(proc_pid, adjust_score);
                printf(
                    "oom score for process %i \"%s\" has been adjusted to %i\n",
                    proc_pid, proc_name, adjust_score);
            }
            free(proc_name);
        }
    }

    return 0;
}

static void give_score_for_existing_program(char* proc_name, int adjust_score)
{
    size_t command_buffer_size = 1024;
    char command_buffer[command_buffer_size];
    snprintf(command_buffer, command_buffer_size, "/bin/bash -c \"pidof %s\"",
        proc_name);

    FILE* fp = popen(command_buffer, "r");
    if (fp == NULL) {
        return;
    }

    char output[1024];
    while (fgets(output, sizeof(output), fp) != NULL) {
        char* saveptr;
        char* pid_string = strtok_r(output, " ", &saveptr);
        while (pid_string != NULL) {
            char* endptr;
            pid_t pid = (pid_t)strtoimax(pid_string, &endptr, 10);
            set_oom_score_adj(pid, adjust_score);
            printf("oom score for process %i \"%s\" has been adjusted to %i\n",
                pid, proc_name, adjust_score);
            pid_string = strtok_r(NULL, " ", &saveptr);
        }
    }

    pclose(fp);
}

static void give_score_for_existing_program_list(char* oom_score_adj_arg)
{
    char* saveptr;
    char* programs_copy
        = malloc(sizeof(char) * (strlen(oom_score_adj_arg) + 1));
    strcpy(programs_copy, oom_score_adj_arg);
    char* name_with_score = strtok_r(programs_copy, ", ", &saveptr);
    while (name_with_score != NULL) {
        int score = 0;
        char* name;
        separate_score_and_name(name_with_score, &name, &score);
        give_score_for_existing_program(name, score);
        name_with_score = strtok_r(NULL, ", ", &saveptr);
        free(name);
    }
    free(programs_copy);
}

static void proc_listen_impl(char* oom_score_adj_arg)
{
    int fd;
    int rc = EXIT_SUCCESS;

    fd = netlink_connect();
    if (fd < 0)
        exit(EXIT_FAILURE);
    rc = netlink_set_event_listen(fd, true);
    if (rc < 0) {
        close(fd);
        exit(EXIT_FAILURE);
    }

    give_score_for_existing_program_list(oom_score_adj_arg);

    rc = netlink_handle_process_events(fd, oom_score_adj_arg);
    if (rc < 0) {
        close(fd);
        exit(EXIT_FAILURE);
    }

    netlink_set_event_listen(fd, false);

    close(fd);
    exit(rc);
}

void* proc_listen(void* oom_score_adj_arg)
{
    proc_listen_impl((char*)oom_score_adj_arg);
    return NULL;
}

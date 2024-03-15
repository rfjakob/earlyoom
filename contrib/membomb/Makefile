CFLAGS += -Wall -Wextra -g -fstack-protector-all -std=gnu99

.PHONY: all
all: membomb membomb.subthread

membomb : $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -o $@ eat_all_memory.c membomb.c

membomb.subthread: $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -lpthread -o $@ eat_all_memory.c membomb.subthread.c

.PHONY: format
format:
	clang-format --style=file -i *.c

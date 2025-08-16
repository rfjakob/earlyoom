CFLAGS += -Wall -Wextra -g -fstack-protector-all -std=gnu99

.PHONY: all
all: membomb membomb.subthread membomb.mmap

membomb : $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -o $@ eat_all_memory.c membomb.c

membomb.subthread: $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -lpthread -o $@ eat_all_memory.c membomb.subthread.c

membomb.mmap: $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -lpthread -o $@ eat_all_memory.c membomb.mmap.c

.PHONY: format
format:
	clang-format --style=file -i *.c

.PHONY: clean
clean:
	rm -f membomb membomb.subthread membomb.mmap

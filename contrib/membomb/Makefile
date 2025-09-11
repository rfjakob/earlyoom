CFLAGS += -Wall -Wextra -g -fstack-protector-all -std=gnu99

.PHONY: all
all: membomb membomb.subthread membomb.mmap_anon membomb.mmap_file keep_active

membomb : $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -o $@ eat_all_memory.c membomb.c

membomb.subthread: $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -lpthread -o $@ eat_all_memory.c membomb.subthread.c

membomb.mmap_anon: $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -lpthread -o $@ eat_all_memory.c membomb.mmap_anon.c

membomb.mmap_file: $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -lpthread -o $@ eat_all_memory.c membomb.mmap_file.c

keep_active: $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -lpthread -o $@ keep_active.c

.PHONY: format
format:
	clang-format --style=file -i *.c

.PHONY: clean
clean:
	rm -f membomb membomb.subthread membomb.mmap_anon membomb.mmap_file keep_active

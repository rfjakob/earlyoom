CFLAGS += -Wall -Wextra -g -fstack-protector-all -std=gnu99

membomb: $(wildcard *.c *.h) Makefile
	$(CC) $(CFLAGS) -o $@ $(wildcard *.c)

GITVERSION=\"$(shell git describe --tags --dirty)\"
CFLAGS=-Wall -DGITVERSION=$(GITVERSION)

earlyoomd: main.c sysinfo.c
	$(CC) $(CFLAGS) -o earlyoomd main.c sysinfo.c

clean:
	rm -f earlyoomd

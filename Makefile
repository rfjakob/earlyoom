GITVERSION=\"$(shell git describe --tags --dirty)\"
CFLAGS=-Wall -DGITVERSION=$(GITVERSION)

earlyoomd: main.c sysinfo.c
	$(CC) $(CFLAGS) -o earlyoom main.c sysinfo.c

clean:
	rm -f earlyoom

install:
	cp earlyoom /usr/local/bin
	cp earlyoom.service /etc/systemd/system/earlyoom.service

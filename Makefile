GITVERSION=\"$(shell git describe --tags --dirty)\"
CFLAGS=-Wall -DGITVERSION=$(GITVERSION)

earlyoomd: main.c sysinfo.c
	$(CC) $(CFLAGS) -o earlyoom main.c sysinfo.c

clean:
	rm -f earlyoom

install:
	cp earlyoom -f /usr/local/bin/earlyoom
	cp earlyoom.service /etc/systemd/system/earlyoom.service
	systemctl enable earlyoom

uninstall:
	rm -f /usr/local/bin/earlyoom
	systemctl disable earlyoom
	rm -f /etc/systemd/system/earlyoom.service

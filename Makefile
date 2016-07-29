VERSION ?= $(shell git describe --tags --dirty)
CFLAGS = -Wall -Wextra -DVERSION=\"$(VERSION)\" -g

.PHONY: earlyoom
earlyoom:
	$(CC) $(CFLAGS) -o earlyoom main.c meminfo.c kill.c

clean:
	rm -f earlyoom

install: earlyoom
	cp earlyoom -f /usr/local/bin/earlyoom
	cp earlyoom.service /etc/systemd/system/earlyoom.service
	systemctl enable earlyoom

install-initscript: earlyoom
	cp earlyoom -f /usr/local/bin/earlyoom
	cp earlyoom.initscript /etc/init.d/earlyoom
	chmod a+x /etc/init.d/earlyoom
	update-rc.d earlyoom start 18 2 3 4 5 . stop 20 0 1 6 .

uninstall:
	rm -f /usr/local/bin/earlyoom
	systemctl disable earlyoom
	rm -f /etc/systemd/system/earlyoom.service

uninstall-initscript:
	rm -f /usr/local/bin/earlyoom
	rm -f /etc/init.d/earlyoom
	update-rc.d earlyoom remove

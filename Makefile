VERSION ?= $(shell git describe --tags --dirty)
CFLAGS   = -Wextra -DVERSION=\"$(VERSION)\" -g
PREFIX  ?= "/usr/local"
DESTDIR ?= ""
TARGET   = $(DESTDIR)$(PREFIX)

.PHONY: earlyoom
earlyoom:
	$(CC) $(CFLAGS) -o earlyoom main.c meminfo.c kill.c

clean:
	rm -f earlyoom

install: earlyoom
	install -D -m 755 ./earlyoom "$(TARGET)/bin/earlyoom"
	install -D -m 644 ./earlyoom.service "$(TARGET)/lib/systemd/system/earlyoom.service"
	sed -i s~\$${PREFIX}~$(PREFIX)~g "$(TARGET)/lib/systemd/system/earlyoom.service"

install-initscript: earlyoom
	cp earlyoom -f "$(TARGET)/bin/earlyoom"
	cp earlyoom.initscript "$(DESTDIR)/etc/init.d/earlyoom"
	chmod a+x "$(DESTDIR)/etc/init.d/earlyoom"
	update-rc.d earlyoom start 18 2 3 4 5 . stop 20 0 1 6 .

uninstall:
	rm -f "$(TARGET)/bin/earlyoom"
	rm -f "$(TARGET)/lib/systemd/system/earlyoom.service"

uninstall-initscript:
	rm -f "$(TARGET)/bin/earlyoom"
	rm -f "$(DESTDIR)/etc/init.d/earlyoom"
	update-rc.d earlyoom remove

VERSION ?= $(shell git describe --tags --dirty)
CFLAGS += -Wall -Wextra -DVERSION=\"$(VERSION)\" -g

DESTDIR ?=
PREFIX ?= /usr/local
SYSCONFDIR ?= /etc

VERSION ?= "(unknown version)"

.PHONY: all clean install uninstall

all: earlyoom

earlyoom: $(wildcard *.c)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f earlyoom

install: install-bin earlyoom.service
	install -d $(DESTDIR)$(SYSCONFDIR)/systemd/system/
	install -m 644 earlyoom.service $(DESTDIR)$(SYSCONFDIR)/systemd/system/
	systemctl enable earlyoom

install-initscript: install-bin earlyoom.initscript
	install -d $(DESTDIR)$(SYSCONFDIR)/init.d/
	cp earlyoom.initscript $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	chmod a+x $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	update-rc.d earlyoom start 18 2 3 4 5 . stop 20 0 1 6 .

install-bin: earlyoom
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $< $(DESTDIR)$(PREFIX)/bin

uninstall: uninstall-bin
	systemctl disable earlyoom
	rm -f $(DESTDIR)$(SYSCONFDIR)/systemd/system/earlyoom.service

uninstall-initscript: uninstall-bin
	rm -f $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	update-rc.d earlyoom remove

uninstall-bin:
	rm -f $(DESTDIR)$(PREFIX)/bin/earlyoom

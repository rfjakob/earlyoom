VERSION ?= $(shell git describe --tags --dirty 2> /dev/null)
CFLAGS += -Wall -Wextra -DVERSION=\"$(VERSION)\" -g

DESTDIR ?=
PREFIX ?= /usr/local
BINDIR ?= /bin
SYSCONFDIR ?= /etc
SYSTEMDDIR ?= $(SYSCONFDIR)/systemd

ifeq ($(VERSION),)
VERSION := "(unknown version)"
endif

.PHONY: all clean install uninstall

all: earlyoom

earlyoom: $(wildcard *.c *.h)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(wildcard *.c)

clean:
	rm -f earlyoom earlyoom.service earlyoom.initscript earlyoom.1.gz

install: earlyoom.service install-bin install-default install-man
	install -d $(DESTDIR)$(SYSTEMDDIR)/system/
	install -m 644 $< $(DESTDIR)$(SYSTEMDDIR)/system/
	-systemctl enable earlyoom

install-initscript: earlyoom.initscript install-bin install-default
	install -d $(DESTDIR)$(SYSCONFDIR)/init.d/
	install -m 755 $< $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	-update-rc.d earlyoom start 18 2 3 4 5 . stop 20 0 1 6 .

earlyoom.%: earlyoom.%.in
	sed "s|:TARGET:|$(PREFIX)$(BINDIR)|g;s|:SYSCONFDIR:|$(SYSCONFDIR)|g" $< > $@

install-default: earlyoom.default install-man
	install -d $(DESTDIR)$(SYSCONFDIR)/default/
	install -m 644 $< $(DESTDIR)$(SYSCONFDIR)/default/earlyoom

install-bin: earlyoom
	install -d $(DESTDIR)$(PREFIX)$(BINDIR)/
	install -m 755 $< $(DESTDIR)$(PREFIX)$(BINDIR)/

install-man: earlyoom.1.gz
	install -d $(DESTDIR)$(PREFIX)/share/man/man1/
	install -m 644 $< $(DESTDIR)$(PREFIX)/share/man/man1/

earlyoom.1.gz: earlyoom.1
	gzip -k $<

uninstall: uninstall-bin uninstall-man
	systemctl disable earlyoom
	rm -f $(DESTDIR)$(SYSTEMDDIR)/system/earlyoom.service

uninstall-man:
	rm -f $(DESTDIR)$(PREFIX)/share/man/man1/earlyoom.1.gz

uninstall-initscript: uninstall-bin
	rm -f $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	update-rc.d earlyoom remove

uninstall-bin:
	rm -f $(DESTDIR)$(PREFIX)$(BINDIR)/earlyoom

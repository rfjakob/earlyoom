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
	$(CC) $(CFLAGS) -o $@ $(wildcard *.c)

clean:
	rm -f earlyoom earlyoom.service earlyoom.initscript

install: earlyoom.service install-bin install-default
	install -d $(DESTDIR)$(SYSTEMDDIR)/system/
	install -m 644 $< $(DESTDIR)$(SYSTEMDDIR)/system/
	-systemctl enable earlyoom

install-initscript: earlyoom.initscript install-bin install-default
	install -d $(DESTDIR)$(SYSCONFDIR)/init.d/
	cp $< $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	chmod a+x $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	-update-rc.d earlyoom start 18 2 3 4 5 . stop 20 0 1 6 .

earlyoom.%: earlyoom.%.in
	sed "s|:TARGET:|$(PREFIX)$(BINDIR)|g;s|:SYSCONFDIR:|$(SYSCONFDIR)|g" $< > $@

install-default: earlyoom.default
	install -d $(DESTDIR)$(SYSCONFDIR)/default/
	install -m 644 $< $(DESTDIR)$(SYSCONFDIR)/default/

install-bin: earlyoom
	install -d $(DESTDIR)$(PREFIX)$(BINDIR)/
	install -m 755 $< $(DESTDIR)$(PREFIX)$(BINDIR)/

uninstall: uninstall-bin
	systemctl disable earlyoom
	rm -f $(DESTDIR)$(SYSTEMDDIR)/system/earlyoom.service

uninstall-initscript: uninstall-bin
	rm -f $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	update-rc.d earlyoom remove

uninstall-bin:
	rm -f $(DESTDIR)$(PREFIX)$(BINDIR)/earlyoom

# Setting GIT_DIR keeps git from ascending to parent directories
# and gives a nicer error message
VERSION ?= $(shell GIT_DIR=$(shell pwd)/.git git describe --tags --dirty)
ifeq ($(VERSION),)
VERSION := "(unknown version)"
$(warning Could not get version from git, setting to $(VERSION))
endif
CFLAGS += -Wall -Wextra -Wformat-security -Wconversion -DVERSION=\"$(VERSION)\" -g -fstack-protector-all -std=gnu99

DESTDIR ?=
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
MAN1DIR ?= $(MANDIR)/man1
SYSCONFDIR ?= $(PREFIX)/etc
SYSTEMDUNITDIR ?= $(SYSCONFDIR)/systemd/system
PANDOC := $(shell command -v pandoc 2> /dev/null)

.PHONY: all clean install uninstall format test

all: earlyoom earlyoom.1 earlyoom.service

earlyoom: $(wildcard *.c *.h) Makefile
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(CFLAGS) -o $@ $(wildcard *.c)

.PHONY: earlyoom.profile
earlyoom.profile:
	$(CC) $(LDFLAGS) $(CPPFLAGS) $(CFLAGS) -DPROFILE_FIND_LARGEST_PROCESS -o earlyoom.profile $(wildcard *.c)

earlyoom.1: MANPAGE.md
ifdef PANDOC
	pandoc MANPAGE.md -s -t man > earlyoom.1
else
	@echo "pandoc is not installed, skipping earlyoom.1 manpage generation"
endif

clean:
	rm -f earlyoom earlyoom.profile earlyoom.service earlyoom.initscript earlyoom.1 earlyoom.1.gz gmon.out*

install: earlyoom.service install-bin install-default install-man
	install -d $(DESTDIR)$(SYSTEMDUNITDIR)
	install -m 644 $< $(DESTDIR)$(SYSTEMDUNITDIR)
	-chcon -t systemd_unit_file_t $(DESTDIR)$(SYSTEMDUNITDIR)/$<
	-systemctl enable earlyoom

install-initscript: earlyoom.initscript install-bin install-default
	install -d $(DESTDIR)$(SYSCONFDIR)/init.d/
	install -m 755 $< $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	-update-rc.d earlyoom start 18 2 3 4 5 . stop 20 0 1 6 .

earlyoom.%: earlyoom.%.in
	sed "s|:TARGET:|$(BINDIR)|g;s|:SYSCONFDIR:|$(SYSCONFDIR)|g" $< > $@

install-default: earlyoom.default install-man
	install -d $(DESTDIR)$(SYSCONFDIR)/default/
	install -m 644 $< $(DESTDIR)$(SYSCONFDIR)/default/earlyoom

install-bin: earlyoom
	install -d $(DESTDIR)$(BINDIR)/
	install -m 755 $< $(DESTDIR)$(BINDIR)/

install-man: earlyoom.1.gz
ifdef PANDOC
	install -d $(DESTDIR)$(MAN1DIR)/
	install -m 644 $< $(DESTDIR)$(MAN1DIR)/
endif

earlyoom.1.gz: earlyoom.1
ifdef PANDOC
	gzip -f -k -n $<
endif

uninstall: uninstall-bin uninstall-man
	systemctl disable earlyoom
	rm -f $(DESTDIR)$(SYSTEMDUNITDIR)/earlyoom.service

uninstall-man:
	rm -f $(DESTDIR)$(MAN1DIR)/earlyoom.1.gz

uninstall-initscript: uninstall-bin
	rm -f $(DESTDIR)$(SYSCONFDIR)/init.d/earlyoom
	update-rc.d earlyoom remove

uninstall-bin:
	rm -f $(DESTDIR)$(BINDIR)/earlyoom

# Depends on earlyoom compilation to make sure the syntax is ok.
format: earlyoom
	clang-format --style=file -i *.h *.c
	go fmt .

test: earlyoom
	cppcheck -q . || echo "skipping optional cppcheck"
	go test -v

.PHONY: bench
bench:
	go test -run=NONE -bench=.

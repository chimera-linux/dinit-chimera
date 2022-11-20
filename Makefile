CC         ?= cc
CFLAGS     ?= -O2
PREFIX     ?= /usr
SYSCONFDIR ?= /etc
BINDIR     ?= $(PREFIX)/bin
LIBDIR     ?= $(PREFIX)/lib
LIBEXECDIR ?= $(PREFIX)/libexec
DATADIR    ?= $(PREFIX)/share
MANDIR     ?= $(DATADIR)/man/man8
SDINITDIR  ?= $(LIBDIR)/dinit.d
DINITDIR   ?= $(SYSCONFDIR)/dinit.d
EXTRA_CFLAGS = -Wall -Wextra

BIN_PROGRAMS = modules-load seedrng

MANPAGES = modules-load.8

all: bin/seedrng

bin/seedrng:
	$(CC) $(EXTRA_CFLAGS) $(CFLAGS) $(LDFLAGS) seedrng.c -o bin/seedrng

clean:
	rm -f bin/seedrng

install: bin/seedrng
	install -d $(DESTDIR)$(BINDIR)
	install -d $(DESTDIR)$(DATADIR)
	install -d $(DESTDIR)$(SYSCONFDIR)
	install -d $(DESTDIR)$(MANDIR)
	install -d $(DESTDIR)$(LIBEXECDIR)/dinit/early
	install -d $(DESTDIR)$(SDINITDIR)/boot.d
	install -d $(DESTDIR)$(DINITDIR)
	install -d $(DESTDIR)$(DINITDIR)/scripts
	install -d $(DESTDIR)$(DINITDIR)/boot.d
	touch $(DESTDIR)$(DINITDIR)/boot.d/.empty
	touch $(DESTDIR)$(SDINITDIR)/boot.d/.empty
	# early scripts
	for script in early-scripts/*.sh; do \
		install -m 755 $$script \
			$(DESTDIR)$(LIBEXECDIR)/dinit/early; \
	done
	# shutdown script
	install -m 755 bin/shutdown $(DESTDIR)$(LIBEXECDIR)/dinit
	# programs
	for prog in $(BIN_PROGRAMS); do \
		install -m 755 bin/$$prog $(DESTDIR)$(BINDIR); \
	done
	# manpages
	for man in $(MANPAGES); do \
		install -m 644 man/$$man $(DESTDIR)$(MANDIR); \
	done
	# services
	for srv in services/*; do \
		install -m 644 $$srv $(DESTDIR)$(DINITDIR); \
	done
	# system services
	for srv in system-services/*; do \
		install -m 644 $$srv $(DESTDIR)$(SDINITDIR); \
	done
	# default-enabled services
	for f in 1 2 3 4 5 6; do \
		ln -s ../agetty-tty$$f $(DESTDIR)$(SDINITDIR)/boot.d/agetty-tty$$f; \
	done

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

SYSTEM_SERVICES = \
	boot \
	early-aux-filesystems \
	early-aux-fsck \
	early-filesystems \
	early-hwclock \
	early-modules \
	early-root-fsck \
	early-root-rw \
	early-static-devnodes \
	early-udev-settle \
	early-udev-trigger \
	early-udevd	\
	early.target \
	init.target \
	login.target \
	network.target \
	recovery \
	single \
	system

SERVICES = \
	agetty-console \
	agetty-hvc0 \
	agetty-hvsi0 \
	agetty-tty1 \
	agetty-tty2 \
	agetty-tty3 \
	agetty-tty4 \
	agetty-tty5 \
	agetty-tty6 \
	agetty-ttyS0 \
	agetty-ttyUSB0

EARLY_SCRIPTS = \
	aux-filesystems \
	aux-filesystems-stop \
	filesystems \
	hwclock \
	hwclock-stop \
	modules \
	rcboot-stop \
	rcboot \
	root-fsck \
	static-devnodes

all: bin/seedrng

bin/seedrng:
	$(CC) $(EXTRA_CFLAGS) $(CFLAGS) $(LDFLAGS) seedrng.c -o bin/seedrng

clean:
	rm -f bin/seedrng

install:
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
	for script in $(EARLY_SCRIPTS); do \
		install -m 755 early-scripts/$$script.sh \
			$(DESTDIR)$(LIBEXECDIR)/dinit/early; \
	done
	install -m 755 early-scripts/crypt.awk $(DESTDIR)$(LIBEXECDIR)/dinit/early
	# programs
	for prog in $(BIN_PROGRAMS); do \
		install -m 755 bin/$$prog $(DESTDIR)$(BINDIR); \
	done
	# manpages
	for man in $(MANPAGES); do \
		install -m 644 man/$$man $(DESTDIR)$(MANDIR); \
	done
	# system services
	for srv in $(SYSTEM_SERVICES); do \
		install -m 644 services/$$srv $(DESTDIR)$(SDINITDIR); \
	done
	# services
	for srv in $(SERVICES); do \
		install -m 644 services/$$srv $(DESTDIR)$(DINITDIR); \
	done
	# default-enabled services
	for f in 1 2 3 4 5 6; do \
		ln -s ../agetty-tty$$f $(DESTDIR)$(SDINITDIR)/boot.d/agetty-tty$$f; \
	done

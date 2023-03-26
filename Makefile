CC         ?= cc
CXX        ?= c++
CFLAGS     ?= -O2
CXXFLAGS   ?= -O2
PREFIX     ?= /usr
SYSCONFDIR ?= /etc
LIBDIR     ?= $(PREFIX)/lib
LIBEXECDIR ?= $(PREFIX)/libexec
DATADIR    ?= $(PREFIX)/share
MANDIR     ?= $(DATADIR)/man/man8
SDINITDIR  ?= $(LIBDIR)/dinit.d
DINITDIR   ?= $(SYSCONFDIR)/dinit.d
EXTRA_CFLAGS = -Wall -Wextra
EXTRA_CXXFLAGS = $(EXTRA_CFLAGS) -fno-rtti -fno-exceptions

MANPAGES = init-modules.target.8

all: seedrng hwclock-helper binfmt-helper lo-helper

seedrng:
	$(CC) $(EXTRA_CFLAGS) $(CFLAGS) $(LDFLAGS) seedrng.c -o seedrng

hwclock-helper:
	$(CC) $(EXTRA_CFLAGS) $(CFLAGS) $(LDFLAGS) hwclock-helper.c -o hwclock-helper

lo-helper:
	$(CC) $(EXTRA_CFLAGS) $(CFLAGS) $(LDFLAGS) lo-helper.c -o lo-helper

binfmt-helper:
	$(CXX) $(EXTRA_CXXFLAGS) $(CXXFLAGS) $(LDFLAGS) binfmt-helper.cc -o binfmt-helper

clean:
	rm -f seedrng hwclock-helper binfmt-helper lo-helper

install: seedrng hwclock-helper binfmt-helper lo-helper
	install -d $(DESTDIR)$(DATADIR)
	install -d $(DESTDIR)$(SYSCONFDIR)
	install -d $(DESTDIR)$(MANDIR)
	install -d $(DESTDIR)$(LIBEXECDIR)/dinit/early
	install -d $(DESTDIR)$(LIBEXECDIR)/dinit/helpers
	install -d $(DESTDIR)$(LIBDIR)/dinit
	install -d $(DESTDIR)$(SDINITDIR)/boot.d
	install -d $(DESTDIR)$(DINITDIR)
	install -d $(DESTDIR)$(DINITDIR)/boot.d
	touch $(DESTDIR)$(DINITDIR)/boot.d/.empty
	touch $(DESTDIR)$(SDINITDIR)/boot.d/.empty
	# early scripts
	for script in scripts/*.sh; do \
		install -m 755 $$script \
			$(DESTDIR)$(LIBEXECDIR)/dinit/early; \
	done
	# shutdown script
	install -m 755 dinit-shutdown $(DESTDIR)$(LIBDIR)/dinit/shutdown-hook
	# helper programs
	install -m 755 seedrng $(DESTDIR)$(LIBEXECDIR)/dinit/helpers/seedrng
	install -m 755 hwclock-helper $(DESTDIR)$(LIBEXECDIR)/dinit/helpers/hwclock
	install -m 755 binfmt-helper $(DESTDIR)$(LIBEXECDIR)/dinit/helpers/binfmt
	install -m 755 lo-helper $(DESTDIR)$(LIBEXECDIR)/dinit/helpers/lo
	# manpages
	for man in $(MANPAGES); do \
		install -m 644 man/$$man $(DESTDIR)$(MANDIR); \
	done
	# system services
	for srv in services/*; do \
		install -m 644 $$srv $(DESTDIR)$(SDINITDIR); \
	done

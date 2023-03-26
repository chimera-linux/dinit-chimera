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

HELPERS = \
	helpers/binfmt \
	helpers/hwclock \
	helpers/lo \
	helpers/seedrng

all: $(HELPERS)

helpers/%: helpers/%.c
	$(CC) $(EXTRA_CFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@

helpers/%: helpers/%.cc
	$(CXX) $(EXTRA_CXXFLAGS) $(CXXFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(HELPERS)

install: $(HELPERS)
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
	for helper in $(HELPERS); do \
		install -m 755 $$helper \
			$(DESTDIR)$(LIBEXECDIR)/dinit/helpers; \
	done
	# manpages
	for man in $(MANPAGES); do \
		install -m 644 man/$$man $(DESTDIR)$(MANDIR); \
	done
	# system services
	for srv in services/*; do \
		install -m 644 $$srv $(DESTDIR)$(SDINITDIR); \
	done

SUBDIRS = intl po macros plugins src 

EXTRA_DIST = \
	gnumeric.desktop README HACKING xlibtool \
	gnumeric.keys.in gnumeric.mime

dist-hook:
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples

Applicationsdir = $(datadir)/apps/Applications
Applications_DATA = gnumeric.desktop

Mimedir = $(datadir)/mime-info
Mime_DATA = gnumeric.keys gnumeric.mime

gnumeric.keys: gnumeric.keys.in Makefile
	sed -e 's^\@icondir\@^$(icondir)^g' < gnumeric.keys.in > gnumeric.keys.tmp \
	&& mv gnumeric.keys.tmp gnumeric.keys 


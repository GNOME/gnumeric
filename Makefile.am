SUBDIRS = intl po macros src plugins

EXTRA_DIST = gnumeric.desktop README HACKING

dist-hook:
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples

Applicationsdir = $(datadir)/apps/Applications
Applications_DATA = gnumeric.desktop
SUBDIRS = intl po macros src plugins

EXTRA_DIST = gnumeric.desktop README HACKING

dist-hook:
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples

Productivitydir = $(datadir)/apps/Productivity
Productivity_DATA = gnumeric.desktop
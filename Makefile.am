SUBDIRS = intl po macros plugins src 

appicondir = $(datadir)/pixmaps
appicon_DATA = gnome-gnumeric.png

EXTRA_DIST = \
	gnumeric.desktop README HACKING xlibtool \
	gnumeric.keys.in gnumeric.mime $(appicon_DATA)

dist-hook:
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples

Applicationsdir = $(datadir)/apps/Applications
Applications_DATA = gnumeric.desktop

Mimedir = $(datadir)/mime-info
Mime_DATA = gnumeric.keys gnumeric.mime

gnumeric.keys: gnumeric.keys.in Makefile
	sed -e 's^\@icondir\@^$(appicondir)^g' < $(srcdir)/gnumeric.keys.in > gnumeric.keys.tmp \
	&& mv gnumeric.keys.tmp gnumeric.keys 


SUBDIRS = intl po macros libglade plugins src doc

EXTRA_DIST = \
	gnumeric.desktop README HACKING gnumeric.spec.in \
	gnumeric.keys.in gnumeric.mime $(appicon_DATA)

appicondir = $(datadir)/pixmaps
appicon_DATA = gnome-gnumeric.png gnome-gnumeric-xl.png

Applicationsdir = $(datadir)/gnome/apps/Applications
Applications_DATA = gnumeric.desktop

Mimedir = $(datadir)/mime-info
Mime_DATA = gnumeric.keys gnumeric.mime

gnewdir = $(datadir)/mc/templates
gnew_DATA = gnumeric.desktop

gnumeric.keys: gnumeric.keys.in Makefile
	sed -e 's^\@icondir\@^$(appicondir)^g' < $(srcdir)/gnumeric.keys.in > gnumeric.keys.tmp \
	&& mv gnumeric.keys.tmp gnumeric.keys 

dist-hook:
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples	
	cp gnumeric.spec $(distdir)


SUBDIRS = intl po macros plugins src doc icons

change_logs = \
	OChangeLog

EXTRA_DIST = \
	gnumeric.desktop.in README HACKING gnumeric.spec.in 	\
	gnumeric.keys.in gnumeric.mime $(appicon_DATA) 		\
	gnumeric.gnorba $(change_logs)

appicondir = $(datadir)/pixmaps
appicon_DATA = gnome-gnumeric.png \
               gnome-application-x-gnumeric.png \
               gnome-application-x-xls.png

Applicationsdir = $(datadir)/gnome/apps/Applications
Applications_DATA = gnumeric.desktop

Mimedir = $(datadir)/mime-info
Mime_DATA = gnumeric.keys gnumeric.mime

gnewdir = $(datadir)/mc/templates
gnew_DATA = gnumeric.desktop

gnorbadir = $(sysconfdir)/CORBA/servers
gnorba_DATA = gnumeric.gnorba

gnumeric.keys: gnumeric.keys.in Makefile
	sed -e 's^\@icondir\@^$(appicondir)^g' < $(srcdir)/gnumeric.keys.in > gnumeric.keys.tmp \
	&& mv gnumeric.keys.tmp gnumeric.keys 

dist-hook: gnumeric.spec
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples	
	cp gnumeric.spec $(distdir)


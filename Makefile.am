SUBDIRS = intl po macros idl plugins wizards src graph doc icons

change_logs = \
	OOChangeLog OChangeLog ChangeLog

bin_SCRIPTS = gnumeric-config

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

if BONOBO
gnumeric_version="$(VERSION)-bonobo"
else
gnumeric_version="$(VERSION)"
endif
gnumeric_datadir = $(datadir)/gnumeric/$(gnumeric_version)
gnumeric_libdir  =  $(libdir)/gnumeric/$(gnumeric_version)

## We create gnumeric-config here and not from configure because we want
## to get the paths expanded correctly.  Macros like srcdir are given
## the value NONE in configure if the user doesn't specify them (this
## is an autoconf feature, not a bug).

gnumeric-config: gnumeric-config.in Makefile
## Use sed and then mv to avoid problems if the user interrupts.
	sed -e 's?\@VERSION\@?$(VERSION)?g' \
	    -e 's?\@BONOBO\@?$(BONOBO)?g' \
	    -e 's?\@prefix\@?$(prefix)?g' \
	    -e 's?\@exec_prefix\@?$(exec_prefix)?g' \
	    -e 's?\@datadir\@?$(gnumeric_datadir)?g' \
	    -e 's?\@libdir\@?$(gnumeric_libdir)?g' \
	    < $(srcdir)/gnumeric-config.in > gnumeric-config.tmp \
	    && mv gnumeric-config.tmp gnumeric-config



SUBDIRS = . po idl src plugins doc icons templates tools omf-install

XML_I18N_XML_KIND = --pass-through

change_logs = ChangeLog \
	OChangeLog-1999-07-09 \
	OChangeLog-2000-02-23 OChangeLog-2000-10-10  \
	OChangeLog-2001-06-26

confexecdir=$(libdir)
confexec_DATA = gnumericConf.sh

CLEANFILES=gnumericConf.sh

appicondir = $(datadir)/pixmaps
appicon_DATA = gnome-gnumeric.png \
               gnome-application-x-gnumeric.png \
               gnome-application-x-xls.png

Applicationsdir = $(datadir)/gnome/apps/Applications
Applications_in_files = gnumeric.desktop.in
Applications_DATA = gnumeric.desktop
@INTLTOOL_DESKTOP_RULE@
gnewdir = $(datadir)/mc/templates
gnew_in_files = gnumeric.desktop.in
gnew_DATA = gnumeric.desktop
@INTLTOOL_DESKTOP_RULE@

Mimedir = $(datadir)/mime-info
Mime_DATA = gnumeric.keys gnumeric.mime

@INTLTOOL_OAF_RULE@

oafdir = $(datadir)/oaf
oaf_DATA = $(oaf_in_files:.oaf.in=.oaf)
oaf_in_files = GNOME_Gnumeric.oaf.in

EXTRA_DIST = \
	gnumeric.desktop.in gnumeric.spec.in gnumericConf.sh.in		\
	gnumeric.keys.in gnumeric.mime $(appicon_DATA) 			\
	gnumeric.xsd							\
	README HACKING NEWS BEVERAGES BUGS MAINTAINERS AUTHORS $(change_logs)	\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	$(oaf_in_files)

if WITH_BONOBO
gnumeric_version="$(VERSION)-bonobo"
else
gnumeric_version="$(VERSION)"
endif

## Cloned from gnome-print
##
## We create printConf.sh here and not from configure because we want
## to get the paths expanded correctly.  Macros like srcdir are given
## the value NONE in configure if the user doesn't specify them (this
## is an autoconf feature, not a bug).

gnumericConf.sh: gnumericConf.sh.in Makefile
## Use sed and then mv to avoid problems if the user interrupts.
	sed -e 's?\@gnumeric_prefix\@?$(prefix)?g' \
	    -e 's?\@gnumeric_exec_prefix\@?$(exec_prefix)?g' \
	    -e 's?\@gnumeric_version\@?$(gnumeric_version)?g' \
	    -e 's?\@gnumeric_datadir\@?$(gnumeric_datadir)?g' \
	    -e 's?\@gnumeric_libdir\@?$(gnumeric_libdir)?g' \
	    -e 's?\@gnumeric_plugindir\@?$(gnumeric_plugindir)?g' \
	    -e 's?\@gnumeric_gladedir\@?$(gnumeric_gladedir)?g' \
	    -e 's?\@gnumeric_bonobo_enabled\@?$(gnumeric_bonobo_enabled)?g' \
	      < $(srcdir)/gnumericConf.sh.in > gnumericConf.tmp \
	  && mv gnumericConf.tmp gnumericConf.sh

gnumeric.keys: gnumeric.keys.in Makefile
	sed -e 's^\@icondir\@^$(appicondir)^g' < $(srcdir)/gnumeric.keys.in > gnumeric.keys.tmp \
	&& mv gnumeric.keys.tmp gnumeric.keys 

dist-hook: gnumeric.spec
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples	
	cp gnumeric.spec $(distdir)







SUBDIRS = . po src plugins icons templates doc schemas omf-install

XML_I18N_XML_KIND = --pass-through

change_logs = ChangeLog \
	OChangeLog-1999-07-09 \
	OChangeLog-2000-02-23 OChangeLog-2000-10-10  \
	OChangeLog-2001-06-26

Applicationsdir = $(datadir)/applications/
Applications_in_files = gnumeric.desktop.in
Applications_DATA = gnumeric.desktop
@INTLTOOL_DESKTOP_RULE@
gnewdir = $(datadir)/mc/templates
gnew_in_files = gnumeric.desktop.in
gnew_DATA = gnumeric.desktop
@INTLTOOL_DESKTOP_RULE@

Mimedir = $(datadir)/mime-info
Mime_DATA = gnumeric.keys gnumeric.mime

serverdir = $(libdir)/bonobo/servers/
server_in_files = GNOME_Gnumeric.server.in
server_DATA	= GNOME_Gnumeric.server
GNOME_Gnumeric.server.in : GNOME_Gnumeric.server.in.in
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" $< > $@

@INTLTOOL_SERVER_RULE@

CLEANFILES = intltool-extract intltool-merge intltool-update \
	 $(server_in_files) $(server_DATA)

EXTRA_DIST = \
	gnumeric.desktop.in gnumeric.spec.in GNOME_Gnumeric.server.in.in\
	gnumeric.keys.in gnumeric.mime 					\
	gnumeric.xsd							\
	README HACKING NEWS BEVERAGES BUGS MAINTAINERS AUTHORS $(change_logs)	\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	$(server_in_files)

gnumeric.keys: gnumeric.keys.in Makefile
	sed -e 's^\@gnumeric_icondir\@^$(gnumeric_icondir)^g' < $(srcdir)/gnumeric.keys.in > gnumeric.keys.tmp \
	&& mv gnumeric.keys.tmp gnumeric.keys 

dist-hook: gnumeric.spec
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples	
	cp gnumeric.spec $(distdir)

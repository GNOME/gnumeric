SUBDIRS = . po src plugins icons templates doc schemas
# automake does not like subdirs that are not directly below
#if WITH_WIN32
#  SUBDIRS += src/cut-n-paste-code/goffice/graph/plugins
#endif

distcleancheck_listfiles = find . -type f -print | grep -v 'omf\.out' | grep -v '^\./doc'

distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper' | grep -v 'omf' | grep -v 'figures'

XML_I18N_XML_KIND = --pass-through

change_logs = ChangeLog \
	OChangeLog-1999-07-09 \
	OChangeLog-2000-02-23 OChangeLog-2000-10-10  \
	OChangeLog-2001-06-26

# Gnome autogen.sh uses ACLOCAL_FLAGS; inherit it here.
ACLOCAL_AMFLAGS = @ACLOCAL_FLAGS@

@INTLTOOL_DESKTOP_RULE@

Applicationsdir = $(datadir)/applications/
Applications_in_files = gnumeric.desktop.in
Applications_DATA = gnumeric.desktop

gnewdir = $(datadir)/mc/templates
gnew_in_files = gnumeric.desktop.in
gnew_DATA = gnumeric.desktop

Mimedir = $(datadir)/mime-info
Mime_DATA = gnumeric.keys gnumeric.mime

if WITH_GNOME
serverdir = $(libdir)/bonobo/servers/
server_in_files = GNOME_Gnumeric.server.in
server_DATA	= GNOME_Gnumeric.server
GNOME_Gnumeric.server.in : GNOME_Gnumeric.server.in.in Makefile
	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" $< > $@
endif

@INTLTOOL_SERVER_RULE@

CLEANFILES = $(server_DATA) $(server_in_files) \
	gnumeric.desktop gnumeric.keys
DISTCLEANFILES = intltool-extract intltool-merge intltool-update

EXTRA_DIST = \
	gnumeric.desktop.in gnumeric.spec.in GNOME_Gnumeric.server.in.in\
	gnumeric.keys.in gnumeric.mime 					\
	gnumeric.xsd							\
	README HACKING NEWS BEVERAGES BUGS MAINTAINERS AUTHORS $(change_logs)	\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	xmldocs.make 		\
	omf.make		\
	$(server_in_files) $(server_DATA)

gnumeric.keys: gnumeric.keys.in Makefile
	sed -e 's^\@gnumeric_icondir\@^$(gnumeric_icondir)^g' < $(srcdir)/gnumeric.keys.in > gnumeric.keys.tmp \
	&& mv gnumeric.keys.tmp gnumeric.keys 

gnumeric.desktop: gnumeric.desktop.in Makefile $(INTLTOOL_MERGE) $(wildcard $(top_srcdir)/po/*.po)
	 LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@.tmp
if WITH_GNOME
	sed -e 's/F_OR_U/U/' <$@.tmp >$@.tmp2
else
	sed -e 's/F_OR_U/F/' <$@.tmp >$@.tmp2
endif
	sed -e 's/BUGZILLAVERSION/@VERSION@/' <$@.tmp2 >$@
	rm -f $@.tmp $@.tmp2

dist-hook: gnumeric.spec
	-cd $(distdir) ; rm -f $(server_in_files) $(server_DATA)
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples	
	cp gnumeric.spec $(distdir)

# disable the check because the old perl plugin is hard to clean
distuninstallcheck:
	@:

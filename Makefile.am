SUBDIRS = . po po-functions src plugins icons templates doc schemas test
if WITH_WIN32
  SUBDIRS += tools
endif

XML_I18N_XML_KIND = --pass-through

change_logs = ChangeLog \
	OChangeLog-1999-07-09 \
	OChangeLog-2000-02-23 OChangeLog-2000-10-10  \
	OChangeLog-2001-06-26

# gnewdir = $(datadir)/mc/templates
# gnew_in_files = gnumeric.desktop.in
# gnew_DATA = gnumeric.desktop

# Mimedir = $(datadir)/mime-info
# Mime_DATA = gnumeric.keys gnumeric.mime

# Old stale functionality
#
#if WITH_GNOME
#serverdir = $(libdir)/bonobo/servers/
#server_in_files = GNOME_Gnumeric.server.in
#server_DATA	= GNOME_Gnumeric.server
#GNOME_Gnumeric.server.in : GNOME_Gnumeric.server.in.in Makefile
#	sed -e "s|\@LIBEXECDIR\@|$(libexecdir)|" $< > $@
#
# @INTLTOOL_SERVER_RULE@
#endif

# Unused as far as I know
#gnumeric.keys: gnumeric.keys.in Makefile
#	sed -e 's^\@gnumeric_icondir\@^$(gnumeric_icondir)^g' < $(srcdir)/gnumeric.keys.in > gnumeric.keys.tmp \
#	&& mv gnumeric.keys.tmp gnumeric.keys 
#

CLEANFILES = gnumeric.desktop gnumeric.keys
DISTCLEANFILES = intltool-extract intltool-merge intltool-update

EXTRA_DIST = \
	README HACKING NEWS BEVERAGES BUGS MAINTAINERS AUTHORS $(change_logs)	\
	gnumeric.desktop.in	\
	gnumeric.keys.in	\
	gnumeric.mime		\
	gnumeric.spec		\
	gnumeric.xsd		\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	xmldocs.make 		\
	omf.make

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libspreadsheet-1.7.pc

@INTLTOOL_DESKTOP_RULE@

Applicationsdir = $(datadir)/applications/
Applications_in_files = gnumeric.desktop.in
Applications_DATA = gnumeric.desktop

gnumeric.desktop: gnumeric.desktop.in Makefile $(INTLTOOL_MERGE) $(POFILES_FULL)
	 LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@.tmp
if WITH_GNOME
	sed -e 's/F_OR_U/U/' <$@.tmp >$@.tmp2
else
	sed -e 's/F_OR_U/F/' <$@.tmp >$@.tmp2
endif
	sed -e 's/BUGZILLAVERSION/@VERSION@/' <$@.tmp2 >$@
	rm -f $@.tmp $@.tmp2

dist-hook:
	mkdir $(distdir)/samples
	cp $(srcdir)/samples/*gnumeric $(distdir)/samples	

## It seems the following overrides can be simplified:
## distcleancheck_listfiles = find . -type f -print | grep -v 'omf\.out' | grep -v '^\./doc'
## distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper' | grep -v 'omf' | grep -v 'figures'

# xmldocs.make is screwed:
distcleancheck_listfiles = \
  find . -type f -print -o -path ./doc -prune

# We are not able to uninstall the scrollkeeper files:
distuninstallcheck_listfiles = \
  find . -type f -print -o -path ./var/scrollkeeper -prune

## If I understand this correctly, this hack is needed only --with-perl:
if WITH_PERL
# disable the check because the old perl plugin is hard to clean
distuninstallcheck:
	@:
endif

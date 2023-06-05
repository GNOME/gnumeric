SUBDIRS = . po po-functions src plugins icons @COMPONENT_DIR@ templates doc tools schemas test introspection

XML_I18N_XML_KIND = --pass-through

# We used to distribute old ChangeLog files too.
change_logs = ChangeLog

CLEANFILES = org.gnumeric.gnumeric.desktop org.gnumeric.gnumeric.appdata.xml
DISTCLEANFILES = intltool-extract intltool-merge intltool-update $(pkgconfig_DATA)

# Flags for distcheck, mostly to install things that nominally go under the
# directories of other packages into the dist dir instead.  (They won't work
# there, but that's ok.)
DISTCHECK_CONFIGURE_FLAGS = --without-perl --without-python \
	--enable-introspection=auto \
	--with-gir-dir=\$${datadir}/gir-1.0 \
	--with-typelib-dir=\$${libdir}/girepository-1.0 \
	--with-gi-overrides-dir=\$${libdir}/gi/overrides \
	--with-goffice-plugins-dir=\$${libdir}/goffice

DIST_SAMPLES = $(shell (cd $(srcdir)/test && perl -I. -e 'use GnumericTest; foreach (&GnumericTest::corpus("dist")) { print "$$_\n"; }'))

EXTRA_DIST = \
	README HACKING NEWS BEVERAGES BUGS MAINTAINERS AUTHORS $(change_logs)	\
	COPYING COPYING-gpl2 COPYING-gpl3 \
	org.gnumeric.gnumeric.appdata.xml.in	\
	org.gnumeric.gnumeric.desktop.in	\
	gnumeric.mime		\
	gnumeric.xsd		\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	libspreadsheet.pc.in	\
	$(DIST_SAMPLES)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libspreadsheet-@GNUMERIC_API_VER@.pc

libspreadsheet-@GNUMERIC_API_VER@.pc: libspreadsheet.pc
	cp $< $@

@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_RULE@

appdatadir = $(datadir)/metainfo
appdata_in_files = org.gnumeric.gnumeric.appdata.xml.in
appdata_DATA = org.gnumeric.gnumeric.appdata.xml

# Don't set the date inside this xml file.  We want reproducible builds.
org.gnumeric.gnumeric.appdata.xml: org.gnumeric.gnumeric.appdata.xml.in Makefile $(INTLTOOL_MERGE) $(POFILES_FULL)
	LC_ALL=C $(INTLTOOL_MERGE) -x -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@.tmp
	sed -e 's/##VERSION##/@VERSION@/' <$@.tmp >$@
	rm -f $@.tmp

Applicationsdir = $(datadir)/applications/
Applications_in_files = org.gnumeric.gnumeric.desktop.in
Applications_DATA = org.gnumeric.gnumeric.desktop

org.gnumeric.gnumeric.desktop: org.gnumeric.gnumeric.desktop.in Makefile $(INTLTOOL_MERGE) $(POFILES_FULL)
	LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@.tmp
	sed -e 's/F_OR_U/U/' <$@.tmp >$@
	rm -f $@.tmp

configexecincludedir = $(includedir)/libspreadsheet-@GNUMERIC_API_VER@/spreadsheet
install-exec-local: gnumeric-features.h
	$(mkinstalldirs) $(DESTDIR)$(configexecincludedir)
	file=$(DESTDIR)$(configexecincludedir)/$< ; \
	if test -r $$file && cmp -s $< $$file; then :; \
	else $(INSTALL_DATA) $< $$file; fi

uninstall-local:
	rm -f $(DESTDIR)$(configexecincludedir)/gnumeric-features.h

## If I understand this correctly, this hack is needed only --with-perl:
if WITH_PERL
# disable the check because the old perl plugin is hard to clean
distuninstallcheck:
	@:
endif

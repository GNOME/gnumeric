SUBDIRS = . po po-functions src plugins icons @COMPONENT_DIR@ templates doc tools schemas test introspection

XML_I18N_XML_KIND = --pass-through

# We used to distribute old ChangeLog files too.
change_logs = ChangeLog

CLEANFILES = gnumeric.desktop gnumeric.keys gnumeric.appdata.xml
DISTCLEANFILES = intltool-extract intltool-merge intltool-update $(pkgconfig_DATA)

DISTCHECK_CONFIGURE_FLAGS = --without-perl --without-python \
	--enable-introspection=auto \
	--with-gir-dir=\$${datadir}/gir-1.0 \
	--with-typelib-dir=\$${libdir}/girepository-1.0 \
	--with-gi-overrides-dir=\$${libdir}/gi/overrides

DIST_SAMPLES = $(shell (cd $(srcdir)/test && perl -e 'use GnumericTest; foreach (&GnumericTest::corpus("dist")) { print "$$_\n"; }'))

EXTRA_DIST = \
	README HACKING NEWS BEVERAGES BUGS MAINTAINERS AUTHORS $(change_logs)	\
	COPYING COPYING-gpl2 COPYING-gpl3 \
	gnumeric.appdata.xml.in	\
	gnumeric.desktop.in	\
	gnumeric.keys.in	\
	gnumeric.mime		\
	gnumeric.spec		\
	gnumeric.xsd		\
	intltool-extract.in	\
	intltool-merge.in	\
	intltool-update.in	\
	xmldocs.make 		\
	libspreadsheet.pc.in	\
	omf.make		\
	$(DIST_SAMPLES)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libspreadsheet-@GNUMERIC_API_VER@.pc

libspreadsheet-@GNUMERIC_API_VER@.pc: libspreadsheet.pc
	cp $< $@

@INTLTOOL_DESKTOP_RULE@
@INTLTOOL_XML_RULE@

appdatadir = $(datadir)/appdata
appdata_in_files = gnumeric.appdata.xml.in
appdata_DATA = gnumeric.appdata.xml

gnumeric.appdata.xml: gnumeric.appdata.xml.in Makefile $(INTLTOOL_MERGE) $(POFILES_FULL)
	LC_ALL=C $(INTLTOOL_MERGE) -x -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@

Applicationsdir = $(datadir)/applications/
Applications_in_files = gnumeric.desktop.in
Applications_DATA = gnumeric.desktop

gnumeric.desktop: gnumeric.desktop.in Makefile $(INTLTOOL_MERGE) $(POFILES_FULL)
	LC_ALL=C $(INTLTOOL_MERGE) -d -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@.tmp
	sed -e 's/F_OR_U/U/' <$@.tmp >$@.tmp2
	sed -e 's/BUGZILLAVERSION/@VERSION@/' <$@.tmp2 >$@
	rm -f $@.tmp $@.tmp2

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

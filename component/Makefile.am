goffice_gnumeric_LTLIBRARIES = gnumeric.la
goffice_gnumericdir = $(GOFFICE_PLUGINS_DIR)/gnumeric
gnumeric_la_LDFLAGS = -module -avoid-version -no-undefined
gnumeric_la_LIBADD = \
	$(top_builddir)/src/libspreadsheet.la \
	@GNUMERIC_LIBS@
xml_DATA = $(xml_in_files:.xml.in=.xml)

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src -I$(top_builddir)/src \
	-DGNOMELOCALEDIR=\"$(datadir)/locale\" @GNUMERIC_CFLAGS@

gnumeric_la_SOURCES =	\
		gnumeric.c

xml_in_files = plugin.xml.in
xmldir	 = $(goffice_gnumericdir)

@INTLTOOL_XML_RULE@

gtkactiondir=$(gnumeric_datadir)
gtkaction_DATA = Gnumeric-embed.xml
Gnumeric-embed.xml: Gnumeric-embed.xml.in
	sed -e 's/ _name *=/ name=/' <$<  >$@

EXTRA_DIST = $(xml_in_files) Gnumeric-embed.xml.in
DISTCLEANFILES = $(xml_in_files:.xml.in=.xml) Gnumeric-embed.xml

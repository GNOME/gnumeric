AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)				\
    $(GDA_CFLAGS)				\
    $(GNOMEDB_CFLAGS)

gnumeric_plugin_gdaifdir = $(gnumeric_plugindir)/gdaif
xmldir = $(gnumeric_plugin_gdaifdir)
gnumeric_plugin_gdaif_LTLIBRARIES = gdaif.la
gdaif_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
gdaif_la_SOURCES = plugin-gda.c
gdaif_la_LIBADD = $(GDA_LIBS) $(GNOMEDB_LIBS)

xml_in_files = plugin.xml.in ui.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

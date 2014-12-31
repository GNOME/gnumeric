AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)				\
    $(GDA_CFLAGS)				\
    $(GNOMEDB_CFLAGS)

gnumeric_plugin_gnomedbdir = $(gnumeric_plugindir)/gnome-db
xmldir = $(gnumeric_plugin_gnomedbdir)
gnumeric_plugin_gnomedb_LTLIBRARIES = gnomedb.la
gnomedb_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
gnomedb_la_SOURCES = plugin-gnomedb.c
gnomedb_la_LIBADD = $(GDA_LIBS) $(GNOMEDB_LIBS)

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

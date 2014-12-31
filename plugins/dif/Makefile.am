AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_difdir = $(gnumeric_plugindir)/dif
xmldir = $(gnumeric_plugin_difdir)
gnumeric_plugin_dif_LTLIBRARIES = dif.la
dif_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
dif_la_SOURCES = dif.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_xml_saxdir = $(gnumeric_plugindir)/xml_sax
gnumeric_plugin_xml_sax_DATA = plugin.xml
gnumeric_plugin_xml_sax_LTLIBRARIES = xml_sax.la
xml_sax_la_LDFLAGS = -module -avoid-version
xml_sax_la_SOURCES = xml2.c

EXTRA_DIST = $(gnumeric_plugin_xml_sax_DATA)

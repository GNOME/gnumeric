INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -DGNUMERIC_VERSION=\""$(gnumeric_version)"\"	\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_xml2dir = $(gnumeric_plugindir)/xml2
gnumeric_plugin_xml2_DATA = plugin.xml
gnumeric_plugin_xml2_LTLIBRARIES = xml2.la
xml2_la_LDFLAGS = -module -avoid-version
xml2_la_SOURCES = xml2.c

EXTRA_DIST = $(gnumeric_plugin_xml2_DATA)

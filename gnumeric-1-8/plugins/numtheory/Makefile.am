AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_numtheorydir = $(gnumeric_plugindir)/numtheory
xmldir = $(gnumeric_plugin_numtheorydir)
gnumeric_plugin_numtheory_LTLIBRARIES = numtheory.la
numtheory_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
numtheory_la_SOURCES = numtheory.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_xbasedir = $(gnumeric_plugindir)/xbase
xmldir = $(gnumeric_plugin_xbasedir)
gnumeric_plugin_xbase_LTLIBRARIES = xbase.la
xbase_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
xbase_la_SOURCES = boot.c xbase.h xbase.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

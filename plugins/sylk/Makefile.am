AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_sylkdir = $(gnumeric_plugindir)/sylk
xmldir = $(gnumeric_plugin_sylkdir)
gnumeric_plugin_sylk_LTLIBRARIES = sylk.la
sylk_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
sylk_la_SOURCES = sylk.c sylk-write.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

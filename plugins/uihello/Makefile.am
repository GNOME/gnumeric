AM_CPPFLAGS = \
    -DPLUGIN_ID=\"Gnumeric_UIHello\"		\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_testuidir = $(gnumeric_plugindir)/uihello
xmldir = $(gnumeric_plugin_testuidir)
gnumeric_plugin_testui_LTLIBRARIES = plugin.la
plugin_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
plugin_la_SOURCES = uihello.c

xml_in_files = plugin.xml.in hello.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

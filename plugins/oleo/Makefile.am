AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_oleodir = $(gnumeric_plugindir)/oleo
xmldir = $(gnumeric_plugin_oleodir)
gnumeric_plugin_oleo_LTLIBRARIES = oleo.la
oleo_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
oleo_la_SOURCES = boot.c oleo.h oleo.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

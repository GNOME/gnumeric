AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_scdir = $(gnumeric_plugindir)/sc
xmldir = $(gnumeric_plugin_scdir)
gnumeric_plugin_sc_LTLIBRARIES = sc.la
sc_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
sc_la_SOURCES = sc.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

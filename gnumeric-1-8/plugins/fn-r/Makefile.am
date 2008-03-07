AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_fnrdir = $(gnumeric_plugindir)/fn-r
xmldir = $(gnumeric_plugin_fnrdir)
gnumeric_plugin_fnr_LTLIBRARIES = rstat.la
rstat_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
rstat_la_SOURCES = functions.c extra.c extra.h

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

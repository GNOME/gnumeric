AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\"    \
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_mpsdir = $(gnumeric_plugindir)/mps
xmldir = $(gnumeric_plugin_mpsdir)
gnumeric_plugin_mps_LTLIBRARIES = mps.la
mps_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
mps_la_SOURCES = mps.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

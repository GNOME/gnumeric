INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\"    \
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_mpsdir = $(gnumeric_plugindir)/mps
xmldir = $(gnumeric_plugin_mpsdir)
gnumeric_plugin_mps_LTLIBRARIES = mps.la
mps_la_LDFLAGS = -module -avoid-version
mps_la_SOURCES = mps.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

# It's a hack, but we tag our own command onto the merge rule...
@INTLTOOL_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_mps_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files)

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

XML_I18N_XML_KIND = --pass-through
# It's a hack, but we tag our own command onto the merge rule...
@XML_I18N_MERGE_XML_RULE@
	sh $(top_builddir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_mps_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

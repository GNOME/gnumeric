INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)				\
    $(GDA_CFLAGS)

gnumeric_plugin_gdaifdir = $(gnumeric_plugindir)/gdaif
xmldir = $(gnumeric_plugin_gdaifdir)
gnumeric_plugin_gdaif_LTLIBRARIES = gdaif.la
gdaif_la_LDFLAGS = -module -avoid-version
gdaif_la_SOURCES = plugin-gda.c
gdaif_la_LIBADD = $(GDA_LIBS)

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
# It's a hack, but we tag our own command onto the merge rule...
@XML_I18N_MERGE_XML_RULE@
	sh $(top_builddir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_gdaif_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

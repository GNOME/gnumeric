INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_xml_saxdir = $(gnumeric_plugindir)/xml_sax
xmldir = $(gnumeric_plugin_xml_saxdir)
gnumeric_plugin_xml_sax_LTLIBRARIES = xml_sax.la
xml_sax_la_LDFLAGS = -module -avoid-version
xml_sax_la_SOURCES = xml-sax-read.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
# It's a hack, but we tag our own command onto the merge rule...
@XML_I18N_MERGE_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_xml_sax_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_derivativesdir = $(gnumeric_plugindir)/derivatives
xmldir = $(gnumeric_plugin_derivativesdir)
gnumeric_plugin_derivatives_LTLIBRARIES = derivatives.la
derivatives_la_LDFLAGS = -module -avoid-version
derivatives_la_SOURCES = options.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

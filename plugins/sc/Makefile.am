INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_scdir = $(gnumeric_plugindir)/sc
xmldir = $(gnumeric_plugin_scdir)
gnumeric_plugin_sc_LTLIBRARIES = sc.la
sc_la_LDFLAGS = -module -avoid-version
sc_la_SOURCES = sc.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_oleodir = $(gnumeric_plugindir)/oleo
xmldir = $(gnumeric_plugin_oleodir)
gnumeric_plugin_oleo_LTLIBRARIES = oleo.la
oleo_la_LDFLAGS = -module -avoid-version
oleo_la_SOURCES = boot.c oleo.h oleo.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

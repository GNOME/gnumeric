INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_xbasedir = $(gnumeric_plugindir)/xbase
xmldir = $(gnumeric_plugin_xbasedir)
gnumeric_plugin_xbase_LTLIBRARIES = xbase.la
xbase_la_LDFLAGS = -module -avoid-version
xbase_la_SOURCES = boot.c xbase.h xbase.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

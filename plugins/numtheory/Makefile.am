INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_numtheorydir = $(gnumeric_plugindir)/numtheory
xmldir = $(gnumeric_plugin_numtheorydir)
gnumeric_plugin_numtheory_LTLIBRARIES = numtheory.la
numtheory_la_LDFLAGS = -module -avoid-version
numtheory_la_SOURCES = numtheory.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

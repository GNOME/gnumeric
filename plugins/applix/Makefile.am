INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_applixdir = $(gnumeric_plugindir)/applix
xmldir = $(gnumeric_plugin_applixdir)
gnumeric_plugin_applix_LTLIBRARIES = applix.la
applix_la_LDFLAGS = -module -avoid-version
applix_la_SOURCES = boot.c applix.h applix-read.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

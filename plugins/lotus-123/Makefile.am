INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_lotusdir = $(gnumeric_plugindir)/lotus
xmldir = $(gnumeric_plugin_lotusdir)
gnumeric_plugin_lotus_LTLIBRARIES = lotus.la
lotus_la_LDFLAGS = -module -avoid-version
lotus_la_SOURCES = \
	boot.c			\
	lotus-types.h		\
	lotus.h			\
	lotus.c			\
	lotus-formula.h		\
	lotus-formula.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

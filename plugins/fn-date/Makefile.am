INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_fndatedir = $(gnumeric_plugindir)/fn-date
xmldir = $(gnumeric_plugin_fndatedir)
gnumeric_plugin_fndate_LTLIBRARIES = plugin.la
plugin_la_LDFLAGS = -module -avoid-version
plugin_la_SOURCES = functions.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

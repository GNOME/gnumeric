AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_applixdir = $(gnumeric_plugindir)/applix
xmldir = $(gnumeric_plugin_applixdir)
gnumeric_plugin_applix_LTLIBRARIES = applix.la
applix_la_LDFLAGS = -module @GNUMERIC_PLUGIN_LDFLAGS@
applix_la_SOURCES = boot.c applix.h applix-read.c applix-write.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_applixdir = $(gnumeric_plugindir)/applix
xmldir = $(gnumeric_plugin_applixdir)
gnumeric_plugin_applix_LTLIBRARIES = applix.la
applix_la_LDFLAGS = -module -avoid-version
applix_la_SOURCES = boot.c applix.h applix-read.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

# It's a hack, but we tag our own command onto the merge rule...
@INTLTOOL_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_applix_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files)

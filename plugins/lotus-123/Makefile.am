AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_lotusdir = $(gnumeric_plugindir)/lotus
xmldir = $(gnumeric_plugin_lotusdir)
gnumeric_plugin_lotus_LTLIBRARIES = lotus.la
lotus_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
lotus_la_SOURCES = \
	boot.c			\
	lotus-types.h		\
	lotus.h			\
	lotus.c			\
	lotus-formula.h		\
	lotus-formula.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

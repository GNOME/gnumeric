AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_qprodir = $(gnumeric_plugindir)/qpro
xmldir = $(gnumeric_plugin_qprodir)
gnumeric_plugin_qpro_LTLIBRARIES = qpro.la
qpro_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
qpro_la_SOURCES =		\
	qpro.h			\
	qpro-read.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

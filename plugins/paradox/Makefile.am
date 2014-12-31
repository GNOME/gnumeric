AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS) \
		$(PARADOX_CFLAGS)


gnumeric_plugin_paradoxdir = $(gnumeric_plugindir)/paradox
xmldir = $(gnumeric_plugin_paradoxdir)
gnumeric_plugin_paradox_LTLIBRARIES = paradox.la
paradox_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
paradox_la_SOURCES = px.h paradox.c
paradox_la_LIBADD = $(PARADOX_LIBS)

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_sample_datasourcedir = $(gnumeric_plugindir)/sample_datasource
xmldir = $(gnumeric_plugin_sample_datasourcedir)
gnumeric_plugin_sample_datasource_LTLIBRARIES = sample_datasource.la
sample_datasource_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
sample_datasource_la_SOURCES = sample_datasource.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

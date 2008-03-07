AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_fnerlangdir = $(gnumeric_plugindir)/fn-erlang
xmldir = $(gnumeric_plugin_fnerlangdir)
gnumeric_plugin_fnerlang_LTLIBRARIES = erlang.la
erlang_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
erlang_la_SOURCES = functions.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_excelpluginsdir = $(gnumeric_plugindir)/excelplugins
xmldir = $(gnumeric_plugin_excelpluginsdir)
gnumeric_plugin_excelplugins_LTLIBRARIES = plugin.la xlcall32.la
plugin_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
plugin_la_LIBADD = ../../src/libspreadsheet.la
plugin_la_SOURCES = excelplugins.c
xlcall32_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
xlcall32_la_SOURCES = xlcall32_emulation.c win32replacements.h xlcall.h

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

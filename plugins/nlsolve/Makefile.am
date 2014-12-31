AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_nlsolvedir = $(gnumeric_plugindir)/nlsolve
xmldir = $(gnumeric_plugin_nlsolvedir)
gnumeric_plugin_nlsolve_LTLIBRARIES = nlsolve.la
nlsolve_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
nlsolve_la_SOURCES = \
	boot.h boot.c \
	gnm-nlsolve.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = ChangeLog $(xml_in_files)
CLEANFILES = $(xml_DATA)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    -I$(top_srcdir)	-I$(top_builddir)	\
    -I$(top_srcdir)/src/cut-n-paste-code		\
    $(GNUMERIC_CFLAGS)

goffice_graph_xydir = $(gnumeric_plugindir)/plot/xy
xmldir = $(goffice_graph_xydir)
goffice_graph_xy_LTLIBRARIES = xy.la
xy_la_LDFLAGS = -module -avoid-version
xy_la_SOURCES = \
	gog-xy.c		\
	gog-xy.h		\
	gog-xy-prefs.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml) gog-xy-prefs.glade

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

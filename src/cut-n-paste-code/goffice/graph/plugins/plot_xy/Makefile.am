INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    -I$(top_srcdir)	-I$(top_builddir)	\
    $(GNUMERIC_CFLAGS)

goffice_graph_piedir = $(gnumeric_plugindir)/plot/pie
xmldir = $(goffice_graph_piedir)
goffice_graph_pie_LTLIBRARIES = pie.la
pie_la_LDFLAGS = -module -avoid-version
pie_la_SOURCES = \
	go-plot-pie.c		\
	go-plot-pie.h		\
	go-plot-pie-canvas.c	\
	go-plot-pie-pref.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml) go-plot-pie-pref.glade

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

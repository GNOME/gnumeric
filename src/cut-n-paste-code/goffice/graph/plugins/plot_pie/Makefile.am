INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    -I$(top_srcdir)	-I$(top_builddir)	\
    -I$(top_srcdir)/src/cut-n-paste-code		\
    -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
    $(GNUMERIC_CFLAGS)

goffice_graph_piedir = $(gnumeric_plugindir)/plot_pie
xmldir	 = $(goffice_graph_piedir)
gladedir = $(goffice_graph_piedir)

goffice_graph_pie_LTLIBRARIES = pie.la
pie_la_LDFLAGS = -module -avoid-version
pie_la_SOURCES = \
	go-plot-pie.c		\
	go-plot-pie.h		\
	go-plot-pie-pref.c	\
	go-canvas-plot-pie.c

xml_in_files = plugin.xml.in plot-types.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml) go-plot-pie-pref.glade

@INTLTOOL_XML_RULE@

glade_DATA = go-plot-pie-pref.glade

# do not use the intl-tool stuff to merge the text back
# its simpler to just use gettext directly
plot-types.xml : plot-types.xml.in
	cp $< $@

EXTRA_DIST = $(xml_in_files) $(glade_DATA)
DISTCLEANFILES = $(xml_in_files:.xml.in=.xml)

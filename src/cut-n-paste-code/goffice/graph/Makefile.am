SUBDIRS = plugins # tests

noinst_LTLIBRARIES = libgoffice-graph.la

libgoffice_graph_la_SOURCES = \
	goffice-graph.h			\
	gog-object.c			\
	gog-object.h			\
	gog-object-xml.c		\
	gog-object-xml.h		\
	gog-styled-object.c		\
	gog-styled-object.h		\
	gog-view.c			\
	gog-view.h			\
	\
	gog-graph.c			\
	gog-graph.h			\
	gog-graph-impl.h		\
	gog-chart.c			\
	gog-chart.h			\
	gog-chart-impl.h		\
	\
	gog-axis.c			\
	gog-axis.h			\
	gog-legend.c			\
	gog-legend.h			\
	gog-label.c			\
	gog-label.h			\
	\
	gog-style.c			\
	gog-style.h			\
	gog-theme.c			\
	gog-theme.h			\
	\
	gog-plot.c			\
	gog-plot.h			\
	gog-plot-impl.h			\
	gog-plot-engine.h		\
	gog-plot-engine.c		\
	gog-series.c			\
	gog-series.h			\
	gog-series-impl.h		\
	gog-data-allocator.c		\
	gog-data-allocator.h		\
	gog-data-set.c			\
	gog-data-set.h			\
	go-data.c			\
	go-data.h			\
	go-data-impl.h			\
	\
	gog-guru.c			\
	gog-guru.h			\
	gog-renderer.c			\
	gog-renderer.h			\
	gog-renderer-impl.h		\
	gog-renderer-pixbuf.c		\
	gog-renderer-pixbuf.h		\
	gog-control-foocanvas.c		\
	gog-control-foocanvas.h		\
	gog-renderer-gnome-print.c	\
	gog-renderer-gnome-print.h


gnumeric_glade_DATA = \
	gog-guru.glade			\
	gog-style-prefs.glade

EXTRA_DIST = $(gnumeric_glade_DATA)

# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)


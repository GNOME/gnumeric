SUBDIRS = plugins # tests

noinst_LIBRARIES = libgoffice-graph.a

# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)

libgoffice_graph_a_SOURCES = \
	goffice-graph.h			\
	gog-object.c			\
	gog-object.h			\
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
	gog-background.c		\
	gog-background.h		\
	gog-legend.c			\
	gog-legend.h			\
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
	go-data.c			\
	go-data.h			\
	go-data-impl.h			\
	\
	gog-guru.c			\
	gog-renderer.c			\
	gog-renderer.h			\
	gog-renderer-impl.h		\
	gog-renderer-pixbuf.c		\
	gog-renderer-pixbuf.h		\
	gog-control-foocanvas.c		\
	gog-control-foocanvas.h

gnumeric_glade_DATA = \
	gog-guru.glade

EXTRA_DIST = $(gnumeric_glade_DATA)

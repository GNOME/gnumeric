SUBDIRS = plugins

noinst_LIBRARIES = libgoffice-graph.a

# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)

libgoffice_graph_a_SOURCES = \
	goffice-graph.h			\
	go-graph.c			\
	go-graph.h			\
	go-graph-impl.h			\
	go-graph-item.c			\
	go-graph-item.h			\
	go-chart.c			\
	go-chart.h			\
	go-chart-impl.h			\
	go-plot.c			\
	go-plot.h			\
	go-plot-impl.h			\
	go-plot-engine.h		\
	go-plot-engine.c		\
	go-plot-data.c			\
	go-plot-data.h			\
	go-plot-data-impl.h		\
	go-data.c			\
	go-data.h			\
	go-data-impl.h			\
	\
	go-graph-style.c		\
	go-graph-style.h		\
	go-graph-theme.c		\
	go-graph-theme.h		\
	\
	go-graph-frame.c		\
	go-graph-frame.h		\
	go-graph-axis.c			\
	go-graph-axis.h			\
	\
	go-graph-guru.c			\
	go-canvas-util.h		\
	go-canvas-util.c		\
	go-canvas-group.c		\
	go-canvas-group.h		\
	go-canvas-group-impl.h		\
	go-canvas-item.c		\
	go-canvas-item.h		\
	go-canvas-chart-group.c		\
	go-canvas-chart-group.h		\
	go-canvas-2dplot-group.c	\
	go-canvas-2dplot-group.h

gnumeric_glade_DATA = \
	go-graph-guru.glade

EXTRA_DIST = $(gnumeric_glade_DATA)

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)					\
	$(EXTRA_GNOME_CFLAGS)

if BONOBO
bin_PROGRAMS = graph-bonobo

noinst_PROGRAMS = sample
endif

graph_bonobo_SOURCES =		\
	graph.c			\
	graph.h			\
	graph-vector.c		\
	graph-vector.h		\
	graph-view.c		\
	graph-view.h		\
	graph-view-colbar.c	\
	graph-view-colbar.h	\
	graph-view-plot.c	\
	graph-view-plot.h	\
	graph-view-scatter.c	\
	graph-view-scatter.h	\
	graph-view-stock.c	\
	graph-view-stock.h	\
	graph-view-util.c	\
	graph-view-util.h	\
	layout.c		\
	layout.h		\
	layout-view.c		\
	layout-view.h		\
	main.c			\
	vector.c		\
	vector.h

graph_bonobo_LDADD =		\
	$(EXTRA_GNOME_LIBS)	\
	../idl/libGnumeric.a	\
	../idl/libGraph.a	\
	$(INTLLIBS)

sample_SOURCES =		\
	sample.c		\
	vector.c		\
	vector.h

sample_LDADD =			\
	$(EXTRA_GNOME_LIBS)	\
	../idl/libGnumeric.a	\
	../idl/libGraph.a	\
	$(INTLLIBS)


gnorbadir = $(sysconfdir)/CORBA/servers
gnorba_DATA = graph.gnorba

EXTRA_DIST = graph.gnorba

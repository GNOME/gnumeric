INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)					\
	$(EXTRA_GNOME_CFLAGS)

bin_PROGRAMS = graph


GRAPH_CORBA_GENERATED = 		\
	Graph-common.c			\
	Graph-skels.c			\
	Graph-stubs.c			\
	Graph.h

graph_SOURCES =			\
	data-source.c		\
	data-source.h		\
	series.c		\
	series.h		\
	vector.c		\
	vector.h		\
	$(GRAPH_CORBA_GENERATED)

BUILT_SOURCES = $(GRAPH_CORBA_GENERATED)

$(GRAPH_CORBA_GENERATED): my_graph_idl

my_graph_idl: Graph.idl $(top_srcdir)/src/Gnumeric-value.idl $(top_srcdir)/src/Gnumeric.idl
	orbit-idl -I`$(GNOME_CONFIG) --datadir`/idl -I$(srcdir) $(srcdir)/Graph.idl
	touch my_graph_idl

CLEANFILES = my_graph_idl

MYHACKAROUNDAUTOMAKE=$(shell echo $(GNUMERIC_CORBA_GENERATED))

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)					\
	$(EXTRA_GNOME_CFLAGS)

if BONOBO
noinst_LIBRARIES = libGnumeric.a libGraph.a 
else
endif

GNUMERIC_CORBA_GENERATED = 		\
	Gnumeric-common.c		\
	Gnumeric-skels.c		\
	Gnumeric-stubs.c		\
	Gnumeric.h

GRAPH_CORBA_GENERATED = 		\
	Graph-common.c			\
	Graph-skels.c			\
	Graph-stubs.c			\
	Graph.h	

libGraph_a_SOURCES    = $(GRAPH_CORBA_GENERATED)
libGnumeric_a_SOURCES = $(GNUMERIC_CORBA_GENERATED)

$(GRAPH_CORBA_GENERATED): Graph.idl $(top_srcdir)/idl/Gnumeric.idl
	orbit-idl -I`$(GNOME_CONFIG) --datadir`/idl -I$(srcdir) -I$(top_srcdir) $(srcdir)/Graph.idl
	touch my_graph_idl

$(GNUMERIC_CORBA_GENERATED): Gnumeric.idl 
	orbit-idl -I`$(GNOME_CONFIG) --datadir`/idl -I$(srcdir) $(srcdir)/Gnumeric.idl 
	touch my_gnumeric_idl

Gnumeric-impl.o: Gnumeric.h
Graph-impl.o:    Graph.h

idldir   = $(datadir)/idl
idl_DATA = Graph.idl Gnumeric.idl 

MYHACKAROUNDAUTOMAKE=$(shell echo $(GRAPH_CORBA_GENERATED)) $(shell echo $(GNUMERIC_CORBA_GENERATED))

EXTRA_DIST = $(MYHACKAROUNDAUTOMAKE)
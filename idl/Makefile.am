INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)				 	\
	-I$(top_srcdir)					\
	$(EXTRA_GNOME_CFLAGS)

if BONOBO
noinst_LIBRARIES = libGnumericIDL.a
endif

GNUMERIC_GENERATED = 		\
	GNOME_Gnumeric-common.c		\
	GNOME_Gnumeric-skels.c		\
	GNOME_Gnumeric-stubs.c		\
	GNOME_Gnumeric.h

GNUMERIC_GRAPH_GENERATED = 	\
	GNOME_Gnumeric_Graph-common.c	\
	GNOME_Gnumeric_Graph-skels.c		\
	GNOME_Gnumeric_Graph-stubs.c		\
	GNOME_Gnumeric_Graph.h	

if ENABLE_EVOLUTION
EVOLUTION_GENERATED =		\
	Evolution-Composer.h		\
	Evolution-Composer-common.c	\
	Evolution-Composer-skels.c	\
	Evolution-Composer-stubs.c
endif

libGnumericIDL_a_SOURCES =		\
	$(GNUMERIC_GENERATED)		\
	$(GNUMERIC_GRAPH_GENERATED)	\
	$(EVOLUTION_GENERATED)

stamp_Gnumeric_idl : $(srcdir)/GNOME_Gnumeric.idl 
	$(ORBIT_IDL) -I$(srcdir) $(IDL_FLAGS) $<
	touch $@

$(GNUMERIC_GENERATED): stamp_Gnumeric_idl

stamp_Gnumeric_Graph_idl : $(srcdir)/GNOME_Gnumeric_Graph.idl
	$(ORBIT_IDL) -I$(srcdir) $(IDL_FLAGS) $<
	touch $@

$(GNUMERIC_GRAPH_GENERATED): stamp_Gnumeric_Graph_idl

if ENABLE_EVOLUTION
stamp_evolution_idl :
	$(ORBIT_IDL) $(IDL_FLAGS) `$(GNOME_CONFIG) --datadir`/idl/Evolution-Composer.idl
	touch $@

$(EVOLUTION_GENERATED) : stamp_evolution_idl

endif

idldir   = $(gnumeric_datadir)/idl
idl_DATA = GNOME_Gnumeric_Graph.idl GNOME_Gnumeric.idl 

EXTRA_DIST = $(idl_DATA)
# Do not use built sources BUILT_SOURCES
# it attempts to build the source even if it is not used.
CLEANFILES  = $(GNUMERIC_GENERATED)		\
	      $(GNUMERIC_GRAPH_GENERATED)	\
	      $(EVOLUTION_GENERATED)		\
		stamp_Gnumeric_idl stamp_Gnumeric_Graph_idl stamp_evolution_idl

dist-hook :
	cd $(distdir) && rm -f $(CLEANFILES)

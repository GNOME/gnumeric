INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)				 	\
	-I$(top_srcdir)					\
	$(EXTRA_GNOME_CFLAGS)

if BONOBO
noinst_LIBRARIES = libGnumericIDL.a
endif

GNUMERIC_GENERATED = 		\
	Gnumeric-common.c		\
	Gnumeric-skels.c		\
	Gnumeric-stubs.c		\
	Gnumeric.h

if ENABLE_EVOLUTION
EVOLUTION_GENERATED =		\
	Evolution-Composer.h		\
	Evolution-Composer-common.c	\
	Evolution-Composer-skels.c	\
	Evolution-Composer-stubs.c
endif

GNUMERIC_GRAPHS_GENERATED = 	\
	gnumeric-graphs-common.c	\
	gnumeric-graphs-skels.c		\
	gnumeric-graphs-stubs.c		\
	gnumeric-graphs.h	

libGnumericIDL_a_SOURCES =		\
	$(GNUMERIC_GENERATED)		\
	$(GNUMERIC_GRAPHS_GENERATED)	\
	$(EVOLUTION_GENERATED)

stamp_gnumeric : $(srcdir)/Gnumeric.idl 
	$(ORBIT_IDL) -I$(srcdir) $(IDL_FLAGS) $<
	touch $@

$(GNUMERIC_GENERATED): stamp_gnumeric

stamp_gnumeric_graphs : $(srcdir)/gnumeric-graphs.idl
	$(ORBIT_IDL) -I$(srcdir) $(IDL_FLAGS) $<
	touch $@

$(GNUMERIC_GRAPHS_GENERATED): stamp_gnumeric_graphs

if ENABLE_EVOLUTION
stamp_evolution :
	$(ORBIT_IDL) $(IDL_FLAGS) `$(GNOME_CONFIG) --datadir`/idl/Evolution-Composer.idl
	touch $@

$(EVOLUTION_GENERATED) : stamp_evolution

endif

idldir   = $(gnumeric_datadir)/idl
idl_DATA = gnumeric-graphs.idl Gnumeric.idl 

EXTRA_DIST = $(idl_DATA)
# Do not use built sources BUILT_SOURCES
# it attempts to build the source even if it is not used.
CLEANFILES  += $(GNUMERIC_GENERATED)		\
	       $(GNUMERIC_GRAPHS_GENERATED)	\
	       $(EVOLUTION_GENERATED)		\
		stamp_gnumeric stamp_gnumeric_graphs stamp_evolution

dist-hook :
	cd $(distdir) && rm -f $(CLEANFILES)

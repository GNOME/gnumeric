INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)				 	\
	-I$(top_srcdir)					\
	$(EXTRA_GNOME_CFLAGS)

if BONOBO
noinst_LIBRARIES = libGnumericIDL.a
else
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

$(GNUMERIC_GENERATED): $(srcdir)/Gnumeric.idl 
	orbit-idl -I$(srcdir) $(IDL_FLAGS) $(srcdir)/Gnumeric.idl 
	touch my_gnumeric_idl

$(GNUMERIC_GRAPHS_GENERATED): $(srcdir)/gnumeric-graphs.idl
	orbit-idl -I$(srcdir) $(IDL_FLAGS) $(srcdir)/gnumeric-graphs.idl 
	touch my_gnumeric_graphs_idl

$(EVOLUTION_GENERATED):
	$(ORBIT_IDL) `$(GNOME_CONFIG) --datadir`/idl/Evolution-Composer.idl

idldir   = $(gnumeric_datadir)/idl
idl_DATA = gnumeric-graphs.idl Gnumeric.idl 

EXTRA_DIST = $(idl_DATA)
BUILT_SOURCES = $(EVOLUTION_GENERATED)
CLEANFILES  += my_gnumeric_idl $(GNUMERIC_GENERATED) \
	       my_gnumeric_graphs_idl $(GNUMERIC_GRAPHS_GENERATED) \
	       $(BUILT_SOURCES)

dist-hook :
	cd $(distdir) && rm -f $(CLEANFILES)

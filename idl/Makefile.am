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

$(GNUMERIC_GENERATED): $(srcdir)/Gnumeric.idl 
	orbit-idl -I$(srcdir) $(IDL_FLAGS) $(srcdir)/Gnumeric.idl 

$(GNUMERIC_GRAPHS_GENERATED): $(srcdir)/gnumeric-graphs.idl
	orbit-idl -I$(srcdir) $(IDL_FLAGS) $(srcdir)/gnumeric-graphs.idl 

if ENABLE_EVOLUTION
$(EVOLUTION_GENERATED):
	$(ORBIT_IDL) `$(GNOME_CONFIG) --datadir`/idl/Evolution-Composer.idl
endif

idldir   = $(gnumeric_datadir)/idl
idl_DATA = gnumeric-graphs.idl Gnumeric.idl 

EXTRA_DIST = $(idl_DATA)
# Do not use built sources BUILT_SOURCES
# it attempts to build the source even if it is not used.
CLEANFILES  += $(GNUMERIC_GENERATED) \
	       $(GNUMERIC_GRAPHS_GENERATED) \
	       $(EVOLUTION_GENERATED)

dist-hook :
	cd $(distdir) && rm -f $(CLEANFILES)

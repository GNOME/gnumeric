INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	$(GNUMERIC_CFLAGS)

if WITH_BONOBO
noinst_LIBRARIES = libGnumericIDL.a
endif

GNUMERIC_GENERATED = 		\
	GNOME_Gnumeric-common.c		\
	GNOME_Gnumeric-skels.c		\
	GNOME_Gnumeric-stubs.c		\
	GNOME_Gnumeric.h

if ENABLE_EVOLUTION
EVOLUTION_GENERATED =		\
	Evolution-Composer.h		\
	Evolution-Composer-common.c	\
	Evolution-Composer-skels.c	\
	Evolution-Composer-stubs.c
endif

libGnumericIDL_a_SOURCES =		\
	$(GNUMERIC_GENERATED)		\
	$(EVOLUTION_GENERATED)

stamp_Gnumeric_idl : $(srcdir)/GNOME_Gnumeric.idl 
	$(ORBIT_IDL) -I$(srcdir) $< && touch $@

$(GNUMERIC_GENERATED): stamp_Gnumeric_idl

if ENABLE_EVOLUTION
EVOLUTION_IDL_DIR = $(shell $(GNOME_CONFIG) --datadir)/idl
stamp_evolution_idl :
	$(ORBIT_IDL) `$(EVOLUTION_IDL_DIR)/idl/Evolution-Composer.idl && touch $@

$(EVOLUTION_GENERATED) : stamp_evolution_idl

endif

idldir   = $(gnumeric_datadir)/idl
idl_DATA = GNOME_Gnumeric.idl 

EXTRA_DIST = $(idl_DATA)
# Do not use built sources BUILT_SOURCES
# it attempts to build the source even if it is not used.
CLEANFILES  = $(GNUMERIC_GENERATED)		\
	      $(EVOLUTION_GENERATED)		\
		stamp_Gnumeric_idl stamp_evolution_idl

dist-hook :
	cd $(distdir) && rm -f $(CLEANFILES)

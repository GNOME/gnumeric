AM_CPPFLAGS = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	$(GNUMERIC_CFLAGS)

if WITH_CORBA
noinst_LIBRARIES = libGnumericIDL.a
endif

GNUMERIC_GENERATED = 			\
	GNOME_Gnumeric-common.c		\
	GNOME_Gnumeric-skels.c		\
	GNOME_Gnumeric-stubs.c		\
	GNOME_Gnumeric.h

# A hint is needed to build the header(s) first:
BUILT_SOURCES = GNOME_Gnumeric.h

if ENABLE_EVOLUTION
EVOLUTION_GENERATED =			\
	Evolution-Composer.h		\
	Evolution-Composer-common.c	\
	Evolution-Composer-skels.c	\
	Evolution-Composer-stubs.c
BUILT_SOURCES += Evolution-Composer.h
endif

nodist_libGnumericIDL_a_SOURCES =	\
	$(GNUMERIC_GENERATED)		\
	$(EVOLUTION_GENERATED)

stamp_Gnumeric_idl: $(srcdir)/GNOME_Gnumeric.idl
	$(ORBIT_IDL) -I$(srcdir) $< && touch $@

$(GNUMERIC_GENERATED): stamp_Gnumeric_idl

if ENABLE_EVOLUTION
EVOLUTION_IDL_DIR = $(shell $(GNOME_CONFIG) --datadir)/idl
stamp_evolution_idl:
	$(ORBIT_IDL) `$(EVOLUTION_IDL_DIR)/idl/Evolution-Composer.idl && touch $@

$(EVOLUTION_GENERATED): stamp_evolution_idl

endif

idldir   = $(gnumeric_datadir)/idl
idl_DATA = GNOME_Gnumeric.idl

EXTRA_DIST = $(idl_DATA)

CLEANFILES  = $(nodist_libGnumericIDL_a_SOURCES) \
		stamp_Gnumeric_idl stamp_evolution_idl

1.
Frantisek Listopad

Buh belha klopyta
z komory do kuchyne
vari bramboracku
oplachne starou lzici
sedne si u okna
diva si udivene
To jsem to dopracoval

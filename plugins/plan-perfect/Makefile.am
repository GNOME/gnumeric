# Do not build by default until we are ready.
if WITH_PLAN_PERFECT
PP_TARGET = gnum_plan_perfect.la
else
PP_TARGET =
endif

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = $(PP_TARGET)
gnum_plan_perfect_la_LDFLAGS = -module -avoid-version
gnum_plan_perfect_la_SOURCES = pln.h pln.c

# Do not build by default until we are ready.
if WITH_PLAN_PERFECT
PP_LTLIBRARIES_TARGET = plan_perfect.la
PP_DATA_TARGET = plugin.xml
else
PP_LTLIBRARIES_TARGET =
PP_DATA_TARGET =
endif

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_plan_perfectdir = $(gnumeric_plugindir)/plan_perfect
gnumeric_plugin_plan_perfect_DATA = $(PP_DATA_TARGET)
gnumeric_plugin_plan_perfect_LTLIBRARIES = $(PP_LTLIBRARIES_TARGET)
plan_perfect_la_LDFLAGS = -module -avoid-version
plan_perfect_la_SOURCES = pln.h pln.c

# EXTRA_DIST = $(gnumeric_plugin_plan_perfect_DATA)

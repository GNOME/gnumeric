INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -DGNUMERIC_VERSION=\""$(gnumeric_version)"\"	\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_plan_perfect.la
gnum_plan_perfect_la_LDFLAGS = -module -avoid-version
gnum_plan_perfect_la_SOURCES = pln.h pln.c

gnumeric_guiledir  = $(gnumeric_datadir)/guile
INCLUDES = 					\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(includedir)	-I$(top_srcdir)/src	\
    $(GUILE_INCS)				\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_guiledir = $(gnumeric_plugindir)/guile
gnumeric_plugin_guile_DATA = plugin.xml
gnumeric_plugin_guile_LTLIBRARIES = guile.la
guile_la_LDFLAGS = -module -avoid-version
guile_la_SOURCES = smob-value.c guile-support.c plugin.c guile-support.h smob-value.h
guile_la_LIBADD = $(GUILE_LIBS)

Guilescriptsdir = $(gnumeric_guiledir)
Guilescripts_DATA = functions.scm gnumeric_startup.scm
EXTRA_DIST = $(Guilescripts_DATA)

gnumeric_guiledir  = $(gnumeric_datadir)/guile
INCLUDES = 					\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(includedir)	-I$(top_srcdir)/src	\
    $(GUILE_INCS)				\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_guile.la
gnum_guile_la_LDFLAGS = -module -avoid-version
gnum_guile_la_SOURCES = guile-support.c plugin.c 
gnum_guile_la_LIBADD = $(GUILE_LIBS)

Guilescriptsdir = $(gnumeric_guiledir)
Guilescripts_DATA = functions.scm gnumeric_startup.scm
EXTRA_DIST = $(Guilescripts_DATA)

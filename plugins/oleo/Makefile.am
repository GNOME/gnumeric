INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_oleo.la
gnum_oleo_la_LDFLAGS = -module -avoid-version
gnum_oleo_la_SOURCES = boot.c oleo.h oleo.c

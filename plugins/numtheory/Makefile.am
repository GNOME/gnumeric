INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_numtheory.la
gnum_numtheory_la_LDFLAGS = -module -avoid-version
gnum_numtheory_la_SOURCES = numtheory.c

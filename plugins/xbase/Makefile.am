INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_xbase.la
gnum_xbase_la_LDFLAGS = -module -avoid-version
gnum_xbase_la_SOURCES = boot.c xbase.h xbase.c

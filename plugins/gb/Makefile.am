INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(includedir)	-I$(top_srcdir)/src	\
    -I$(top_srcdir)/libole2			\
    $(GNOME_INCLUDEDIR) $(GB_CFLAGS)

gnumeric_plugin_LTLIBRARIES = gnum_gb.la
gnum_gb_la_LDFLAGS = -module -avoid-version
gnum_gb_la_SOURCES = plugin.c streams.c streams.h
gnum_gb_la_LIBADD = $(GB_LIBS) $(EXTRA_GNOME_LIBS) -lm @GLIB_LIBS@ -lgb -lgbrun

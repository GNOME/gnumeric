INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -DGNUMERIC_VERSION=\""$(gnumeric_version)"\"	\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR) $(GB_CFLAGS)

gnumeric_plugin_LTLIBRARIES = gnum_gb.la
gnum_gb_la_LDFLAGS = -module -avoid-version
gnum_gb_la_SOURCES = plugin.c
gnum_gb_la_LIBADD = $(GB_LIBS) $(EXTRA_GNOME_LIBS) -lm @GLIB_LIBS@ -lgb -lgbrun

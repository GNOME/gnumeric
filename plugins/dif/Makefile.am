INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_dif.la
gnum_dif_la_LDFLAGS = -module -avoid-version
gnum_dif_la_SOURCES = dif.h dif.c

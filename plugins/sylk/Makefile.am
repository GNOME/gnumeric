INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_sylk.la
gnum_sylk_la_LDFLAGS = -module -avoid-version
gnum_sylk_la_SOURCES = sylk.c


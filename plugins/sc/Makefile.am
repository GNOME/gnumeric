INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_sc.la
gnum_sc_la_LDFLAGS = -module -avoid-version
gnum_sc_la_SOURCES = sc.c

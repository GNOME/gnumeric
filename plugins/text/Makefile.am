INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_text.la
gnum_text_la_LDFLAGS = -module -avoid-version
gnum_text_la_SOURCES = text-io.c

GDA_TARGET = gnum_gdaif.la

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)					\
    $(GDA_CFLAGS)

gnumeric_plugin_LTLIBRARIES = $(GDA_TARGET)
gnum_gdaif_la_LDFLAGS = -module -avoid-version
gnum_gdaif_la_SOURCES = plugin-gda.c
gnum_gdaif_la_LIBADD = $(GDA_LIBS)

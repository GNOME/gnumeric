INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)					\
    $(GDA_CFLAGS)

gnumeric_plugin_gdaifdir = $(gnumeric_plugindir)/gdaif
gnumeric_plugin_gdaif_DATA = plugin.xml
gnumeric_plugin_gdaif_LTLIBRARIES = gdaif.la
gdaif_la_LDFLAGS = -module -avoid-version
gdaif_la_SOURCES = plugin-gda.c
gdaif_la_LIBADD = $(GDA_LIBS)

EXTRA_DIST = $(gnumeric_plugin_gdaif_DATA)

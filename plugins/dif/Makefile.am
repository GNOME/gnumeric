INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(includedir)	-I$(top_srcdir)/src	\
    -I$(top_builddir)/src			\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_difdir = $(gnumeric_plugindir)/dif
gnumeric_plugin_dif_DATA = plugin.xml
gnumeric_plugin_dif_LTLIBRARIES = dif.la
dif_la_LDFLAGS = -module -avoid-version
dif_la_SOURCES = dif.c

EXTRA_DIST = $(gnumeric_plugin_dif_DATA)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_oleodir = $(gnumeric_plugindir)/oleo
gnumeric_plugin_oleo_DATA = plugin.xml
gnumeric_plugin_oleo_LTLIBRARIES = oleo.la
oleo_la_LDFLAGS = -module -avoid-version
oleo_la_SOURCES = boot.c oleo.h oleo.c

EXTRA_DIST = $(gnumeric_plugin_oleo_DATA)

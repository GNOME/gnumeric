INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_scdir = $(gnumeric_plugindir)/sc
gnumeric_plugin_sc_DATA = plugin.xml
gnumeric_plugin_sc_LTLIBRARIES = sc.la
sc_la_LDFLAGS = -module -avoid-version
sc_la_SOURCES = sc.c

EXTRA_DIST = $(gnumeric_plugin_sc_DATA)

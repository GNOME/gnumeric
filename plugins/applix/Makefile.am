INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_applixdir = $(gnumeric_plugindir)/applix
gnumeric_plugin_applix_DATA = plugin.xml
gnumeric_plugin_applix_LTLIBRARIES = applix.la
applix_la_LDFLAGS = -module -avoid-version
applix_la_SOURCES = boot.c applix.h applix-read.c

EXTRA_DIST = $(gnumeric_plugin_applix_DATA)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_numtheorydir = $(gnumeric_plugindir)/numtheory
gnumeric_plugin_numtheory_DATA = plugin.xml
gnumeric_plugin_numtheory_LTLIBRARIES = numtheory.la
numtheory_la_LDFLAGS = -module -avoid-version
numtheory_la_SOURCES = numtheory.c

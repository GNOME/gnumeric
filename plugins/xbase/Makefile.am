INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_xbasedir = $(gnumeric_plugindir)/xbase
gnumeric_plugin_xbase_DATA = plugin.xml
gnumeric_plugin_xbase_LTLIBRARIES = xbase.la
xbase_la_LDFLAGS = -module -avoid-version
xbase_la_SOURCES = boot.c xbase.h xbase.c

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_sylkdir = $(gnumeric_plugindir)/sylk
gnumeric_plugin_sylk_DATA = plugin.xml
gnumeric_plugin_sylk_LTLIBRARIES = sylk.la
sylk_la_LDFLAGS = -module -avoid-version
sylk_la_SOURCES = sylk.c

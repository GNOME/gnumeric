INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_derivativesdir = $(gnumeric_plugindir)/derivatives
gnumeric_plugin_derivatives_DATA = plugin.xml
gnumeric_plugin_derivatives_LTLIBRARIES = derivatives.la
derivatives_la_LDFLAGS = -module -avoid-version
derivatives_la_SOURCES = options.c

EXTRA_DIST = $(gnumeric_plugin_derivatives_DATA)

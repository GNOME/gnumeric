INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_lotusdir = $(gnumeric_plugindir)/lotus
gnumeric_plugin_lotus_DATA = plugin.xml
gnumeric_plugin_lotus_LTLIBRARIES = lotus.la
lotus_la_LDFLAGS = -module -avoid-version
lotus_la_SOURCES = \
	boot.c			\
	lotus-types.h		\
	lotus.h			\
	lotus.c			\
	lotus-formula.h		\
	lotus-formula.c

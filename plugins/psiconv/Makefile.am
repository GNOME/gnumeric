# TODO: Use %PSICONV_FLAGS% and %PSICONV_LIBS%

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR) $(PSICONV_CFLAGS)

gnumeric_plugin_psiconvdir = $(gnumeric_plugindir)/psiconv
gnumeric_plugin_psiconv_DATA = plugin.xml
gnumeric_plugin_psiconv_LTLIBRARIES = psiconv.la
psiconv_la_LDFLAGS = -module -avoid-version $(PSICONV_LIBS)
psiconv_la_SOURCES = boot.c psiconv-read.c psiconv-plugin.h

EXTRA_DIST = $(gnumeric_plugin_psiconv_DATA)

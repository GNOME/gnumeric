# TODO: Use %PSICONV_FLAGS% and %PSICONV_LIBS%

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR) $(PSICONV_CFLAGS)

gnumeric_plugin_psiconvdir = $(gnumeric_plugindir)/psiconv
xmldir = $(gnumeric_plugin_psiconvdir)
gnumeric_plugin_psiconv_LTLIBRARIES = psiconv.la
psiconv_la_LDFLAGS = -module -avoid-version $(PSICONV_LIBS)
psiconv_la_SOURCES = boot.c psiconv-read.c psiconv-plugin.h

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

# TODO: Use %PSICONV_FLAGS% and %PSICONV_LIBS%

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS) $(PSICONV_CFLAGS)

gnumeric_plugin_psiconvdir = $(gnumeric_plugindir)/psiconv
xmldir = $(gnumeric_plugin_psiconvdir)
gnumeric_plugin_psiconv_LTLIBRARIES = psiconv.la
psiconv_la_LDFLAGS = -module -avoid-version $(PSICONV_LIBS)
psiconv_la_SOURCES = boot.c psiconv-read.c psiconv-plugin.h

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

# It's a hack, but we tag our own command onto the merge rule...
@INTLTOOL_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_psiconv_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files)

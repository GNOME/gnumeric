AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS) $(PSICONV_CFLAGS)

gnumeric_plugin_psiconvdir = $(gnumeric_plugindir)/psiconv
xmldir = $(gnumeric_plugin_psiconvdir)
gnumeric_plugin_psiconv_LTLIBRARIES = psiconv.la
psiconv_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
psiconv_la_LIBADD  = $(PSICONV_LIBS)
psiconv_la_SOURCES = boot.c psiconv-read.c psiconv-plugin.h

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

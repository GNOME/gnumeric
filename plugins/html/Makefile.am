AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_htmldir = $(gnumeric_plugindir)/html
xmldir = $(gnumeric_plugin_htmldir)
gnumeric_plugin_html_LTLIBRARIES = html.la
html_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
html_la_SOURCES = \
    boot.h boot.c \
    html.h html.c html_read.c \
    latex.h latex.c \
    roff.h roff.c \
    font.h font.c
#No longer used now that printing is available.
#epsf.h epsf.c ps.h ps.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = README ChangeLog $(xml_in_files)
CLEANFILES = $(xml_DATA)

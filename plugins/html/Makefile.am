INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_htmldir = $(gnumeric_plugindir)/html
xmldir = $(gnumeric_plugin_htmldir)
gnumeric_plugin_html_LTLIBRARIES = html.la
html_la_LDFLAGS = -module -avoid-version
html_la_SOURCES = \
    boot.h boot.c \
    html.h html.c \
    latex.h latex.c \
    roff.h roff.c \
    font.h font.c 
#No longer used now that printing is available.
#epsf.h epsf.c ps.h ps.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = README ChangeLog $(xml_in_files) $(xml_DATA)

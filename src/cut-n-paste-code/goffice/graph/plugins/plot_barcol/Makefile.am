INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    -I$(top_srcdir)	-I$(top_builddir)	\
    -I$(top_srcdir)/src/cut-n-paste-code		\
    $(GNUMERIC_CFLAGS)

goffice_graph_barcoldir = $(gnumeric_plugindir)/plot_barcol
xmldir	 = $(goffice_graph_barcoldir)
gladedir = $(goffice_graph_barcoldir)

goffice_graph_barcol_LTLIBRARIES = barcol.la
barcol_la_LDFLAGS = -module -avoid-version
barcol_la_SOURCES = \
	gog-barcol-plot.c		\
	gog-barcol-plot.h		\
	gog-barcol-plot-pref.c

xml_in_files = plugin.xml.in plot-types.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml) gog-barcol-plot-pref.glade

@INTLTOOL_XML_RULE@

glade_DATA = gog-barcol-plot-pref.glade

# do not use the intl-tool stuff to merge the text back
# its simpler to just use gettext directly
plot-types.xml : plot-types.xml.in
	cp $< $@

EXTRA_DIST = $(xml_in_files) $(glade_DATA)
DISTCLEANFILES = $(xml_in_files:.xml.in=.xml)

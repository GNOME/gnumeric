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
barcol_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
barcol_la_SOURCES = gog-1.5d.c 		\
		    gog-1.5d.h		\
		    gog-line.c		\
		    gog-line.h		\
		    gog-barcol.c 	\
		    gog-barcol.h	\
		    gog-barcol-prefs.c

xml_in_files = plugin.xml.in plot-types.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml) gog-barcol-prefs.glade

@INTLTOOL_XML_RULE@

glade_DATA = gog-barcol-prefs.glade

# do not use the intl-tool stuff to merge the text back
# its simpler to just use gettext directly
plot-types.xml : plot-types.xml.in
	cp $< $@

EXTRA_DIST = $(xml_in_files) $(glade_DATA)
DISTCLEANFILES = $(xml_in_files:.xml.in=.xml)

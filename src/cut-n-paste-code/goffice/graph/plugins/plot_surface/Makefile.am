INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    -I$(top_srcdir)	-I$(top_builddir)	\
    -I$(top_srcdir)/src/cut-n-paste-code		\
    $(GNUMERIC_CFLAGS)

goffice_graph_surfacedir = $(gnumeric_plugindir)/plot_surface
xmldir	 = $(goffice_graph_surfacedir)
gladedir = $(goffice_graph_surfacedir)

goffice_graph_surface_LTLIBRARIES = surface.la
surface_la_LDFLAGS = -module -avoid-version
surface_la_SOURCES = \
	gog-surface.c		\
	gog-surface.h		\
	gog-contour-prefs.c

xml_in_files = plugin.xml.in plot-types.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

glade_DATA = gog-contour-prefs.glade

# do not use the intl-tool stuff to merge the text back
# its simpler to just use gettext directly
plot-types.xml : plot-types.xml.in
	cp $< $@

EXTRA_DIST = $(xml_in_files) $(glade_DATA)
DISTCLEANFILES = $(xml_in_files:.xml.in=.xml)

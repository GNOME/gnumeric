INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_oleodir = $(gnumeric_plugindir)/oleo
xmldir = $(gnumeric_plugin_oleodir)
gnumeric_plugin_oleo_LTLIBRARIES = oleo.la
oleo_la_LDFLAGS = -module -avoid-version
oleo_la_SOURCES = boot.c oleo.h oleo.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

# It's a hack, but we tag our own command onto the merge rule...
@INTLTOOL_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_oleo_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files)

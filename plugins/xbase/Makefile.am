INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_xbasedir = $(gnumeric_plugindir)/xbase
xmldir = $(gnumeric_plugin_xbasedir)
gnumeric_plugin_xbase_LTLIBRARIES = xbase.la
xbase_la_LDFLAGS = -module -avoid-version
xbase_la_SOURCES = boot.c xbase.h xbase.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
# It's a hack, but we tag our own command onto the merge rule...
@XML_I18N_MERGE_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_xbase_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_lotusdir = $(gnumeric_plugindir)/lotus
xmldir = $(gnumeric_plugin_lotusdir)
gnumeric_plugin_lotus_LTLIBRARIES = lotus.la
lotus_la_LDFLAGS = -module -avoid-version
lotus_la_SOURCES = \
	boot.c			\
	lotus-types.h		\
	lotus.h			\
	lotus.c			\
	lotus-formula.h		\
	lotus-formula.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
# It's a hack, but we tag our own command onto the merge rule...
@XML_I18N_MERGE_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_lotus_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

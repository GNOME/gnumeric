PP_LTLIBRARIES_TARGET = plan_perfect.la
PP_DATA_TARGET = plugin.xml

AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_plan_perfectdir = $(gnumeric_plugindir)/plan_perfect
xmldir = $(gnumeric_plugin_plan_perfectdir)
gnumeric_plugin_plan_perfect_LTLIBRARIES = $(PP_LTLIBRARIES_TARGET)
plan_perfect_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
plan_perfect_la_SOURCES = pln.h pln.c charset.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

# Do not build by default until we are ready.
if WITH_PLAN_PERFECT
PP_LTLIBRARIES_TARGET = plan_perfect.la
PP_DATA_TARGET = plugin.xml
else
PP_LTLIBRARIES_TARGET =
PP_DATA_TARGET =
endif

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_plan_perfectdir = $(gnumeric_plugindir)/plan_perfect
xmldir = $(gnumeric_plugin_plan_perfectdir)
gnumeric_plugin_plan_perfect_LTLIBRARIES = $(PP_LTLIBRARIES_TARGET)
plan_perfect_la_LDFLAGS = -module -avoid-version
plan_perfect_la_SOURCES = pln.h pln.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
# It's a hack, but we tag our own command onto the merge rule...
@XML_I18N_MERGE_XML_RULE@
if WITH_PLAN_PERFECT
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_plan_perfect_LTLIBRARIES)
endif

EXTRA_DIST = $(xml_in_files) $(xml_DATA)

gnumeric_guiledir  = $(gnumeric_datadir)/guile
INCLUDES = 					\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GUILE_INCS)				\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_guiledir = $(gnumeric_plugindir)/guile
xmldir = $(gnumeric_plugin_guiledir)
gnumeric_plugin_guile_LTLIBRARIES = guile.la
guile_la_LDFLAGS = -module -avoid-version
guile_la_SOURCES = smob-value.c guile-support.c plugin.c guile-support.h smob-value.h
guile_la_LIBADD = $(GUILE_LIBS)

Guilescriptsdir = $(gnumeric_guiledir)
Guilescripts_DATA = functions.scm gnumeric_startup.scm

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

# It's a hack, but we tag our own command onto the merge rule...
@INTLTOOL_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_guile_LTLIBRARIES)

EXTRA_DIST = $(Guilescripts_DATA) $(xml_in_files)

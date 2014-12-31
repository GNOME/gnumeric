This directory isn't currently in use.
If you decide to resurrect it, make sore you remove all references to
plugins/guile/* files from po/POTFILES.skip.
Do it before you delete this paragraph here.

gnumeric_guiledir  = $(gnumeric_datadir)/guile
AM_CPPFLAGS = \
    -DPLUGIN_ID=\"Gnumeric_guile\"		\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GUILE_INCS)				\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_guiledir = $(gnumeric_plugindir)/guile
xmldir = $(gnumeric_plugin_guiledir)
gnumeric_plugin_guile_LTLIBRARIES = guile.la
guile_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
guile_la_SOURCES = smob-value.c guile-support.c plugin.c guile-support.h smob-value.h
guile_la_LIBADD = $(GUILE_LIBS)

Guilescriptsdir = $(gnumeric_guiledir)
Guilescripts_DATA = functions.scm gnumeric_startup.scm

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(Guilescripts_DATA) $(xml_in_files)
CLEANFILES = $(xml_DATA)

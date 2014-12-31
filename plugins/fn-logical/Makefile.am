AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_fnlogicaldir = $(gnumeric_plugindir)/fn-logical
xmldir = $(gnumeric_plugin_fnlogicaldir)
gnumeric_plugin_fnlogical_LTLIBRARIES = plugin.la
plugin_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
plugin_la_SOURCES = functions.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

pofiles = $(wildcard $(top_srcdir)/po/*.po) $(wildcard $(top_srcdir)/po-functions/*.po)

plugin.xml: plugin.xml.in $(pofiles)
	LC_ALL=C $(INTLTOOL_MERGE) -x -u -c $(top_builddir)/po/.intltool-merge-cache $(top_srcdir)/po $< $@.tmp
	@PERL@ -pi -e "s/VERSION/${VERSION}/" $@.tmp
	mv $@.tmp $@

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_glpkdir = $(gnumeric_plugindir)/glpk
xmldir = $(gnumeric_plugin_glpkdir)
gnumeric_plugin_glpk_LTLIBRARIES = glpk.la
glpk_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
glpk_la_SOURCES = \
	boot.h boot.c \
	gnm-glpk.c \
	glpk-write.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = ChangeLog $(xml_in_files)
CLEANFILES = $(xml_DATA)

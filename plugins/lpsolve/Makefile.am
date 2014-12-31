AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_lpsolvedir = $(gnumeric_plugindir)/lpsolve
xmldir = $(gnumeric_plugin_lpsolvedir)
gnumeric_plugin_lpsolve_LTLIBRARIES = lpsolve.la
lpsolve_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
lpsolve_la_SOURCES = \
	boot.h boot.c \
	gnm-lpsolve.c \
	lpsolve-write.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = ChangeLog $(xml_in_files)
CLEANFILES = $(xml_DATA)

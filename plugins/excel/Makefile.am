INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_exceldir = $(gnumeric_plugindir)/excel
xmldir = $(gnumeric_plugin_exceldir)
gnumeric_plugin_excel_LTLIBRARIES = excel.la
excel_la_LDFLAGS = -module -avoid-version
excel_la_SOURCES =		\
	boot.c			\
	boot.h			\
	excel.h			\
	formula-types.h		\
	ms-excel-biff.h		\
	ms-biff.c		\
	ms-biff.h		\
	biff-types.h		\
	escher-types.h		\
	ms-container.c		\
	ms-container.h		\
	ms-escher.c		\
	ms-escher.h		\
	ms-excel-util.c	        \
	ms-excel-util.h	        \
	ms-excel-xf.h		\
	ms-excel-read.c	 	\
	ms-excel-read.h		\
	ms-excel-write.c	\
	ms-excel-write.h	\
	ms-formula-read.c	\
	ms-formula-read.h	\
	ms-formula-write.c	\
	ms-formula-write.h	\
	ms-obj.c		\
	ms-obj.h		\
	ms-chart.h		\
	ms-chart.c		\
	ms-summary.c		\
	ms-summary.h

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

# It's a hack, but we tag our own command onto the merge rule...
@INTLTOOL_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnumeric_plugin_excel_LTLIBRARIES)

EXTRA_DIST = $(xml_in_files)

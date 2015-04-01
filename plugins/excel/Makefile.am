AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(top_srcdir)/src	-I$(top_builddir)/src		\
    $(GNUMERIC_CFLAGS) $(Z_CPPFLAGS)

BUILT_SOURCES =					\
	biff-types.c

gnumeric_plugin_exceldir = $(gnumeric_plugindir)/excel
xmldir = $(gnumeric_plugin_exceldir)
gnumeric_plugin_excel_LTLIBRARIES = excel.la
excel_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)

# - Why do we need Z_LIBS here?
# - Because ms-escher.c contains a call to uncompress().
# - But I tried to remove it and everything worked fine; how is that possible?
# - Because when libgsf and libz are dynamic libraries, it's enough if you
#   link against libgsf.  But if libz is a static library, then you need to
#   list it here in order to get it.
#
# To sum up, keep $(Z_LIBS) here, even though it's needed only in a rare case.
#
excel_la_LIBADD  = $(Z_LIBS)

excel_la_SOURCES =		\
	boot.c			\
	boot.h			\
	excel.h			\
	formula-types.h		\
	ms-excel-biff.h		\
	ms-biff.c		\
	ms-biff.h		\
	biff-types.c		\
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
	rc4.c			\
	rc4.h			\
	crypt-md4.c		\
	crypt-md4.h		\
	md5.c			\
	md5.h			\
	\
	excel-xml-read.c	\
	\
	xls-read-pivot.c	\
	xls-write-pivot.c	\
	\
	xlsx-utils.h		\
	xlsx-utils.c		\
	xlsx-read.c		\
	xlsx-write.c

biff-types.c: biff-types.h $(top_srcdir)/tools/biffnames
	$(PERL) $(top_srcdir)/tools/biffnames <$< >$@.tmp
	mv $@.tmp $@

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)	\
	xlsx-read-docprops.c	\
	xlsx-write-docprops.c	\
	xlsx-read-pivot.c	\
	xlsx-write-pivot.c	\
	xlsx-read-color.c	\
	xlsx-read-drawing.c	\
	xlsx-write-drawing.c

CLEANFILES = $(xml_DATA)

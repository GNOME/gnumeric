INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS) $(GB_CFLAGS)

gnum_gbdir = $(gnumeric_plugindir)/gb
xmldir = $(gnum_gbdir)
gnum_gb_LTLIBRARIES = gnum_gb.la
gnum_gb_la_LDFLAGS = -module -avoid-version

gnum_gb_la_SOURCES =			\
	common.c			\
	common.h			\
	excel-gb-application.c		\
	excel-gb-application.h		\
	excel-gb-context.c		\
	excel-gb-context.h		\
	excel-gb-interior.c		\
	excel-gb-interior.h		\
	excel-gb-range.c		\
	excel-gb-range.h		\
	excel-gb-selection.c		\
	excel-gb-selection.h		\
	excel-gb-worksheet.c		\
	excel-gb-worksheet.h		\
	excel-gb-worksheets.c		\
	excel-gb-worksheets.h		\
	excel-gb-worksheet-function.c	\
	excel-gb-worksheet-function.h	\
	plugin.c			\
	streams.h			\
	streams.c

gnum_gb_la_LIBADD =		\
	$(GB_LIBS)		\
	$(GNUMERIC_LIBS)	\
	-lm @GLIB_LIBS@ -lgb -lgbrun

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

XML_I18N_XML_KIND = --pass-through
# It's a hack, but we tag our own command onto the merge rule...
@XML_I18N_MERGE_XML_RULE@
	sh $(top_srcdir)/tools/fixup-plugin-xml $@ $(gnum_gb_LTLIBRARIES)

EXTRA_DIST = README main.gba gnumeric.gbp $(xml_in_files) $(xml_DATA)

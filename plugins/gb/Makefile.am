INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(includedir)	-I$(top_srcdir)/src	\
    $(GNOME_INCLUDEDIR) $(GB_CFLAGS)

gnumeric_plugin_LTLIBRARIES = gnum_gb.la

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
	excel-gb-worksheet-function.c	\
	excel-gb-worksheet-function.h	\
	plugin.c			\
	streams.h			\
	streams.c

gnum_gb_la_LIBADD =		\
	$(GB_LIBS)		\
	$(EXTRA_GNOME_LIBS)	\
	-lm @GLIB_LIBS@ -lgb -lgbrun

EXTRA_DIST = README main.gba gnumeric.gbp
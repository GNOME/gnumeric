INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -DGNUMERIC_VERSION=\""$(gnumeric_version)"\"	\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_csv.la
gnum_csv_la_LDFLAGS = -module -avoid-version
gnum_csv_la_SOURCES = csv-io.h csv-io.c

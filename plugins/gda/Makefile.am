hack=$(shell gnome-config --includedir)
INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -DGNUMERIC_VERSION=\""$(gnumeric_version)"\"	\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)					\
    -I$(hack)/gda

gnumeric_plugin_LTLIBRARIES = gnum_gdaif.la
gnum_gdaif_la_LDFLAGS = -module -avoid-version
gnum_gdaif_la_SOURCES = plugin-gda.c
gnum_gdaif_la_LIBADD = -L$(hack)/lib -lgda-client $(GNORBA_LIBS)

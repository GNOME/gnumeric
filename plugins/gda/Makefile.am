# Do not build by default until we are ready.
if WITH_GDA
GDA_TARGET = gnum_gdaif.la
else
GDA_TARGET =
endif

hack=$(shell gnome-config --includedir)
INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)					\
    -I$(hack)/gda

#
# Do not build the plugin until there is a maintainer
#
gnumeric_plugin_LTLIBRARIES = $(GDA_TARGET)
gnum_gdaif_la_LDFLAGS = -module -avoid-version
gnum_gdaif_la_SOURCES = plugin-gda.c
gnum_gdaif_la_LIBADD = -L$(hack)/lib -lgda-client $(GNORBA_LIBS)

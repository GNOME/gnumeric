#
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(top_srcdir)/src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins/$(VERSION)
hack=$(shell gnome-config --includedir)

plugin_LTLIBRARIES = libgnum_gdaif.la

libgnum_gdaif_la_SOURCES = plugin-gda.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)                             \
	-I$(hack)/gda

libgnum_gdaif_la_LIBADD = -L$(hack)/lib -lgda-client $(GNORBA_LIBS)

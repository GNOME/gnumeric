#
# Sample Makefile for a Gnumeric plugin.
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(top_srcdir)/src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins/$(VERSION)

plugin_LTLIBRARIES = libgnum_csv.la

libgnum_csv_la_SOURCES = csv-io.h csv-io.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"             \
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)

#
# Sample Makefile for a Gnumeric plugin.
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(top_srcdir)/src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins

CFLAGS += -g -Wall 	\
	-Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

plugin_LTLIBRARIES = libcsv.la

libcsv_la_SOURCES = csv-io.h csv-io.c libcsv.c libcsv.h

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)

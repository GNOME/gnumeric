#
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(top_srcdir)/src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins
hack=$(shell gnome-config --includedir)


CFLAGS += -g -Wall 	\
	-Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

plugin_LTLIBRARIES = libgdaif.la

libgdaif_la_SOURCES = plugin-gda.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)                             \
	-I$(hack)/gda

libgdaif_la_LIBADD = -L$(hack)/lib -lgda-client $(GNORBA_LIBS)

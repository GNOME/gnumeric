#
# Makefile for the Python Gnumeric plugin.
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(srcdir)/../../src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins

CFLAGS += -g -Wall 	\
	-Wshadow             -Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

plugin_LTLIBRARIES = libpython.la

libpython_la_SOURCES = python.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)

libpython_la_LDADD = -l$(PYTHON_LIBS)
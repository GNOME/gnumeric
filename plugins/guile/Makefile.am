#
# Makefile for the Guile Gnumeric plugin.
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(srcdir)/../../src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins

# CFLAGS += -g -Wall 	\
# 	-Wshadow             -Wpointer-arith  	\
# 	-Wmissing-prototypes -Wmissing-declarations 

plugin_LTLIBRARIES = libgnumguile.la

libgnumguile_la_SOURCES = plugin.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)					\
	$(GUILE_INCS)					\
	$(GNOME_INCLUDEDIR)

#libguile_la_LDFLAGS = $(PYTHON_LIB_LOC)
libgnumguile_la_LIBADD = $(GUILE_LIBS)

Guilescriptsdir = $(datadir)/gnumeric/guile

Guilescripts_DATA = gnumeric_startup.scm

EXTRA_DIST = gnumeric_startup.scm

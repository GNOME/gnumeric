#
# Makefile for the html Gnumeric plugin.
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(top_srcdir)/src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins

plugin_LTLIBRARIES = libhtml.la

libhtml_la_SOURCES = boot.h boot.c html.h html.c latex.h latex.c \
				roff.h roff.c font.h font.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)

EXTRA_DIST = README ChangeLog

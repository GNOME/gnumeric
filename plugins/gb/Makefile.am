#
# Makefile for the Gnome Basic Gnumeric plugin.
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(srcdir)/../../src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins/$(VERSION)
plugin_LTLIBRARIES = libgnum_gb.la

libgnum_gb_la_SOURCES = plugin.c
libgnum_gb_la_LIBADD = $(EXTRA_GNOME_LIBS) -lm @GLIB_LIBS@ -lgb -lgbrun

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"             \
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)


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

plugin_LTLIBRARIES = libgnum_stf.la 

libgnum_stf_la_SOURCES = stf.h stf.c dialog-stf.h dialog-stf.c dialog-stf-main-page.c dialog-stf-csv-page.c dialog-stf-fixed-page.c dialog-stf-format-page.c dialog-stf-preview.h dialog-stf-preview.c stf-util.h stf-util.c stf-separated.h stf-separated.c stf-fixed.h stf-fixed.c 

gladedir = $(datadir)/gnumeric/glade

glade_DATA = stf.glade	

glade_msgs = stf.glade.h

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"     	\
	-DGNUMERIC_GLADEDIR=\""$(gladedir)"\"		\
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)

EXTRA_DIST = $(glade_DATA) $(glade_msgs)








# Where to install the plugin
#
if BONOBO
gnumeric_version="$(VERSION)-bonobo"
else
gnumeric_version="$(VERSION)"
endif
gnumeric_libdir  = $(libdir)/gnumeric/$(gnumeric_version)
gnumeric_datadir = $(datadir)/gnumeric/$(gnumeric_version)
plugindir = $(gnumeric_libdir)/plugins
gladedir  = $(gnumeric_datadir)/glade

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

plugin_LTLIBRARIES = libgnum_stf.la 

libgnum_stf_la_SOURCES = \
	stf.h stf.c \
	dialog-stf.h dialog-stf.c \
	dialog-stf-main-page.c dialog-stf-csv-page.c \
	dialog-stf-fixed-page.c dialog-stf-format-page.c \
	dialog-stf-preview.h dialog-stf-preview.c \
	stf-util.h stf-util.c \
	stf-separated.h stf-separated.c \
	stf-fixed.h stf-fixed.c 

glade_DATA = stf.glade	
glade_msgs = stf.glade.h

EXTRA_DIST = $(glade_DATA) $(glade_msgs)


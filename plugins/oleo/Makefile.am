GNUMERIC_SRC_DIR = $(top_srcdir)/src

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

plugindir = $(libdir)/gnumeric/plugins/$(VERSION)

plugin_LTLIBRARIES = libgnum_oleo.la

libgnum_oleo_la_SOURCES =		\
        boot.c  \
	oleo.c	\
	oleo.h

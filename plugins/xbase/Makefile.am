GNUMERIC_SRC_DIR = $(top_srcdir)/src

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

plugindir = $(libdir)/gnumeric/plugins/$(VERSION)

plugin_LTLIBRARIES = libgnum_xbase.la

libgnum_xbase_la_SOURCES =		\
	boot.c			\
	xbase.c			\
	xbase.h

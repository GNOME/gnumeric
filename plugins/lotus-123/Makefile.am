GNUMERIC_SRC_DIR = $(top_srcdir)/src

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

plugindir = $(libdir)/gnumeric/plugins/$(VERSION)

plugin_LTLIBRARIES = libgnum_lotus.la

libgnum_lotus_la_SOURCES =		\
	boot.c			\
	config.h		\
	lotus.c			\
	lotus.h			\
	lotus-types.h		\
	lotus-formula.c		\
	lotus-formula.h		\
	lotus-types.h

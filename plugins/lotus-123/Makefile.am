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

plugin_LTLIBRARIES = libgnum_lotus.la

libgnum_lotus_la_SOURCES =	\
	boot.c			\
	config.h		\
	lotus.c			\
	lotus.h			\
	lotus-types.h		\
	lotus-formula.c		\
	lotus-formula.h		\
	lotus-types.h

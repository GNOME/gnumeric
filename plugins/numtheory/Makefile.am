# Where can we find the Gnumeric include files.
GNUMERIC_SRC_DIR = $(top_srcdir)/src

# Where to install the plugin
plugindir = $(libdir)/gnumeric/plugins/$(VERSION)

plugin_LTLIBRARIES = libgnum_numtheory.la

libgnum_numtheory_la_SOURCES = numtheory.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"             \
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)

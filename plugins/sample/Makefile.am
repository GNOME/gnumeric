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

# NOTE :
# The naming convention is very intentional
# We are forced to use the prefix 'lib' by automake and libtool
#    There is probably a way to avoid it but it is not worth to effort
#    to find out.
# The 'gnum_' prfix is a safety measure to avoid conflicts where the
#    plugin 'libpython.so' needs to link with the real 'libpython.so'
plugin_LTLIBRARIES = libgnum_sample.la

libgnum_sample_la_SOURCES = plugin-sample.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"             \
	-I$(includedir)					\
	$(GNOME_INCLUDEDIR)

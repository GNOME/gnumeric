#
# A sample Makefile.am template for plugins.
#

# The standard preprocessor defines and includes
INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

# The naming convention used for plugins is
#	gnum_<name>.so
gnumeric_plugin_LTLIBRARIES = gnum_sample.la

# Don't bother versioning
gnum_sample_la_LDFLAGS = -module -avoid-version
gnum_sample_la_SOURCES = plugin-sample.c

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    -I$(top_srcdir)	-I$(top_builddir)	\
    -I$(top_srcdir)/src/cut-n-paste-code		\
    $(GNUMERIC_CFLAGS)

GOFFICE_PLUGIN_FLAGS = $(GNUMERIC_PLUGIN_LDFLAGS)

INCLUDES = \
    -I$(top_srcdir)					\
    -I$(top_srcdir)/src					\
    -I$(top_builddir)/src				\
    -I$(top_srcdir)/src/cut-n-paste-code		\
    -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
    $(GNUMERIC_CFLAGS)

GOFFICE_PLUGIN_FLAGS = $(GNUMERIC_PLUGIN_LDFLAGS)

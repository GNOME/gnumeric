# prune this when the code moves
INCLUDES = -I$(top_builddir)/src/cut-n-paste-code		\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/goffice		\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)

GOFFICE_PLUGIN_FLAGS = $(GNUMERIC_PLUGIN_LDFLAGS)

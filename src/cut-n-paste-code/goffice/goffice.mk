# GO_ICON_DIR should be in a utility routine

INCLUDES = \
    -I$(top_srcdir)/src/cut-n-paste-code/goffice		\
    -I$(top_srcdir)/src/cut-n-paste-code/foocanvas		\
    $(GNUMERIC_CFLAGS) -DGO_ICON_DIR=\"$(gnumeric_icondir)\"

goffice_gladedir = ${gnumeric_gladedir}

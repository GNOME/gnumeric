INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -DGNUMERIC_VERSION=\""$(gnumeric_version)"\"	\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_html.la
gnum_html_la_LDFLAGS = -module -avoid-version
gnum_html_la_SOURCES = \
    boot.h boot.c \
    html.h html.c \
    latex.h latex.c \
    roff.h roff.c \
    font.h font.c 
#No longer used now that printing is available.
#epsf.h epsf.c ps.h ps.c

EXTRA_DIST = README ChangeLog

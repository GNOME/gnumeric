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
	-DGNUMERIC_VERSION=\""$(VERSION)"\"             \
	-I$(includedir)					\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

plugin_LTLIBRARIES = libgnum_html.la

libgnum_html_la_SOURCES = boot.h boot.c html.h html.c latex.h latex.c \
			roff.h roff.c font.h font.c 

#No longer used now that printing is available.
#epsf.h epsf.c ps.h ps.c

EXTRA_DIST = README ChangeLog

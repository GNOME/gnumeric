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
guiledir  = $(gnumeric_datadir)/guile

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"             \
	-I$(includedir)					\
	-I$(top_srcdir)/src				\
	$(GUILE_INCS)					\
	$(GNOME_INCLUDEDIR)

plugin_LTLIBRARIES = libgnum_guile.la

libgnum_guile_la_SOURCES = plugin.c

#libgnum_guile_la_LDFLAGS = $(PYTHON_LIB_LOC)
libgnum_guile_la_LIBADD = $(GUILE_LIBS)

Guilescriptsdir = $(gnumeric_guiledir)
Guilescripts_DATA = functions.scm gnumeric_startup.scm
EXTRA_DIST = $(Guilescripts_DATA)

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
	$(GNOME_INCLUDEDIR) 				\
	$(GB_CFLAGS)

plugin_LTLIBRARIES = libgnum_gb.la

libgnum_gb_la_SOURCES = plugin.c

libgnum_gb_la_LIBADD = $(GB_LIBS) $(EXTRA_GNOME_LIBS) -lm @GLIB_LIBS@ -lgb -lgbrun

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

hack=$(shell gnome-config --includedir)
INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"             \
	-I$(includedir)					\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)                             \
	-I$(hack)/gda

plugin_LTLIBRARIES = libgnum_gdaif.la

libgnum_gdaif_la_SOURCES = plugin-gda.c

libgnum_gdaif_la_LIBADD = -L$(hack)/lib -lgda-client $(GNORBA_LIBS)

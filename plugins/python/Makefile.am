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
pythondir = $(gnumeric_datadir)/python

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)					\
	-I$(top_srcdir)/src				\
	$(PY_CFLAGS)					\
	$(GNOME_INCLUDEDIR)

#plugin_LTLIBRARIES = libgnum_python.la
noinst_LTLIBRARIES = libgnum_python.la

libgnum_python_la_SOURCES = python.c
libgnum_python_la_LDFLAGS = $(PY_LIB_LOC)  
libgnum_python_la_LIBADD = -l$(PY_LIBS) $(PY_EXTRA_LIBS)

Pythonscriptsdir = $(pythondir)

Pythonscripts_DATA = gnumeric_startup.py

EXTRA_DIST = gnumeric_startup.py

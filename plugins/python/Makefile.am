gnumeric_pythondir = $(gnumeric_datadir)/python
INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(PY_CFLAGS)					\
    $(GNOME_INCLUDEDIR)

noinst_LTLIBRARIES = gnum_python.la
gnum_python_la_LDFLAGS = -module -avoid-version \
			 $(PY_LIB_LOC)  
gnum_python_la_SOURCES = python.c
gnum_python_la_LIBADD = -l$(PY_LIBS) $(PY_EXTRA_LIBS)

gnumeric_plugin_pythondir = $(gnumeric_plugindir)/python
gnumeric_plugin_python_DATA = plugin.xml

Pythonscriptsdir = $(gnumeric_pythondir)
Pythonscripts_DATA = gnumeric_startup.py gnumeric_defs.py
EXTRA_DIST = $(Pythonscripts_DATA) $(gnumeric_plugin_python_DATA)

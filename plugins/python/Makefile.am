gnumeric_pythondir = $(gnumeric_datadir)/python
INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(PY_CFLAGS)					\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_pythondir = $(gnumeric_plugindir)/python
xmldir = $(gnumeric_plugin_pythondir)
gnumeric_plugin_python_LTLIBRARIES = python.la

python_la_LDFLAGS = -module -avoid-version $(PY_LIB_LOC)  
python_la_LIBADD = -l$(PY_LIBS) $(PY_EXTRA_LIBS)
python_la_SOURCES = python.c

Pythonscriptsdir = $(gnumeric_pythondir)
Pythonscripts_DATA = gnumeric_startup.py gnumeric_defs.py

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@XML_I18N_MERGE_XML_RULE@

EXTRA_DIST = $(Pythonscripts_DATA) $(xml_in_files) $(xml_DATA)

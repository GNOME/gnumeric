gnumeric_pythondir = $(gnumeric_datadir)/python
INCLUDES = \
    -DPLUGIN_ID=\"Gnumeric_python\"		\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(PY_CFLAGS)				\
    $(GNUMERIC_CFLAGS)

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

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(Pythonscripts_DATA) $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

INCLUDES = \
    -DPLUGIN_ID=\"Gnumeric_PythonLoader\"		\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(PY_CFLAGS)				\
    $(PYGTK_CFLAGS)				\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_python_loaderdir = $(gnumeric_plugindir)/python-loader
xmldir = $(gnumeric_plugin_python_loaderdir)
gnumeric_plugin_python_loader_LTLIBRARIES = python_loader.la
gnumeric_plugin_python_loader_DATA = ui-console-menu.xml

python_loader_la_LDFLAGS = -module -avoid-version $(PY_LIB_LOC)  
python_loader_la_LIBADD = -l$(PY_LIBS) $(PY_EXTRA_LIBS)
python_loader_la_SOURCES = \
	python-loader.h \
	python-loader.c \
	gnm-python.h \
	gnm-python.c \
	gnm-py-interpreter.h \
	gnm-py-interpreter.c \
	py-gnumeric.h \
	py-gnumeric.c \
	py-interpreter-selector.c \
	py-interpreter-selector.h \
	py-command-line.c \
	py-command-line.h \
	py-console.c \
	py-console.h \
	boot.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files) $(gnumeric_plugin_python_loader_DATA)
DISTCLEANFILES = $(xml_DATA)

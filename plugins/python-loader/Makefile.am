AM_CPPFLAGS = \
    -DPLUGIN_ID=\"Gnumeric_PythonLoader\"		\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNM_PY_CFLAGS)				\
    $(PYGTK_CFLAGS)				\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_python_loaderdir = $(gnumeric_plugindir)/python-loader
xmldir = $(gnumeric_plugin_python_loaderdir)
gnumeric_plugin_python_loader_LTLIBRARIES = python_loader.la

python_loader_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS) $(GNM_PY_LDFLAGS)
python_loader_la_LIBADD = $(GNM_PY_LIBADD)
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

xml_in_files = plugin.xml.in ui-console-menu.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA)

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(PY_CFLAGS)				\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_python_loaderdir = $(gnumeric_plugindir)/python-loader
xmldir = $(gnumeric_plugin_python_loaderdir)
gnumeric_plugin_python_loader_LTLIBRARIES = python_loader.la

python_loader_la_LDFLAGS = -module -avoid-version $(PY_LIB_LOC)  
python_loader_la_LIBADD = -l$(PY_LIBS) $(PY_EXTRA_LIBS)
python_loader_la_SOURCES = python-loader.c \
			   py-gnumeric.c \
			   py-gnumeric.h 

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

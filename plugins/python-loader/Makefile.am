INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(PY_CFLAGS)					\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_python_loaderdir = $(gnumeric_plugindir)/python-loader
gnumeric_plugin_python_loader_DATA = plugin.xml
gnumeric_plugin_python_loader_LTLIBRARIES = python_loader.la

python_loader_la_LDFLAGS = -module -avoid-version $(PY_LIB_LOC)  
python_loader_la_LIBADD = -l$(PY_LIBS) $(PY_EXTRA_LIBS)
python_loader_la_SOURCES = python-loader.c \
			   py-gnumeric.c \
			   py-gnumeric.h 
EXTRA_DIST = $(gnumeric_plugin_python_loader_DATA)

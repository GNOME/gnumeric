gnumeric_plugin_py_funcdir = $(gnumeric_plugindir)/py-func
xmldir = $(gnumeric_plugin_py_funcdir)
gnumeric_plugin_py_func_SCRIPTS = py_func.py

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(gnumeric_plugin_py_func_SCRIPTS) $(xml_in_files)
CLEANFILES = $(xml_DATA)

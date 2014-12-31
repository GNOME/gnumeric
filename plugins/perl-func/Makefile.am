gnumeric_plugin_perl_funcdir = $(gnumeric_plugindir)/perl-func
xmldir = $(gnumeric_plugin_perl_funcdir)
gnumeric_plugin_perl_func_SCRIPTS = perl_func.pl

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(gnumeric_plugin_perl_func_SCRIPTS) $(xml_in_files)
CLEANFILES = $(xml_DATA)

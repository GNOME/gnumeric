gnumeric_plugin_gnome_glossarydir = $(gnumeric_plugindir)/gnome-glossary
xmldir = $(gnumeric_plugin_gnome_glossarydir)
gnumeric_plugin_gnome_glossary_DATA = glossary-po-header
gnumeric_plugin_gnome_glossary_SCRIPTS = gnome_glossary.py

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(gnumeric_plugin_gnome_glossary_DATA) \
             $(gnumeric_plugin_gnome_glossary_SCRIPTS) \
             $(xml_in_files)
CLEANFILES = $(xml_DATA)

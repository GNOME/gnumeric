schemadir   = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = gnumeric-dialogs.schemas gnumeric-general.schemas

schema_source_data = $(foreach file, $(schema_DATA), $(srcdir)/$(file))

install-data-local: $(schema_installed_data)
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(schema_source_data)

xml_in_files = plot-types.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

xmldir = $(gnumeric_datadir)
EXTRA_DIST=$(schema_DATA) $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

schemadir   = @GCONF_SCHEMA_FILE_DIR@
schema_in_files = gnumeric-dialogs.schemas.in gnumeric-general.schemas.in
schema_DATA = $(schema_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

# don't do this if we are building in eg. rpm
install-data-local:
	if test -z "$(DESTDIR)" ; then \
		for p in $(schema_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$$p; \
		done \
	fi

xml_in_files = plot-types.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

xmldir = $(gnumeric_datadir)
EXTRA_DIST = $(schema_DATA) $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

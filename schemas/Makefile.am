schemadir   = @GCONF_SCHEMA_FILE_DIR@
schema_in_files = gnumeric-dialogs.schemas.in gnumeric-general.schemas.in gnumeric-plugins.schemas.in 
schema_DATA = $(schema_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

install-data-local:
	for p in $(schema_DATA) ; do \
		GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$$p; \
	done

EXTRA_DIST = $(schema_DATA) $(schema_in_files)

schemadir   = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = gnumeric-dialogs.schemas

schema_source_data = $(foreach file, $(schema_DATA), $(srcdir)/$(file))

install-data-local: $(schema_installed_data)
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(schema_source_data)

EXTRA_DIST=$(schema_DATA)
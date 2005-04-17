schemadir   = @GCONF_SCHEMA_FILE_DIR@
schema_in_files = gnumeric-dialogs.schemas.in gnumeric-general.schemas.in gnumeric-plugins.schemas.in 
if WITH_GNOME
schema_DATA = $(schema_in_files:.schemas.in=.schemas)
else
schema_DATA =
endif

@INTLTOOL_SCHEMAS_RULE@

if GCONF_SCHEMAS_INSTALL
# don't do this if we are building in eg. rpm
install-data-local:
	if test -z "$(DESTDIR)" ; then \
		for p in $(schema_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $$p; \
		done \
	fi
else
install-data-local:
endif

if WITH_WIN32
if !CROSS_COMPILING
SUFFIXES = .reg .hkcu.reg
regdir = $(prefix)/etc/win32/reg
reg_DATA = $(schema_in_files:.schemas.in=.reg) $(schema_in_files:.schemas.in=.hkcu.reg)
CLEANFILES += $(reg_DATA)

%.reg:   %.schemas.in
	$(top_builddir)/tools/gconf-schemas-to-win32-reg.exe $< >$@
%.hkcu.reg:   %.schemas.in
	$(top_builddir)/tools/gconf-schemas-to-win32-reg.exe -c $< >$@
endif
endif

EXTRA_DIST = $(schema_DATA) $(schema_in_files)
CLEANFILES = $(schema_DATA)

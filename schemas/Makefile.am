schema_in_files = gnumeric-dialogs.schemas.in gnumeric-general.schemas.in gnumeric-plugins.schemas.in 
schemadir   = @GCONF_SCHEMA_FILE_DIR@
if WITH_GNOME
schema_DATA = $(schema_in_files:.schemas.in=.schemas)
endif

EXTRA_DIST = $(schema_in_files)
CLEANFILES = $(schema_DATA)

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
SUFFIXES = .schemas.in .reg .hkcu.reg
regdir = $(prefix)/etc/win32/reg
reg_DATA = $(schema_in_files:.schemas.in=.reg) $(schema_in_files:.schemas.in=.hkcu.reg)
CLEANFILES += $(reg_DATA)

.schemas.in.reg:
	cd $(bindir) ; $(abs_top_builddir)/tools/gconf-schemas-to-win32-reg.exe $(abs_builddir)/$< >$(abs_builddir)/$@
.schemas.in.hkcu.reg:
	cd $(bindir) ; $(abs_top_builddir)/tools/gconf-schemas-to-win32-reg.exe -c $(abs_builddir)/$< >$(abs_builddir)/$@
endif

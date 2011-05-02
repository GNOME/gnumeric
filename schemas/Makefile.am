schema_in_files = gnumeric-dialogs.schemas.in gnumeric-general.schemas.in gnumeric-plugins.schemas.in
if WITH_GCONF
schemadir   	= @GCONF_SCHEMA_FILE_DIR@
schema_DATA 	= $(schema_in_files:.schemas.in=.schemas)
endif

EXTRA_DIST =
CLEANFILES = $(schema_DATA)

@INTLTOOL_SCHEMAS_RULE@

if HAVE_GSETTINGS
gsettings_SCHEMAS =	\
	org.gnome.gnumeric.gschema.xml	\
	org.gnome.gnumeric.dialogs.gschema.xml	\
	org.gnome.gnumeric.plugin.gschema.xml
CLEANFILES += $(gsettings_SCHEMAS)

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

endif

EXTRA_DIST += $(schema_in_files)	\
	org.gnome.gnumeric.gschema.xml.in.in	\
	org.gnome.gnumeric.dialogs.gschema.xml.in.in	\
	org.gnome.gnumeric.plugin.gschema.xml.in.in

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

if WITH_NATIVE_WIN32
SUFFIXES = .schemas.in .reg .hkcu.reg
regdir = $(prefix)/etc/win32/reg
reg_DATA = $(schema_in_files:.schemas.in=.reg) $(schema_in_files:.schemas.in=.hkcu.reg)
CLEANFILES += $(reg_DATA)

.schemas.in.reg:
	@PERL@ $(top_srcdir)/tools/handle-conf-options --reg $< >$(abs_builddir)/$@

.schemas.in.hkcu.reg:
	@PERL@ $(top_srcdir)/tools/handle-conf-options --hkcu-reg $< >$(abs_builddir)/$@
endif

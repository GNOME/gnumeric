SUFFIXES = .schemas.in .reg .hkcu.reg .xml.in .xml.in.in

gschema_inin_files = \
	org.gnome.gnumeric.gschema.xml.in.in	\
	org.gnome.gnumeric.dialogs.gschema.xml.in.in	\
	org.gnome.gnumeric.plugin.gschema.xml.in.in
gschema_in_files = $(gschema_inin_files:.xml.in.in=.xml.in)

schema_in_files = gnumeric-dialogs.schemas.in gnumeric-general.schemas.in gnumeric-plugins.schemas.in

if WITH_GCONF
schemadir   	= @GCONF_SCHEMA_FILE_DIR@
schema_DATA 	= $(schema_in_files:.schemas.in=.schemas)
endif

EXTRA_DIST =
CLEANFILES = $(schema_DATA)

@INTLTOOL_SCHEMAS_RULE@

if HAVE_GSETTINGS
gsettings_SCHEMAS = $(gschema_in_files:.xml.in=.xml)
CLEANFILES += $(gsettings_SCHEMAS)

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

endif

EXTRA_DIST += $(schema_in_files) $(gschema_inin_files)

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
regdir = $(prefix)/etc/win32/reg
reg_DATA = $(gschema_in_files:.xml.in=.reg) $(gschema_in_files:.xml.in=.hkcu.reg)
CLEANFILES += $(reg_DATA)
endif

# The resulting .reg files are only useful on win32 but we can produce them
# on any system.
.xml.in.reg:
	@PERL@ $(top_srcdir)/tools/handle-conf-options --reg $< >$(abs_builddir)/$@

.xml.in.hkcu.reg:
	@PERL@ $(top_srcdir)/tools/handle-conf-options --hkcu-reg $< >$(abs_builddir)/$@

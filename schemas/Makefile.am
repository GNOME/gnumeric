SUFFIXES = .reg .hkcu.reg .xml.in .xml.in.in .gschema.xml.in

gschema_inin_files = \
	org.gnome.gnumeric.gschema.xml.in.in	\
	org.gnome.gnumeric.dialogs.gschema.xml.in.in	\
	org.gnome.gnumeric.plugin.gschema.xml.in.in
gschema_in_files = $(gschema_inin_files:.xml.in.in=.xml.in)

EXTRA_DIST =
CLEANFILES =

@INTLTOOL_SCHEMAS_RULE@

if HAVE_GSETTINGS
gsettings_SCHEMAS = $(gschema_in_files:.xml.in=.xml)
CLEANFILES += $(gsettings_SCHEMAS)

@INTLTOOL_XML_NOMERGE_RULE@

@GSETTINGS_RULES@

endif

EXTRA_DIST += $(gschema_inin_files)

if WITH_NATIVE_WIN32
regdir = $(prefix)/etc/win32/reg
reg_DATA = $(gschema_in_files:.gschema.xml.in=.reg) $(gschema_in_files:.gschema.xml.in=.hkcu.reg)
CLEANFILES += $(reg_DATA)
endif

# The resulting .reg files are only useful on win32 but we can produce them
# on any system.
.gschema.xml.in.reg:
	@PERL@ $(top_srcdir)/tools/handle-conf-options --reg $< >$(abs_builddir)/$@

.gschema.xml.in.hkcu.reg:
	@PERL@ $(top_srcdir)/tools/handle-conf-options --hkcu-reg $< >$(abs_builddir)/$@

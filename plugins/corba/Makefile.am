INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_corbadir = $(gnumeric_plugindir)/corba
xmldir = $(gnumeric_plugin_corbadir)
gnumeric_plugin_corba_LTLIBRARIES = corba.la
corba_la_LDFLAGS = -module -avoid-version
corba_la_SOURCES = $(GNUMERIC_GENERATED)	\
	corba-application.c 			\
	corba-workbook.c 			\
	corba-workbook.h 			\
	corba-sheet.c 

GNUMERIC_GENERATED = 		\
	GNOME_Gnumeric-common.c		\
	GNOME_Gnumeric-skels.c		\
	GNOME_Gnumeric-stubs.c		\
	GNOME_Gnumeric.h

stamp_Gnumeric_idl : $(srcdir)/GNOME_Gnumeric.idl 
	$(ORBIT_IDL) -I$(srcdir) $< && touch $@

$(GNUMERIC_GENERATED): stamp_Gnumeric_idl

idldir   = $(gnumeric_datadir)/idl
idl_DATA = GNOME_Gnumeric.idl 

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

# Do not use built sources BUILT_SOURCES
# it attempts to build the source even if it is not used.
DISTCLEANFILES	= $(GNUMERIC_GENERATED) stamp_Gnumeric_idl $(xml_DATA)
EXTRA_DIST	= $(idl_DATA) $(xml_in_files)

dist-hook :
	cd $(distdir) && rm -f $(CLEANFILES)


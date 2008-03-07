AM_CPPFLAGS = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(GNUMERIC_CFLAGS) $(CORBA_LIBS_CFLAGS)

gnumeric_plugin_corbadir = $(gnumeric_plugindir)/corba
xmldir = $(gnumeric_plugin_corbadir)
gnumeric_plugin_corba_LTLIBRARIES = corba.la
corba_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS)
corba_la_LIBADD  = $(CORBA_LIBS)
corba_la_SOURCES =				\
	corba-application.c 			\
	corba-workbook.c 			\
	corba-workbook.h 			\
	corba-sheet.c 				\
	corba-sheet.h

nodist_corba_la_SOURCES =		\
	GNOME_Gnumeric-common.c		\
	GNOME_Gnumeric-skels.c		\
	GNOME_Gnumeric-stubs.c		\
	GNOME_Gnumeric.h

# A hint is needed to build the header first:
BUILT_SOURCES = GNOME_Gnumeric.h

stamp_Gnumeric_idl: $(srcdir)/GNOME_Gnumeric.idl 
	$(ORBIT_IDL) -I$(srcdir) $< && touch $@

$(nodist_corba_la_SOURCES): stamp_Gnumeric_idl

idldir   = $(gnumeric_datadir)/idl
idl_DATA = GNOME_Gnumeric.idl 

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

CLEANFILES	= $(nodist_corba_la_SOURCES) stamp_Gnumeric_idl $(xml_DATA)
EXTRA_DIST	= $(idl_DATA) $(xml_in_files)

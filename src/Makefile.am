#
# Gnumeric's Makefile.
# Author: Miguel de Icaza (miguel@kernel.org)
#
# This Makefile has targets for three programs:
#
# gnumeric: 
#	the plain spreadsheet
#
# gnumeric-bonobo:
#       spreadsheet with CORBA and Bonobo additions
# 
# 

SUBDIRS = dialogs functions widgets

if BONOBO
bin_PROGRAMS = gnumeric-bonobo
CLEANFILES  += gnumeric
else
bin_PROGRAMS = gnumeric
CLEANFILES  +=gnumeric-bonobo
endif

#noinst_PROGRAMS = number-match
gladedir   = $(datadir)/gnumeric/glade

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
	-I$(includedir)				 	\
	-I$(srcdir)/widgets				\
	-I$(srcdir)/functions				\
	-I$(srcdir)/dialogs				\
	$(EXTRA_GNOME_CFLAGS)				\
	$(GUILE_INCS)

if BONOBO
GNUMERIC_CORBA_GENERATED = 			\
	Gnumeric-common.c			\
	Gnumeric-skels.c			\
	Gnumeric-stubs.c			\
	Gnumeric.h

noinst_LIBRARIES = libGnumeric.a

libGnumeric_a_SOURCES = $(GNUMERIC_CORBA_GENERATED)

else
GNUMERIC_CORBA_GENERATED = 
endif

GNUMERIC_BASE_SOURCES =				\
	about.c					\
	analysis-tools.c			\
	analysis-tools.h			\
	application.c				\
	application.h				\
	auto-format.c				\
	auto-format.h				\
	border.c				\
	border.h				\
	cell.h					\
	cell.c					\
	cell-draw.c				\
	cell-draw.h				\
	cellspan.c				\
	cellspan.h				\
	clipboard.c				\
	clipboard.h				\
	collect.h				\
	collect.c				\
	color.c					\
	color.h					\
	colrow.c				\
	colrow.h				\
	commands.h				\
	commands.c				\
	command-context.h			\
	command-context.c			\
	command-context-corba.c			\
	command-context-corba.h			\
	command-context-gui.c			\
	command-context-gui.h			\
	complex.h				\
	complex.c				\
	corba.h					\
	cursors.c				\
	cursors.h				\
	dates.c					\
	dates.h					\
	datetime.c				\
	datetime.h				\
	eval.h					\
	eval.c					\
	expr.c					\
	expr.h					\
	expr-name.c				\
	expr-name.h				\
	file.c					\
	file.h					\
	formats.h				\
	formats.c				\
	func.c					\
	func.h					\
	format.c				\
	format.h				\
	global-gnome-font.c			\
	global-gnome-font.h			\
	goal-seek.c				\
	goal-seek.h				\
	gnumeric-sheet.c			\
	gnumeric-sheet.h			\
	gnumeric-util.c				\
	gnumeric-util.h				\
	gnumeric-type-util.h			\
	gnumeric.h				\
	history.c				\
	history.h				\
	item-bar.c				\
	item-bar.h				\
	item-cursor.c				\
	item-cursor.h				\
	item-debug.c				\
	item-debug.h				\
	item-edit.c				\
	item-edit.h				\
	item-grid.c				\
	item-grid.h				\
	main.h					\
	mathfunc.c				\
	mathfunc.h				\
	mstyle.c				\
	mstyle.h				\
	number-match.c				\
	number-match.h				\
	numbers.h				\
	parser.y				\
	pattern.h				\
	pattern.c				\
	pixmaps.h				\
	plugin.c				\
	plugin.h				\
	portability.h				\
	print.c					\
	print.h					\
	print-cell.c				\
	print-cell.h				\
	print-info.c				\
	print-info.h				\
	print-preview.c				\
	print-preview.h				\
	ranges.c				\
	ranges.h				\
	regression.c				\
	regression.h				\
	render-ascii.c				\
	render-ascii.h				\
	selection.c				\
	selection.h				\
	sheet.c					\
	sheet.h					\
	sheet-private.h				\
	sheet-autofill.c			\
	sheet-autofill.h			\
	sheet-object.c				\
	sheet-object.h				\
	sheet-object-widget.c			\
	sheet-object-widget.h			\
	sheet-object-graphic.c			\
	sheet-object-graphic.h			\
	sheet-style.c				\
	sheet-view.c				\
	sheet-view.h				\
	solver-lp.c                     	\
	solver.h				\
	sort.c					\
	sort.h					\
	str.c					\
	str.h					\
	stf.c					\
	stf.h					\
	stf-parse.c				\
	stf-parse.h				\
	style.c					\
	style.h					\
	summary.c				\
	summary.h				\
	symbol.h				\
	symbol.c				\
	utils.c					\
	utils.h					\
	value.c					\
	value.h					\
	value-sheet.c				\
	wizard.h				\
	workbook.c				\
	workbook.h				\
	workbook-cmd-format.c			\
	workbook-cmd-format.h			\
	workbook-format-toolbar.c		\
	workbook-format-toolbar.h		\
	workbook-private.h			\
	workbook-view.h				\
	workbook-view.c				\
	xml-io.c				\
	xml-io.h				\
	$(GNUMERIC_CORBA_GENERATED)

GNUMERIC_BONOBO_SOURCES = 			\
	embeddable-grid.c			\
	embeddable-grid.h			\
	sheet-object-bonobo.c			\
	sheet-object-bonobo.h			\
	sheet-object-container.c		\
	sheet-object-container.h		\
	sheet-object-item.c			\
	sheet-object-item.h			\
	sheet-vector.c				\
	sheet-vector.h

Gnumeric-impl.o: Gnumeric.h

Vector-impl.o: Vector.h

$(GNUMERIC_CORBA_GENERATED): Gnumeric.idl 
	orbit-idl -I`$(GNOME_CONFIG) --datadir`/idl -I$(srcdir) $(srcdir)/Gnumeric.idl 

gnumeric_SOURCES =					\
	main.c						\
	normal-args.c					\
	$(GNUMERIC_BASE_SOURCES)

corba-sheet.c corba-workbook-factory.c corba-workbook.c Gnumeric-impl.c \
embeddable-grid.c: Gnumeric.h

sheet-vector.h: Vector.h

gnumeric_bonobo_SOURCES =				\
	main.c						\
	corba-args.c					\
	corba-sheet.c					\
	corba-workbook.c				\
	corba-workbook-factory.c			\
	$(GNUMERIC_BASE_SOURCES)			\
	$(GNUMERIC_BONOBO_SOURCES)

gnumeric_LDFLAGS = -export-dynamic

gnumeric_libs =						\
	../plugins/excel/libexcel.a			\
	../plugins/excel/libole2/libole2.a 		\
	dialogs/libdialogs.a				\
	widgets/libwidgets.a				\
	functions/libfunctions.a		

gnumeric_LDADD = 					\
	$(gnumeric_libs)				\
	$(EXTRA_GNOME_LIBS)				\
	$(GNOME_XML_LIB) 				\
	$(GUILE_LIBS)					\
	$(INTLLIBS)

gnumeric_bonobo_LDADD =					\
	$(gnumeric_libs)				\
	../wizards/graphics/libwizard_graphics.a	\
	libGnumeric.a					\
	../graph/libGraph.a				\
	$(EXTRA_GNOME_LIBS)				\
	$(GNOME_XML_LIB) 				\
	$(GUILE_LIBS)					\
	$(INTLLIBS)

wc:
	wc -l $(gnumeric_SOURCES)

wc_bonobo:
	wc -l $(gnumeric_SOURCES) $(GNUMERIC_BONOBO_SOURCES)

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.xpm $(distdir)/pixmaps

#number_match_SOURCES = number-match.c  formats.c dates.c number-match-test.c

#number_match_LDADD = 			\
#	$(GNOME_LIBDIR) 		\
#	$(GNOMEUI_LIBS)			\
#	$(GNOME_XML_LIB) 		\
#	$(INTLLIBS)

idldir   = $(datadir)/idl
idl_DATA = Gnumeric.idl Gnumeric-value.idl


MYHACKAROUNDAUTOMAKE=$(shell echo $(GNUMERIC_CORBA_GENERATED))

EXTRA_DIST = $(idl_DATA) $(MYHACKAROUNDAUTOMAKE) portability.h.in


#
# Gnumeric's Makefile.
# Author: Miguel de Icaza (miguel@kernel.org)
#
# This Makefile has targets for three programs:
#
# gnumeric: 
#	the plain spreadsheet
#
# gnumeric-corba:
#       spreadsheet with CORBA additions
#
# gnumeric-bonobo:
#       spreadsheet with CORBA and Bonobo additions
# 
# Note that gnumeric/gnumeric-corba and gnumeric-bonobo are mutually
# exclusive.
# 


if BONOBO
bin_PROGRAMS = gnumeric-bonobo
else
bin_PROGRAMS = gnumeric
endif

#noinst_PROGRAMS = number-match

#
# Keep this in sync with ../icons/Makefile.am
#
iconsdir = $(datadir)/pixmaps/gnumeric

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"		\
	-I$(includedir)				 	\
	$(EXTRA_GNOME_CFLAGS)				\
	$(GUILE_INCS)

GNUMERIC_BASE_SOURCES =				\
	about.c					\
	analysis-tools.c			\
	cell.h					\
	cell.c					\
	cell-draw.c				\
	cellspan.c				\
	clipboard.c				\
	clipboard.h				\
	collect.h				\
	collect.c				\
	color.c					\
	color.h					\
	complex.h				\
	complex.c				\
	corba.h					\
	cursors.c				\
	cursors.h				\
	dates.c					\
	dates.h					\
	dialog-analysis-tools.c			\
	dialog-cell-comment.c			\
	dialog-cell-format.c			\
	dialog-cell-sort.c			\
	dialog-function-select.c		\
	dialog-function-wizard.c		\
	dialog-goal-seek.c			\
	dialog-goto-cell.c			\
	dialog-insert-cells.c			\
	dialog-define-names.c			\
	dialog-delete-cells.c			\
	dialog-paste-special.c			\
	dialog-printer-setup.c			\
	dialog-summary.c			\
	dialog-solver.c				\
	dialog-zoom.c				\
	dialogs.h				\
	dump.c					\
	dump.h					\
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
	fn-database.c				\
	fn-date.c				\
	fn-eng.c				\
	fn-financial.c				\
	fn-information.c			\
	fn-logical.c				\
	fn-lookup.c				\
	fn-math.c				\
	fn-misc.c				\
	fn-sheet.c				\
	fn-stat.c				\
	fn-string.c				\
	format.c				\
	format.h				\
	goal-seek.c				\
	goal-seek.h				\
	gnumeric-sheet.c			\
	gnumeric-sheet.h			\
	gnumeric-util.c				\
	gnumeric-util.h				\
	gnumeric.h				\
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
	number-match.c				\
	number-match.h				\
	numbers.h				\
	parser.y				\
	pattern-selector.c			\
	pattern-selector.h			\
	pixmaps.h				\
	plugin.c				\
	plugin.h				\
	plugin-manager.c			\
	print.c					\
	print.h					\
	print-cell.c				\
	print-cell.h				\
	print-info.c				\
	print-info.h				\
	ranges.c				\
	ranges.h				\
	render-ascii.c				\
	render-ascii.h				\
	sheet.c					\
	sheet.h					\
	sheet-autofill.c			\
	sheet-autofill.h			\
	sheet-object.c				\
	sheet-object.h				\
	sheet-object-graphic.c			\
	sheet-object-graphic.h			\
	sheet-view.c				\
	sheet-view.h				\
	solver-lp.c                     	\
	solver.h				\
	str.c					\
	str.h					\
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
	widget-editable-label.c			\
	widget-editable-label.h			\
	widget-font-selector.c			\
	widget-font-selector.h			\
	workbook.c				\
	xml-io.c				\
	xml-io.h

if BONOBO
GNUMERIC_CORBA_GENERATED = 			\
	Gnumeric-common.c			\
	Gnumeric-skels.c			\
	Gnumeric-stubs.c			\
	Gnumeric.h
else
GNUMERIC_CORBA_GENERATED =
endif

GNUMERIC_BONOBO_SOURCES = 			\
	embeddable-grid.c			\
	embeddable-grid.h			\
	sheet-object-container.c		\
	sheet-object-container.h

BUILT_SOURCES = $(GNUMERIC_CORBA_GENERATED)

Gnumeric-impl.o: Gnumeric.h

$(GNUMERIC_CORBA_GENERATED): Gnumeric.idl
	orbit-idl -I`$(GNOME_CONFIG) --datadir`/idl $(srcdir)/Gnumeric.idl

gnumeric_SOURCES =				\
	main.c					\
	normal-args.c				\
	$(GNUMERIC_BASE_SOURCES)

gnumeric_bonobo_SOURCES =			\
	main.c					\
	corba-args.c				\
	corba-sheet.c				\
	corba-workbook.c			\
	corba-workbook-factory.c		\
	$(GNUMERIC_CORBA_GENERATED)		\
	$(GNUMERIC_BASE_SOURCES)		\
	$(GNUMERIC_BONOBO_SOURCES)

gnumeric_LDFLAGS = -export-dynamic

gnumeric_LDADD = 				\
	../plugins/excel/libexcel.a		\
	../plugins/excel/libole2/libole2.a 	\
	$(EXTRA_GNOME_LIBS)			\
	$(GNOME_XML_LIB) 			\
	$(GUILE_LIBS)				\
	$(INTLLIBS)

gnumeric_bonobo_LDADD =				\
	../plugins/excel/libexcel.a		\
	../plugins/excel/libole2/libole2.a 	\
	$(EXTRA_GNOME_LIBS)			\
	$(GNOME_XML_LIB) 			\
	$(GUILE_LIBS)				\
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
idl_DATA = Gnumeric.idl

gladedir   = $(datadir)/gnumeric/glade
glade_DATA = 				\
	font-sel.glade			\
	hf-config.glade			\
	import.glade			\
	print.glade 			\
	summary.glade			\
	names.glade

glade_msgs = 				\
	font-sel.glade.h		\
	hf-config.glade.h		\
	import.glade.h			\
	print.glade.h

EXTRA_DIST = $(idl_DATA) $(glade_DATA) $(glade_msgs) $(GNUMERIC_CORBA_GENERATED)



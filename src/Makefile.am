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

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/libglade			\
	$(GNOME_INCLUDEDIR)				\
	$(GUILE_INCS)

GNUMERIC_BASE_SOURCES =				\
	about.c					\
	cell.h					\
	cell.c					\
	cellspan.c				\
	clipboard.c				\
	clipboard.h				\
	color.c					\
	color.h					\
	cursors.c				\
	cursors.h				\
	dates.c					\
	dates.h					\
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
	dialog-solver.c				\
	dialog-zoom.c				\
	dialogs.h				\
	dump.c					\
	dump.h					\
	eval.h					\
	eval.c					\
	expr.c					\
	expr.h					\
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
	symbol.h				\
	symbol.c				\
	utils.c					\
	utils.h					\
	widget-editable-label.c			\
	widget-editable-label.h			\
	workbook.c				\
	xml-io.c				\
	xml-io.h

GNUMERIC_CORBA_GENERATED = 			\
	Gnumeric-common.c			\
	Gnumeric-skels.c			\
	Gnumeric-stubs.c			\
	Gnumeric.h

GNUMERIC_BONOBO_SOURCES = 			\
	sheet-object-container.c		\
	sheet-object-container.h

BUILT_SOURCES = $(GNUMERIC_CORBA_GENERATED)

Gnumeric-impl.c: Gnumeric.h

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
	../libglade/glade/libglade.a		\
	$(GNOME_LIBDIR) 			\
	$(GNOMEUI_LIBS)				\
	$(GNOME_XML_LIB) 			\
	$(GUILE_LIBS)				\
	$(INTLLIBS)

gnumeric_bonobo_LDADD =				\
	../plugins/excel/libexcel.a		\
	../plugins/excel/libole2/libole2.a 	\
	../libglade/glade/libglade.a		\
	$(BONOBO_LIBS)				\
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
	touch $(distdir)/Gnumeric.idl

EXTRA_DIST = Gnumeric.idl

#number_match_SOURCES = number-match.c  formats.c dates.c number-match-test.c


#number_match_LDADD = 			\
#	$(GNOME_LIBDIR) 		\
#	$(GNOMEUI_LIBS)			\
#	$(GNOME_XML_LIB) 		\
#	$(INTLLIBS)

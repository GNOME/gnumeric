# CFLAGS += -g -Wall 	\
# 	-Wshadow             -Wpointer-arith  	\
# 	-Wmissing-prototypes -Wmissing-declarations 

bin_PROGRAMS = gnumeric gnumeric-corba

#noinst_PROGRAMS = number-match

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	$(GNOME_INCLUDEDIR)				\
	$(GUILE_INCS)

GNUMERIC_BASE_SOURCES =			\
	about.c				\
	cell.h				\
	cell.c				\
	cellspan.c			\
	clipboard.c			\
	clipboard.h			\
	color.c				\
	color.h				\
	cursors.c			\
	cursors.h			\
	dates.c				\
	dates.h				\
	dialog-cell-comment.c		\
	dialog-cell-format.c		\
	dialog-goto-cell.c		\
	dialog-insert-cells.c		\
	dialog-define-names.c		\
	dialog-delete-cells.c		\
	dialog-paste-special.c		\
	dialog-zoom.c			\
	dialogs.h			\
	dump.c				\
	dump.h				\
	eval.h				\
	eval.c				\
	expr.c				\
	expr.h				\
	file.c				\
	file.h				\
	formats.h			\
	formats.c			\
	func.c				\
	func.h				\
	fn-date.c			\
	fn-math.c			\
	fn-misc.c			\
	fn-sheet.c			\
	fn-string.c			\
	format.c			\
	format.h			\
	gnumeric-sheet.c		\
	gnumeric-sheet.h		\
	gnumeric-util.c			\
	gnumeric-util.h			\
	gnumeric.h			\
	item-bar.c			\
	item-bar.h			\
	item-cursor.c			\
	item-cursor.h			\
	item-debug.c			\
	item-debug.h			\
	item-edit.c			\
	item-edit.h			\
	item-grid.c			\
	item-grid.h			\
	main.h				\
	number-match.c			\
	number-match.h			\
	numbers.h			\
	parser.y			\
	pattern-selector.c		\
	pattern-selector.h		\
	pixmaps.h			\
	plugin.c			\
	plugin.h			\
	plugin-manager.c		\
	render-ascii.c			\
	render-ascii.h			\
	sheet.c				\
	sheet.h				\
	sheet-autofill.c		\
	sheet-autofill.h		\
	sheet-object.c			\
	sheet-object.h			\
	sheet-view.c			\
	sheet-view.h			\
	str.c				\
	str.h				\
	style.c				\
	style.h				\
	symbol.h			\
	symbol.c			\
	utils.c				\
	utils.h				\
	widget-editable-label.c		\
	widget-editable-label.h		\
	workbook.c			\
	xml-io.c			\
	xml-io.h

GNUMERIC_CORBA_GENERATED = 		\
	Gnumeric-common.c		\
	Gnumeric-skels.c		\
	Gnumeric-stubs.c		\
	Gnumeric.h

BUILT_SOURCES = $(GNUMERIC_CORBA_GENERATED)

Gnumeric-impl.c: Gnumeric.h

$(GNUMERIC_CORBA_GENERATED): Gnumeric.idl
	orbit-idl -I`$(GNOME_CONFIG) --datadir`/idl $(srcdir)/Gnumeric.idl

gnumeric_SOURCES =			\
	main.c				\
	normal-args.c			\
	$(GNUMERIC_BASE_SOURCES)

gnumeric_corba_SOURCES = 		\
	main.c				\
	corba-args.c			\
	Gnumeric-impl.c			\
	$(GNUMERIC_CORBA_GENERATED)	\
	$(GNUMERIC_BASE_SOURCES)

gnumeric_LDFLAGS = -export-dynamic
gnumeric_corba_LDFLAGS = $(gnumeric_LDFLAGS)

gnumeric_LDADD = 			\
	../plugins/excel/libexcel.a	\
	$(GNOME_LIBDIR) 		\
	$(GNOMEUI_LIBS)			\
	$(GNOME_XML_LIB) 		\
	$(GUILE_LIBS)			\
	$(INTLLIBS)

gnumeric_corba_LDADD = $(gnumeric_LDADD) $(GNOMEGNORBA_LIBS)

wc:
	wc -l $(gnumeric_SOURCES)

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

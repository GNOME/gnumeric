# CFLAGS += -g -Wall 	\
# 	-Wshadow             -Wpointer-arith  	\
# 	-Wmissing-prototypes -Wmissing-declarations 

bin_PROGRAMS = gnumeric

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	$(GNOME_INCLUDEDIR)

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
	dialog-cell-format.c		\
	dialog-goto-cell.c		\
	dialog-insert-cells.c		\
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
	func.c				\
	func.h				\
	fn-date.c			\
	fn-math.c			\
	fn-misc.c			\
	fn-sheet.c			\
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
	workbook.c			\
	xml-io.c			\
	xml-io.h

#if WITH_ORBIT
#GNUMERIC_CORBA_GENERATED =		\
#	Gnumeric-common.c		\
#	Gnumeric-skels.c		\
#	Gnumeric-stubs.c		\
#	Gnumeric.h
#
#GNUMERIC_CORBA_SOURCES =		\
#	Gnumeric-impl.c			\
#	$(GNUMERIC_CORBA_GENERATED) 
#
#$(GNUMERIC_CORBA_GENERATED: Gnumeric.idl
#	orbit-idl Gnumeric.idl
#
#	$(GNUMERIC_CORBA_SOURCES)	\
#endif

gnumeric_SOURCES =			\
	main.c				\
	$(GNUMERIC_BASE_SOURCES)

gnumeric_LDFLAGS = -export-dynamic
gnumeric_LDADD = 			\
	$(GNOME_LIBDIR) 		\
	$(GNOMEUI_LIBS)			\
	$(GNOME_XML_LIB) 		\
	$(INTLLIBS)


wc:
	wc -l $(gnumeric_SOURCES)

dist-hook:
	mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.xpm $(distdir)/pixmaps

EXTRA_DIST = Gnumeric.idl

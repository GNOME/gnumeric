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

INCLUDES = 								\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 			\
	-DGNUMERIC_DATADIR=\""$(gnumeric_datadir)"\"			\
	-DGNUMERIC_LIBDIR=\""$(gnumeric_libdir)"\"			\
	-DGNOME_ICONDIR=\""$(datadir)/pixmaps"\"			\
	-DGNOME_DATADIR=\""$(datadir)"\"	 			\
	-I$(includedir)				 			\
	-I$(top_srcdir)							\
	-I$(srcdir)							\
	-I$(srcdir)/widgets						\
	-I$(srcdir)/functions						\
	-I$(srcdir)/dialogs						\
	$(EXTRA_GNOME_CFLAGS)						\
	$(GUILE_INCS)

GNUMERIC_BASE_SOURCES =				\
	about.c					\
	analysis-tools.c			\
	analysis-tools.h			\
	application.c				\
	application.h				\
	auto-format.c				\
	auto-format.h				\
	cell.h					\
	cell.c					\
	cell-draw.c				\
	cell-draw.h				\
	cellspan.c				\
	cellspan.h				\
	clipboard.c				\
	clipboard.h				\
	cmd-edit.h				\
	cmd-edit.c				\
	collect.h				\
	collect.c				\
	colrow.c				\
	colrow.h				\
	commands.h				\
	commands.c				\
	complete.c				\
	complete.h				\
	complete-sheet.c			\
	complete-sheet.h			\
	command-context.c			\
	command-context.h			\
	command-context-priv.h			\
	command-context-corba.c			\
	command-context-corba.h			\
	io-context.c				\
	io-context.h				\
	io-context-priv.h			\
	complex.h				\
	complex.c				\
	corba.h					\
	dates.c					\
	dates.h					\
	datetime.c				\
	datetime.h				\
	dependent.c				\
	dependent.h				\
	error-info.c				\
	error-info.h				\
	eval.h					\
	eval.c					\
	expr.c					\
	expr.h					\
	expr-name.c				\
	expr-name.h				\
	file.c					\
	file.h					\
	file-priv.h					\
	file-autoft.c				\
	file-autoft.h				\
	format-template.h			\
	format-template.c			\
	formats.h				\
	formats.c				\
	func.c					\
	func.h					\
	func-util.h				\
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
	gui-clipboard.c				\
	gui-clipboard.h				\
	gui-file.c				\
	gui-file.h				\
	gui-gnumeric.h				\
	gutils.c				\
	gutils.h				\
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
	module-plugin-defs.h			\
	mstyle.c				\
	mstyle.h				\
	number-match.c				\
	number-match.h				\
	numbers.h				\
	parser.y				\
	parse-util.c				\
	parse-util.h				\
	pattern.h				\
	pattern.c				\
	plugin-loader-module.c			\
	plugin-loader-module.h			\
	plugin-loader.c				\
	plugin-loader.h				\
	plugin-service.c			\
	plugin-service.h			\
	plugin-util.c				\
	plugin-util.h				\
	plugin.c				\
	plugin.h				\
	portability.h				\
	position.c				\
	position.h				\
	preview-grid.c				\
	preview-grid.h				\
	print.c					\
	print.h					\
	print-cell.c				\
	print-cell.h				\
	print-info.c				\
	print-info.h				\
	ranges.c				\
	ranges.h				\
	regression.c				\
	regression.h				\
	rendered-value.c			\
	rendered-value.h			\
	search.h				\
	search.c				\
	selection.c				\
	selection.h				\
	sheet.c					\
	sheet.h					\
	sheet-merge.c				\
	sheet-merge.h				\
	sheet-private.h				\
	sheet-autofill.c			\
	sheet-autofill.h			\
	sheet-object.c				\
	sheet-object.h				\
	sheet-object-impl.h			\
	sheet-object-cell-comment.c		\
	sheet-object-cell-comment.h		\
	sheet-object-graphic.c			\
	sheet-object-graphic.h			\
	sheet-object-widget.c			\
	sheet-object-widget.h			\
	sheet-style.c				\
	sheet-style.h				\
	sheet-control-gui.c			\
	sheet-control-gui.h			\
	solver-lp.c                     	\
	solver.h				\
	sort.c					\
	sort.h					\
	str.c					\
	str.h					\
	stf.c					\
	stf.h					\
	stf-export.c				\
	stf-export.h				\
	stf-parse.c				\
	stf-parse.h				\
	style.c					\
	style.h					\
	style-border.c				\
	style-border.h				\
	style-color.c				\
	style-color.h				\
	summary.c				\
	summary.h				\
	symbol.h				\
	symbol.c				\
	value.c					\
	value.h					\
	value-sheet.c				\
	workbook.c				\
	workbook.h				\
	workbook-cmd-format.c			\
	workbook-cmd-format.h			\
	workbook-edit.c				\
	workbook-edit.h				\
	workbook-format-toolbar.c		\
	workbook-format-toolbar.h		\
	workbook-object-toolbar.c		\
	workbook-object-toolbar.h		\
	workbook-private.h			\
	workbook-private.c			\
	workbook-view.c				\
	workbook-view.h				\
	workbook-control.c 			\
	workbook-control.h 			\
	workbook-control-priv.h 		\
	workbook-control-gui.c 			\
	workbook-control-gui.h 			\
	workbook-control-gui-priv.h 		\
	xml-io.c				\
	xml-io.h				\
	xml-io-autoft.c				\
	xml-io-autoft.h

GNUMERIC_BONOBO_SOURCES = 			\
	bonobo-io.c				\
	bonobo-io.h				\
	embeddable-grid.c			\
	embeddable-grid.h			\
	graph-vector.c				\
	graph-vector.h				\
	sheet-object-bonobo.c			\
	sheet-object-bonobo.h			\
	sheet-object-container.c		\
	sheet-object-container.h		\
	sheet-object-item.c			\
	sheet-object-item.h
#	workbook-control-bonobo.h 		\
#	workbook-control-bonobo.c

gnumeric_SOURCES =				\
	main.c					\
	normal-args.c				\
	$(GNUMERIC_BASE_SOURCES)

gnumeric_bonobo_SOURCES =			\
	main.c					\
	corba-args.c				\
	$(GNUMERIC_BASE_SOURCES)		\
	$(GNUMERIC_BONOBO_SOURCES)

gnumeric_LDFLAGS = -export-dynamic

gnumeric_libs =					\
	dialogs/libdialogs.a			\
	widgets/libwidgets.a			\
	functions/libfunctions.a

gnumeric_LDADD = 				\
	$(gnumeric_libs)			\
	$(EXTRA_GNOME_LIBS)			\
	$(GNOME_XML_LIB) 			\
	$(GUILE_LIBS)				\
	$(INTLLIBS)
#	$(PSPELL_LIBS)

gnumeric_bonobo_LDADD =				\
	$(gnumeric_libs)			\
	-lbonobo-print				\
	../idl/libGnumericIDL.a			\
	$(EXTRA_GNOME_LIBS)			\
	$(GNOME_XML_LIB) 			\
	$(GUILE_LIBS)				\
	$(PSPELL_LIBS)				\
	$(INTLLIBS)

dist-hook:
	-mkdir $(distdir)/pixmaps
	cp $(srcdir)/pixmaps/*.xpm $(distdir)/pixmaps

#number_match_SOURCES = number-match.c  formats.c dates.c number-match-test.c

#number_match_LDADD = 			\
#	$(GNOME_LIBDIR) 		\
#	$(GNOMEUI_LIBS)			\
#	$(GNOME_XML_LIB) 		\
#	$(INTLLIBS)

EXTRA_DIST = portability.h.in GNOME_Gnumeric.xml

uidir=$(datadir)/gnome/ui
ui_DATA = GNOME_Gnumeric.xml

#
# Keep this in sync with ../../icons/Makefile.am
#
iconsdir = $(datadir)/pixmaps/gnumeric

if BONOBO
gnumeric_version="$(VERSION)-bonobo"
else
gnumeric_version="$(VERSION)"
endif
gnumeric_libdir = $(datadir)/gnumeric/$(gnumeric_version)

INCLUDES = 						\
	-I$(srcdir)/..					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"		\
	-I$(includedir)				 	\
	$(EXTRA_GNOME_CFLAGS)				\
	$(GUILE_INCS)

noinst_LIBRARIES = libdialogs.a

libdialogs_a_SOURCES =				\
	dialog-advanced-filter.c		\
	dialog-analysis-tools.c			\
	dialog-autocorrect.c			\
	dialog-autosave.c			\
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
	dialog-plugin-manager.c 		\
	dialog-printer-setup.c			\
	dialog-sheet-order.c			\
	dialog-simple-input.c			\
	dialog-solver.c				\
	dialog-stf.c				\
	dialog-stf.h				\
	dialog-stf-main-page.c			\
	dialog-stf-csv-page.c			\
	dialog-stf-fixed-page.c			\
	dialog-stf-format-page.c		\
	dialog-stf-preview.c			\
	dialog-stf-preview.h			\
	dialog-stf-export.c			\
	dialog-stf-export.h			\
	dialog-stf-export-private.h		\
	dialog-stf-export-sheet-page.c		\
	dialog-stf-export-format-page.c		\
	dialog-summary.c			\
	dialog-workbook-attr.c			\
	dialog-zoom.c				\
	utils-dialog.c                          \
	dialogs.h				\
	tools.h                                 \
	utils-dialog.h				\
	utils-dialog.c

gladedir   = $(gnumeric_libdir)/glade
glade_DATA = 				\
	advanced-filter.glade		\
	analysis-tools.glade		\
	autocorrect.glade		\
	autocorrect-exceptions.glade	\
	autosave.glade			\
	autosave-prompt.glade		\
	col-width.glade			\
	font-sel.glade			\
	hf-config.glade			\
	import.glade			\
	names.glade			\
	print.glade 			\
	row-height.glade		\
	sheet-order.glade		\
	sheet-rename.glade		\
	solver.glade			\
	solver-options.glade		\
	summary.glade			\
	dialog-zoom.glade		\
	insert-cells.glade		\
	delete-cells.glade		\
	cell-format.glade		\
	cell-sort.glade			\
	dialog-stf.glade		\
	dialog-stf-export.glade		\
	plugin-manager.glade		\
	function-select.glade 		\
	formula-guru.glade		\
	workbook-attr.glade

glade_msgs = 				\
	advanced-filter.glade.h		\
	analysis-tools.glade.h		\
	autocorrect-exceptions.glade.h	\
	autocorrect.glade.h		\
	autosave.glade.h		\
	autosave-prompt.glade.h		\
	cell-sort.glade.h		\
	col-width.glade.h		\
	font-sel.glade.h		\
	hf-config.glade.h		\
	import.glade.h			\
	insert-cells.glade.h		\
	delete-cells.glade.h		\
	names.glade.h			\
	print.glade.h			\
	row-height.glade.h		\
	sheet-order.glade.h		\
	sheet-rename.glade.h		\
	solver.glade.h			\
	solver-options.glade.h		\
	summary.glade.h			\
	dialog-zoom.glade.h		\
	cell-format.glade.h		\
	cell-sort.glade.h		\
	dialog-stf.glade.h		\
	dialog-stf-export.glade.h	\
	plugin-manager.glade.h		\
	function-select.glade.h		\
	formula-guru.glade.h		\
	workbook-attr.glade.h

EXTRA_DIST = $(glade_DATA) $(glade_msgs)




#
# Keep this in sync with ../../icons/Makefile.am
#
iconsdir = $(datadir)/pixmaps/gnumeric

if BONOBO
gnumeric_version="$(VERSION)-bonobo"
libdialogs_a_SOURCES =	$(base_files) dialog-graph-guru.c
else
gnumeric_version="$(VERSION)"
libdialogs_a_SOURCES =	$(base_files)
endif
gnumeric_libdir = $(datadir)/gnumeric/$(gnumeric_version)

INCLUDES = 								\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 			\
	-DGNUMERIC_DATADIR=\""$(gnumeric_datadir)"\"			\
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"				\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/src						\
	-I$(srcdir)							\
	-I$(includedir)				 			\
	$(EXTRA_GNOME_CFLAGS)						\
	$(GUILE_INCS)

noinst_LIBRARIES = libdialogs.a

base_files =					\
	dialog-about.c				\
	dialog-advanced-filter.c		\
	dialog-analysis-tools.c			\
	dialog-autocorrect.c			\
	dialog-autocorrect.h			\
	dialog-autoformat.c			\
	dialog-autosave.c			\
	dialog-cell-comment.c			\
	dialog-cell-format.c			\
	dialog-cell-sort.c			\
	dialog-consolidate.c			\
	dialog-function-select.c		\
	dialog-formula-guru.c			\
	dialog-goal-seek.c			\
	dialog-goto-cell.c			\
	dialog-insert-cells.c			\
	dialog-define-names.c			\
	dialog-delete-cells.c			\
	dialog-paste-special.c			\
	dialog-plugin-manager.c 		\
	dialog-printer-setup.c			\
	dialog-search-replace.c			\
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
	dialog-validate.c			\
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
	autocorrect-exceptions.glade	\
	autocorrect.glade		\
	autoformat.glade		\
	autosave-prompt.glade		\
	autosave.glade			\
	cell-format.glade		\
	cell-sort.glade			\
	col-width.glade			\
	colrow.glade			\
	consolidate.glade		\
	correlation.glade		\
	covariance.glade		\
	delete-cells.glade		\
	descriptive-stats.glade		\
	dialog-stf-export.glade		\
	dialog-stf.glade		\
	dialog-zoom.glade		\
	font-sel.glade			\
	formula-guru.glade		\
	function-select.glade 		\
	graph-guru.glade		\
	hf-config.glade			\
	insert-cells.glade		\
	names.glade			\
	plugin-manager.glade		\
	print.glade 			\
	rank.glade			\
	row-height.glade		\
	search-replace.glade		\
	sheet-order.glade		\
	sheet-rename.glade		\
	solver-options.glade		\
	solver.glade			\
	summary.glade			\
	validate.glade			\
	workbook-attr.glade

EXTRA_DIST = $(glade_DATA)

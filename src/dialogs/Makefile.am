#
# Keep this in sync with ../../icons/Makefile.am
#

if WITH_BONOBO
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
	-I$(top_srcdir)							\
	-I$(top_srcdir)/src						\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libdialogs.a

base_files =					\
	dialog-about.c				\
	dialog-advanced-filter.c		\
	dialog-analysis-tools.c			\
	dialog-autocorrect.c			\
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
	dialog-search.c				\
	dialog-search-replace.c			\
	dialog-sheet-name.c			\
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
	dialog-tabulate.c			\
	dialog-summary.c			\
	dialog-workbook-attr.c			\
	dialog-zoom.c				\
	dialogs.h				\
	tools.h

gladedir   = $(gnumeric_libdir)/glade
glade_DATA = 				\
	advanced-filter.glade		\
	analysis-tools.glade		\
	anova-one.glade			\
	anova-two.glade			\
	autocorrect.glade		\
	autoformat.glade		\
	autosave.glade			\
	cell-comment.glade		\
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
	exp-smoothing.glade		\
	font-sel.glade			\
	formula-guru.glade		\
	fourier-analysis.glade		\
	function-select.glade 		\
	graph-guru.glade		\
	goalseek.glade			\
	hf-config.glade			\
	histogram.glade			\
	insert-cells.glade		\
	mean-tests.glade		\
	moving-averages.glade		\
	names.glade			\
	plugin-manager.glade		\
	print.glade 			\
	rank.glade			\
	random-generation.glade		\
	regression.glade		\
	row-height.glade		\
	sampling.glade			\
	search.glade			\
	search-replace.glade		\
	sheet-order.glade		\
	sheet-rename.glade		\
	solver.glade			\
	summary.glade			\
	tabulate.glade			\
	variance-tests.glade		\
	workbook-attr.glade

EXTRA_DIST = $(glade_DATA)

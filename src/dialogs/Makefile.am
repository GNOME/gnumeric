#
# Keep this in sync with ../../icons/Makefile.am
#

if WITH_BONOBO
gnumeric_version="$(VERSION)-bonobo"
libdialogs_a_SOURCES =	$(base_files)
else
gnumeric_version="$(VERSION)"
libdialogs_a_SOURCES =	$(base_files)
endif

INCLUDES = 								\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 			\
	-DGNUMERIC_DATADIR=\""$(gnumeric_datadir)"\"			\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/src						\
	-I$(top_srcdir)/src/tools					\
	-I$(top_srcdir)/src/cut-n-paste-code/foocanvas			\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libdialogs.a

base_files =					\
	dao-gui-utils.c				\
	dao-gui-utils.h				\
	dialog-about.c				\
	dialog-advanced-filter.c		\
	dialog-analysis-tools.c			\
	dialog-autocorrect.c			\
	dialog-autofilter.c			\
	dialog-autoformat.c			\
	dialog-autosave.c			\
	dialog-cell-comment.c			\
	dialog-cell-format.c			\
	dialog-cell-sort.c			\
	dialog-col-width.c			\
	dialog-col-row.c			\
	dialog-consolidate.c			\
	dialog-fill-series.c			\
	dialog-function-select.c		\
	dialog-formula-guru.c			\
	dialog-goal-seek.c			\
	dialog-goto-cell.c			\
	dialog-hyperlink.c			\
	dialog-insert-cells.c			\
	dialog-define-names.c			\
	dialog-delete-cells.c			\
	dialog-merge.c				\
	dialog-password.c			\
	dialog-paste-special.c			\
	dialog-pivottable.c			\
	dialog-plugin-manager.c 		\
	dialog-printer-setup.c			\
	dialog-preferences.c			\
	dialog-random-generator.c		\
	dialog-row-height.c			\
	dialog-scenarios.c			\
	dialog-search.c				\
	dialog-search-replace.c			\
	dialog-sheet-order.c			\
	dialog-shuffle.c			\
	dialog-simulation.c			\
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
	tool-dialogs.h

gladedir   = $(gnumeric_gladedir)
glade_DATA = 				\
	advanced-filter.glade		\
	anova-one.glade			\
	anova-two.glade			\
	autocorrect.glade		\
	autofilter-expression.glade	\
	autofilter-top10.glade       	\
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
	fill-series.glade		\
	font-sel.glade			\
	formula-guru.glade		\
	fourier-analysis.glade		\
	function-select.glade 		\
	goalseek.glade			\
	goto.glade			\
	hf-config.glade			\
	histogram.glade			\
	hyperlink.glade			\
	insert-cells.glade		\
	mean-tests.glade		\
	merge.glade			\
	moving-averages.glade		\
	names.glade			\
	pivottable.glade		\
	plugin-manager.glade		\
	preferences.glade 		\
	print.glade 			\
	rank.glade			\
	random-generation.glade		\
	regression.glade		\
	row-height.glade		\
	sampling.glade			\
	scenario-add.glade		\
	scenario-manager.glade		\
	search.glade			\
	search-replace.glade		\
	sheet-order.glade		\
	shuffle.glade			\
	so-arrow.glade			\
	so-checkbox.glade		\
	so-fill.glade			\
	so-frame.glade			\
	so-scrollbar.glade		\
	simulation.glade		\
	solver.glade			\
	summary.glade			\
	tabulate.glade			\
	variance-tests.glade		\
	workbook-attr.glade

EXTRA_DIST = $(glade_DATA)

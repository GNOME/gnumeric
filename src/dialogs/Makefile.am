#
# Keep this in sync with ../../icons/Makefile.am
#

libdialogs_la_SOURCES = $(base_files)

if ENABLE_SOLVER
  libdialogs_la_SOURCES += dialog-solver.c
endif

AM_CPPFLAGS = 								\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 			\
	-DGNUMERIC_DATADIR=\""$(gnumeric_datadir)"\"			\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/src						\
	-I$(top_srcdir)/src/tools					\
	-DGNUMERIC_INTERNAL						\
	$(GNUMERIC_CFLAGS)

noinst_LTLIBRARIES = libdialogs.la

base_files =					\
	help.h					\
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
	dialog-data-table.c			\
	dialog-fill-series.c			\
	dialog-function-select.c		\
	dialog-formula-guru.c			\
	dialog-goal-seek.c			\
	dialog-goto-cell.c			\
	dialog-hyperlink.c			\
	dialog-insert-cells.c			\
	dialog-define-names.c			\
	dialog-paste-names.c			\
	dialog-delete-cells.c			\
	dialog-merge.c				\
	dialog-password.c			\
	dialog-paste-special.c			\
	dialog-plugin-manager.c 		\
	dialog-preferences.c			\
	dialog-random-generator.c		\
	dialog-recent.c				\
	dialog-row-height.c			\
	dialog-quit.c				\
	dialog-scenarios.c			\
	dialog-search.c				\
	dialog-search-replace.c			\
	dialog-sheet-order.c			\
	dialog-shuffle.c			\
	dialog-simulation.c			\
	dialog-so-list.c			\
	dialog-so-styled.c			\
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
	dialog-tabulate.c			\
	dialog-doc-metadata.c			\
	dialog-view.c				\
	dialog-workbook-attr.c			\
	dialog-zoom.c				\
	dialogs.h				\
	tool-dialogs.h				\
	dialog-printer-setup.c

gladedir   = $(gnumeric_datadir)/glade
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
	dao.glade			\
	data-table.glade		\
	define-name.glade		\
	delete-cells.glade		\
	descriptive-stats.glade		\
	dialog-stf-export.glade		\
	dialog-stf.glade		\
	dialog-zoom.glade		\
	doc-meta-data.glade		\
	exp-smoothing.glade		\
	fill-series.glade		\
	font-sel.glade			\
	formula-guru.glade		\
	fourier-analysis.glade		\
	function-select.glade 		\
	goalseek.glade			\
	goto.glade			\
	hf-config.glade			\
	hf-dt-format.glade		\
	histogram.glade			\
	hyperlink.glade			\
	insert-cells.glade		\
	mean-tests.glade		\
	merge.glade			\
	moving-averages.glade		\
	paste-names.glade		\
	plugin-manager.glade		\
	preferences.glade 		\
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
	so-checkbox.glade		\
	so-frame.glade			\
	so-list.glade			\
	so-scrollbar.glade		\
	simulation.glade		\
	solver.glade			\
	tabulate.glade			\
	variance-tests.glade		\
	view.glade			\
	workbook-attr.glade		\
	print.glade

EXTRA_DIST = $(glade_DATA)

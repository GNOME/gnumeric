#
# Keep this in sync with ../../icons/Makefile.am
#

nodist_libdialogs_la_SOURCES = embedded-ui.c

AM_CPPFLAGS = 								\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 			\
	-DGNUMERIC_DATADIR=\""$(gnumeric_datadir)"\"			\
	-I$(top_srcdir)							\
	-I$(top_srcdir)/src						\
	-I$(top_srcdir)/src/tools					\
	-DGNUMERIC_INTERNAL						\
	$(GNUMERIC_CFLAGS)

noinst_LTLIBRARIES = libdialogs.la

libdialogs_la_SOURCES =					\
	help.h					\
	dao-gui-utils.c				\
	dao-gui-utils.h				\
	dialog-about.c				\
	dialog-advanced-filter.c		\
	dialog-analysis-tools.c			\
	dialog-analysis-tool-chi-squared.c	\
	dialog-analysis-tool-frequency.c	\
	dialog-analysis-tool-kaplan-meier.c	\
	dialog-analysis-tool-normality.c	\
	dialog-analysis-tool-one-mean.c		\
	dialog-analysis-tool-principal-components.c	\
	dialog-analysis-tool-sign-test.c	\
	dialog-analysis-tool-wilcoxon-mann-whitney.c    \
	dialog-autofilter.c			\
	dialog-autoformat.c			\
	dialog-autosave.c			\
	dialog-cell-comment.c			\
	dialog-cell-format.c			\
	dialog-cell-format-cond.c		\
	dialog-cell-sort.c			\
	dialog-col-width.c			\
	dialog-col-row.c			\
	dialog-consolidate.c			\
	dialog-data-slicer.c			\
	dialog-data-table.c			\
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
	dialog-plugin-manager.c 		\
	dialog-preferences.c			\
	dialog-random-generator.c		\
	dialog-random-generator-cor.c		\
	dialog-recent.c				\
	dialog-row-height.c			\
	dialog-quit.c				\
	dialog-scenarios.c			\
	dialog-search.c				\
	dialog-search-replace.c			\
	dialog-sheet-order.c			\
	dialog-sheet-rename.c			\
	dialog-sheet-resize.c			\
	dialog-sheetobject-size.c		\
	dialog-shuffle.c			\
	dialog-simulation.c			\
	dialog-so-list.c			\
	dialog-so-styled.c			\
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
	dialog-tabulate.c			\
	dialog-doc-metadata.c			\
	dialog-view.c				\
	dialog-workbook-attr.c			\
	dialog-zoom.c				\
	dialogs.h				\
	embedded-ui.h				\
	tool-dialogs.h				\
	dialog-printer-setup.c

# uidir   = $(gnumeric_datadir)/ui
# ui_DATA =

embedded_uis = 			\
	advanced-filter.ui		\
	anova-one.ui			\
	anova-two.ui			\
	autofilter-expression.ui	\
	autofilter-top10.ui		\
	autoformat.ui			\
	autosave.ui			\
	cell-comment.ui			\
	cell-format-cond.ui		\
	cell-format.ui			\
	cell-sort.ui			\
	chi-squared.ui			\
	col-width.ui			\
	colrow.ui			\
	consolidate.ui			\
	correlation.ui			\
	covariance.ui			\
	dao.ui				\
	data-slicer.ui			\
	data-table.ui			\
	define-name.ui			\
	delete-cells.ui			\
	descriptive-stats.ui		\
	dialog-stf-export.ui		\
	dialog-stf.ui			\
	dialog-zoom.ui			\
	doc-meta-data.ui		\
	exp-smoothing.ui		\
	fill-series.ui			\
	formula-guru.ui			\
	fourier-analysis.ui		\
	frequency.ui			\
	function-select.ui		\
	goalseek.ui			\
	goto.ui				\
	hf-config.ui			\
	hf-dt-format.ui			\
	histogram.ui			\
	hyperlink.ui			\
	insert-cells.ui			\
	kaplan-meier.ui			\
	mean-tests.ui			\
	merge.ui			\
	moving-averages.ui		\
	normality-tests.ui		\
	one-mean-test.ui		\
	paste-special.ui		\
	plugin-manager.ui		\
	preferences.ui			\
	principal-components.ui		\
	print.ui			\
	quit.ui				\
	random-generation-cor.ui	\
	random-generation.ui		\
	rank.ui				\
	recent.ui			\
	regression.ui			\
	row-height.ui			\
	sampling.ui			\
	scenario-add.ui			\
	scenario-manager.ui		\
	search-replace.ui		\
	search.ui			\
	sheet-order.ui			\
	sheet-rename.ui			\
	sheet-resize.ui			\
	sheetobject-size.ui		\
	shuffle.ui			\
	sign-test-two.ui		\
	sign-test.ui			\
	simulation.ui			\
	so-button.ui			\
	so-checkbox.ui			\
	so-frame.ui			\
	so-list.ui			\
	so-radiobutton.ui		\
	so-scrollbar.ui			\
	solver.ui			\
	tabulate.ui			\
	variance-tests.ui		\
	view.ui				\
	wbcg.ui				\
	wilcoxon-mann-whitney.ui	\
	workbook-attr.ui

BUILT_SOURCES = embedded-ui.c

embedded-ui.c: $(top_srcdir)/tools/embedder $(embedded_uis) Makefile
	cd $(srcdir) && @PERL@ $(abs_top_srcdir)/tools/embedder \
		--register-function=gnm_register_ui_files \
		--register-file-function=go_rsm_register_file \
		--id-prefix=gnm: \
		--include '<goffice/goffice.h>' \
		--include embedded-ui.h \
		COMPRESS $(embedded_uis) >$(abs_builddir)/$@

EXTRA_DIST = $(ui_DATA) $(embedded_uis)

CLEANFILES = $(BUILT_SOURCES)

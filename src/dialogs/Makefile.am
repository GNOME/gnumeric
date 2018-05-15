#
# Keep this in sync with ../../icons/Makefile.am
#

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
	help.h						\
	dao-gui-utils.c					\
	dao-gui-utils.h					\
	dialog-about.c					\
	dialog-advanced-filter.c			\
	dialog-analysis-tools.c				\
	dialog-analysis-tool-chi-squared.c		\
	dialog-analysis-tool-frequency.c		\
	dialog-analysis-tool-kaplan-meier.c		\
	dialog-analysis-tool-normality.c		\
	dialog-analysis-tool-one-mean.c			\
	dialog-analysis-tool-principal-components.c	\
	dialog-analysis-tool-sign-test.c		\
	dialog-analysis-tool-wilcoxon-mann-whitney.c	\
	dialog-autofilter.c				\
	dialog-autoformat.c				\
	dialog-autosave.c				\
	dialog-cell-comment.c				\
	dialog-cell-format.c				\
	dialog-cell-format-cond.c			\
	dialog-cell-sort.c				\
	dialog-col-width.c				\
	dialog-col-row.c				\
	dialog-consolidate.c				\
	dialog-data-slicer.c				\
	dialog-data-table.c				\
	dialog-fill-series.c				\
	dialog-function-select.c			\
	dialog-formula-guru.c				\
	dialog-goal-seek.c				\
	dialog-goto-cell.c				\
	dialog-hyperlink.c				\
	dialog-insert-cells.c				\
	dialog-define-names.c				\
	dialog-delete-cells.c				\
	dialog-merge.c					\
	dialog-password.c				\
	dialog-paste-special.c				\
	dialog-plugin-manager.c				\
	dialog-preferences.c				\
	dialog-random-generator.c			\
	dialog-random-generator-cor.c			\
	dialog-recent.c					\
	dialog-row-height.c				\
	dialog-quit.c					\
	dialog-scenarios.c				\
	dialog-search.c					\
	dialog-search-replace.c				\
	dialog-sheet-compare.c				\
	dialog-sheet-order.c				\
	dialog-sheet-rename.c				\
	dialog-sheet-resize.c				\
	dialog-sheetobject-size.c			\
	dialog-shuffle.c				\
	dialog-simulation.c				\
	dialog-so-list.c				\
	dialog-so-styled.c				\
	dialog-solver.c					\
	dialog-stf.c					\
	dialog-stf.h					\
	dialog-stf-main-page.c				\
	dialog-stf-csv-page.c				\
	dialog-stf-fixed-page.c				\
	dialog-stf-format-page.c			\
	dialog-stf-preview.c				\
	dialog-stf-preview.h				\
	dialog-stf-export.c				\
	dialog-stf-export.h				\
	dialog-tabulate.c				\
	dialog-doc-metadata.c				\
	dialog-view.c					\
	dialog-workbook-attr.c				\
	dialog-zoom.c					\
	dialogs.h					\
	embedded-ui.h					\
	tool-dialogs.h					\
	dialog-printer-setup.c

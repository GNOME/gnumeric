figs =							\
	figures/add-decimals.png				\
	figures/advanced-filter-1.png			\
	figures/advanced-filter-2.png			\
	figures/align-center.png				\
	figures/align-left.png				\
	figures/align-right.png				\
	figures/analysis-tools.png			\
	figures/analysistools-correlation-ex1.png	\
	figures/analysistools-correlation-ex2.png	\
	figures/analysistools-correlation-ex3.png	\
	figures/analysistools-correlation.png		\
	figures/analysistools-covariance.png		\
	figures/analysistools-descstats.png		\
	figures/analysistools-outputoptions.png		\
	figures/analysistools-ranges.png			\
	figures/analysistools-rank.png			\
	figures/analysistools-tools.png			\
	figures/arrow.png				\
	figures/auto-correct.png				\
	figures/auto-save.png				\
	figures/auto-sum.png				\
	figures/autocorrect.png				\
	figures/autofill-1.png				\
	figures/autofill-10.png				\
	figures/autofill-2.png				\
	figures/autofill-3.png				\
	figures/autofill-4.png				\
	figures/autofill-5.png				\
	figures/autofill-6.png				\
	figures/autofill-7.png				\
	figures/autofill-8.png				\
	figures/autofill-9.png				\
	figures/autosave.png				\
	figures/bold.png					\
	figures/border.png				\
	figures/cells-1.png				\
	figures/cells-2.png				\
	figures/define-name.png				\
	figures/dnd-selection-1.png			\
	figures/dndselection-1.png			\
	figures/drawing-toolbar.png			\
	figures/editing-1.png				\
	figures/editing-cell-selection-1.png		\
	figures/file-dialog.png				\
	figures/filter.png				\
	figures/function-wizard.png			\
	figures/function.png				\
	figures/goal-seek.png				\
	figures/graphing.png				\
	figures/insert-object.png			\
	figures/italic.png				\
	figures/line.png					\
	figures/modify-comment.png			\
	figures/money.png				\
	figures/number-format-buttonbar.png		\
	figures/number-format-color-dialog.png		\
	figures/number-format-dialog.png			\
	figures/number-format-font-dialog.png		\
	figures/number-format-justification-dialog-2.png	\
	figures/number-format-justification.png		\
	figures/oval.png					\
	figures/percentage.png				\
	figures/plug-ins.png				\
	figures/plugins.png				\
	figures/print-large.png				\
	figures/print-preview-back.png			\
	figures/print-preview-first.png			\
	figures/print-preview-fit.png			\
	figures/print-preview-fittall.png		\
	figures/print-preview-fitwide.png		\
	figures/print-preview-last.png			\
	figures/print-preview-next.png			\
	figures/print-preview-toolbar.png		\
	figures/print-preview-zoomin.png			\
	figures/print-preview-zoomout.png		\
	figures/print-preview.png			\
	figures/print-worksheet.png			\
	figures/print.png				\
	figures/printing-preview.png			\
	figures/printing-setup-header.png		\
	figures/printing-setup-margins.png		\
	figures/printing-setup-page-1.png		\
	figures/printing-setup-page.png			\
	figures/rect.png					\
	figures/redo.png					\
	figures/remove-decimals.png			\
	figures/selection-1.png				\
	figures/selection-10.png				\
	figures/selection-2.png				\
	figures/selection-3.png				\
	figures/selection-4.png				\
	figures/selection-5.png				\
	figures/selection-6.png				\
	figures/selection-7.png				\
	figures/selection-8.png				\
	figures/selection-9.png				\
	figures/sheet-indicator.png			\
	figures/solver-01.png				\
	figures/solver-02.png				\
	figures/solver-03.png				\
	figures/solver-04.png				\
	figures/solver-05.png				\
	figures/sort-ascending.png			\
	figures/sort-descending.png			\
	figures/sort.png					\
	figures/stock-copy.png				\
	figures/stock-cut.png				\
	figures/stock-new.png				\
	figures/stock-open.png				\
	figures/stock-paste.png				\
	figures/stock-save.png				\
	figures/summary.png				\
	figures/thousands.png				\
	figures/toolbar.png				\
	figures/underline.png				\
	figures/undo.png					\
	figures/worksheet-all-1.png			\
	figures/worksheet-cols-1.png			\
	figures/worksheet-data-1.png			\
	figures/worksheet-data-2.png			\
	figures/worksheet-data-3.png			\
	figures/worksheet-data-4.png			\
	figures/worksheet-info-1.png			\
	figures/worksheet-rows-1.png			\
	figures/worksheet-running-calc-1.png		\
	figures/worksheet-zoomin-1.jpg			\
	figures/worksheet-zoomout-1.jpg			\
	figures/zoom-dialog.png				\
	figures/zoom-toolbar.png				\
	figures/zoom.png					\
	figures/analysistools-ANOVA2w-ex1.png		\
	figures/analysistools-ANOVA2w-ex2.png		\
	figures/analysistools-ANOVA2wo-ex1.png		\
	figures/analysistools-ANOVA2wo-ex2.png		\
	figures/analysistools-covariance-ex1.png		\
	figures/analysistools-covariance-ex2.png		\
	figures/analysistools-descstats-ex1.png		\
	figures/analysistools-descstats-ex2.png	


docname = gnumeric
lang = C
sgml_ents = 					\
	editing.sgml				\
	number-format.sgml			\
	formulas.sgml				\
	cell_refer.sgml				\
	selections.sgml				\
	copying-moving.sgml			\
	printing.sgml				\
	autofill.sgml				\
	introduction.sgml			\
	files.sgml				\
	autosave.sgml				\
	functions.sgml				\
	printing.sgml				\
	analysis-tools.sgml			\
	solver.sgml				\
	autocorrect.sgml			\
	filter.sgml				\
	authors.sgml				\
	license.sgml

include $(top_srcdir)/doc/sgmldocs.make
dist-hook: app-dist-hook

PERL_FILES =					\
	make-func-index.pl			\
	make-func-list.pl

# include generated files to simplify installation
EXTRA_DIST +=					\
	topic.dat				\
	func.defs				\
	func-header.sgml func-footer.sgml	\
	func-list.sgml func-index.sgml		\
	gnumeric.1				\
	$(PERL_FILES)

functions.sgml: func-list.sgml func-index.sgml func-header.sgml func-footer.sgml
	cd $(srcdir) && cat func-header.sgml func-index.sgml func-list.sgml func-footer.sgml > functions.sgml

func-list.sgml: func.defs make-func-list.pl
	cd $(srcdir) && perl make-func-list.pl func.defs > func-list.sgml

func-index.sgml: func.defs make-func-index.pl
	cd $(srcdir) && perl make-func-index.pl func.defs > func-index.sgml

func.defs: 
	gnumericdir=`cd $(top_builddir)/src && pwd`; \
	cd $(srcdir) && $$gnumericdir/gnumeric --dump-func-defs=func.defs

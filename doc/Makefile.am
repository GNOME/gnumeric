## Process this file with automake to produce Makefile.in.

SUBDIRS =
EXTRA_DIST=make-func-list.pl
noinst_DATA=
DISTCLEANFILES=

@YELP_HELP_RULES@

HELP_ID=gnumeric

REALITSTOOL=@ITSTOOL@
ITSTOOLWRAPPER=$(abs_top_srcdir)/tools/itstoolwrapper
ITSTOOL=$(PERL) $(ITSTOOLWRAPPER) "$(REALITSTOOL)"

## Unfortunately, I have to turn off "cs" translations.  I am unable to
## release without.  The cause is an itstool and/or libxml2 bug that causes
## the build process to crash, see
##
##    https://github.com/itstool/itstool/issues/22
##
## You can turn "cs" back on for yourself, but please do not commit such
## a change.
##
## I apologize for having to do this, especially since distributions that
## patch itstool and/or libxml2 probably won't notice and undo this.  The
## net result is likely to be that the cs translation does not reach users.
##
## Morten Welinder, 20210308.  (If that seems like a long time ago and
## if the bug link suggests improvement in the situation, feel free to
## discuss with me on the gnumeric mailing list.)
HELP_LINGUAS=de es sv
HELP_FILES=gnumeric.xml

HELP_EXTRA=                                    \
	gnumeric.xml				\
	about-authors.xml			\
	about-history.xml			\
	about-license.xml			\
	analysis-complexNumbers.xml		\
	analysis-goalseek.xml			\
	analysis-overview.xml			\
	analysis-scenarios.xml			\
	analysis-simulation.xml			\
	analysis-solver.xml			\
	analysis-statistical.xml		\
	appendix-glossary.xml			\
	appendix-keybindings.xml		\
	bugs.xml				\
	compiling.xml				\
	configuration-localization.xml		\
	configuration-overview.xml		\
	configuration-plugins.xml		\
	configuration-preferences.xml		\
	configuration-toolbars.xml		\
	data-commentNlink.xml			\
	data-delete.xml				\
	data-entry-advanced.xml			\
	data-entry-external.xml			\
	data-entry.xml				\
	data-filter.xml				\
	data-format-conditional.xml		\
	data-format.xml				\
	data-generate.xml			\
	data-insert.xml				\
	data-modify.xml				\
	data-move-copy.xml			\
	data-overview.xml			\
	data-selections.xml			\
	data-types.xml				\
	documenting.xml				\
	extending-functions.xml			\
	extending-overview.xml			\
	extending-plugins.xml			\
	extending-python.xml			\
	files-email.xml				\
	files-formats.xml			\
	files-opening.xml			\
	files-overview.xml			\
	files-saving.xml			\
	files-ssconvert.xml			\
	files-textopen.xml			\
	files-textsave.xml			\
	func-footer.xml				\
	func-header.xml				\
	functions.xml				\
	getting-involved.xml			\
	graphics-drawings.xml			\
	graphics-images.xml			\
	graphics-overview.xml			\
	graphics-plots.xml			\
	graphics-widgets.xml			\
	gui-menus.xml				\
	gui-mouse.xml				\
	gui-other-elements.xml			\
	gui-overview.xml			\
	gui-toolbars.xml			\
	installing.xml				\
	legal.xml				\
	manual-usage.xml			\
	morehelp.xml				\
	printing.xml				\
	programing.xml				\
	quick-start.xml				\
	welcome.xml				\
	workbooks.xml				\
	worksheets.xml

HELP_MEDIA= \
	figures/advanced-filter-1.png					\
	figures/advanced-filter-2.png					\
	figures/analysis-simulation-confidence-interval-equation.png	\
	figures/analysis-simulation-demand-ex1.png			\
	figures/analysis-simulation-demand-ex2.png			\
	figures/analysis-simulation-interations-equation1.png		\
	figures/analysis-simulation-interations-equation2.png		\
	figures/analysis-simulation-maximumtime-dialog.png		\
	figures/analysis-simulation-options-dialog.png			\
	figures/analysis-simulation-output-dialog.png			\
	figures/analysis-simulation-profit-ex1.png			\
	figures/analysis-simulation-profit-ex2.png			\
	figures/analysis-simulation-report-screen.png			\
	figures/analysis-simulation-simtable-screen.png			\
	figures/analysis-simulation-summary-dialog.png			\
	figures/analysis-simulation-variables-dialog.png		\
	figures/analysistools-ANOVA1-ex1.png				\
	figures/analysistools-ANOVA1-ex2.png				\
	figures/analysistools-ANOVA2w-ex1.png				\
	figures/analysistools-ANOVA2w-ex2.png				\
	figures/analysistools-ANOVA2wo-ex1.png				\
	figures/analysistools-ANOVA2wo-ex2.png				\
	figures/analysistools-correlation-ex1.png			\
	figures/analysistools-correlation-ex2.png			\
	figures/analysistools-correlation-ex3.png			\
	figures/analysistools-correlation.png				\
	figures/analysistools-covariance-ex1.png			\
	figures/analysistools-covariance-ex2.png			\
	figures/analysistools-covariance.png				\
	figures/analysistools-descstats-ex1-options.png			\
	figures/analysistools-descstats-ex1.png				\
	figures/analysistools-descstats-ex2.png				\
	figures/analysistools-descstats.png				\
	figures/analysistools-exp-smoothing-a-holt-winters-formula.png	\
	figures/analysistools-exp-smoothing-a-holt-winters-stderr.png	\
	figures/analysistools-exp-smoothing-holt-formula.png		\
	figures/analysistools-exp-smoothing-holt-stderr.png		\
	figures/analysistools-exp-smoothing-hunter-formula.png		\
	figures/analysistools-exp-smoothing-hunter-stderr.png		\
	figures/analysistools-exp-smoothing-m-holt-winters-formula.png	\
	figures/analysistools-exp-smoothing-m-holt-winters-stderr.png	\
	figures/analysistools-exp-smoothing-roberts-formula.png		\
	figures/analysistools-exp-smoothing-roberts-stderr.png		\
	figures/analysistools-fourier-formula.png			\
	figures/analysistools-fourier.png				\
	figures/analysistools-frequency-cats.png			\
	figures/analysistools-frequency-graphs.png			\
	figures/analysistools-frequency.png				\
	figures/analysistools-frequency-results.png			\
	figures/analysistools-ftest-ex1.png				\
	figures/analysistools-ftest-ex2.png				\
	figures/analysistools-ftest.png					\
	figures/analysistools-histogram-bins.png			\
	figures/analysistools-histogram-cutoffs.png			\
	figures/analysistools-histogram-ex1.png				\
	figures/analysistools-histogram-ex2.png				\
	figures/analysistools-histogram-ex3.png				\
	figures/analysistools-histogram-ex4.png				\
	figures/analysistools-histogram-ex5.png				\
	figures/analysistools-histogram-graphs.png			\
	figures/analysistools-histogram.png				\
	figures/analysistools-kaplan-ex1.png				\
	figures/analysistools-kaplan-ex2.png				\
	figures/analysistools-kaplan-ex3.png				\
	figures/analysistools-kaplan-groups.png				\
	figures/analysistools-kaplan-options.png			\
	figures/analysistools-kaplan.png				\
	figures/analysistools-moving-average-ex1.png			\
	figures/analysistools-moving-average-ex2.png			\
	figures/analysistools-moving-average-ex3.png			\
	figures/analysistools-moving-average-formula-central.png	\
	figures/analysistools-moving-average-formula-spencer.png	\
	figures/analysistools-moving-average-formula-weighted.png	\
	figures/analysistools-moving-average-options.png		\
	figures/analysistools-moving-average.png			\
	figures/analysistools-normality-ex1.png				\
	figures/analysistools-normality-ex2.png				\
	figures/analysistools-normality-ex3.png				\
	figures/analysistools-normality-ex4.png				\
	figures/analysistools-normality.png				\
	figures/analysistools-normality-testspec.png			\
	figures/analysistools-outputoptions.png				\
	figures/analysistools-pcanalysis-ex1.png			\
	figures/analysistools-pcanalysis-ex2.png			\
	figures/analysistools-pcanalysis.png				\
	figures/analysistools-random-ex1.png				\
	figures/analysistools-random-ex2.png				\
	figures/analysistools-random.png				\
	figures/analysistools-ranges.png				\
	figures/analysistools-rank-ex1.png				\
	figures/analysistools-rank-ex2.png				\
	figures/analysistools-rank.png					\
	figures/analysistools-regression-ex1.png			\
	figures/analysistools-regression-ex2.png			\
	figures/analysistools-regression-ex3.png			\
	figures/analysistools-regression.png				\
	figures/analysistools-sampling-ex1.png				\
	figures/analysistools-sampling-ex2.png				\
	figures/analysistools-sampling.png				\
	figures/analysistools-signtest-ex1.png				\
	figures/analysistools-signtest-ex2.png				\
	figures/analysistools-signtest-ex3.png				\
	figures/analysistools-signtest.png				\
	figures/analysistools-smoothing-ex1.png				\
	figures/analysistools-smoothing-ex2.png				\
	figures/analysistools-smoothing-ex3.png				\
	figures/analysistools-smoothing-ex4.png				\
	figures/analysistools-smoothing-ex5.png				\
	figures/analysistools-smoothing-ex6.png				\
	figures/analysistools-smoothing-ex7.png				\
	figures/analysistools-smoothing-ex8.png				\
	figures/analysistools-smoothing.png				\
	figures/analysistools-tools.png					\
	figures/analysistools-ttest-equal-ex1.png			\
	figures/analysistools-ttest-equal-ex2.png			\
	figures/analysistools-ttest-equal.png				\
	figures/analysistools-ttest-paired-ex1.png			\
	figures/analysistools-ttest-paired-ex2.png			\
	figures/analysistools-ttest-paired.png				\
	figures/analysistools-ttest.png					\
	figures/analysistools-ttest-unequal-ex1.png			\
	figures/analysistools-ttest-unequal-ex2.png			\
	figures/analysistools-ttest-unequal.png				\
	figures/analysistools-ztest-ex1.png				\
	figures/analysistools-ztest-ex2.png				\
	figures/analysistools-ztest.png					\
	figures/arrowhead-dimensions.png				\
	figures/button-align-center.png					\
	figures/button-align-left.png					\
	figures/button-align-right.png					\
	figures/button-arrow.png					\
	figures/button-bold.png						\
	figures/button-borders.png					\
	figures/button-button.png					\
	figures/button-center-across-selection.png			\
	figures/button-checkbox.png					\
	figures/button-combo.png					\
	figures/button-copy.png						\
	figures/button-cut.png						\
	figures/button-decrease-indent.png				\
	figures/button-decrease-precision.png				\
	figures/button-equals.png					\
	figures/button-fill.png						\
	figures/button-font-size.png					\
	figures/button-font-type.png					\
	figures/button-frame.png					\
	figures/button-function.png					\
	figures/button-graphEditor-add.png				\
	figures/button-graph.png					\
	figures/button-increase-indent.png				\
	figures/button-increase-precision.png				\
	figures/button-insert-component.png				\
	figures/button-insert-hyperlink.png				\
	figures/button-insert-object.png				\
	figures/button-italic.png					\
	figures/button-label.png					\
	figures/button-line.png						\
	figures/button-list.png						\
	figures/button-merge.png					\
	figures/button-money.png					\
	figures/button-new.png						\
	figures/button-open.png						\
	figures/button-oval.png						\
	figures/button-paste.png					\
	figures/button-percent.png					\
	figures/button-preview.png					\
	figures/button-print.png					\
	figures/button-radio.png					\
	figures/button-rectangle.png					\
	figures/button-redo-and-history.png				\
	figures/button-save.png						\
	figures/button-scrollbar.png					\
	figures/button-slider.png					\
	figures/button-sort-az.png					\
	figures/button-sort-za.png					\
	figures/button-spin.png						\
	figures/button-split.png					\
	figures/button-subscript.png					\
	figures/button-sum.png						\
	figures/button-superscript.png					\
	figures/button-text-colour.png					\
	figures/button-thousands.png					\
	figures/button-underline.png					\
	figures/button-undo-and-history.png				\
	figures/button-zoom.png						\
	figures/cell-grid.png						\
	figures/cells-1.png						\
	figures/cells-2.png						\
	figures/cell-selected.png					\
	figures/cell-with-text.png					\
	figures/chart_area_1_1.png					\
	figures/chart_area_1_2.png					\
	figures/chart_area_1_3.png					\
	figures/chart_bar_1_1.png					\
	figures/chart_bar_1_2.png					\
	figures/chart_bar_1_3.png					\
	figures/chart_bubble_1_1.png					\
	figures/chart_column_1_1.png					\
	figures/chart_column_1_2.png					\
	figures/chart_column_1_3.png					\
	figures/chart_line_1_1.png					\
	figures/chart_line_1_2.png					\
	figures/chart_line_1_3.png					\
	figures/chart_line_2_1.png					\
	figures/chart_line_2_2.png					\
	figures/chart_line_2_3.png					\
	figures/chart_pie_1_1.png					\
	figures/chart_pie_2_1.png					\
	figures/chart_radar_1_1.png					\
	figures/chart_radar_1_2.png					\
	figures/chart_radar_1_3.png					\
	figures/chart_ring_1_1.png					\
	figures/chart_ring_1_2.png					\
	figures/chart_scatter_1_1.png					\
	figures/chart_scatter_3_1.png					\
	figures/chart_scatter_3_2.png					\
	figures/chart_surface_1.png					\
	figures/chart_surface_2.png					\
	figures/dialog-auto-correct.png					\
	figures/dialog-autosave.png					\
	figures/dialog-fileopen-withTags.png				\
	figures/dialog-filesave-compact-withTags.png			\
	figures/dialog-filesave-expanded-withTags.png			\
	figures/dialog-filter.png					\
	figures/dialog-insert-object.png				\
	figures/dialog-properties-arrow.png				\
	figures/dialog-properties-checkbox.png				\
	figures/dialog-properties-frame.png				\
	figures/dialog-properties-label.png				\
	figures/dialog-properties-line.png				\
	figures/dialog-properties-oval.png				\
	figures/dialog-properties-rectangle.png				\
	figures/dialog-properties-scrollbar.png				\
	figures/drawing-arrow.png					\
	figures/drawing-line.png					\
	figures/drawing-oval.png					\
	figures/drawing-rectangle.png					\
	figures/example-colGraph-modified.png				\
	figures/example-colGraph.png					\
	figures/example-columnSelect.png				\
	figures/example-data.png					\
	figures/files-html32-example.png				\
	figures/files-html40-example.png				\
	figures/files-html-example.png					\
	figures/formula-dmedian.png					\
	figures/gnumeric-empty.png					\
	figures/gnumeric-icon-24.png					\
	figures/gnumeric-labelled.png					\
	figures/gnumeric-power-example.png				\
	figures/graph-axes-grid.png					\
	figures/graph-components.png					\
	figures/graph-example-area.png					\
	figures/graph-example-bar.png					\
	figures/graph-example-bubble.png				\
	figures/graph-example-column.png				\
	figures/graph-example-line.png					\
	figures/graph-example-pie.png					\
	figures/graph-example-radar.png					\
	figures/graph-example-ring.png					\
	figures/graph-example-surface-t1.png				\
	figures/graph-example-surface-t2.png				\
	figures/graph-example-xyplot.png				\
	figures/graphguru-axes-category-bounds.png			\
	figures/graphguru-axes-category-details.png			\
	figures/graphguru-axes-category-style.png			\
	figures/graphguru-axes-continuous-bounds.png			\
	figures/graphguru-axes-continuous-format.png			\
	figures/graphguru-backPanels-gradient.png			\
	figures/graphguru-backPanels-image.png				\
	figures/graphguru-backPanels-none.png				\
	figures/graphguru-backPanels-pattern.png			\
	figures/graphguru-components.png				\
	figures/graphguru-plot-barCol.png				\
	figures/graphguru-plot-bubble.png				\
	figures/graphguru-plot-pie.png					\
	figures/graphguru-plot-radar.png				\
	figures/graphguru-plot-ring.png					\
	figures/graphguru-series-data-bubble.png			\
	figures/graphguru-series-data-single.png			\
	figures/graphguru-series-data-xy.png				\
	figures/graphguru-series-error.png				\
	figures/graphguru-series-style-filled.png			\
	figures/graphguru-series-style-line.png				\
	figures/graphguru-title-font.png				\
	figures/graph-hierarchy.png					\
	figures/graphical-elements.png					\
	figures/graphical-elements-selected.png				\
	figures/graphical-elements-stacked.png				\
	figures/graphs-types-area.png					\
	figures/graphs-types-bar.png					\
	figures/graphs-types-bubble.png					\
	figures/graphs-types-column.png					\
	figures/graphs-types-line.png					\
	figures/graphs-types-pie.png					\
	figures/graphs-types-radar.png					\
	figures/graphs-types-ring.png					\
	figures/graphs-types-stock.png					\
	figures/graphs-types-surface.png				\
	figures/graphs-types-xyplot.png					\
	figures/icon-locked.png						\
	figures/icon-unlocked.png					\
	figures/icon-visible.png					\
	figures/info-area.png						\
	figures/menubar.png						\
	figures/menu-context-col-row-header.png				\
	figures/menu-context-graph-order.png				\
	figures/menu-context-graph.png					\
	figures/menu-context-grid.png					\
	figures/menu-context-object-order.png				\
	figures/menu-context-object.png					\
	figures/menu-context-sheet-tabs.png				\
	figures/menu-context-tabs.png					\
	figures/menu-context-toolbars.png				\
	figures/menu-data-export.png					\
	figures/menu-data-import.png					\
	figures/menu-data-labelled.png					\
	figures/menu-edit-delete-cells.png				\
	figures/menu-edit-labelled.png					\
	figures/menu-edit-modify-names.png				\
	figures/menu-edit-paste-special.png				\
	figures/menu-edit-select.png					\
	figures/menu-file-labelled.png					\
	figures/menu-file-printarea.png					\
	figures/menu-format-column.png					\
	figures/menu-format-labelled.png				\
	figures/menu-format-sheet.png					\
	figures/menu-help-labelled.png					\
	figures/menu-insert-comment.png					\
	figures/menu-insert-fw.png					\
	figures/menu-insert-hyperlink.png				\
	figures/menu-insert-labelled.png				\
	figures/menu-insert-names.png					\
	figures/menu-statistics-labelled.png				\
	figures/menu-tools-labelled.png					\
	figures/menu-torn-off.png					\
	figures/menu-view-labelled.png					\
	figures/menu-view-properties-dialog-cm.png			\
	figures/menu-view-properties-dialog.png				\
	figures/number-format-border-dialog.png				\
	figures/number-format-color-dialog.png				\
	figures/number-format-dialog.png				\
	figures/number-format-font-dialog.png				\
	figures/number-format-justification-dialog-2.png		\
	figures/number-format-protection.png				\
	figures/number-format-validation.png				\
	figures/number-format-validation-warning.png			\
	figures/pointer_arrow_left_std.png				\
	figures/pointer_cross_hair.png					\
	figures/pointer_cross_wide.png					\
	figures/pointer_diagonal_resize.png				\
	figures/pointer_double_horizontal_arrow.png			\
	figures/pointer_double_vertical_arrow.png			\
	figures/pointer_four_way_arrow.png				\
	figures/pointer_hand_left.png					\
	figures/pointer_left.png					\
	figures/pointer_resize_multiple.png				\
	figures/pointer_right.png					\
	figures/pointer_text_edit.png					\
	figures/pointer_zoom_in.png					\
	figures/pointer_zoom_out.png					\
	figures/preferences-copypaste.png				\
	figures/preferences-files.png					\
	figures/preferences-font-header.png				\
	figures/preferences-font.png					\
	figures/preferences-screen.png					\
	figures/preferences-sorting.png					\
	figures/preferences-tools.png					\
	figures/preferences-undo.png					\
	figures/preferences-windows.png					\
	figures/printing-preview.png					\
	figures/printing-setup-header-config.png			\
	figures/printing-setup-header.png				\
	figures/printing-setup-page-paper-type.png			\
	figures/printing-setup-page.png					\
	figures/printing-setup-printarea.png				\
	figures/printing-setup-scale.png				\
	figures/printing-setup-sheet.png				\
	figures/print-large.png						\
	figures/print-preview-back.png					\
	figures/print-preview-first.png					\
	figures/print-preview-fitonetoone.png				\
	figures/print-preview-fit.png					\
	figures/print-preview-last.png					\
	figures/print-preview-next.png					\
	figures/print-preview-zoomin.png				\
	figures/print-preview-zoomout.png				\
	figures/print-worksheet-file-general.png			\
	figures/print-worksheet-job.png					\
	figures/print-worksheet-page_setup.png				\
	figures/print-worksheet-paper.png				\
	figures/print-worksheet-printer-advanced.png			\
	figures/print-worksheet-printer-finishing.png			\
	figures/print-worksheet-printer-general.png			\
	figures/print-worksheet-printer-image_quality.png		\
	figures/print-worksheet-printer-job.png				\
	figures/print-worksheet-printer.png				\
	figures/print-worksheet-print_range.png				\
	figures/selection-10.png					\
	figures/selection-1.png						\
	figures/selection-2.png						\
	figures/selection-3.png						\
	figures/selection-4.png						\
	figures/selection-5.png						\
	figures/selection-8.png						\
	figures/selection-9.png						\
	figures/solver-01.png						\
	figures/solver-02.png						\
	figures/solver-03.png						\
	figures/solver-04.png						\
	figures/solver-05.png						\
	figures/textguru-export-panel1-withTags.png			\
	figures/textguru-export-panel2-withTags.png			\
	figures/textguru-import-panel1-withTags.png			\
	figures/textguru-import-panel2a-withTags.png			\
	figures/textguru-import-panel2b-withTags.png			\
	figures/textguru-import-panel3-withTags.png			\
	figures/toolbar-extension-menu.png				\
	figures/toolbar-format-long.png					\
	figures/toolbar-format.png					\
	figures/toolbar-object.png					\
	figures/toolbars.png						\
	figures/toolbar-standard.png					\
	figures/widget-checkbox.png					\
	figures/widget-combobox.png					\
	figures/widget-entryBox-outlined.png				\
	figures/widget-frame.png					\
	figures/widget-label.png					\
	figures/widget-list.png						\
	figures/widget-scrollbar.png					\
	figures/widget-slider.png					\
	figures/widget-spinbutton.png					\
	figures/worksheet-cols-1.png					\
	figures/worksheet-data-1.png					\
	figures/worksheet-data-2.png					\
	figures/worksheet-data-3.png					\
	figures/worksheet-data-4.png					\
	figures/worksheet-managing-dialog.png				\
	figures/worksheet-rows-1.png					\
	figures/worksheet-running-calc-1.png

install-exec-hook:
	for d in C $(HELP_LINGUAS); do \
		helpdir="$(DESTDIR)$(HELP_DIR)/$$d/$(HELP_ID)"; \
		install -d $$helpdir; \
		$(RM) $$helpdir/index.docbook; \
		$(LN_S) gnumeric.xml $$helpdir/index.docbook; \
	done

uninstall-hook:
	for d in C $(HELP_LINGUAS); do \
		helpdir="$(DESTDIR)$(HELP_DIR)/$$d/$(HELP_ID)"; \
		$(RM) $$helpdir/index.docbook; \
	done

functions_xml_parts = C/func.defs C/func-header.xml C/func-footer.xml

C/functions.xml: $(srcdir)/make-func-list.pl $(functions_xml_parts)
	(cat $(srcdir)/C/func-header.xml ;				\
	 $(PERL) $(srcdir)/make-func-list.pl C/func.defs ;		\
	 cat $(srcdir)/C/func-footer.xml				\
	) >functions.tmp ;						\
	if xmllint -noent --nonet --format --encode "UTF-8" functions.tmp >functions.out ; then	\
	    mv functions.out $@; rm functions.tmp;					\
	fi

C/func.defs: # $(top_builddir)/src/gnumeric$(EXEEXT)
	LC_ALL="C" ; export LC_ALL ; $(top_builddir)/src/sstest --dump-func-defs="$@"

EXTRA_DIST+=C/func.defs
DISTCLEANFILES+=C/func.defs

EXTRA_DIST+=textdefs.tex
if ENABLE_PDFDOCS
noinst_DATA += gnumeric.pdf
EXTRA_DIST += gnumeric.pdf
endif

if ENABLE_PDF_VIA_DBCONTEXT
gnumeric.pdf:
	(cd C && env TEXINPUTS=$(srcdir):.: dbcontext -t tex -Pfo.setup=1 -I . \
		-P imagedata.default.scale='scale=600' \
		-o ../gnumeric.tex $(srcdir)/gnumeric.xml)
	sed -i -e 's/\xE2\xA8\x89/\\ensuremath{\\times}/' gnumeric.tex
	sed -i -e 's/\\begin{verbatim}\([^_]*\)\\end{verbatim}/\\texttt{\1}/g' gnumeric.tex
	sed -i -e 's/\\texttt{\\emph{\\small{\(.*\)}}}:/\\texttt{\\emph{\1}}:/g' gnumeric.tex
	sed -i -e 's/ *>{} */\\ensuremath{{}>{}}/g' gnumeric.tex
	sed -i -e 's/ *<{} */\\ensuremath{{}<{}}/g' gnumeric.tex
	sed -i -e 's/\\makeglossary/\\makeglossary\n\\setlength{\\headheight}{36pt}/g' gnumeric.tex
	env TEXMFCNF=$(srcdir): \
		TEXINPUTS=$(srcdir):/usr/share/texmf/tex/context/dbcontext/style: \
		texexec --pdf --mode=A4 --verbose gnumeric.tex
endif

if ENABLE_PDF_VIA_DBLATEX
gnumeric.pdf:
	(cd C && dblatex -t tex -Pfo.setup=1 -I . \
		-P imagedata.default.scale='scale=0.6' \
		-P latex.encoding=utf8 \
		-P latex.output.revhistory=0 \
		-o ../gnumeric.tex $(srcdir)/gnumeric.xml)
	sed -i -e 's/T2A,T2D,T1/T2A,T2D,LGR,T1/' gnumeric.tex
	sed -i -e 's/utf8x/utf8/' gnumeric.tex
	sed -i -f utf8.sed gnumeric.tex
	sed -i -e 's/\\begin{verbatim}\([^_]*\)\\end{verbatim}/\\texttt{\1}/g' gnumeric.tex
	sed -i -e 's/\texttt{\emph{\small{\(.*\)}}}:/\texttt{\emph{\1}}:/g' gnumeric.tex
	sed -i -e 's/ *>{} */\\ensuremath{{}>{}}/g' gnumeric.tex
	sed -i -e 's/ *<{} */\\ensuremath{{}<{}}/g' gnumeric.tex
	sed -i -e 's/\\makeglossary/\\makeglossary\n\\setlength{\\headheight}{36pt}\n\\input{textdefs.tex}/g' gnumeric.tex
	for runs in $$(seq 1 4); do \
		env TEXINPUTS=$(srcdir): \
			pdflatex -interaction nonstopmode gnumeric.tex ; \
	done
endif

manpagedir   = $(mandir)/man1
manpage_DATA = gnumeric.1 ssconvert.1 ssindex.1 ssgrep.1 ssdiff.1
EXTRA_DIST += $(manpage_DATA) utf8.sed

winhelp_DATA =
winhelpdir=$(gnumeric_datadir)/$(subdir)
if WITH_WIN32
winhelp_DATA += gnumeric.chm gnumeric.hhmap

gnumeric.hhmap: chm-stamp
	-(cd C/chm && wine 'C:\Program Files\HTML Help Workshop\hhc.exe' gnumeric.hhp)
	cp C/chm/gnumeric.hhmap .

gnumeric.chm: gnumeric.hhmap
	cp C/chm/gnumeric.chm .

endif

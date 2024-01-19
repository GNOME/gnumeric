# Gnumeric's Makefile.am
# Author: Miguel de Icaza (miguel@kernel.org)

AUTOMAKE_OPTIONS = subdir-objects

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = \
	--add-include-path="${INTROSPECTION_GIRDIR}" \
	-v --warn-all --accept-unprefixed \
	--identifier-prefix="Gnm"  --identifier-prefix="Gnumeric" \
	-I$(top_builddir) \
	-DGOFFICE_NAMESPACE_DISABLE
INTROSPECTION_COMPILER_ARGS = \
	--includedir=$(srcdir) \
	--includedir=. \
	--includedir="${INTROSPECTION_GIRDIR}"

if WITH_DECIMAL64
  # Very wrong and any actual use will likely crash
  INTROSPECTION_SCANNER_ARGS += -D_Decimal64=double
endif

libspreadsheet_includedir = $(includedir)/libspreadsheet-@GNUMERIC_API_VER@/spreadsheet

EXTRA_DIST =

lib_LTLIBRARIES = libspreadsheet.la

bin_PROGRAMS =
check_PROGRAMS =
noinst_PROGRAMS = sstest

if WITH_GTK
  bin_PROGRAMS += gnumeric
endif

bin_PROGRAMS += ssconvert ssindex ssgrep ssdiff

AM_CPPFLAGS = 								\
	-I$(top_srcdir)							\
	-I$(srcdir)							\
	$(GNUMERIC_CFLAGS)

# A hint is needed to build these headers first:
BUILT_SOURCES =					\
	gnm-marshalers.c			\
	gnm-marshalers.h			\
	gnmresources.c				\
	gnmresources.h				\
	gnumeric-paths.h

# These also need to occur in gnm.gresource.xml
resource_ui_files =				\
	dialogs/advanced-filter.ui		\
	dialogs/anova-one.ui			\
	dialogs/anova-two.ui			\
	dialogs/autofilter-expression.ui	\
	dialogs/autofilter-top10.ui		\
	dialogs/autoformat.ui			\
	dialogs/autosave.ui			\
	dialogs/cell-comment.ui			\
	dialogs/cell-format-cond.ui		\
	dialogs/cell-format.ui			\
	dialogs/cell-sort.ui			\
	dialogs/chi-squared.ui			\
	dialogs/col-width.ui			\
	dialogs/colrow.ui			\
	dialogs/consolidate.ui			\
	dialogs/correlation.ui			\
	dialogs/covariance.ui			\
	dialogs/dao.ui				\
	dialogs/data-slicer.ui			\
	dialogs/data-table.ui			\
	dialogs/define-name.ui			\
	dialogs/delete-cells.ui			\
	dialogs/descriptive-stats.ui		\
	dialogs/dialog-stf-export.ui		\
	dialogs/dialog-stf.ui			\
	dialogs/dialog-zoom.ui			\
	dialogs/doc-meta-data.ui		\
	dialogs/exp-smoothing.ui		\
	dialogs/fill-series.ui			\
	dialogs/formula-guru.ui			\
	dialogs/fourier-analysis.ui		\
	dialogs/frequency.ui			\
	dialogs/function-select.ui		\
	dialogs/goalseek.ui			\
	dialogs/goto.ui				\
	dialogs/hf-config.ui			\
	dialogs/hf-dt-format.ui			\
	dialogs/histogram.ui			\
	dialogs/hyperlink.ui			\
	dialogs/insert-cells.ui			\
	dialogs/kaplan-meier.ui			\
	dialogs/mean-tests.ui			\
	dialogs/merge.ui			\
	dialogs/moving-averages.ui		\
	dialogs/normality-tests.ui		\
	dialogs/one-mean-test.ui		\
	dialogs/paste-special.ui		\
	dialogs/plugin-manager.ui		\
	dialogs/preferences.ui			\
	dialogs/principal-components.ui		\
	dialogs/print.ui			\
	dialogs/quit.ui				\
	dialogs/random-generation-cor.ui	\
	dialogs/random-generation.ui		\
	dialogs/rank.ui				\
	dialogs/recent.ui			\
	dialogs/regression.ui			\
	dialogs/row-height.ui			\
	dialogs/sampling.ui			\
	dialogs/scenario-add.ui			\
	dialogs/scenario-manager.ui		\
	dialogs/search-replace.ui		\
	dialogs/search.ui			\
	dialogs/sheet-compare.ui		\
	dialogs/sheet-order.ui			\
	dialogs/sheet-rename.ui			\
	dialogs/sheet-resize.ui			\
	dialogs/sheetobject-size.ui		\
	dialogs/shuffle.ui			\
	dialogs/sign-test-two.ui		\
	dialogs/sign-test.ui			\
	dialogs/simulation.ui			\
	dialogs/so-button.ui			\
	dialogs/so-checkbox.ui			\
	dialogs/so-frame.ui			\
	dialogs/so-list.ui			\
	dialogs/so-radiobutton.ui		\
	dialogs/so-scrollbar.ui			\
	dialogs/solver.ui			\
	dialogs/tabulate.ui			\
	dialogs/variance-tests.ui		\
	dialogs/view.ui				\
	dialogs/wbcg.ui				\
	dialogs/wilcoxon-mann-whitney.ui	\
	dialogs/workbook-attr.ui

dialogs_sources = 						\
	dialogs/dao-gui-utils.c					\
	dialogs/dialog-about.c					\
	dialogs/dialog-advanced-filter.c			\
	dialogs/dialog-analysis-tools.c				\
	dialogs/dialog-analysis-tool-chi-squared.c		\
	dialogs/dialog-analysis-tool-frequency.c		\
	dialogs/dialog-analysis-tool-kaplan-meier.c		\
	dialogs/dialog-analysis-tool-normality.c		\
	dialogs/dialog-analysis-tool-one-mean.c			\
	dialogs/dialog-analysis-tool-principal-components.c	\
	dialogs/dialog-analysis-tool-sign-test.c		\
	dialogs/dialog-analysis-tool-wilcoxon-mann-whitney.c	\
	dialogs/dialog-autofilter.c				\
	dialogs/dialog-autoformat.c				\
	dialogs/dialog-autosave.c				\
	dialogs/dialog-cell-comment.c				\
	dialogs/dialog-cell-format.c				\
	dialogs/dialog-cell-format-cond.c			\
	dialogs/dialog-cell-sort.c				\
	dialogs/dialog-col-width.c				\
	dialogs/dialog-col-row.c				\
	dialogs/dialog-consolidate.c				\
	dialogs/dialog-data-slicer.c				\
	dialogs/dialog-data-table.c				\
	dialogs/dialog-fill-series.c				\
	dialogs/dialog-function-select.c			\
	dialogs/dialog-formula-guru.c				\
	dialogs/dialog-goal-seek.c				\
	dialogs/dialog-goto-cell.c				\
	dialogs/dialog-hyperlink.c				\
	dialogs/dialog-insert-cells.c				\
	dialogs/dialog-define-names.c				\
	dialogs/dialog-delete-cells.c				\
	dialogs/dialog-merge.c					\
	dialogs/dialog-password.c				\
	dialogs/dialog-paste-special.c				\
	dialogs/dialog-plugin-manager.c				\
	dialogs/dialog-preferences.c				\
	dialogs/dialog-random-generator.c			\
	dialogs/dialog-random-generator-cor.c			\
	dialogs/dialog-recent.c					\
	dialogs/dialog-row-height.c				\
	dialogs/dialog-quit.c					\
	dialogs/dialog-scenarios.c				\
	dialogs/dialog-search.c					\
	dialogs/dialog-search-replace.c				\
	dialogs/dialog-sheet-compare.c				\
	dialogs/dialog-sheet-order.c				\
	dialogs/dialog-sheet-rename.c				\
	dialogs/dialog-sheet-resize.c				\
	dialogs/dialog-sheetobject-size.c			\
	dialogs/dialog-shuffle.c				\
	dialogs/dialog-simulation.c				\
	dialogs/dialog-so-list.c				\
	dialogs/dialog-so-styled.c				\
	dialogs/dialog-solver.c					\
	dialogs/dialog-stf.c					\
	dialogs/dialog-stf-main-page.c				\
	dialogs/dialog-stf-csv-page.c				\
	dialogs/dialog-stf-fixed-page.c				\
	dialogs/dialog-stf-format-page.c			\
	dialogs/dialog-stf-preview.c				\
	dialogs/dialog-stf-export.c				\
	dialogs/dialog-tabulate.c				\
	dialogs/dialog-doc-metadata.c				\
	dialogs/dialog-view.c					\
	dialogs/dialog-workbook-attr.c				\
	dialogs/dialog-zoom.c					\
	dialogs/dialog-printer-setup.c

# Not installed
dialogs_headers = \
	dialogs/help.h						\
	dialogs/dao-gui-utils.h					\
	dialogs/dialog-stf.h					\
	dialogs/dialog-stf-preview.h				\
	dialogs/dialog-stf-export.h				\
	dialogs/dialogs.h					\
	dialogs/tool-dialogs.h

widgets_SOURCES = \
	widgets/gnm-dao.c				\
	widgets/gnm-fontbutton.c			\
	widgets/gnm-format-sel.c			\
	widgets/gnm-notebook.c				\
	widgets/gnm-radiobutton.c			\
	widgets/gnm-cell-combo-view.c			\
	widgets/gnm-filter-combo-view.c			\
	widgets/gnm-validation-combo-view.c		\
	widgets/gnm-sheet-sel.c				\
	widgets/gnm-sheet-slicer-combo-view.c		\
	widgets/gnm-so-anchor-mode-chooser.c		\
	widgets/gnm-workbook-sel.c			\
	widgets/gnm-cell-renderer-text.c		\
	widgets/gnm-cell-renderer-expr-entry.c	\
	widgets/gnm-cell-renderer-toggle.c		\
	widgets/gnm-dashed-canvas-line.c		\
	widgets/gnm-expr-entry.c			\
	widgets/gnm-text-view.c

widgetsdir = $(libspreadsheet_includedir)/widgets
widgets_HEADERS = \
	widgets/gnm-dao.h				\
	widgets/gnm-fontbutton.h			\
	widgets/gnm-format-sel.h			\
	widgets/gnm-notebook.h				\
	widgets/gnm-radiobutton.h			\
	widgets/gnm-cell-combo-view.h			\
	widgets/gnm-cell-combo-view-impl.h		\
	widgets/gnm-filter-combo-view.h			\
	widgets/gnm-validation-combo-view.h		\
	widgets/gnm-sheet-sel.h				\
	widgets/gnm-sheet-slicer-combo-view.h		\
	widgets/gnm-so-anchor-mode-chooser.h		\
	widgets/gnm-workbook-sel.h			\
	widgets/gnm-cell-renderer-text.h		\
	widgets/gnm-cell-renderer-expr-entry.h	\
	widgets/gnm-cell-renderer-toggle.h		\
	widgets/gnm-dashed-canvas-line.h		\
	widgets/gnm-expr-entry.h			\
	widgets/gnm-text-view.h

tools_SOURCES =	\
	tools/analysis-anova.c			\
	tools/analysis-auto-expression.c	\
	tools/analysis-chi-squared.c		\
	tools/analysis-exp-smoothing.c		\
	tools/analysis-frequency.c		\
	tools/analysis-histogram.c		\
	tools/analysis-kaplan-meier.c		\
	tools/analysis-normality.c		\
	tools/analysis-one-mean-test.c		\
	tools/analysis-principal-components.c	\
	tools/analysis-sign-test.c		\
	tools/analysis-signed-rank-test.c	\
	tools/analysis-tools.c			\
	tools/analysis-wilcoxon-mann-whitney.c	\
	tools/auto-correct.c			\
	tools/dao.c				\
	tools/data-shuffling.c			\
	tools/fill-series.c			\
	tools/filter.c				\
	tools/gnm-solver.c			\
	tools/goal-seek.c			\
	tools/random-generator-cor.c		\
	tools/random-generator.c		\
	tools/scenarios.c			\
	tools/simulation.c			\
	tools/tabulate.c

toolsdir = $(libspreadsheet_includedir)/tools
tools_HEADERS = \
	tools/analysis-anova.h			\
	tools/analysis-auto-expression.h	\
	tools/analysis-chi-squared.h		\
	tools/analysis-exp-smoothing.h		\
	tools/analysis-frequency.h		\
	tools/analysis-histogram.h		\
	tools/analysis-kaplan-meier.h		\
	tools/analysis-normality.h		\
	tools/analysis-one-mean-test.h		\
	tools/analysis-principal-components.h	\
	tools/analysis-sign-test.h		\
	tools/analysis-signed-rank-test.h	\
	tools/analysis-tools.h			\
	tools/analysis-wilcoxon-mann-whitney.h	\
	tools/auto-correct.h			\
	tools/dao.h				\
	tools/data-shuffling.h			\
	tools/fill-series.h			\
	tools/filter.h				\
	tools/gnm-solver.h			\
	tools/goal-seek.h			\
	tools/random-generator-cor.h		\
	tools/random-generator.h		\
	tools/scenarios.h			\
	tools/simulation.h			\
	tools/tabulate.h			\
	tools/tools.h

noinst_HEADERS = dead-kittens.h compilation.h gnm-i18n.h \
	$(dialogs_headers)

# Another hint, see bugs #172211 and #172212:
non-intermediate: gnm-marshalers.c gnmresources.c

resource_files = $(shell $(GLIB_COMPILE_RESOURCES) --sourcedir=$(top_srcdir) --generate-dependencies $(srcdir)/gnm.gresource.xml)

gnmresources.h: gnm.gresource.xml
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(top_srcdir) --c-name _gnm --generate-header --manual-register
gnmresources.c: gnm.gresource.xml $(resource_files) Makefile
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) $< \
		--target=$@ --sourcedir=$(top_srcdir) --c-name _gnm --generate-source --manual-register


CLEANFILES = $(BUILT_SOURCES)			\
	gnumeric.exe.a

libspreadsheet_la_CPPFLAGS = $(AM_CPPFLAGS) -DGNUMERIC_INTERNAL
libspreadsheet_la_LDFLAGS = -release $(VERSION)
libspreadsheet_la_LIBADD = 			\
	$(GNUMERIC_LIBS)			\
	$(INTLLIBS)

libspreadsheet_la_SOURCES =		\
	go-val.h			\
	go-val.c			\
	\
	goffice-data.h 			\
	go-data-cache-source.c 		\
	go-data-cache-source.h 		\
	go-data-cache-impl.h 		\
	go-data-cache.h 		\
	go-data-cache.c 		\
	go-data-cache-field-impl.h 	\
	go-data-cache-field.h 		\
	go-data-cache-field.c 		\
	go-data-slicer-impl.h 		\
	go-data-slicer.h 		\
	go-data-slicer.c 		\
	go-data-slicer-field-impl.h 	\
	go-data-slicer-field.h 		\
	go-data-slicer-field.c 		\
	\
	gnm-data-cache-source.h 	\
	gnm-data-cache-source.c 	\
	gnm-sheet-slicer.h 		\
	gnm-sheet-slicer.c 		\
	gnm-sheet-slicer-combo.h	\
	gnm-sheet-slicer-combo.c	\
	\
	gnm-cell-combo.h		\
	gnm-cell-combo.c		\
	\
	gnm-marshalers.c			\
	application.c				\
	auto-format.c				\
	cell-draw.c				\
	cell.c					\
	cellspan.c				\
	clipboard.c				\
	cmd-edit.c				\
	collect.c				\
	colrow.c				\
	command-context-stderr.c		\
	command-context.c			\
	commands.c				\
	complete-sheet.c			\
	complete.c				\
	complex.c				\
	consolidate.c				\
	criteria.c				\
	dependent.c				\
	expr-deriv.c				\
	expr-name.c				\
	expr.c					\
	file-autoft.c				\
	format-template.c			\
	func-builtin.c				\
	func.c					\
	gnm-commands-slicer.c			\
	gnm-datetime.c				\
	gnm-format.c				\
	gnm-graph-window.c			\
	gnm-pane-impl.h				\
	gnm-pane.c				\
	gnm-plugin.c				\
	gnm-random.c				\
	gnm-so-filled.c				\
	gnm-so-line.c				\
	gnm-so-path.c				\
	gnm-so-polygon.c			\
	gnumeric-conf.c				\
	gnumeric-simple-canvas.c		\
	graph.c					\
	gui-clipboard.c				\
	gui-file.c				\
	gui-util.c				\
	gutils.c				\
	history.c				\
	hlink.c					\
	input-msg.c				\
	item-bar.c				\
	item-cursor.c				\
	item-edit.c				\
	item-grid.c				\
	libgnumeric.c				\
	mathfunc.c				\
	mstyle.c				\
	number-match.c				\
	outoflinedocs.c				\
	parse-util.c				\
	parser.y				\
	pattern.c				\
	position.c				\
	preview-grid-impl.h			\
	preview-grid.c				\
	print-cell.c				\
	print-info.c				\
	print.c					\
	rangefunc-strings.c			\
	rangefunc.c				\
	ranges.c				\
	rendered-value.c			\
	search.c				\
	selection.c				\
	session.c				\
	sf-bessel.c				\
	sf-dpq.c				\
	sf-gamma.c				\
	sf-trig.c				\
	sheet-autofill.c			\
	sheet-conditions.c			\
	sheet-control-gui.c			\
	sheet-control.c		  		\
	sheet-diff.c		  		\
	sheet-filter.c				\
	sheet-merge.c				\
	sheet-object-cell-comment.c		\
	sheet-object-component.c		\
	sheet-object-graph.c			\
	sheet-object-image.c			\
	sheet-object-widget.c			\
	sheet-object.c				\
	sheet-style.c				\
	sheet-utils.c				\
	sheet-view.c		  		\
	sheet.c					\
	sort.c					\
	stf-export.c				\
	stf-parse.c				\
	stf.c					\
	style-border.c				\
	style-color.c				\
	style-conditions.c			\
	style.c					\
	undo.c					\
	validation.c				\
	value-sheet.c				\
	value.c					\
	wbc-gtk-actions.c			\
	wbc-gtk-edit.c				\
	wbc-gtk.c				\
	workbook-cmd-format.c			\
	workbook-control.c 			\
	workbook-view.c				\
	workbook.c				\
	xml-sax-read.c				\
	xml-sax-write.c				\
	$(dialogs_sources)			\
	$(tools_SOURCES)

if WITH_GTK
  libspreadsheet_la_SOURCES += $(widgets_SOURCES)
else
  EXTRA_DIST += $(widgets_SOURCES)
endif

nodist_libspreadsheet_la_SOURCES = gnmresources.c gnmresources.h

# When we switch to subversion fix this to nest as
# libspreadsheet-ver/spreadsheet/header
# doing it now would require massive cvs surgery.
libspreadsheet_include_HEADERS =		\
	application.h				\
	auto-format.h				\
	cell-draw.h				\
	cell.h					\
	cellspan.h				\
	clipboard.h				\
	cmd-edit.h				\
	collect.h				\
	colrow.h				\
	command-context-stderr.h		\
	command-context.h			\
	commands.h				\
	complete-sheet.h			\
	complete.h				\
	complex.h				\
	consolidate.h				\
	criteria.h				\
	dependent.h				\
	expr-deriv.h				\
	expr-impl.h				\
	expr-name.h				\
	expr.h					\
	file-autoft.h				\
	format-template.h			\
	func-builtin.h				\
	func.h					\
	gnm-command-impl.h			\
	gnm-commands-slicer.h			\
	gnm-data-cache-source.h			\
	gnm-datetime.h				\
	gnm-format.h				\
	gnm-graph-window.h			\
	gnm-pane.h				\
	gnm-plugin.h				\
	gnm-random.h				\
	gnm-sheet-slicer.h			\
	gnm-so-filled.h				\
	gnm-so-line.h				\
	gnm-so-path.h				\
	gnm-so-polygon.h			\
	gnumeric-conf.h				\
	gnumeric-fwd.h				\
	gnumeric-simple-canvas.h		\
	gnumeric.h				\
	go-data-cache-field.h			\
	go-data-cache-source.h			\
	go-data-cache.h				\
	go-data-slicer-field.h			\
	go-data-slicer.h			\
	go-val.h				\
	graph.h					\
	gui-clipboard.h				\
	gui-file.h				\
	gui-util.h				\
	gutils.h				\
	history.h				\
	hlink-impl.h				\
	hlink.h					\
	input-msg.h				\
	item-bar.h				\
	item-cursor.h				\
	item-edit.h				\
	item-grid.h				\
	libgnumeric.h				\
	mathfunc.h				\
	mstyle.h				\
	number-match.h				\
	numbers.h				\
	parse-util.h				\
	pattern.h				\
	position.h				\
	preview-grid.h				\
	print-cell.h				\
	print-info.h				\
	print.h					\
	rangefunc-strings.h			\
	rangefunc.h				\
	ranges.h				\
	regression.h				\
	rendered-value.h			\
	search.h				\
	selection.h				\
	session.h				\
	sf-bessel.h				\
	sf-dpq.h				\
	sf-gamma.h				\
	sf-trig.h				\
	sheet-autofill.h			\
	sheet-conditions.h			\
	sheet-control-gui-priv.h		\
	sheet-control-gui.h			\
	sheet-control-priv.h			\
	sheet-control.h				\
	sheet-diff.h				\
	sheet-filter-combo.h			\
	sheet-filter.h				\
	sheet-merge.h				\
	sheet-object-cell-comment.h		\
	sheet-object-component.h		\
	sheet-object-graph.h			\
	sheet-object-image.h			\
	sheet-object-impl.h			\
	sheet-object-widget-impl.h		\
	sheet-object-widget.h			\
	sheet-object.h				\
	sheet-private.h				\
	sheet-style.h				\
	sheet-utils.h				\
	sheet-view.h				\
	sheet.h					\
	sort.h					\
	stf-export.h				\
	stf-parse.h				\
	stf.h					\
	style-border.h				\
	style-color.h				\
	style-conditions.h			\
	style-font.h				\
	style.h					\
	undo.h					\
	validation-combo.h			\
	validation.h				\
	value.h					\
	wbc-gtk-impl.h				\
	wbc-gtk.h				\
	workbook-cmd-format.h			\
	workbook-control-priv.h			\
	workbook-control.h			\
	workbook-priv.h				\
	workbook-view.h				\
	workbook.h				\
	xml-io-version.h			\
	xml-sax.h

ALL_HDRS = \
	$(libspreadsheet_include_HEADERS)	\
	$(tools_HEADERS)			\
	$(dialogs_headers)			\
	$(widgets_HEADERS)

gnumeric_SOURCES =				\
	io-context-gtk.c			\
	io-context-gtk.h			\
	main-application.c

gnumeric_LDADD =				\
	libspreadsheet.la			\
	$(GNUMERIC_LIBS)			\
	$(INTLLIBS)
gnumeric_LDFLAGS = -export-dynamic

ssconvert_LDADD   = $(gnumeric_LDADD)
ssconvert_LDFLAGS = -export-dynamic
ssconvert_SOURCES =				\
	ssconvert.c
ssindex_LDADD   = $(gnumeric_LDADD)
ssindex_LDFLAGS = -export-dynamic
ssindex_SOURCES =				\
	ssindex.c
ssgrep_LDADD   = $(gnumeric_LDADD)
ssgrep_LDFLAGS = -export-dynamic
ssgrep_SOURCES =				\
	ssgrep.c
sstest_LDADD   = $(gnumeric_LDADD)
sstest_LDFLAGS = -export-dynamic
sstest_SOURCES =				\
	sstest.c
ssdiff_LDADD   = $(gnumeric_LDADD)
ssdiff_LDFLAGS = -export-dynamic
ssdiff_SOURCES =				\
	ssdiff.c

if WITH_NATIVE_WIN32
    gnumeric_LDADD += gnumeric_rc.o
    gnumeric_LDFLAGS	+= -mwindows -Wl,--stack,0x4000000
    ssconvert_LDFLAGS	+= -mconsole -Wl,--stack,0x4000000
    ssindex_LDFLAGS	+= -mconsole
    ssgrep_LDFLAGS	+= -mconsole

    libspreadsheet_la_DEPENDENCIES = libspreadsheet.def
    libspreadsheet_la_LDFLAGS += -no-undefined -export-symbols libspreadsheet.def
endif

if WITH_WIN32
libspreadsheet.def: local.def
	echo EXPORTS > $@ && \
	cat $^ | sort >> $@

local.def: stamp-local.def
	@true

stamp-local.def: $(libspreadsheet_include_HEADERS) Makefile $(top_srcdir)/tools/dumpdef.pl
	hdrs='$(libspreadsheet_include_HEADERS) dialogs/dialogs.h dialogs/tool-dialogs.h tools/dao.h tools/goal-seek.h tools/analysis-tools.h tools/gnm-solver.h widgets/gnm-expr-entry.h'; \
	hdrs_list=''; \
	for hdr in $$hdrs; do \
	  if test -f $(srcdir)/$$hdr; then \
	    hdrs_list="$$hdrs_list $(srcdir)/$$hdr"; \
	  else \
	    hdrs_list="$$hdrs_list $$hdr"; \
	  fi; \
	done; \
	cat $(top_builddir)/gnumeric-config.h $$hdrs_list | \
		sed -e 's/^#[ \t]*include[ \t]\+.*$$//g' | \
		$(CPP) $(AM_CPPFLAGS) "-DGNM_VAR_DECL=__declspec(dllexport)" -P - > xgen-localdef.1 && \
	perl $(top_srcdir)/tools/dumpdef.pl \
		xgen-localdef.1 > xgen-localdef.2 \
	&& (cmp -s xgen-localdef.2 local.def || \
		cp xgen-localdef.2 local.def) \
	&& rm -f xgen-localdef.1 xgen-localdef.2 \
	&& echo timestamp > $@

endif


# Rules for the marshalers code:
GENMARSHAL_COMMAND = $(GLIB_GENMARSHAL) --prefix=gnm_

.list.h:
	$(GENMARSHAL_COMMAND) --header $< >$@

.list.c:
	(echo '/* This file has been automatically generated.  Do not edit. */' && \
	echo '#include <$*.h>' && \
	$(GENMARSHAL_COMMAND) --body $< ) >$@

# Depends on this Makefile, because it uses make variables.
gnumeric-paths.h: Makefile
	@echo 'creating $@'
	@( \
	echo '/* This file has been automatically generated.  Do not edit. */'; \
	echo ''; \
	echo '#ifndef GNUMERIC_PATHS_H'; \
	echo '#define GNUMERIC_PATHS_H'; \
	echo ''; \
	echo '#define GNUMERIC_PREFIX		"$(prefix)"'; \
	echo '#define GNUMERIC_SYSCONFDIR	"$(sysconfdir)"'; \
	echo '#define GNUMERIC_DATADIR		"$(gnumeric_datadir)"'; \
	echo '#define GNUMERIC_LIBDIR		"$(gnumeric_libdir)"'; \
	echo '#define GNUMERIC_LOCALEDIR	"$(gnumeric_localedir)"'; \
	echo '#define GNUMERIC_EXTERNPLUGINDIR		"$(gnumeric_externplugindir)"'; \
	echo ''; \
	echo '#endif /* GNUMERIC_PATHS_H */'; \
	) >$@

update-gnumeric-conf:
	make -C ${top_srcdir}/schemas
	rm -f gnumeric-conf.c.tmp
	@PERL@ -pe 'if (/AUTOMATICALLY GENERATED CODE BELOW/) { print; print "\n"; last; }' <gnumeric-conf.c >gnumeric-conf.c.tmp
	@PERL@ ${top_srcdir}/tools/handle-conf-options --cfile ${top_srcdir}/schemas/org.gnome.gnumeric*.gschema.xml.in >>gnumeric-conf.c.tmp
	if cmp gnumeric-conf.c gnumeric-conf.c.tmp; then \
	  rm gnumeric-conf.c.tmp; \
	else \
	  mv gnumeric-conf.c.tmp gnumeric-conf.c; \
	fi
	rm -f gnumeric-conf.h.tmp
	@PERL@ -pe 'if (/AUTOMATICALLY GENERATED CODE BELOW/) { print; print "\n"; last; }' <gnumeric-conf.h >gnumeric-conf.h.tmp
	@PERL@ ${top_srcdir}/tools/handle-conf-options --hfile ${top_srcdir}/schemas/org.gnome.gnumeric*.gschema.xml.in >>gnumeric-conf.h.tmp
	echo '' >>gnumeric-conf.h.tmp
	@PERL@ -ne '$$p=1 if (/AUTOMATICALLY GENERATED CODE ABOVE/); print if $$p;' <gnumeric-conf.h >>gnumeric-conf.h.tmp
	if cmp gnumeric-conf.h gnumeric-conf.h.tmp; then \
	  rm gnumeric-conf.h.tmp; \
	else \
	  mv gnumeric-conf.h.tmp gnumeric-conf.h; \
	fi

if WITH_NATIVE_WIN32
gnumeric_rc.o: gnumeric.rc ../gnumeric-config.h
	$(WINDRES) -DVERSION=$(VERSION) -DVERSION_NUMBER=`echo $(VERSION).0.0.0 | sed 's/\([0-9]*\)[^.]*\.*\([0-9]*\)[^.]*\.*\([0-9]*\)[^.]*\.*\([0-9]*\).*/\1,\2,\3,\4/'` --include-dir $(srcdir) -i $< -o $@
install-exec-hook:
else
install-exec-hook:
	cd $(DESTDIR)$(bindir) && \
	mv -f gnumeric$(EXEEXT) gnumeric-$(VERSION)$(EXEEXT) && \
	$(LN_S) gnumeric-$(VERSION)$(EXEEXT) gnumeric$(EXEEXT)
endif

uninstall-hook:
	-cd $(DESTDIR)$(bindir) && \
	rm -f gnumeric-$(VERSION)$(EXEEXT)

# include conditional files to ensure everything gets distributed no matter
# what the build
EXTRA_DIST +=						\
	gnm.gresource.xml				\
	pixmaps/line_pattern_dash_dot.png		\
	pixmaps/line_pattern_dash_dot_dot.png		\
	pixmaps/line_pattern_dashed.png			\
	pixmaps/line_pattern_dotted.png			\
	pixmaps/line_pattern_double.png			\
	pixmaps/line_pattern_hair.png			\
	pixmaps/line_pattern_medium.png			\
	pixmaps/line_pattern_medium_dash.png		\
	pixmaps/line_pattern_medium_dash_dot.png	\
	pixmaps/line_pattern_medium_dash_dot_dot.png	\
	pixmaps/line_pattern_slant.png			\
	pixmaps/line_pattern_thick.png			\
	pixmaps/line_pattern_thin.png			\
	pixmaps/right-down.png				\
	pixmaps/down-right.png				\
	pixmaps/gnumeric_splash_1.4.png			\
	pixmaps/unknown_image.png			\
	pixmaps/sheet_move_marker.png			\
	gnumeric.css					\
	gnm-marshalers.list				\
	README.gnm_float				\
	gnumeric.rc					\
	GNOME_Gnumeric-gtk.xml				\
	$(resource_ui_files)

if HAVE_INTROSPECTION

INTROSPECTION_GIRS += Gnm-@GNUMERIC_API_VER@.gir

Gnm-@GNUMERIC_API_VER@.gir: $(INTROSPECTION_SCANNER) libspreadsheet.la Makefile
Gnm_@GNUMERIC_API_VER_@_gir_INCLUDES = GObject-2.0 Gio-2.0 libxml2-2.0 \
									   Gsf-1 GOffice-@GOFFICE_API_VER@
Gnm_@GNUMERIC_API_VER_@_gir_CFLAGS = $(AM_CPPFLAGS)
Gnm_@GNUMERIC_API_VER_@_gir_LIBS = libspreadsheet.la
Gnm_@GNUMERIC_API_VER_@_gir_FILES = \
	$(libspreadsheet_la_SOURCES) \
	$(ALL_HDRS)


girdir = $(GIRDIR)
gir_DATA = $(INTROSPECTION_GIRS)

typelibsdir = $(TYPELIBDIR)
typelibs_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelibs_DATA)

endif # HAVE_INTROSPECTION

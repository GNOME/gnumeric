#
# Gnumeric's Makefile.am
# Author: Miguel de Icaza (miguel@kernel.org)
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

SUBDIRS = tools cut-n-paste-code

lib_LTLIBRARIES = libspreadsheet.la

bin_PROGRAMS =
check_PROGRAMS =
noinst_PROGRAMS = sstest

noinst_HEADERS = dead-kittens.h compilation.h embedded-imgs.h gnm-i18n.h

if WITH_GTK
  SUBDIRS += dialogs widgets
  bin_PROGRAMS += gnumeric
  check_PROGRAMS += test-pango
endif

bin_PROGRAMS += ssconvert ssindex ssgrep ssdiff

AM_CPPFLAGS = 								\
	-I$(top_srcdir)							\
	-I$(srcdir)							\
	-I$(srcdir)/widgets						\
	-I$(srcdir)/dialogs						\
	-I$(srcdir)/tools						\
	$(GNUMERIC_CFLAGS)

# A hint is needed to build these headers first:
BUILT_SOURCES =					\
	gnm-marshalers.c			\
	gnm-marshalers.h			\
	embedded-imgs.c				\
	gnumeric-paths.h

# Another hint, see bugs #172211 and #172212:
non-intermediate: gnm-marshalers.c embedded-imgs.c

CLEANFILES = $(BUILT_SOURCES)			\
	gnumeric.exe.a

libspreadsheet_la_CPPFLAGS = $(AM_CPPFLAGS) -DGNUMERIC_INTERNAL
libspreadsheet_la_LDFLAGS = -release $(VERSION)
libspreadsheet_la_LIBADD = 			\
	$(gnumeric_app_libs)			\
	$(GNUMERIC_LIBS)			\
	$(INTLLIBS)

libspreadsheet_la_SOURCES =			\
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
	cell.c					\
	cell-draw.c				\
	cellspan.c				\
	clipboard.c				\
	cmd-edit.c				\
	collect.c				\
	colrow.c				\
	command-context.c			\
	command-context-stderr.c		\
	commands.c				\
	complete.c				\
	complete-sheet.c			\
	complex.c				\
	consolidate.c				\
	dependent.c				\
	expr.c					\
	expr-name.c				\
	file-autoft.c				\
	format-template.c			\
	func.c					\
	func-builtin.c				\
	gnm-commands-slicer.c			\
	gnm-datetime.c				\
	gnm-format.c				\
	gnm-graph-window.c			\
	gnm-pane.c				\
	gnm-pane-impl.h				\
	gnm-random.c				\
	gnumeric-simple-canvas.c		\
	graph.c					\
	gutils.c				\
	gui-clipboard.c				\
	gui-file.c				\
	gui-util.c				\
	hlink.c					\
	history.c				\
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
	parser.y				\
	parse-util.c				\
	pattern.c				\
	position.c				\
	preview-grid.c				\
	preview-grid-impl.h			\
	print-info.c				\
	rangefunc.c				\
	rangefunc-strings.c			\
	ranges.c				\
	rendered-value.c			\
	search.c				\
	selection.c				\
	session.c				\
	sf-bessel.c				\
	sf-dpq.c				\
	sf-gamma.c				\
	sf-trig.c				\
	sheet.c					\
	sheet-view.c		  		\
	sheet-control.c		  		\
	sheet-control-gui.c			\
	sheet-merge.c				\
	sheet-autofill.c			\
	sheet-filter.c				\
	sheet-utils.c				\
	sheet-object.c				\
	sheet-object-cell-comment.c		\
	gnm-so-filled.c				\
	gnm-so-line.c				\
	gnm-so-path.c				\
	gnm-so-polygon.c			\
	sheet-object-component.c		\
	sheet-object-graph.c			\
	sheet-object-image.c			\
	sheet-object-widget.c			\
	sheet-style.c				\
	gnm-plugin.c				\
	sort.c					\
	stf.c					\
	stf-export.c				\
	stf-parse.c				\
	style.c					\
	style-border.c				\
	style-color.c				\
	style-conditions.c			\
	undo.c					\
	validation.c				\
	value.c					\
	value-sheet.c				\
	workbook.c				\
	workbook-cmd-format.c			\
	workbook-view.c				\
	workbook-control.c 			\
	wbc-gtk.c				\
	wbc-gtk-actions.c			\
	wbc-gtk-edit.c				\
	xml-sax-read.c				\
	xml-sax-write.c				\
	gnumeric-conf.c				\
	print.c					\
	print-cell.c

nodist_libspreadsheet_la_SOURCES = embedded-imgs.c

# When we switch to subversion fix this to nest as
# libspreadsheet-ver/spreadsheet/header
# doing it now would require massive cvs surgery.
libspreadsheet_includedir = $(includedir)/libspreadsheet-@GNUMERIC_API_VER@/spreadsheet
libspreadsheet_include_HEADERS = 	\
	application.h				\
	auto-format.h				\
	cell.h					\
	cell-draw.h				\
	cellspan.h				\
	clipboard.h				\
	cmd-edit.h				\
	collect.h				\
	colrow.h				\
	command-context.h			\
	command-context-stderr.h		\
	commands.h				\
	complete.h				\
	complete-sheet.h			\
	complex.h				\
	consolidate.h				\
	dependent.h				\
	expr.h					\
	expr-impl.h				\
	expr-name.h				\
	file-autoft.h				\
	format-template.h			\
	func.h					\
	func-builtin.h				\
	gnm-command-impl.h			\
	gnm-commands-slicer.h			\
	gnm-data-cache-source.h			\
	gnm-datetime.h				\
	gnm-format.h				\
	gnm-graph-window.h			\
	gnm-pane.h				\
	gnm-random.h				\
	gnm-sheet-slicer.h			\
	gnm-style-impl.h			\
	gnumeric.h				\
	gnumeric-fwd.h				\
	gnumeric-simple-canvas.h		\
	go-data-cache-source.h 			\
	go-data-cache.h 			\
	go-data-cache-field.h 			\
	go-data-slicer.h			\
	go-data-slicer-field.h			\
	go-val.h				\
	graph.h					\
	gutils.h				\
	gui-clipboard.h				\
	gui-file.h				\
	gui-util.h				\
	hlink.h					\
	hlink-impl.h				\
	history.h				\
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
	print-info.h				\
	rangefunc.h				\
	rangefunc-strings.h			\
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
	sheet.h					\
	sheet-view.h    			\
	sheet-control.h    			\
	sheet-control-priv.h 			\
	sheet-control-gui.h			\
	sheet-control-gui-priv.h		\
	sheet-merge.h				\
	sheet-private.h				\
	sheet-autofill.h			\
	sheet-filter.h				\
	sheet-utils.h				\
	sheet-filter-combo.h			\
	sheet-object.h				\
	sheet-object-impl.h			\
	sheet-object-cell-comment.h		\
	sheet-object-widget-impl.h		\
	gnm-so-filled.h				\
	gnm-so-line.h				\
	gnm-so-path.h				\
	gnm-so-polygon.h			\
	sheet-object-component.h		\
	sheet-object-graph.h			\
	sheet-object-image.h			\
	sheet-object-widget.h			\
	sheet-style.h				\
	gnm-plugin.h				\
	sort.h					\
	stf.h					\
	stf-export.h				\
	stf-parse.h				\
	style.h					\
	style-border.h				\
	style-color.h				\
	style-conditions.h			\
	style-font.h				\
	undo.h					\
	validation.h				\
	validation-combo.h			\
	value.h					\
	workbook.h				\
	workbook-priv.h				\
	workbook-cmd-format.h			\
	workbook-view.h				\
	workbook-control.h 			\
	workbook-control-priv.h 		\
	wbc-gtk.h				\
	wbc-gtk-impl.h				\
	xml-io-version.h			\
	xml-sax.h				\
	gnumeric-conf.h				\
	print.h					\
	print-cell.h

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

test_pango_LDADD   = $(gnumeric_LDADD)
test_pango_LDFLAGS = -export-dynamic
test_pango_SOURCES =				\
	test-pango.c

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
	hdrs='$(libspreadsheet_include_HEADERS) dialogs/dialogs.h dialogs/tool-dialogs.h tools/dao.h tools/goal-seek.h tools/analysis-tools.h tools/gnm-solver.h widgets/gnumeric-expr-entry.h'; \
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

gnumeric_app_libs =				\
	dialogs/libdialogs.la			\
	tools/libtools.la			\
	widgets/libwidgets.la

embedded_imgs =						\
	gnumeric.css					\
	pixmaps/add_decimals.png			\
	pixmaps/auto-sum.xpm				\
	pixmaps/autofilter_16.png			\
	pixmaps/autofilter_24.png			\
	pixmaps/autofilter_delete_16.png		\
	pixmaps/autofilter_delete_24.png		\
	pixmaps/border_all.xpm				\
	pixmaps/border_bottom.xpm			\
	pixmaps/border_double_bottom.xpm		\
	pixmaps/border_left.xpm				\
	pixmaps/border_none.xpm				\
	pixmaps/border_outside.xpm			\
	pixmaps/border_right.xpm			\
	pixmaps/border_thick_bottom.xpm			\
	pixmaps/border_thick_outside.xpm		\
	pixmaps/border_top_n_bottom.xpm			\
	pixmaps/border_top_n_double_bottom.xpm		\
	pixmaps/border_top_n_thick_bottom.xpm		\
	pixmaps/bottom_border.xpm			\
	pixmaps/brush-16.png				\
	pixmaps/brush-22.png				\
	pixmaps/bucket.xpm				\
	pixmaps/center_across_selection_16.xpm		\
	pixmaps/center_across_selection_24.xpm		\
	pixmaps/column_add_16.xpm			\
	pixmaps/column_add_24.xpm			\
	pixmaps/column_delete_16.xpm			\
	pixmaps/column_delete_24.xpm			\
	pixmaps/column_hide_16.xpm			\
	pixmaps/column_hide_24.xpm			\
	pixmaps/column_size_16.xpm			\
	pixmaps/column_size_24.xpm			\
	pixmaps/column_unhide_16.xpm			\
	pixmaps/column_unhide_24.xpm			\
	pixmaps/comment_add_16.xpm			\
	pixmaps/comment_delete_16.xpm			\
	pixmaps/comment_edit_16.xpm			\
	pixmaps/cursor_cross.xpm			\
	pixmaps/diag_border.xpm				\
	pixmaps/down-right.png				\
	pixmaps/equal-sign.xpm				\
	pixmaps/expr_entry.png				\
	pixmaps/font.xpm				\
	pixmaps/format_money_24.png			\
	pixmaps/format_percent_24.png			\
	pixmaps/formula_guru_16.png			\
	pixmaps/formula_guru_24.png			\
	pixmaps/gnm_subscript_16.png			\
	pixmaps/gnm_subscript_24.png			\
	pixmaps/gnm_superscript_16.png			\
	pixmaps/gnm_superscript_24.png			\
	pixmaps/gnumeric_splash_1.4.png			\
	pixmaps/gp_125grey.xpm				\
	pixmaps/gp_25grey.xpm				\
	pixmaps/gp_50grey.xpm				\
	pixmaps/gp_625grey.xpm				\
	pixmaps/gp_75grey.xpm				\
	pixmaps/gp_bricks.xpm				\
	pixmaps/gp_diag.xpm				\
	pixmaps/gp_diag_cross.xpm			\
	pixmaps/gp_foreground_solid.xpm			\
	pixmaps/gp_horiz.xpm				\
	pixmaps/gp_large_circles.xpm			\
	pixmaps/gp_rev_diag.xpm				\
	pixmaps/gp_semi_circle.xpm			\
	pixmaps/gp_small_circle.xpm			\
	pixmaps/gp_solid.xpm				\
	pixmaps/gp_thatch.xpm				\
	pixmaps/gp_thick_diag_cross.xpm			\
	pixmaps/gp_thin_diag.xpm			\
	pixmaps/gp_thin_diag_cross.xpm			\
	pixmaps/gp_thin_horiz.xpm			\
	pixmaps/gp_thin_horiz_cross.xpm			\
	pixmaps/gp_thin_rev_diag.xpm			\
	pixmaps/gp_thin_vert.xpm			\
	pixmaps/gp_vert.xpm				\
	pixmaps/graph_guru_16.xpm			\
	pixmaps/graph_guru_24.xpm			\
	pixmaps/group_16.xpm				\
	pixmaps/group_24.xpm				\
	pixmaps/halign-fill_24.png			\
	pixmaps/halign-general_24.png			\
	pixmaps/hf_cell.png				\
	pixmaps/hf_date.png				\
	pixmaps/hf_page.png				\
	pixmaps/hf_pages.png				\
	pixmaps/hf_sheet.png				\
	pixmaps/hf_time.png				\
	pixmaps/hide_detail_16.xpm			\
	pixmaps/hide_detail_24.xpm			\
	pixmaps/insert_component_16.xpm			\
	pixmaps/insert_component_24.xpm			\
	pixmaps/insert_image_16.png			\
	pixmaps/insert_image_24.png			\
	pixmaps/insert_shaped_component_16.xpm		\
	pixmaps/insert_shaped_component_24.xpm		\
	pixmaps/inside_border.xpm			\
	pixmaps/inside_horiz_border.xpm			\
	pixmaps/inside_vert_border.xpm			\
	pixmaps/left_border.xpm				\
	pixmaps/line_pattern_dash_dot.xpm		\
	pixmaps/line_pattern_dash_dot_dot.xpm		\
	pixmaps/line_pattern_dashed.xpm			\
	pixmaps/line_pattern_dotted.xpm			\
	pixmaps/line_pattern_double.xpm			\
	pixmaps/line_pattern_hair.xpm			\
	pixmaps/line_pattern_medium.xpm			\
	pixmaps/line_pattern_medium_dash.xpm		\
	pixmaps/line_pattern_medium_dash_dot.xpm	\
	pixmaps/line_pattern_medium_dash_dot_dot.xpm	\
	pixmaps/line_pattern_slant.xpm			\
	pixmaps/line_pattern_thick.xpm			\
	pixmaps/line_pattern_thin.xpm			\
	pixmaps/link_add_16.png				\
	pixmaps/link_add_24.png				\
	pixmaps/link_delete_16.png			\
	pixmaps/link_edit_16.png			\
	pixmaps/link_email_16.png			\
	pixmaps/link_email_24.png			\
	pixmaps/link_external_16.png			\
	pixmaps/link_external_24.png			\
	pixmaps/link_internal_16.png			\
	pixmaps/link_internal_24.png			\
	pixmaps/link_url_16.png				\
	pixmaps/link_url_24.png				\
	pixmaps/merge_cells_16.xpm			\
	pixmaps/merge_cells_24.xpm			\
	pixmaps/no_border.xpm				\
	pixmaps/object_arrow_24.png			\
	pixmaps/object_button_24.png			\
	pixmaps/object_checkbox_24.png			\
	pixmaps/object_combo_24.png			\
	pixmaps/object_ellipse_24.png			\
	pixmaps/object_frame_24.png			\
	pixmaps/object_label_24.png			\
	pixmaps/object_line_24.png			\
	pixmaps/object_list_24.png			\
	pixmaps/object_radiobutton_24.png		\
	pixmaps/object_rectangle_24.png			\
	pixmaps/object_scrollbar_24.png			\
	pixmaps/object_slider_24.png			\
	pixmaps/object_spinbutton_24.png		\
	pixmaps/outline_border.xpm			\
	pixmaps/pivottable_16.png			\
	pixmaps/pivottable_24.png			\
	pixmaps/protection_no_24.png			\
	pixmaps/protection_yes_24.png			\
	pixmaps/protection_yes_48.png			\
	pixmaps/remove_decimals.png			\
	pixmaps/rev_diag_border.xpm			\
	pixmaps/right-down.png				\
	pixmaps/right_border.xpm			\
	pixmaps/row_add_16.xpm				\
	pixmaps/row_add_24.xpm				\
	pixmaps/row_delete_16.xpm			\
	pixmaps/row_delete_24.xpm			\
	pixmaps/row_hide_16.xpm				\
	pixmaps/row_hide_24.xpm				\
	pixmaps/row_size_16.xpm				\
	pixmaps/row_size_24.xpm				\
	pixmaps/row_unhide_16.xpm			\
	pixmaps/row_unhide_24.xpm			\
	pixmaps/sheet_move_marker.xpm			\
	pixmaps/show_detail_16.xpm			\
	pixmaps/show_detail_24.xpm			\
	pixmaps/split_cells_16.xpm			\
	pixmaps/split_cells_24.xpm			\
	pixmaps/thousands.xpm				\
	pixmaps/top_border.xpm				\
	pixmaps/ungroup_16.xpm				\
	pixmaps/ungroup_24.xpm				\
	pixmaps/unknown_image.png			\
	pixmaps/visible.png

embedded-imgs.c: $(top_srcdir)/tools/embedder $(embedded_imgs) Makefile
	cd $(srcdir) && @PERL@ $(abs_top_srcdir)/tools/embedder \
		--register-function=gnm_register_imgs_files \
		--register-file-function=go_rsm_register_file \
		--id-prefix=gnm: \
		--include '<goffice/goffice.h>' \
		--include embedded-imgs.h \
		NOCOMPRESS $(embedded_imgs) >$(abs_builddir)/$@

# Rules for the marshalers code:
GENMARSHAL_COMMAND = $(GLIB_GENMARSHAL) --prefix=gnm_

.list.h:
	$(GENMARSHAL_COMMAND) --header $< >$@

.list.c:
	(echo '/* This file has been automatically generated.  Do not edit. */' && \
	echo '#include "$*.h"' && \
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
	echo '#define GNUMERIC_ICONDIR		"$(gnumeric_icondir)"'; \
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
EXTRA_DIST = \
	$(embedded_imgs)			\
	gnm-marshalers.list			\
	README.gnm_float			\
	gnumeric.rc				\
	GNOME_Gnumeric-gtk.xml.in

gtkactiondir=$(gnumeric_datadir)

gtkaction_DATA = GNOME_Gnumeric-gtk.xml
GNOME_Gnumeric-gtk.xml: GNOME_Gnumeric-gtk.xml.in
	sed -e 's/ _name *=/ name=/' <$<  >$@
DISTCLEANFILES = GNOME_Gnumeric-gtk.xml

if HAVE_INTROSPECTION

INTROSPECTION_GIRS += Gnm-@GNUMERIC_API_VER@.gir

Gnm-@GNUMERIC_API_VER@.gir: $(INTROSPECTION_SCANNER) libspreadsheet.la Makefile
Gnm_@GNUMERIC_API_VER_@_gir_INCLUDES = GObject-2.0 Gio-2.0 libxml2-2.0 \
									   Gsf-1 GOffice-@GOFFICE_API_VER@
Gnm_@GNUMERIC_API_VER_@_gir_CFLAGS = $(AM_CPPFLAGS)
Gnm_@GNUMERIC_API_VER_@_gir_LIBS = libspreadsheet.la
Gnm_@GNUMERIC_API_VER_@_gir_FILES = \
	$(libspreadsheet_la_SOURCES) \
	$(libspreadsheet_include_HEADERS) \
	tools/dao.c \
	tools/dao.h \
	tools/scenarios.c \
	tools/scenarios.h \
	tools/tools.h \
	widgets/gnumeric-expr-entry.c \
	widgets/gnumeric-expr-entry.h


girdir = $(GIRDIR)
gir_DATA = $(INTROSPECTION_GIRS)

typelibsdir = $(TYPELIBDIR)
typelibs_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelibs_DATA)

endif # HAVE_INTROSPECTION

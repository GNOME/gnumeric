#
# Gnumeric's Makefile.
# Author: Miguel de Icaza (miguel@kernel.org)

SUBDIRS = tools cut-n-paste-code

if WITH_GTK
  SUBDIRS += pixmaps dialogs widgets
  bin_PROGRAMS = gnumeric ssconvert
  check_PROGRAMS = # test-pango number-match
else
  bin_PROGRAMS = ssconvert
endif

INCLUDES = 								\
	-I$(top_srcdir)							\
	-I$(srcdir)							\
	-I$(srcdir)/widgets						\
	-I$(srcdir)/dialogs						\
	-I$(srcdir)/tools						\
	-I$(top_srcdir)/src/cut-n-paste-code/foocanvas			\
	$(GNUMERIC_CFLAGS)

CLEANFILES = gnumeric-paths.h

BUILT_SOURCES =					\
	gnm-marshalers.h			\
	gnm-marshalers.c			\
	gnumeric-paths.h

GNUMERIC_BASE =					\
	$(BUILT_SOURCES)			\
	application.c				\
	application.h				\
	auto-format.c				\
	auto-format.h				\
	cell.h					\
	cell.c					\
	cell-draw.c				\
	cell-draw.h				\
	cellspan.c				\
	cellspan.h				\
	clipboard.c				\
	clipboard.h				\
	cmd-edit.h				\
	cmd-edit.c				\
	collect.h				\
	collect.c				\
	colrow.c				\
	colrow.h				\
	commands.h				\
	commands.c				\
	complete.c				\
	complete.h				\
	complete-sheet.c			\
	complete-sheet.h			\
	command-context.c			\
	command-context.h			\
	command-context-priv.h			\
	command-context-stderr.c		\
	command-context-stderr.h		\
	consolidate.c				\
	consolidate.h				\
	io-context.c				\
	io-context.h				\
	io-context-priv.h			\
	complex.h				\
	complex.c				\
	dates.c					\
	dates.h					\
	datetime.c				\
	datetime.h				\
	dependent.h				\
	dependent.c				\
	error-info.c				\
	error-info.h				\
	expr.c					\
	expr.h					\
	expr-impl.h				\
	expr-name.c				\
	expr-name.h				\
	file.c					\
	file.h					\
	file-priv.h				\
	file-autoft.c				\
	file-autoft.h				\
	format-template.h			\
	format-template.c			\
	formats.c				\
	func.c					\
	func.h					\
	func-builtin.c				\
	func-builtin.h				\
	format.c				\
	format.h				\
	global-gnome-font.c			\
	global-gnome-font.h			\
	gnumeric-simple-canvas.c		\
	gnumeric-simple-canvas.h		\
	gnumeric-canvas.c			\
	gnumeric-canvas.h			\
	gnumeric-pane.c				\
	gnumeric-pane.h				\
	gnumeric.h				\
	graph.c					\
	graph.h					\
	gui-clipboard.c				\
	gui-clipboard.h				\
	gui-file.c				\
	gui-file.h				\
	gui-util.c				\
	gui-util.h				\
	gui-gnumeric.h				\
	gutils.c				\
	gutils.h				\
	hlink.c					\
	hlink.h					\
	hlink-impl.h				\
	history.c				\
	history.h				\
	input-msg.c				\
	input-msg.h				\
	item-acetate.c				\
	item-acetate.h				\
	item-bar.c				\
	item-bar.h				\
	item-cursor.c				\
	item-cursor.h				\
	item-debug.c				\
	item-debug.h				\
	item-edit.c				\
	item-edit.h				\
	item-grid.c				\
	item-grid.h				\
	libgnumeric.h				\
	libgnumeric.c				\
	mathfunc.c				\
	mathfunc.h				\
	module-plugin-defs.h			\
	mstyle.c				\
	mstyle.h				\
	number-match.c				\
	number-match.h				\
	numbers.h				\
	parser.y				\
	parse-util.c				\
	parse-util.h				\
	pattern.h				\
	pattern.c				\
	pivottable.h				\
	pivottable.c				\
	plugin-loader-module.c			\
	plugin-loader-module.h			\
	plugin-loader.c				\
	plugin-loader.h				\
	plugin-service.c			\
	plugin-service.h			\
	plugin-service-impl.h			\
	plugin-util.c				\
	plugin-util.h				\
	plugin.c				\
	plugin.h				\
	position.c				\
	position.h				\
	preview-grid.c				\
	preview-grid.h				\
	preview-grid-impl.h			\
	print.c					\
	print.h					\
	print-cell.c				\
	print-cell.h				\
	print-info.c				\
	print-info.h				\
	rangefunc.c				\
	rangefunc.h				\
	rangefunc-strings.c			\
	rangefunc-strings.h			\
	ranges.c				\
	ranges.h				\
	regression.c				\
	regression.h				\
	regutf8.c				\
	regutf8.h				\
	rendered-value.c			\
	rendered-value.h			\
	search.h				\
	search.c				\
	selection.c				\
	selection.h				\
	session.c				\
	session.h				\
	sheet.c					\
	sheet.h					\
	sheet-view.c		  		\
	sheet-view.h    			\
	sheet-control.c		  		\
	sheet-control.h    			\
	sheet-control-priv.h 			\
	sheet-control-gui.c			\
	sheet-control-gui.h			\
	sheet-control-gui-priv.h		\
	sheet-merge.c				\
	sheet-merge.h				\
	sheet-private.h				\
	sheet-autofill.c			\
	sheet-autofill.h			\
	sheet-filter.h				\
	sheet-filter.c				\
	sheet-object.c				\
	sheet-object.h				\
	sheet-object-impl.h			\
	sheet-object-cell-comment.c		\
	sheet-object-cell-comment.h		\
	sheet-object-graphic.c			\
	sheet-object-graphic.h			\
	sheet-object-graph.c			\
	sheet-object-graph.h			\
	sheet-object-image.c			\
	sheet-object-image.h			\
	sheet-object-widget.c			\
	sheet-object-widget.h			\
	sheet-style.c				\
	sheet-style.h				\
	solver.h				\
	sort.c					\
	sort.h					\
	str.c					\
	str.h					\
	stf.c					\
	stf.h					\
	stf-export.c				\
	stf-export.h				\
	stf-parse.c				\
	stf-parse.h				\
	style.c					\
	style.h					\
	style-border.c				\
	style-border.h				\
	style-color.c				\
	style-color.h				\
	summary.c				\
	summary.h				\
	symbol.h				\
	symbol.c				\
	validation.c				\
	validation.h				\
	value.c					\
	value.h					\
	value-sheet.c				\
	workbook.c				\
	workbook.h				\
	workbook-priv.h				\
	workbook-cmd-format.c			\
	workbook-cmd-format.h			\
	workbook-edit.c				\
	workbook-edit.h				\
	workbook-view.c				\
	workbook-view.h				\
	workbook-control.c 			\
	workbook-control.h 			\
	workbook-control-priv.h 		\
	workbook-control-gui.c 			\
	workbook-control-gui.h 			\
	workbook-control-gui-priv.h 		\
	wbc-gtk.c				\
	wbc-gtk.h				\
	wbcg-actions.c				\
	xml-io.c				\
	xml-io.h				\
	xml-io-version.h			\
	xml-io-autoft.c				\
	xml-io-autoft.h				\
	gnumeric-gconf.c			\
	gnumeric-gconf.h			\
	gnumeric-gconf-priv.h

GNUMERIC_GNOME = 				\
	bonobo-io.c				\
	bonobo-io.h				\
	sheet-object-bonobo.c			\
	sheet-object-bonobo.h			\
	sheet-object-container.c		\
	sheet-object-container.h		\
	sheet-object-item.c			\
	sheet-object-item.h

# old dead code that is archived in cvs
# it was disabled when we jumped to GtkAction
#	wbc-bonobo.c				\
#	workbook-control-component.c		\
#	workbook-control-component.h 		\
#	workbook-control-component-priv.h

GNUMERIC_SOURCE	= $(GNUMERIC_BASE)

gnumeric_SOURCES =				\
	$(GNUMERIC_SOURCE)			\
	io-context-gtk.c			\
	io-context-gtk.h			\
	main-application.c

gnumeric_LDADD = 				\
	$(gnumeric_app_libs)			\
	$(GNUMERIC_LIBS) -lpopt			\
	$(INTLLIBS)

gnumeric_LDFLAGS = -export-dynamic

gnumeric_app_libs =				\
	dialogs/libdialogs.a			\
	tools/libtools.a			\
	cut-n-paste-code/goffice/libgoffice.la	\
	cut-n-paste-code/goffice/cut-n-paste/pcre/libpcre.a		\
	cut-n-paste-code/foocanvas/libfoocanvas/libfoocanvas.a	\
	widgets/libwidgets.a

if ENABLE_SOLVER
gnumeric_app_libs +=				\
	tools/solver/libsolver.a		\
	tools/solver/glpk/source/libglpk.a	\
	tools/solver/lp_solve/liblp_solve.a
endif

ssconvert_LDADD   = $(gnumeric_LDADD)
ssconvert_LDFLAGS = $(gnumeric_LDFLAGS)
ssconvert_SOURCES =				\
	$(GNUMERIC_SOURCE)			\
	ssconvert.c

#test_pango_LDADD   = $(gnumeric_LDADD)
#test_pango_LDFLAGS = $(gnumeric_LDFLAGS)
#test_pango_SOURCES =				\
#	$(GNUMERIC_SOURCE) 			\
#	test-pango.c

gnm-marshalers.h : gnm-marshalers.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=gnm_ > $@
gnm-marshalers.c : gnm-marshalers.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --body --prefix=gnm_ > $@.tmp
	echo '/* This file has been automatically generated.  Do not edit.  */' >$@
	echo '#include "'$*.h'"' >>$@
	cat $@.tmp >>$@
	rm -f $@.tmp

gnumeric-paths.h: gnumeric-paths.sh
	@sh $< > $@.tmp ;					\
	if cmp -s $@.tmp $@; then				\
	    rm $@.tmp;						\
	else							\
	    mv $@.tmp $@ ; echo updated gnumeric-paths.h ;	\
	fi

install-exec-hook:
	cd $(DESTDIR)$(bindir) && \
	mv -f gnumeric$(EXEEXT) gnumeric-$(VERSION)$(EXEEXT) && \
	$(LN_S) gnumeric-$(VERSION)$(EXEEXT) gnumeric$(EXEEXT)

uninstall-hook:
	-cd $(DESTDIR)$(bindir) && \
	rm -f gnumeric-$(VERSION)$(EXEEXT)

# do not include the paths in the tarball
dist-hook :
	-rm -f $(distdir)/gnumeric-paths.h $(distdir)/gnumeric-paths.sh

# include conditional files to ensure everything gets distributed no matter
# what the build
EXTRA_DIST = GNOME_Gnumeric-gtk.xml				\
	    gnm-marshalers.list	                        	\
	    main-application.c 					\
	    main-component.c					\
	    $(GNUMERIC_GNOME) README.gnm_float			\
	    gnumeric-paths.sh.in ssconvert.c

#number_match_SOURCES = number-match.c  formats.c dates.c number-match-test.c

#number_match_LDADD = 			\
#	$(GNOME_LIBDIR) 		\
#	$(GNOMEUI_LIBS)			\
#	$(GNOME_XML_LIB) 		\
#	$(INTLLIBS)

gtkactiondir=$(gnumeric_datadir)
gtkaction_DATA = GNOME_Gnumeric-gtk.xml

# Bonobo requires things to be in a subdir of this form
#bonobo_component_uidir=$(gnumeric_datadir)/gnome-2.0/ui
#bonobo_component_ui_DATA = GNOME_Gnumeric.xml GNOME_Gnumeric_Component.xml
#if ENABLE_BONOBO_COMPONENT
#libexec_PROGRAMS = gnumeric-component
#gnumeric_component_SOURCES =			\
#	$(GNUMERIC_SOURCE)			\
#	main-component.c
#
#gnumeric_component_LDADD = $(gnumeric_LDADD)
#
#gnumeric_component_LDFLAGS = $(gnumeric_LDFLAGS)
#
#gnumeric_component_app_libs = $(gnumeric_app_libs)
#endif

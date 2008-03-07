AM_CPPFLAGS = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I..						\
	-DGNUMERIC_INTERNAL				\
	$(GNUMERIC_CFLAGS)

noinst_LTLIBRARIES = libwidgets.la

libwidgets_la_SOURCES =				\
	gnm-dao.c				\
	gnm-dao.h				\
	gnm-format-sel.c			\
	gnm-format-sel.h			\
	widget-editable-label.c			\
	widget-editable-label.h			\
	widget-font-selector.c			\
	widget-font-selector.h			\
	gnm-cell-combo-foo-view.c		\
	gnm-cell-combo-foo-view.h		\
	gnm-cell-combo-foo-view-impl.h		\
	gnm-filter-combo-foo-view.c		\
	gnm-filter-combo-foo-view.h		\
	gnm-validation-combo-foo-view.c		\
	gnm-validation-combo-foo-view.h		\
	gnumeric-cell-renderer-text.c		\
	gnumeric-cell-renderer-text.h		\
	gnumeric-cell-renderer-expr-entry.c	\
	gnumeric-cell-renderer-expr-entry.h	\
	gnumeric-cell-renderer-toggle.c		\
	gnumeric-cell-renderer-toggle.h		\
	gnumeric-dashed-canvas-line.c		\
	gnumeric-dashed-canvas-line.h		\
	gnumeric-expr-entry.c			\
	gnumeric-expr-entry.h			\
	gnumeric-lazy-list.c			\
	gnumeric-lazy-list.h

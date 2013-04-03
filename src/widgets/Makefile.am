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
	gnm-fontbutton.c			\
	gnm-fontbutton.h			\
	gnm-format-sel.c			\
	gnm-format-sel.h			\
	gnm-notebook.c				\
	gnm-radiobutton.c			\
	gnm-radiobutton.h			\
	gnm-cell-combo-view.c			\
	gnm-cell-combo-view.h			\
	gnm-cell-combo-view-impl.h		\
	gnm-filter-combo-view.c			\
	gnm-filter-combo-view.h			\
	gnm-validation-combo-view.c		\
	gnm-validation-combo-view.h		\
	gnm-sheet-slicer-combo-view.c		\
	gnm-sheet-slicer-combo-view.h		\
	gnumeric-cell-renderer-text.c		\
	gnumeric-cell-renderer-text.h		\
	gnumeric-cell-renderer-expr-entry.c	\
	gnumeric-cell-renderer-expr-entry.h	\
	gnumeric-cell-renderer-toggle.c		\
	gnumeric-cell-renderer-toggle.h		\
	gnumeric-dashed-canvas-line.c		\
	gnumeric-dashed-canvas-line.h		\
	gnumeric-expr-entry.c			\
	gnumeric-lazy-list.c			\
	gnumeric-lazy-list.h			\
	gnumeric-text-view.c			\
	gnumeric-text-view.h

libtools_la_includedir = $(includedir)/libspreadsheet-@GNUMERIC_API_VER@/spreadsheet/widgets
libtools_la_include_HEADERS = 	\
	gnm-notebook.h				\
	gnumeric-expr-entry.h

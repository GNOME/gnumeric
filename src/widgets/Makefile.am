INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
        -DGNUMERIC_GLADEDIR=\""$(gnumeric_gladedir)"\"	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libwidgets.a

libwidgets_a_SOURCES =				\
	widget-editable-label.c			\
	widget-editable-label.h			\
	widget-font-selector.c			\
	widget-font-selector.h			\
	gnumeric-cell-renderer.c		\
	gnumeric-cell-renderer.h		\
	gnumeric-dashed-canvas-line.c		\
	gnumeric-dashed-canvas-line.h		\
	gnumeric-expr-entry.c			\
	gnumeric-expr-entry.h			\
	gnumeric-combo-text.c			\
	gnumeric-combo-text.h

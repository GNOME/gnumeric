INCLUDES = 						\
	-I$(srcdir)/..					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
        -DGNUMERIC_GLADEDIR=\""$(gnumeric_gladedir)"\"	\
	-I$(includedir)				 	\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libwidgets.a

libwidgets_a_SOURCES =				\
	gnumeric-vscrollbar.c			\
	gnumeric-vscrollbar.h			\
	gnumeric-hscrollbar.c			\
	gnumeric-hscrollbar.h			\
	widget-editable-label.c			\
	widget-editable-label.h			\
	widget-font-selector.c			\
	widget-font-selector.h			\
	gnumeric-dashed-canvas-line.c		\
	gnumeric-dashed-canvas-line.h		\
	gnumeric-expr-entry.c			\
	gnumeric-expr-entry.h			\
	gnumeric-combo-text.c			\
	gnumeric-combo-text.h

if BONOBO
gnumeric_version="$(VERSION)-bonobo"
else
gnumeric_version="$(VERSION)"
endif

gnumeric_datadir = $(datadir)/gnumeric/$(gnumeric_version)
gladedir   = $(gnumeric_datadir)/glade

INCLUDES = 						\
	-I$(srcdir)/..					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"		\
	-I$(includedir)				 	\
	$(EXTRA_GNOME_CFLAGS)				\
	$(GUILE_INCS)

noinst_LIBRARIES = libwidgets.a

libwidgets_a_SOURCES =				\
	gtk-combo-box.c				\
	gtk-combo-box.h				\
	gnumeric-toolbar.c			\
	gnumeric-toolbar.h			\
	widget-color-combo.c			\
	widget-color-combo.h			\
	widget-pixmap-combo.c			\
	widget-pixmap-combo.h			\
	widget-editable-label.c			\
	widget-editable-label.h			\
	widget-font-selector.c			\
	widget-font-selector.h			\
	gnumeric-dashed-canvas-line.c		\
 	gnumeric-dashed-canvas-line.h		\
 	gtk-combo-text.c			\
 	gtk-combo-text.h

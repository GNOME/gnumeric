noinst_LTLIBRARIES = libgoffice-gui-utils.la
if WITH_WIN32
    noinst_LTLIBRARIES += libhtmlhelp-stub.la

    libhtmlhelp_stub_la_SOURCES = htmlhelp-stub.c htmlhelp-stub.h
    libhtmlhelp_stub_la_LIBADD = -luuid -lhtmlhelp
    libhtmlhelp_stub_la_LDFLAGS = -no-undefined -Wl,libhtmlhelp-stub.def
endif

libgoffice_gui_utils_la_SOURCES =	\
	go-combo-box.c			\
	go-combo-box.h			\
	go-color-group.c		\
	go-color-group.h		\
	go-color-palette.c		\
	go-color-palette.h		\
	go-combo-color.c		\
	go-combo-color.h		\
	go-combo-pixmaps.h		\
	go-combo-pixmaps.c		\
	go-combo-text.c			\
	go-combo-text.h			\
	\
	go-font-sel.c			\
	go-font-sel.h			\
	go-format-sel.c			\
	go-format-sel.h			\
	go-gui-utils.c			\
	go-gui-utils.h			\
	\
	go-action-combo-color.c 	\
	go-action-combo-color.h 	\
	go-action-combo-pixmaps.c 	\
	go-action-combo-pixmaps.h 	\
	go-action-combo-stack.c 	\
	go-action-combo-stack.h 	\
	go-action-combo-text.c 		\
	go-action-combo-text.h

include $(srcdir)/../goffice.mk

gladedir   = $(gnumeric_gladedir)
glade_DATA = \
	go-font-sel.glade 	\
	go-format-sel.glade 

EXTRA_DIST = htmlhelp-stub.c htmlhelp-stub.h

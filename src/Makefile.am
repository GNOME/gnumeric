CFLAGS += -Wall 	\
	-Wshadow             -Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

bin_PROGRAMS = gnumeric test-format

INCLUDES = 			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(includedir)				 \
	$(GNOME_INCLUDEDIR)

gnumeric_SOURCES =		\
	cell.h			\
	gnumeric-sheet.h	\
	gnumeric-sheet.c	\
	gnumeric.h		\
	item-bar.c		\
	item-bar.h		\
	item-cursor.c		\
	item-cursor.h		\
	item-debug.c		\
	item-debug.h		\
	item-grid.c		\
	item-grid.h		\
	main.c			\
	sheet.c			\
	sheet.h			\
	style.c			\
	style.h			\
	workbook.c

gnumeric_LDADD = \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS)	\
	$(INTLLIBS)

test_format_SOURCES = format.c

test_format_LDADD = \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS)
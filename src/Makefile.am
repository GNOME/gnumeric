CFLAGS += -Wall 	\
	-Wshadow             -Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

bin_PROGRAMS = gnumeric

INCLUDES = 			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(includedir)				 \
	$(GNOME_INCLUDEDIR)

gnumeric_SOURCES =		\
	cell.h			\
	gnumeric-sheet.h	\
	gnumeric-sheet.c	\
	gnumeric.h		\
	main.c			\
	sheet.c			\
	sheet.h			\
	style.h			\
	workbook.c

gnumeric_LDADD = \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS)	\
	$(INTLLIBS)

CFLAGS += -g -Wall 	\
	-Wshadow             -Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

bin_PROGRAMS = gnumeric
noinst_PROGRAMS = test-format 

INCLUDES = 			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(includedir)				 \
	$(GNOME_INCLUDEDIR)

gnumeric_SOURCES =		\
	cell.h			\
	expr.c			\
	expr.h			\
	gnumeric-sheet.c	\
	gnumeric-sheet.h	\
	gnumeric.h		\
	item-bar.c		\
	item-bar.h		\
	item-cursor.c		\
	item-cursor.h		\
	item-debug.c		\
	item-debug.h		\
	item-edit.c		\
	item-edit.h		\
	item-grid.c		\
	item-grid.h		\
	main.c			\
	parser.y		\
	sheet.c			\
	sheet.h			\
	style.c			\
	style.h			\
	symbol.h		\
	symbol.c		\
	utils.c			\
	utils.h			\
	workbook.c

gnumeric_LDADD = \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS)	\
	$(INTLLIBS)

test_format_SOURCES = format.c

test_format_LDADD = \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS)

#test_token_SOURCES = 		\
#	test-token.c		\
#	token.c			\
#	token.h##
#
#test_token_LDADD = \
#	$(GNOME_LIBDIR)	\
#	$(GNOMEUI_LIBS) -lgmp


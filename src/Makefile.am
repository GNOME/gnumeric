CFLAGS += -g -Wall 	\
	-Wshadow             -Wpointer-arith  	\
	-Wmissing-prototypes -Wmissing-declarations 

bin_PROGRAMS = gnumeric
check_PROGRAMS = test-format test-parser

INCLUDES = 			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" \
	-I$(includedir)				 \
	$(GNOME_INCLUDEDIR)

GNOME_XML_LIB = -lxml

GNUMERIC_BASE_SOURCES =		\
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
	parser.y		\
	sheet.c			\
	sheet.h			\
	style.c			\
	style.h			\
	symbol.h		\
	symbol.c		\
	utils.c			\
	utils.h			\
	workbook.c		\
	xml-io.c

gnumeric_SOURCES =		\
	main.c			\
	$(GNUMERIC_BASE_SOURCES)


gnumeric_LDADD = \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS)	\
	$(GNOME_XML_LIB) \
	$(INTLLIBS)

test_format_SOURCES = format.c

test_format_LDADD = \
	$(GNOME_LIBDIR) \
	$(GNOMEUI_LIBS) \
	$(INTLLIBS)

test_parser_SOURCES = 		\
	test-parser.c		\
	$(GNUMERIC_BASE_SOURCES)

test_parser_LDADD =		\
	$(GNOME_LIBDIR)		\
	$(GNOME_XML_LIB)	 \
	$(GNOMEUI_LIBS) $(INTLLIBS) -lgmp

#test_token_SOURCES = 		\
#	test-token.c		\
#	token.c			\
#	token.h##
#
#test_token_LDADD = \
#	$(GNOME_LIBDIR)	\
#	$(GNOMEUI_LIBS) -lgmp


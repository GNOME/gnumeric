INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

noinst_LIBRARIES = libexcel.a

libexcel_a_SOURCES =	\
	boot.c		\
	boot.h		\
	ms-excel-biff.h	\
	ms-biff.h	\
	ms-escher.c	\
	ms-escher.h	\
	ms-excel.c	\
	ms-excel.h	\
	ms-formula.c	\
	ms-formula.h	\
	ms-obj.c	\
	ms-obj.h	\
	ms-ole.c	\
	ms-ole.h

bin_PROGRAMS =		\
	ole

ole_SOURCES =		\
	ole.c

ole_LDADD =		\
	ms-ole.o	\
        $(GNOME_LIBDIR)	\
        $(GNOME_LIBS)	\
        $(INTLLIBS)


INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	-I$(srcdir)/libole2					\
	$(GNOME_INCLUDEDIR)

noinst_LIBRARIES = libexcel.a

libexcel_a_SOURCES =		\
	boot.c			\
	boot.h			\
	excel.h			\
	ms-excel-biff.h		\
	ms-biff.c		\
	ms-biff.h		\
	biff-types.h		\
	escher-types.h		\
	ms-escher.c		\
	ms-escher.h		\
	ms-excel.c		\
	ms-excel.h		\
	ms-excel-write.c	\
	ms-excel-write.h	\
	ms-formula.c		\
	ms-formula.h		\
	ms-obj.c		\
	ms-obj.h

bin_PROGRAMS =			\
	ole

ole_SOURCES =			\
	ole.c


ole_LDADD =			\
	ms-biff.o		\
	libole2/libole2.a	\
        $(GNOME_LIBDIR)		\
        $(GNOME_LIBS)		\
        $(INTLLIBS)

SUBDIRS = libole2

libole2_sources = 		\
	ms-ole.c		\
	ms-ole.h		\
	ms-ole-summary.c	\
	ms-ole-summary.h	\
	ms-ole-vba.c		\
	ms-ole-vba.h

if LIBOLE2_PUBLIC_LIBRARY
lib_LTLIBRARIES = libole2.la
libole2_la_SOURCES =		\
	$(libole2_sources)	\
	version.c
libole2includedir      = $(includedir)/libole2
libole2include_HEADERS =	\
	libole2.h		\
	ms-ole.h		\
	ms-ole-summary.h	\
	ms-ole-vba.h
else
## Following is when libole2-src is compiled from gnumeric
INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)				\
	-DHAVE_MMAP
## XXX delete HAVE_MMAP when gnumeric's configure.in includes AC_HAVE_MMAP

noinst_LIBRARIES = libole2.a
libole2_a_SOURCES =		\
	$(libole2_sources)
endif

## Following is useful when libole2-src is compiled from gnumeric
INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

libole2_sources = 		\
	ms-ole.c		\
	ms-ole.h		\
	ms-ole-summary.c	\
	ms-ole-summary.h	\
	version.c

if LIBOLE2_PUBLIC_LIBRARY
lib_LTLIBRARIES = libole2.la
libole2_la_SOURCES =		\
	$(libole2_sources)
libole2includedir      = $(includedir)/libole2
libole2include_HEADERS =	\
	libole2.h		\
	ms-ole-summary.h	\
	ms-ole.h
else
## Following is when libole2-src is compiled from gnumeric
lib_LIBRARIES = libole2.a
libole2_a_SOURCES =		\
	$(libole2_sources)
endif

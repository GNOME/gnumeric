INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

lib_LTLIBRARIES = libole2.la

libole2_la_SOURCES =		\
	ms-ole.c		\
	ms-ole.h		\
	ms-ole-summary.c	\
	ms-ole-summary.h


libole2includedir      = $(includedir)/libole2
libole2include_HEADERS = ms-ole.h

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

noinst_LIBRARIES = libexcel.a

libexcel_a_SOURCES =	\
	ms-biff.c	\
	ms-biff.h	\
	ms-excel.c	\
	ms-excel.h	\
	ms-ole.c	\
	ms-ole.h

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

if BONOBO
noinst_LIBRARIES = libole2.a libstorage_ole.a
else
noinst_LIBRARIES = libole2.a
endif

libole2_a_SOURCES =		\
	ms-ole.c		\
	ms-ole.h		\
	ms-ole-summary.c	\
	ms-ole-summary.h

libstorage_ole_a_SOURCES =	\
	gnome-storage-ole.c	\
	gnome-storage-ole.h	\
	gnome-stream-ole.c	\
	gnome-stream-ole.h



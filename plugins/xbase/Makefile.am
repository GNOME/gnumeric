INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)

noinst_LIBRARIES = libxbase.a

libxbase_a_SOURCES =		\
	boot.c			\
	boot.h			\
	xbase.c			\
	xbase.h

SUBDIRS = graph utils gui-utils drawing ms-compat libpresent test data

noinst_LTLIBRARIES = libgoffice.la

libgoffice_la_LIBADD = 				\
	utils/libgoffice-utils.la		\
	gui-utils/libgoffice-gui-utils.la	\
	graph/libgoffice-graph.la		\
	drawing/libgoffice-drawing.la		\
	ms-compat/libgoffice-ms-compat.la	\
	libpresent/libgoffice-libpresent.la

libgoffice_la_SOURCES = 	\
	goffice.c		\
	goffice.h

# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)

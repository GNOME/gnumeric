SUBDIRS = app graph utils gui-utils drawing ms-compat libpresent test pixmaps cut-n-paste

noinst_LTLIBRARIES = libgoffice.la

libgoffice_la_LIBADD = 				\
	utils/libgoffice-utils.la		\
	app/libgoffice-app.la			\
	gui-utils/libgoffice-gui-utils.la	\
	graph/libgoffice-graph.la		\
	drawing/libgoffice-drawing.la		\
	ms-compat/libgoffice-ms-compat.la	\
	libpresent/libgoffice-libpresent.la

libgoffice_la_SOURCES = 	\
	goffice.c		\
	goffice.h		\
	goffice-config.h

EXTRA_DIST = goffice.mk

include $(srcdir)/goffice.mk

# prune this when the code moves
INCLUDES = -I$(top_srcdir)/src/cut-n-paste-code/goffice		\
	   $(GNUMERIC_CFLAGS)

check_PROGRAMS = test-ppt

ldadd =	$(top_builddir)/src/cut-n-paste-code/goffice/libpresent/libgoffice-libpresent.la	\
	$(top_builddir)/src/cut-n-paste-code/goffice/ms-compat/libgoffice-ms-compat.la		\
	$(top_builddir)/src/cut-n-paste-code/goffice/drawing/libgoffice-drawing.la		\
	$(GNUMERIC_LIBS)

test_ppt_LDADD			= $(ldadd)

test_ppt_SOURCES =	\
	test-ppt.c



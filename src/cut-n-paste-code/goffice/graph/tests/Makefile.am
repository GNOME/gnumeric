# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)

check_PROGRAMS = test-canvas

ldadd = ../libgoffice-graph.a \
	$(top_builddir)/src/cut-n-paste-code/foocanvas/libfoocanvas/libfoocanvas.a \
	$(GNUMERIC_LIBS)

test_canvas_LDADD	= $(ldadd)

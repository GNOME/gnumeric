INCLUDES = \
	-I$(top_srcdir) 				\
	-I$(top_builddir)				\
	$(WARN_CFLAGS) 					\
	$(GNUMERIC_CFLAGS) -I$(top_srcdir)/src			\
        -DFOOCANVASLIBDIR=\""$(libdir)"\" 		\
        -DFOOCANVASDATADIR=\""$(datadir)"\" 		\
        -DFOOCANVASPIXMAPDIR=\""$(datadir)/pixmaps"\"	\
        -DFOOCANVASBINDIR=\""$(bindir)"\" 		\
        -DFOOCANVASLOCALSTATEDIR=\""$(localstatedir)"\" 	\
        -DFOOCANVASLOCALEDIR=\""$(gnomelocaledir)"\" 	\
	-DG_LOG_DOMAIN=\"Foocanvas\"			\
	-DVERSION=\"$(VERSION)\"

lib_LIBRARIES = libfoocanvas.a

RENDER_LIBS=
NULL=
libfoocanvas_a_LIBADD = \
	$(RENDER_LIBS)				\
	$(NULL)

libfoocanvasincludedir = $(includedir)/libfoocanvas-1.0/libfoocanvas


libfoocanvas_a_SOURCES =			\
	foo-canvas-i18n.h			\
	foo-canvas-line.c			\
	foo-canvas-line.h			\
	foo-canvas-pixbuf.c			\
	foo-canvas-pixbuf.h			\
	foo-canvas-polygon.c			\
	foo-canvas-polygon.h			\
	foo-canvas-rect-ellipse.c		\
	foo-canvas-rect-ellipse.h		\
	foo-canvas-text.c			\
	foo-canvas-text.h			\
	foo-canvas-util.c			\
	foo-canvas-util.h			\
	foo-canvas-widget.c			\
	foo-canvas-widget.h			\
	foo-canvas.c				\
	foo-canvas.h				\
	libfoocanvas.h				\
	libfoocanvastypes.c

$(libfoocanvas_2_a_OBJECTS): $(marshal_sources)

marshal_sources =				\
	foo-canvas-marshal.c			\
	foo-canvas-marshal.h

BUILT_SOURCES =					\
	$(marshal_sources)

foo-canvas-marshal.h: foo-canvas-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=foo_canvas_marshal > $@
foo-canvas-marshal.c: foo-canvas-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --body --prefix=foo_canvas_marshal > $@

EXTRA_DIST = \
	foo-canvas-marshal.list	\
	libfoocanvas.pc.in


DONT_DIST_SOURCE = $(marshal_sources)

dist-hook:
	for file in $(DONT_DIST_SOURCE) ; do \
	    rm -f $(distdir)/$$file ; \
	done

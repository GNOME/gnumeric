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

libfoocanvas_a_LIBADD = \
	$(RENDER_LIBS)				\
	$(OINK)

libfoocanvasincludedir = $(includedir)/libfoocanvas-1.0/libfoocanvas


libfoocanvas_a_SOURCES =			\
	foo-canvas-i18n.h			\
	foo-canvas-line.c			\
	foo-canvas-pixbuf.c			\
	foo-canvas-polygon.c			\
	foo-canvas-rect-ellipse.c		\
	foo-canvas-text.c			\
	foo-canvas-util.c			\
	foo-canvas-widget.c			\
	foo-canvas.c				\
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

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libfoocanvas.pc

DONT_DIST_SOURCE = $(marshal_sources)

dist-hook:
	for file in $(DONT_DIST_SOURCE) ; do \
	    rm -f $(distdir)/$$file ; \
	done

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

noinst_LIBRARIES = libfoocanvas.a

RENDER_LIBS=
NULL=
libfoocanvas_a_LIBADD = \
	$(RENDER_LIBS)				\
	$(NULL)

libfoocanvasincludedir = $(includedir)/libfoocanvas-1.0/libfoocanvas


BUILT_SOURCES =					\
	foo-canvas-marshal.c			\
	foo-canvas-marshal.h

libfoocanvas_a_SOURCES =			\
	$(BUILT_SOURCES)			\
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

foo-canvas-marshal.h: foo-canvas-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=foo_canvas_marshal > $@
foo-canvas-marshal.c: foo-canvas-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --body --prefix=foo_canvas_marshal > $@.tmp
	echo '/* This file has been automatically generated.  Do not edit.  */' >$@
	echo '#include "'$*.h'"' >>$@
	cat $@.tmp >>$@
	rm -f $@.tmp

EXTRA_DIST = \
	foo-canvas-marshal.list	\
	libfoocanvas.pc.in


DONT_DIST_SOURCE = $(BUILT_SOURCES)

dist-hook:
	for file in $(DONT_DIST_SOURCE) ; do \
	    rm -f $(distdir)/$$file ; \
	done

noinst_HEADERS = \
	foo-canvas-i18n.h \
	foo-canvas-line.h \
	foo-canvas-marshal.h \
	foo-canvas-pixbuf.h \
	foo-canvas-polygon.h \
	foo-canvas-rect-ellipse.h \
	foo-canvas-text.h \
	foo-canvas-util.h \
	foo-canvas-widget.h \
	foo-canvas.h \
	libfoocanvas.h

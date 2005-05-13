AM_CPPFLAGS = \
	-I$(top_srcdir) 				\
	-I$(top_builddir)				\
	$(WARN_CFLAGS) 					\
	$(GNUMERIC_CFLAGS) -I$(top_srcdir)/src				\
        -DFOOCANVASLIBDIR=\""$(libdir)"\" 		\
        -DFOOCANVASDATADIR=\""$(datadir)"\" 		\
        -DFOOCANVASPIXMAPDIR=\""$(datadir)/pixmaps"\"	\
        -DFOOCANVASBINDIR=\""$(bindir)"\" 		\
        -DFOOCANVASLOCALSTATEDIR=\""$(localstatedir)"\"	\
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


libfoocanvas_a_SOURCES =			\
	foo-canvas-marshal.list			\
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

GENMARSHAL_COMMAND = $(GLIB_GENMARSHAL) --prefix=foo_canvas_marshal
SUFFIXES = .list

.list.h:
	$(GENMARSHAL_COMMAND) --header $< >$@

.list.c:
	(echo '/* This file has been automatically generated.  Do not edit. */' && \
	echo '#include "$*.h"' && \
	$(GENMARSHAL_COMMAND) --body $< ) >$@

# A hint is needed to build the header first:
BUILT_SOURCES = foo-canvas-marshal.h

# Another hint, see bugs #172211 and #172212:
non-intermediate: foo-canvas-marshal.c

CLEANFILES = foo-canvas-marshal.h foo-canvas-marshal.c

EXTRA_DIST = libfoocanvas.pc.in


noinst_HEADERS = \
	foo-canvas-marshal.h \
	foo-canvas-i18n.h \
	foo-canvas-line.h \
	foo-canvas-pixbuf.h \
	foo-canvas-polygon.h \
	foo-canvas-rect-ellipse.h \
	foo-canvas-text.h \
	foo-canvas-util.h \
	foo-canvas-widget.h \
	foo-canvas.h \
	libfoocanvas.h

noinst_LTLIBRARIES = libgoffice-utils.la

libgoffice_utils_la_SOURCES =	\
	go-marshalers.list	\
	\
	goffice-utils.h		\
	go-color.c		\
	go-color.h		\
	go-file.c		\
	go-file.h		\
	go-font.c		\
	go-font.h		\
	\
	go-gradient.c		\
	go-gradient.h		\
	go-line.c		\
	go-line.h		\
	go-marker.c		\
	go-marker.h		\
	go-pattern.c		\
	go-pattern.h		\
	go-locale.c		\
	go-locale.h		\
	go-units.h		\
	go-math.c		\
	go-math.h		\
	numbers.h		\
	datetime.c		\
	datetime.h		\
	format.c		\
	format.h		\
	format-impl.h		\
	formats.c		\
	go-format.c		\
	go-format.h		\
	go-format-match.c	\
	regutf8.c 		\
	regutf8.h 		\
	\
	go-glib-extras.c	\
	go-glib-extras.h	\
	go-libxml-extras.c	\
	go-libxml-extras.h

CLEANFILES =			\
	go-marshalers.h		\
	go-marshalers.c

# A hint is needed to build the header first:
BUILT_SOURCES = go-marshalers.h

GENMARSHAL_COMMAND = $(GLIB_GENMARSHAL) --prefix=go_
SUFFIXES = .list

.list.h: $(GLIB_GENMARSHAL)
	$(GENMARSHAL_COMMAND) --header $< >$@

.list.c: $(GLIB_GENMARSHAL)
	(echo '/* This file has been automatically generated.  Do not edit. */' && \
	echo '#include "$*.h"' && \
	$(GENMARSHAL_COMMAND) --body $< ) >$@

include $(srcdir)/../goffice.mk

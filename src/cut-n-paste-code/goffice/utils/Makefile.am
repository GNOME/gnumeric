noinst_LTLIBRARIES = libgoffice-utils.la

libgoffice_utils_la_SOURCES =	\
	$(BUILT_SOURCES)	\
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

go-marshalers.h : go-marshalers.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=go_ > $@
go-marshalers.c : go-marshalers.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --body --prefix=go_ > $@.tmp
	echo '/* This file has been automatically generated.  Do not edit.  */' >$@
	echo '#include "'$*.h'"' >>$@
	cat $@.tmp >>$@
	rm -f $@.tmp

BUILT_SOURCES =				\
	go-marshalers.h			\
	go-marshalers.c

EXTRA_DIST = go-marshalers.list

include $(srcdir)/../goffice.mk

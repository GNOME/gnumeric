noinst_LTLIBRARIES = libgoffice-utils.la

libgoffice_utils_la_SOURCES =	\
	goffice-utils.h		\
	go-color.c		\
	go-color.h		\
	go-font.c		\
	go-font.h		\
	go-format.c		\
	go-format.h		\
	go-gradient.c		\
	go-gradient.h		\
	go-marker.c		\
	go-marker.h		\
	go-pattern.c		\
	go-pattern.h		\
	go-units.h

# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)


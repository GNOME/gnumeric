noinst_LTLIBRARIES = libgoffice-utils.la

libgoffice_utils_la_SOURCES =	\
	go-units.h		\
	go-color.c		\
	go-color.h		\
	go-pattern.c		\
	go-pattern.h		\
	go-gradient.c		\
	go-gradient.h

# prune this when the code moves
INCLUDES = -I$(top_srcdir) 					\
	   -I$(top_srcdir)/src 					\
	   -I$(top_srcdir)/src/cut-n-paste-code			\
	   -I$(top_srcdir)/src/cut-n-paste-code/foocanvas	\
	    $(GNUMERIC_CFLAGS)


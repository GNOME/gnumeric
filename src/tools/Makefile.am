#
# Gnumeric's Tools Makefile.
# Author: Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)

SUBDIRS = solver

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(srcdir)/solver				\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libtools.a

libtools_a_SOURCES =				


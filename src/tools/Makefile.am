#
# Gnumeric's Tools Makefile.
# Author: Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)

SUBDIRS = solver

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/dialogs			\
	-I$(srcdir)/solver				\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libtools.a

libtools_a_SOURCES =					\
	analysis-tools.c				\
	analysis-tools.h				\
	random-generator.c				\
	random-generator.h				\
	dao.c						\
	dao.h						\
	data-shuffling.c				\
	data-shuffling.h				\
	auto-correct.c					\
	auto-correct.h					\
	filter.h					\
	filter.c					\
	goal-seek.c					\
	goal-seek.h					\
	simulation.c					\
	simulation.h					\
	tools.h


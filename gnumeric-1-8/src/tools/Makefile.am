#
# Gnumeric's Tools Makefile.
# Author: Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)

if ENABLE_SOLVER
  SUBDIRS = solver
endif

AM_CPPFLAGS = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/dialogs			\
	-I$(srcdir)/solver				\
	-DGNUMERIC_INTERNAL				\
	$(GNUMERIC_CFLAGS)

noinst_LTLIBRARIES = libtools.la

libtools_la_SOURCES =					\
	analysis-histogram.c				\
	analysis-histogram.h				\
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
	fill-series.c					\
	fill-series.h					\
	filter.h					\
	filter.c					\
	goal-seek.c					\
	goal-seek.h					\
	scenarios.c					\
	scenarios.h					\
	simulation.c					\
	simulation.h					\
	tabulate.c					\
	tabulate.h					\
	tools.h


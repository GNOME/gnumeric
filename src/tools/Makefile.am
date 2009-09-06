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
	analysis-anova.c				\
	analysis-anova.h				\
	analysis-chi-squared.c				\
	analysis-chi-squared.h				\
	analysis-exp-smoothing.c			\
	analysis-exp-smoothing.h			\
	analysis-frequency.c				\
	analysis-frequency.h				\
	analysis-histogram.c				\
	analysis-histogram.h				\
	analysis-kaplan-meier.c				\
	analysis-kaplan-meier.h				\
	analysis-tools.c				\
	analysis-tools.h				\
	random-generator.c				\
	random-generator.h				\
	random-generator-cor.c				\
	random-generator-cor.h				\
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


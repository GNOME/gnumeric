#
# Gnumeric's Tools Makefile.
# Author: Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)

AM_CPPFLAGS = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/dialogs			\
	-I$(top_builddir)/src				\
	-DGNUMERIC_INTERNAL				\
	$(GNUMERIC_CFLAGS)

noinst_LTLIBRARIES = libtools.la

libtools_la_SOURCES =					\
	analysis-anova.c				\
	analysis-anova.h				\
	analysis-auto-expression.c			\
	analysis-auto-expression.h			\
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
	analysis-normality.c				\
	analysis-normality.h				\
	analysis-one-mean-test.c			\
	analysis-one-mean-test.h			\
	analysis-principal-components.c			\
	analysis-principal-components.h			\
	analysis-sign-test.c				\
	analysis-sign-test.h				\
	analysis-signed-rank-test.c			\
	analysis-signed-rank-test.h			\
	analysis-tools.c				\
	analysis-tools.h				\
	analysis-wilcoxon-mann-whitney.c	\
	analysis-wilcoxon-mann-whitney.h	\
	random-generator.c				\
	random-generator.h				\
	random-generator-cor.c				\
	random-generator-cor.h				\
	dao.c						\
	data-shuffling.c				\
	data-shuffling.h				\
	auto-correct.c					\
	auto-correct.h					\
	fill-series.c					\
	fill-series.h					\
	filter.h					\
	filter.c					\
	gnm-solver.c					\
	goal-seek.c					\
	goal-seek.h					\
	scenarios.c					\
	scenarios.h					\
	simulation.c					\
	simulation.h					\
	tabulate.c					\
	tabulate.h

libtools_la_includedir = $(includedir)/libspreadsheet-@GNUMERIC_API_VER@/spreadsheet/tools
libtools_la_include_HEADERS = 	\
	dao.h						\
	gnm-solver.h				\
	tools.h


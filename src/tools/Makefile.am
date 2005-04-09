#
# Gnumeric's Tools Makefile.
# Author: Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)

if ENABLE_SOLVER
  SUBDIRS = solver
endif

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

if WITH_WIN32
    noinst_PROGRAMS = gconf-schemas-to-win32-reg
    gconf_schemas_to_win32_reg_SOURCES = gconf-schemas-to-win32-reg.c
    gconf_schemas_to_win32_reg_LDADD = $(GNUMERIC_LIBS)
endif


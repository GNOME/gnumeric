SUBDIRS = glpk lp_solve

AM_CPPFLAGS = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/dialogs			\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/tools/solver		\
	-I$(top_srcdir)/src/tools/solver/glpk/include	\
	-DGNUMERIC_INTERNAL				\
	$(GNUMERIC_CFLAGS)

noinst_LTLIBRARIES = libsolver.la

libsolver_la_SOURCES =				\
	api.c					\
	api.h					\
	solver.c				\
	reports.c				\
	reports.h				\
	reports-write.h				\
	reports-write.c

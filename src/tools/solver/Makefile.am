SUBDIRS = lp_solve

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/dialogs			\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/tools/solver		\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libsolver.a

libsolver_a_SOURCES =				\
	api.c					\
	api.h					\
	solver.c				\
	reports.c				\
	reports.h				\
	reports-write.h				\
	reports-write.c

INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/tools/solver		\
	-I$(top_srcdir)/src/tools/solver/lp_solve	\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = liblp_solve.a

liblp_solve_a_SOURCES =				\
	solver-lp-api-wrapper.c			\
	lp-solve-debug.c			\
	lpkit.c					\
	solve.c					

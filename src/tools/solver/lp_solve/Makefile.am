AM_CPPFLAGS = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-include $(CONFIG_HEADER)			\
	-DGNUMERIC_INTERNAL				\
	$(GNUMERIC_CFLAGS)				\
	-DPARSER_LP

noinst_LTLIBRARIES = liblp_solve.la

liblp_solve_la_SOURCES = lp_solve.c lp_solve.h

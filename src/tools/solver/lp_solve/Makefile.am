INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/tools/solver		\
	-I$(top_srcdir)/src/tools/solver/lp_solve	\
	$(GNUMERIC_CFLAGS)				\
	 -DYY_NEVER_INTERACTIVE -DPARSER_CPLEX -DPARSER_LP

noinst_LIBRARIES = liblp_solve.a

liblp_solve_a_SOURCES =				\
    lpkit.h		\
    lp_MDO.c		\
    commonlib.c		\
    colamd.c		\
    fortify.c		\
    lp_rlp.c		\
    lp_crash.c		\
    lp_etaPFI.c		\
    lp_Hash.c		\
    lp_lib.c		\
    lp_wlp.c		\
    lp_rlpt.c		\
    lp_matrix.c		\
    lp_mipbb.c		\
    lp_MPS.c		\
    lp_presolve.c	\
    lp_price.c		\
    lp_pricePSE.c	\
    lp_report.c		\
    lp_scale.c		\
    lp_simplex.c	\
    lp_SOS.c		\
    lp_utils.c		\
    lp_wlpt.c		\
    yacc_read.c

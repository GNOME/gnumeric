AM_CPPFLAGS = 						\
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
    colamd.c		\
    colamd.h		\
    commonlib.c		\
    commonlib.h		\
    fortify.c		\
    lp_BFP.h		\
    lp_Hash.c		\
    lp_Hash.h		\
    lp_MDO.c		\
    lp_MDO.h		\
    lp_MPS.c		\
    lp_MPS.h		\
    lp_SOS.c		\
    lp_SOS.h		\
    lp_crash.c		\
    lp_crash.h		\
    lp_etaPFI.c		\
    lp_etaPFI.h		\
    lp_lib.c		\
    lp_lib.h		\
    lp_matrix.c		\
    lp_matrix.h		\
    lp_mipbb.c		\
    lp_mipbb.h		\
    lp_presolve.c	\
    lp_presolve.h	\
    lp_price.c		\
    lp_price.h		\
    lp_pricePSE.c	\
    lp_pricePSE.h	\
    lp_report.c		\
    lp_report.h		\
    lp_rlp.c		\
    lp_rlp.h		\
    lp_rlpt.c		\
    lp_rlpt.h		\
    lp_scale.c		\
    lp_scale.h		\
    lp_simplex.c	\
    lp_simplex.h	\
    lp_types.h		\
    lp_utils.c		\
    lp_utils.h		\
    lp_wlp.c		\
    lp_wlp.h		\
    lp_wlpt.c		\
    lpglob.h		\
    lpkit.h		\
    lpkit.h		\
    yacc_read.c		\
    yacc_read.h

# icky inclusion of partial file
EXTRA_DIST = lp_BFP1.c

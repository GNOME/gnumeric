SUBDIRS = 

INCLUDES =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/tools/solver		\
	-I$(top_srcdir)/src/tools/solver/glpk/include	\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libglpk.a

libglpk_a_SOURCES =					\
	glpinv.c				\
	glplp.c				\
	glplpx6a.c				\
	glpmat1.c				\
	glpspm.c				\
	glpbbm.c				\
	glplpx1.c				\
	glpspx1.c				\
	glplpx2.c				\
	glplpx6c.c				\
	glpspx2.c				\
	glplib1.c				\
	glplpx3.c				\
	glplib2.c				\
	glplpx4.c				\
	glprsm1.c				\
	glpstr2.c				\
	glplib3.c				\
	glpluf.c				\
	glprsm2.c				
	
	
	

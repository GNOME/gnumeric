SUBDIRS = 

AM_CPPFLAGS =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/tools/solver		\
	-I$(top_srcdir)/src/tools/solver/glpk/include	\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libglpk.a

libglpk_a_SOURCES =					\
	glpdmp.c				\
	glpies1.c				\
	glpies2.c				\
	glpies3.c				\
	glpinv.c				\
	glplib1a.c				\
	glplib1b.c				\
	glplib2.c				\
	glplib3.c				\
	glplib4.c				\
	glplpp1.c				\
	glplpp2.c				\
	glplpx1.c				\
	glplpx2.c				\
	glplpx3.c				\
	glplpx4.c				\
	glplpx5.c				\
	glplpx6a.c				\
	glplpx6c.c				\
	glplpx7.c				\
	glpluf.c				\
	glpmip1.c				\
	glpspm.c				\
	glpspx1.c				\
	glpspx2.c				\
	glpstr.c				

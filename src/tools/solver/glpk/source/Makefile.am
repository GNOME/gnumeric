SUBDIRS = 

AM_CPPFLAGS =						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	-I$(top_srcdir)/src/tools/solver		\
	-I$(top_srcdir)/src/tools/solver/glpk/include	\
	-DGNUMERIC_INTERNAL				\
	$(GNUMERIC_CFLAGS)

noinst_LTLIBRARIES = libglpk.la

libglpk_la_SOURCES =					\
	glpavl.c				\
	glpdmp.c				\
	glpipp1.c				\
	glpipp2.c				\
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
	glplpx6d.c				\
	glplpx7.c				\
	glplpx7a.c				\
	glpluf.c				\
	glpmip1.c				\
	glpmip2.c				\
	glpspx1.c				\
	glpspx2.c				\
	glpstr.c				

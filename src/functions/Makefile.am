INCLUDES = 						\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/src				\
	-I$(top_srcdir)/src/tools			\
	$(GNUMERIC_CFLAGS)

noinst_LIBRARIES = libfunctions.a

libfunctions_a_SOURCES =			\
	fn-database.c				\
	fn-information.c			\
	fn-logical.c				\
	fn-lookup.c				\
	fn-math.c				\
	fn-sheet.c				\
	fn-stat.c

INCLUDES = 						\
	-I$(srcdir)/..					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"		\
	-I$(includedir)				 	\
	$(EXTRA_GNOME_CFLAGS)				\
	$(GUILE_INCS)

noinst_LIBRARIES = libfunctions.a

libfunctions_a_SOURCES =				\
	fn-database.c				\
	fn-date.c				\
	fn-eng.c				\
	fn-financial.c				\
	fn-information.c			\
	fn-logical.c				\
	fn-lookup.c				\
	fn-math.c				\
	fn-sheet.c				\
	fn-stat.c				\
	fn-string.c

INCLUDES = \
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 		\
    -I$(includedir)	-I$(top_srcdir)/src		\
    $(GNOME_INCLUDEDIR)

gnumeric_plugin_LTLIBRARIES = gnum_lotus.la
gnum_lotus_la_LDFLAGS = -module -avoid-version
gnum_lotus_la_SOURCES = \
	boot.c			\
	lotus-types.h		\
	lotus.h			\
	lotus.c			\
	lotus-formula.h		\
	lotus-formula.c

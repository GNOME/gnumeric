#
# Gnumeric's Tools Makefile.
# Author: Ivan, Wong Yat Cheung (email@ivanwong.info)

AM_CPPFLAGS = $(GNUMERIC_CFLAGS) -DGNUMERIC_INTERNAL

noinst_PROGRAMS =
if WITH_WIN32
    noinst_PROGRAMS += gconf-schemas-to-win32-reg
    gconf_schemas_to_win32_reg_SOURCES = gconf-schemas-to-win32-reg.c
    gconf_schemas_to_win32_reg_LDADD = $(GNUMERIC_LIBS)
endif

EXTRA_DIST = dumpdef.pl check-gfrees check-null-false-returns

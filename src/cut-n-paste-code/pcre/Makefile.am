INCLUDES = \
	$(GNUMERIC_CFLAGS) -I$(top_srcdir) \
	 -DSUPPORT_UTF8 \
	 -DNEWLINE=10 \
	 -DPOSIX_MALLOC_THRESHOLD=10 \
	 -DLINK_SIZE=2 \
	 -DMATCH_LIMIT=10000000

noinst_LIBRARIES = libpcre.a
libpcre_a_SOURCES = maketables.c get.c study.c pcre.c pcreposix.c
noinst_HEADERS = internal.h pcreposix.h pcre.h

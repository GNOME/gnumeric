#
# Makefile for the Python Gnumeric plugin.
#

#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(srcdir)/../../src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins

CFLAGS += $(PERL_CCCDLFLAGS)				\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)					\
	$(PERL_CCOPTS)					\
	$(GNOME_INCLUDEDIR)


noinst_PROGRAMS = perl.so

.c.o:
	$(CC) $(CFLAGS) -c $<

perl_so_SOURCES = perl.c

perl.so: perl.o xsinit.o ext.timestamp
	$(PERL_LD) $(PERL_LDDLFLAGS) -o perl.so perl.o xsinit.o $(PERL_LDOPTS)

xsinit.c:
	perl -MExtUtils::Embed -e xsinit -- -o xsinit.c

ext.timestamp:
	( cd ext ; perl Makefile.PL LIB=$(datadir)/gnumeric/perl/lib )
	touch ext.timestamp

Perlscriptsdir = $(datadir)/gnumeric/perl

Perlscripts_DATA = startup.pl

EXTRA_DIST = startup.pl

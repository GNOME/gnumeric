# Where to install the plugin
#
if BONOBO
gnumeric_version="$(VERSION)-bonobo"
else
gnumeric_version="$(VERSION)"
endif
gnumeric_libdir  = $(libdir)/gnumeric/$(gnumeric_version)
gnumeric_datadir = $(datadir)/gnumeric/$(gnumeric_version)
plugindir = $(gnumeric_libdir)/plugins
gladedir  = $(gnumeric_datadir)/glade
gnumeric_datadir = $(datadir)/gnumeric/$(gnumeric_version)
plugindir = $(gnumeric_datadir)/plugins
gnperldir = $(gnumeric_datadir)/perl

PERL_CFLAGS = $(PERL_CCCDLFLAGS)			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
	-I$(includedir)					\
	-I$(top_srcdir)/src				\
	$(PERL_CCOPTS)					\
	$(GNOME_INCLUDEDIR)

#noinst_PROGRAMS = libgnum_perl.so
plugin_PROGRAMS = libgnum_perl.so

libgnum_perl.so_SOURCES = perl.c

libgnum_perl.so: perl.o xsinit.o ext.timestamp
	-$(PERL_LD) $(PERL_LDDLFLAGS) -o libgnum_perl.so perl.o xsinit.o $(PERL_LDOPTS)

perl.o: $(srcdir)/perl.c
	$(PERL_CC) $(PERL_CFLAGS) -c $(srcdir)/perl.c

xsinit.o: xsinit.c
	$(PERL_CC) $(PERL_CFLAGS) -c xsinit.c

xsinit.c:
	-perl -MExtUtils::Embed -e xsinit -- -o xsinit.c

# do all the ext stuff

ext.timestamp: ext/Makefile
	cd ext && $(MAKE)
	echo timestamp > ext.timestamp

ext/Makefile: ext/Makefile.PL 
	-(set -x; 					\
	srcextdir=`cd $(srcdir)/ext && pwd`;		\
	cd ext;						\
	if test ! -r Gnumeric.xs; then			\
	  rm -f Gnumeric.xs;				\
	  $(LN_S) $$srcextdir/Gnumeric.xs Gnumeric.xs;	\
	fi;						\
	if test ! -r Gnumeric.pm; then			\
	  rm -f Gnumeric.pm;				\
	  $(LN_S) $$srcextdir/Gnumeric.pm Gnumeric.pm;	\
	fi;						\
	perl Makefile.PL LIB=$(gnperldir))

ext/Makefile.PL: $(srcdir)/ext/Makefile.PL.in $(top_builddir)/config.status
	cd $(top_builddir) && CONFIG_FILES=plugins/perl/ext/Makefile.PL \
		CONFIG_HEADERS= ./config.status

install-exec-local:
	cd ext && $(MAKE) install

check-local:
	cd ext && $(MAKE) test

Perlscriptsdir = $(gnperldir)

Perlscripts_DATA = startup.pl

EXTRA_DIST = startup.pl				\
	ext/Makefile.PL.in			\
	ext/Gnumeric.xs				\
	ext/Gnumeric.pm


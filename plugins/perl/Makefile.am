# Do not build by default until we are ready.
#if WITH_PERL
PERL_PROGRAMS_TARGET = perl.so
#else
#PERL_PROGRAMS_TARGET =
#endif

gnperldir = $(gnumeric_datadir)/perl
PERL_CFLAGS = -I../.. $(PERL_CCCDLFLAGS)	\
    -DPLUGIN_ID=\"Gnumeric_perl\"		\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\"	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(PERL_CCOPTS)				\
    $(GNUMERIC_CFLAGS)

#
# Do not build the plugin until there is a maintainer
#

gnumeric_plugin_perldir = $(gnumeric_plugindir)/perl
xmldir = $(gnumeric_plugin_perldir)
gnumeric_plugin_perl_PROGRAMS = $(PERL_PROGRAMS_TARGET)
perl_so_SOURCES = perl.c

perl.so$(EXEEXT): perl.o xsinit.o ext.timestamp
	-$(PERL_LD) $(PERL_LDDLFLAGS) -o perl.so perl.o xsinit.o $(PERL_LDOPTS)

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
Perlscripts_DATA = \
	startup.pl		\
	ext/Makefile.PL.in	\
	ext/Gnumeric.xs		\
	ext/Gnumeric.pm

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(Perlscripts_DATA) $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

INCLUDES = \
    -I../.. $(PERL_CCCDLFLAGS) \
    -DPLUGIN_ID=\"Gnumeric_PerlLoader\"		\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src	\
    $(PERL_CCOPTS)				\
    $(GNUMERIC_CFLAGS)

gnumeric_plugin_perl_loaderdir = $(gnumeric_plugindir)/perl-loader
xmldir = $(gnumeric_plugin_perl_loaderdir)
gnumeric_plugin_perl_loader_LTLIBRARIES = perl_loader.la

perl_loader_CFLAGS = $(INCLUDES)
perl_loader_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS) $(PERL_LDDLFLAGS)
perl_loader_la_LIBADD = $(PERL_LDOPTS)
perl_loader_la_SOURCES = \
	boot.c \
	perl-loader.c \
	perl-loader.h \
	xsinit.c \
	perl-gnumeric.h \
	perl-gnumeric.c

perl_loader.so(EXEEXT): perl-loader.o boot.o
	$(PERL_LD) $(PERL_LDDLFLAGS) -o perl_loader.so boot.o xsinit.o $(PERL_LDOPTS)

boot.o: $(srcdir)/boot.c
	$(PERL_CC) $(INCLUDES) -c $(srcdir)/boot.c

xsinit.o: xsinit.c
	$(PERL_CC) $(INCLUDES) -c xsinit.c

xsinit.c:
	perl -MExtUtils::Embed -e xsinit -- -o xsinit.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
DISTCLEANFILES = $(xml_DATA)

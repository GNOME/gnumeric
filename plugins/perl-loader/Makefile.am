AUTOMAKE_OPTIONS=no-dependencies

AM_CPPFLAGS = \
    -I../.. \
    -DPLUGIN_ID=\"Gnumeric_PerlLoader\"		\
    -DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
    -I$(top_srcdir)/src	-I$(top_builddir)/src

gnumeric_plugin_perl_loaderdir = $(gnumeric_plugindir)/perl-loader
xmldir = $(gnumeric_plugin_perl_loaderdir)
gnumeric_plugin_perl_loader_LTLIBRARIES = perl_loader.la

perl_loader_la_LDFLAGS = -module $(GNUMERIC_PLUGIN_LDFLAGS) $(PERL_LDDLFLAGS)
perl_loader_la_LIBADD = $(PERL_LDOPTS) $(GTK_LIBS)
perl_loader_la_SOURCES = \
	boot.c \
	perl-loader.c \
	perl-loader.h \
	perl-gnumeric.h \
	perl-gnumeric.c
nodist_perl_loader_la_SOURCES = \
	xsinit.c

# Auto dep tracking is off here.  Add a few deps by hand
some_deps =					\
	$(top_srcdir)/src/application.h		\
	$(top_srcdir)/src/expr.h		\
	$(top_srcdir)/src/func.h		\
	$(top_srcdir)/src/gnm-plugin.h		\
	$(top_srcdir)/src/gnumeric.h		\
	$(top_srcdir)/src/sheet.h		\
	$(top_srcdir)/src/value.h		\
	$(top_srcdir)/src/workbook.h		\
	$(top_srcdir)/src/workbook-view.h	\
	perl-gnumeric.h				\
	perl-loader.h


boot.lo: $(some_deps)
perl-loader.lo: $(some_deps)
perl-gnumeric.lo: $(some_deps)


# Use the right compiler.  This is not 100% correct: we use GNUMERIC_CFLAGS
# here which might contain stuff we don't like.  It looks good enough,
# though.
dist_noinst_SCRIPTS=perl-cc-wrapper
COMPILE=$(PERL) $(srcdir)/perl-cc-wrapper $(PERL_CC) $(PERL_CCOPTS) \
	$(PERL_CCCDLFLAGS) $(AM_CPPFLAGS) --sanitize=1 $(GNUMERIC_CFLAGS)
LTCOMPILE=$(LIBTOOL) --tag=CC --mode=compile $(COMPILE)

xsinit.c:
	$(PERL) -MExtUtils::Embed -e xsinit -- -o xsinit.c

xml_in_files = plugin.xml.in
xml_DATA = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_RULE@

EXTRA_DIST = $(xml_in_files)
CLEANFILES = $(xml_DATA) xsinit.c

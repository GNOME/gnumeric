#
# Makefile for the Python Gnumeric plugin.
#
# Where can we find the Gnumeric include files.
#
GNUMERIC_SRC_DIR = $(srcdir)/../../src

#
# Where to install the plugin
#
plugindir = $(libdir)/gnumeric/plugins

# CFLAGS += -g -Wall 	\
# 	-Wshadow             -Wpointer-arith  	\
# 	-Wmissing-prototypes -Wmissing-declarations 

plugin_LTLIBRARIES = libperl.la

libperl_la_SOURCES = perl.c xsinit.c

xsinit.c:
	perl -MExtUtils::Embed -e xsinit -- -o xsinit.c

INCLUDES = 						\
	-I$(GNUMERIC_SRC_DIR)				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)					\
	$(PERL_CCOPTS)					\
	$(GNOME_INCLUDEDIR)

libperl_la_LIBADD = $(PERL_LDOPTS)

Perlscriptsdir = $(datadir)/gnumeric/perl

Perlscripts_DATA = startup.pl

EXTRA_DIST = startup.pl


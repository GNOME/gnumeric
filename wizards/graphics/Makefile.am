#
# Gnumeric's Graphics Wizard Makefile
#
# Author: Miguel de Icaza (miguel@kernel.org)
#

wizarddir = $(libdir)/gnumeric/wizards

wizard_LTLIBRARIES = libwizard.la

libwizard_la_SOURCES =			\
	main.c				\
	graphic-context.c		\
	graphic-context.h

libwizard_la_LIBADD =			\
	$(GNOME_LIBDIR) 		\
	$(GNOMEUI_LIBS)			\
	$(GNOME_XML_LIB) 		\
	$(BONOBO_LIBS)			\
	$(INTLLIBS)

INCLUDES = 						\
	-I$(top_srcdir)/libglade			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	$(GNOME_INCLUDEDIR)


#
# Gnumeric's Graphics Wizard Makefile
#
# Author: Miguel de Icaza (miguel@kernel.org)
#
# If you make this a shared module, remember to set
# the WIZARD_PLUGIN variable
#

wizarddir = $(libdir)/gnumeric/wizards/$(VERSION)
iconsdir  = $(datadir)/pixmaps/gnumeric
gladedir   = $(datadir)/gnumeric/glade

glade_DATA = graphics.glade

glade_msgs = graphics.glade.h

if BONOBO
noinst_LIBRARIES = libwizard_graphics.a
endif

libwizard_graphics_a_SOURCES =		\
	main.c				\
	graphic-context.c               \
	graphic-context.h               \
	graphic-type.c                  \
	graphic-type.h			\
	graphic-context.h               \
	wizard.h

INCLUDES = 						\
	-I$(top_srcdir)/libglade			\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
        -DGNUMERIC_ICONSDIR=\""$(iconsdir)"\"           \
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	-I../../					\
	-I../../src					\
	$(GNOME_INCLUDEDIR)

EXTRA_DIST = $(glade_DATA) $(glade_msgs)



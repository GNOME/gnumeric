#
# Gnumeric's Graphics Wizard Makefile
#
# Author: Miguel de Icaza (miguel@kernel.org)
#
# If you make this a shared module, remember to set
# the WIZARD_PLUGIN variable
#

if BONOBO
gnumeric_version="$(VERSION)-bonobo"
else
gnumeric_version="$(VERSION)"
endif
gnumeric_libdir  = $(libdir)/gnumeric/$(gnumeric_version)
gnumeric_datadir = $(datadir)/gnumeric/$(gnumeric_version)
wizarddir = $(gnumeric_libdir)/wizard
gladedir  = $(gnumeric_datadir)/glade

iconsdir  = $(datadir)/pixmaps/gnumeric

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
        -DGNUMERIC_ICONSDIR=\""$(iconsdir)"\"           \
	-I$(includedir)				 	\
	-I$(top_srcdir)/src				\
	-I$(srcdir)/					\
	-I$(top_srcdir)/				\
	$(GNOME_INCLUDEDIR)

EXTRA_DIST = $(glade_DATA) $(glade_msgs)



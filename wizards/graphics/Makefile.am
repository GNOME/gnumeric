#
# Gnumeric's Graphics Wizard Makefile
#
# Author: Miguel de Icaza (miguel@kernel.org)
#

noinst_PROGRAMS = graphics-wizard

graphics_wizard_SOURCES =		\
	chooser.c
	main.c

graphics_wizard_LDADD =			\
	$(GNOME_LIBDIR) 		\
	$(GNOMEUI_LIBS)			\
	$(BONOBO_LIBS)			\
	$(INTLLIBS)

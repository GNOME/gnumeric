#
# Keep this in sync with ../../icons/Makefile.am
#
iconsdir = $(datadir)/pixmaps/gnumeric

INCLUDES = 						\
	-I$(srcdir)/..					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DGNUMERIC_VERSION=\""$(VERSION)"\"		\
        -DGNUMERIC_GLADEDIR=\""$(gladedir)"\"           \
	-DGNUMERIC_ICONDIR=\""$(iconsdir)"\"		\
	-I$(includedir)				 	\
	$(EXTRA_GNOME_CFLAGS)				\
	$(GUILE_INCS)

noinst_LIBRARIES = libdialogs.a

libdialogs_a_SOURCES =				\
	dialog-analysis-tools.c			\
	dialog-cell-comment.c			\
	dialog-cell-format.c			\
	dialog-cell-sort.c			\
	dialog-function-select.c		\
	dialog-function-wizard.c		\
	dialog-goal-seek.c			\
	dialog-goto-cell.c			\
	dialog-insert-cells.c			\
	dialog-define-names.c			\
	dialog-delete-cells.c			\
	dialog-paste-special.c			\
	dialog-printer-setup.c			\
	dialog-summary.c			\
	dialog-solver.c				\
	dialog-zoom.c				\
	dialogs.h

gladedir   = $(datadir)/gnumeric/glade
glade_DATA = 				\
	font-sel.glade			\
	hf-config.glade			\
	import.glade			\
	print.glade 			\
	summary.glade			\
	names.glade

glade_msgs = 				\
	font-sel.glade.h		\
	hf-config.glade.h		\
	import.glade.h			\
	print.glade.h			\
	names.glade.h			\
	summary.glade.h

EXTRA_DIST = $(glade_DATA) $(glade_msgs)



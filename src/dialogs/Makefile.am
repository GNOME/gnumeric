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
	dialog-printer.c			\
	dialog-printer.h			\
	dialog-printer-setup.c			\
	dialog-simple-input.c			\
	dialog-solver.c				\
	dialog-summary.c			\
	dialog-zoom.c				\
	utils-dialog.c                          \
	dialogs.h				\
	tools.h                                 \
	utils-dialog.h				\
	utils-dialog.c

gladedir   = $(datadir)/gnumeric/glade
glade_DATA = 				\
	col-width.glade			\
	font-sel.glade			\
	hf-config.glade			\
	import.glade			\
	names.glade			\
	print.glade 			\
	row-height.glade		\
	sheet-rename.glade		\
	summary.glade			\
	dialog-zoom.glade		\
	insert-cells.glade		\
	delete-cells.glade

glade_msgs = 				\
	col-width.glade.h		\
	font-sel.glade.h		\
	hf-config.glade.h		\
	import.glade.h			\
	names.glade.h			\
	print.glade.h			\
	row-height.glade.h		\
	sheet-rename.glade.h		\
	summary.glade.h			\
	dialog-zoom.glade.h

EXTRA_DIST = $(glade_DATA) $(glade_msgs)




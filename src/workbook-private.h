#ifndef GNUMERIC_WORKBOOK_PRIVATE_H
#define GNUMERIC_WORKBOOK_PRIVATE_H

#include "workbook.h"
#include <gnome.h>

/*
 * Here we should put all the variables that are internal
 * to the workbook and that must not be accessed but by code
 * that runs the Workbook
 *
 * The reason for this is that pretty much all the code
 * depends on workbook.h, so for internals and to avoid
 * recompilation, we put the code here.
 *
 * This is ending up being a transition object for the eventual move to a
 * seprate Model View and controller.  It holds pieces that will be part of the
 * eventual controller objects.
 */
#ifdef ENABLE_BONOBO
#   include <bonobo.h>
#endif
struct _WorkbookPrivate {
#ifdef ENABLE_BONOBO
	/* The base object for the Workbook */
	BonoboObject bonobo_object;

	/* A BonoboContainer */
	BonoboItemContainer   *bonobo_container;

	BonoboPersistFile *persist_file;
	
	/* A list of EmbeddableGrids exported to the world */
	GList      *workbook_views;

	BonoboUIHandler *uih;
#endif

	gboolean during_destruction;

	GtkWidget  *main_vbox;

	GtkWidget  *table;
	GnomeCanvasItem  *auto_expr_label;

	/*
	 * Toolbars
	 */
	GtkWidget *standard_toolbar;
	GtkWidget *format_toolbar;
	GtkWidget *object_toolbar;

	GtkWidget *font_name_selector;
	GtkWidget *font_size_selector;

	/*
	 * GtkCombo for the zoomer
	 */
	GtkWidget *zoom_entry;

	/*
	 * GtkComboStacks for Undo/Redo
	 */
	GtkWidget *undo_combo, *redo_combo;

	const char *current_font_name;

	/* Edit area */
	GtkWidget *selection_descriptor;	/* A GtkEntry */
	struct {
		GtkEntry *entry;		/* The real edit line */
		GtkEntry *temp_entry;		/* A tmp overlay eg from a guru */
		GtkWidget*guru;
		int       signal_changed;
	} edit_line;
	
	/* While editing these should be visible */
	GtkWidget *ok_button, *cancel_button;

	/* While not editing these should be visible */
	GtkWidget *func_button;

        /* The status bar */
        GnomeAppBar *appbar;

	/*
	 * GUI command context
	 */
	CommandContext *gui_context, *corba_context;

	/*
	 * Auto completion
	 */
	void            *auto_complete;         /* GtkType is (Complete *) */
	gboolean         auto_completing;
	char            *auto_complete_text;

	/* Used to detect if the user has backspaced, so we turn off auto-complete */
	int              auto_max_size;
	 
#ifndef ENABLE_BONOBO
	/* Menu items that get toggled */
	GtkWidget  *menu_item_undo;
	GtkWidget  *menu_item_redo;
	GtkWidget  *menu_item_paste_special;
#endif
};

WorkbookPrivate *workbook_private_new (void);
void             workbook_private_delete (WorkbookPrivate *wbp);

#endif /* GNUMERIC_WORKBOOK_PRIVATE_H */

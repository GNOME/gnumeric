#ifndef GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H

#include "gnumeric.h"
#include "workbook-control-gui.h"

struct _WorkbookControlGUI {
	WorkbookControl	wb_control;

	/* FIXME : This is a mess, reorder to be more logical */
#ifdef ENABLE_BONOBO
	BonoboUIComponent *uic;
	GtkWidget *progress_bar;
#else
        /* The status bar */
        GnomeAppBar *appbar;

	/* Menu items that get toggled */
	GtkWidget  *menu_item_undo;
	GtkWidget  *menu_item_redo;
	GtkWidget  *menu_item_paste_special;

	/* Toolbars */
	GtkWidget *standard_toolbar;
	GtkWidget *format_toolbar;
	GtkWidget *object_toolbar;
#endif

	GtkWindow *toplevel;
	GtkNotebook *notebook;

	/* Combos */
	GtkWidget *font_name_selector;
	GtkWidget *font_size_selector;
	GtkWidget *zoom_entry;

	/* ComboStacks */
	GtkWidget *undo_combo, *redo_combo;

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

	gboolean    updating_toolbar;

	/* Auto completion */
	void            *auto_complete;         /* GtkType is (Complete *) */
	gboolean         auto_completing;
	char            *auto_complete_text;
	/* Used to detect if the user has backspaced, so we turn off auto-complete */
	int              auto_max_size;

	/* FIXME : should be in the View */
	/* When editing a cell: the cell (may be NULL) */
	Cell        *editing_cell;
	Sheet       *editing_sheet;
	gboolean     editing;
	gboolean     select_abs_col;
	gboolean     select_abs_row;
	gboolean     select_full_col;
	gboolean     select_full_row;
	gboolean     select_single_cell;

	GtkWidget  *table;
	GnomeCanvasItem  *auto_expr_label;

	/* Edit area */
	GtkWidget *selection_descriptor;	/* A GtkEntry */
	/* Used to pass information to tha async paste handler. */
	PasteTarget *clipboard_paste_callback_data;
};

typedef struct {
	WorkbookControlClass   wb_control_class;
} WorkbookControlGUIClass;

#endif /* GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H */

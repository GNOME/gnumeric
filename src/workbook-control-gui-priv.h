#ifndef GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H
#define GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H

#include "gnumeric.h"
#include "workbook-control-gui.h"
#include "workbook-control-priv.h"
#ifdef ENABLE_BONOBO
#include <bonobo.h>
#endif
#include "file.h"
#include "widgets/gnumeric-expr-entry.h"

struct _WorkbookControlGUI {
	WorkbookControl	wb_control;

	GtkWindow *toplevel;
	GtkNotebook *notebook;

#ifdef ENABLE_BONOBO
	BonoboUIComponent *uic;
	GtkWidget *progress_bar;
#else
        /* The status bar */
        GnomeAppBar *appbar;

	/* Menu items that get enabled/disabled */
	GtkWidget  *menu_item_undo;
	GtkWidget  *menu_item_redo;
	GtkWidget  *menu_item_paste_special;
	GtkWidget  *menu_item_insert_rows;
	GtkWidget  *menu_item_insert_cols;
	GtkWidget  *menu_item_insert_cells;
	GtkWidget  *menu_item_show_detail;
	GtkWidget  *menu_item_hide_detail;

	GtkWidget  *menu_item_sheet_remove;
	GtkWidget  *menu_item_sheets_edit_reorder;
	GtkWidget  *menu_item_sheets_format_reorder;

	GtkWidget  *menu_item_print_setup;
	GtkWidget  *menu_item_search_replace;
	GtkWidget  *menu_item_define_name;
	
	/* Menu items that get toggled */
	GtkWidget  *menu_item_sheet_display_formulas;
	GtkWidget  *menu_item_sheet_hide_zero;
	GtkWidget  *menu_item_sheet_hide_grid;
	GtkWidget  *menu_item_sheet_hide_col_header;
	GtkWidget  *menu_item_sheet_hide_row_header;

	GtkWidget  *menu_item_sheet_display_outlines;
	GtkWidget  *menu_item_sheet_outline_symbols_below;
	GtkWidget  *menu_item_sheet_outline_symbols_right;
	
	/* Toolbars */
	GtkWidget *standard_toolbar;
	GtkWidget *format_toolbar;
	GtkWidget *object_toolbar;
#endif

	/* Combos */
	GtkWidget *font_name_selector;
	GtkWidget *font_size_selector;
	GtkWidget *zoom_entry;
	GtkWidget *fore_color, *back_color;

	/* ComboStacks */
	GtkWidget *undo_combo, *redo_combo;

	struct {
		GnumericExprEntry *entry; /* The real edit line */
		GnumericExprEntry *temp_entry; /* A tmp overlay eg from a guru */
		GtkWidget*guru;
		int       signal_changed;
	} edit_line;

	/* While editing these should be visible */
	GtkWidget *ok_button, *cancel_button;

	/* While not editing these should be visible */
	GtkWidget *func_button;

	gboolean    updating_ui;
	gint        toolbar_sensitivity_timer;
	gboolean    toolbar_is_sensitive;

	/* Auto completion */
	void            *auto_complete;         /* GtkType is (Complete *) */
	gboolean         auto_completing;
	char            *auto_complete_text;

	/* Used to detect if the user has backspaced, so we turn off auto-complete */
	int              auto_max_size;

	/* When editing a cell: the cell (may be NULL) */
	Cell        *editing_cell;
	Sheet       *editing_sheet;
	gboolean     editing;
	SheetControlGUI *rangesel;

	GtkWidget  *table;
	GtkWidget  *auto_expr_label;
	GtkWidget  *status_text;

	/* Edit area */
	GtkWidget *selection_descriptor;	/* A GtkEntry */

	/* Used to pass information to tha async paste handler. */
	PasteTarget *clipboard_paste_callback_data;

	/* Autosave */
        gboolean   autosave;
        gboolean   autosave_prompt;
        gint       autosave_minutes;
        gint       autosave_timer;

	GnumFileSaver *current_saver;
};

typedef struct {
	WorkbookControlClass   wb_control_class;
} WorkbookControlGUIClass;

#endif /* GNUMERIC_WORKBOOK_CONTROL_GUI_PRIV_H */

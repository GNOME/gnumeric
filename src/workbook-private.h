#ifndef GNUMERIC_WORKBOOK_PRIVATE_H
#define GNUMERIC_WORKBOOK_PRIVATE_H

/*
 * Here we should put all the variables that are internal
 * to the workbook and that must not be accessed but by code
 * that runs the Workbook
 *
 * The reason for this is that pretty much all the code
 * depends on workbook.h, so for internals and to avoid
 * recompilation, we put the code hre
 */
struct _WorkbookPrivate {
	gboolean during_destruction;

	GtkWidget  *table;
	GnomeCanvasItem  *auto_expr_label;

	/*
	 * Toolbars
	 */
	GtkWidget *standard_toolbar;
	GtkWidget *format_toolbar;

	GtkWidget *font_name_selector;
	GtkWidget *font_size_selector;

	/*
	 * GtkCombo for the zoomer
	 */
	GtkWidget *zoom_entry;

	/*
	 * Combos for the foreground, background, and borders
	 */
	GtkWidget *fore_combo, *back_combo, *border_combo;

	/*
	 * GtkComboStacks for Undo/Redo
	 */
	GtkWidget *undo_combo, *redo_combo;

	const char *current_font_name;

	/* Edit area */
	GtkWidget *selection_descriptor;
	GtkWidget *edit_line;

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

#ifdef ENABLE_BONOBO
#else
	/* Menu items that get toggled */
	GtkWidget  *menu_item_undo;
	GtkWidget  *menu_item_redo;
	GtkWidget  *menu_item_paste_special;
#endif
};

#endif /* GNUMERIC_WORKBOOK_PRIVATE_H */

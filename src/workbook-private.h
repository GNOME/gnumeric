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

	GtkWidget  *table;
	GnomeCanvasItem  *auto_expr_label;

	/*
	 * Toolbars
	 */
	GtkWidget *standard_toolbar;
	GtkWidget *format_toolbar;

	/*
	 * GtkOptionMenu for the font selector
	 */
	GtkWidget *option_menu;

	/*
	 * GtkEntry for the size specifier
	 */
	GtkWidget *size_widget;

	/*
	 * GtkCombo for the zoomer
	 */
	GtkWidget *zoom_entry;

	/*
	 * ColorCombos for the foreground and background
	 */
	GtkWidget *fore_combo, *back_combo;

	const char *current_font_name;

	/* Edit area */
	GtkWidget  *ea_status;

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

/*
 * The Gnumeric Sheet widget.
 *
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <string.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"

/* Public colors: shared by all of our items in Gnumeric */
GdkColor gs_white, gs_black, gs_light_gray, gs_dark_gray;

static GnomeCanvasClass *sheet_parent_class;
static void stop_cell_selection (GnumericSheet *gsheet);

static void
gnumeric_sheet_destroy (GtkObject *object)
{
	GnumericSheet *gsheet;

	/* Add shutdown code here */
	gsheet = GNUMERIC_SHEET (object);
	
	if (GTK_OBJECT_CLASS (sheet_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (sheet_parent_class)->destroy)(object);
}

static GnumericSheet *
gnumeric_sheet_create (Sheet *sheet, GtkWidget *entry)
{
	GnumericSheet *gsheet;
	GnomeCanvas   *canvas;
	
	gsheet = gtk_type_new (gnumeric_sheet_get_type ());
	canvas = GNOME_CANVAS (gsheet);

	gsheet->sheet   = sheet;
	gsheet->top_col = 0;
	gsheet->top_row = 0;
	gsheet->entry   = entry;
	
	return gsheet;
}

void
gnumeric_sheet_cursor_set (GnumericSheet *sheet, int col, int row)
{
	g_return_if_fail (GNUMERIC_IS_SHEET (sheet));

	sheet->cursor_col = col;
	sheet->cursor_row = row;
}

void
gnumeric_sheet_set_current_value (GnumericSheet *sheet)
{
	Cell *cell;
	int  col, row;
	
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (sheet));

	col = sheet->cursor_col;
	row = sheet->cursor_row;
	cell = sheet_cell_get (sheet->sheet, col, row);
	if (!cell)
		cell = sheet_cell_new (sheet->sheet, sheet->cursor_col, sheet->cursor_row);
	
	cell_set_text (sheet->sheet, cell, gtk_entry_get_text (GTK_ENTRY (sheet->entry)));
	if (sheet->selection)
		stop_cell_selection (sheet);
	sheet_brute_force_recompute (sheet->sheet);
	sheet_redraw_all (sheet->sheet);
}

void
gnumeric_sheet_accept_pending_output (GnumericSheet *sheet)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (sheet));

	if (!sheet->item_editor)
		return;

	gnumeric_sheet_set_current_value (sheet);
	
	gtk_object_destroy (GTK_OBJECT (sheet->item_editor));
	sheet->item_editor = NULL;
}

void
gnumeric_sheet_load_cell_val (GnumericSheet *gsheet)
{
	Sheet *sheet; 
	Workbook *wb;
	GtkWidget *entry;
	Cell *cell;
	
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	
	sheet = gsheet->sheet;
	wb = sheet->parent_workbook;
	entry = wb->ea_input;

	cell = sheet_cell_get (sheet, gsheet->cursor_col, gsheet->cursor_row);
	if (cell && cell->entered_text){
		gtk_entry_set_text (GTK_ENTRY(entry), cell->entered_text->str);
	} else
		gtk_entry_set_text (GTK_ENTRY(entry), "");
}

/*
 * gnumeric_sheet_set_selection:
 *  @Sheet:     The sheet name
 *  @start_col: The starting column.
 *  @start_row: The starting row
 *  @end_col:   The end column
 *  @end_row:   The end row
 *
 * Set the current selection to cover the inclusive area delimited by
 * start_col, start_row, end_col and end_row.  The actual cursor is
 * placed at start_col, start_row
 */
void
gnumeric_sheet_set_selection (GnumericSheet *sheet, SheetSelection *ss)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (ss != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (sheet));

	gnumeric_sheet_cursor_set (sheet, ss->start_col, ss->start_row);
	item_cursor_set_bounds (sheet->item_cursor,
				ss->start_col, ss->start_row,
				ss->end_col, ss->end_row);
}

static void
start_editing_at_cursor (GnumericSheet *sheet, GtkWidget *entry)
{
	GnomeCanvasItem *item;
	GnomeCanvas *canvas = GNOME_CANVAS (sheet);

	gtk_entry_set_text (GTK_ENTRY(entry), "");
	item = gnome_canvas_item_new (GNOME_CANVAS_GROUP(canvas->root),
				      item_edit_get_type (),
				      "ItemEdit::Sheet",    sheet->sheet,
				      "ItemEdit::Grid",     sheet->item_grid,
				      "ItemEdit::Col",      sheet->cursor_col,
				      "ItemEdit::Row",      sheet->cursor_row,
				      "ItemEdit::GtkEntry", entry,
				      NULL);

	sheet->item_editor = ITEM_EDIT (item);
}

/*
 * move_cursor:
 *   @Sheet:    The sheet where the cursor is located
 *   @col:      The new column for the cursor.
 *   @row:      The new row for the cursor.
 *   @clear_selection: If set, clear the selection before moving
 *   Moves the sheet cursor to a new location, it clears the selection,
 *   accepts any pending output on the editing line and moves the cell
 *   cursor.
 */
static void
move_cursor (GnumericSheet *gsheet, int col, int row, int clear_selection)
{
	ItemCursor *item_cursor = gsheet->item_cursor;

	gnumeric_sheet_accept_pending_output (gsheet);
	gnumeric_sheet_cursor_set (gsheet, col, row);

	if (clear_selection)
		sheet_selection_clear_only (gsheet->sheet);

	gnumeric_sheet_make_cell_visible (gsheet, col, row);

	if (clear_selection)
		sheet_selection_append (gsheet->sheet, col, row);
	
	item_cursor_set_bounds (item_cursor, col, row, col, row);
	gnumeric_sheet_load_cell_val (gsheet);
}

/*
 * move_cursor_horizontal:
 *  @Sheet:  The sheet name
 *  @count:  number of units to move the cursor horizontally
 *
 * Moves the cursor count columns
 */
static void
move_cursor_horizontal (GnumericSheet *sheet, int count)
{
	int new_left;
	
	new_left = sheet->cursor_col + count;

	if (new_left < 0)
		return;
	
	move_cursor (sheet, new_left, sheet->cursor_row, 1);
}

/*
 * move_cursor_vertical:
 *  @Sheet:  The sheet name
 *  @count:  number of units to move the cursor vertically
 *
 * Moves the cursor count rows
 */
static void
move_cursor_vertical (GnumericSheet *sheet, int count)
{
	int new_top;

	new_top = sheet->cursor_row + count;

	if (new_top < 0)
		return;

	move_cursor (sheet, sheet->cursor_col, new_top, 1);
}

static void
move_horizontal_selection (GnumericSheet *gsheet, int count)
{
	sheet_selection_extend_horizontal (gsheet->sheet, count);
}

static void
move_vertical_selection (GnumericSheet *gsheet, int count)
{
	sheet_selection_extend_vertical (gsheet->sheet, count);
}

/*
 * gnumeric_sheet_can_move_cursor
 *  @gsheet:   the object
 *
 * Returns true if the cursor keys should be used to select
 * a cell range (if the cursor is in a spot in the expression
 * where it makes sense to have a cell reference), false if not.
 */
static int
gnumeric_sheet_can_move_cursor (GnumericSheet *gsheet)
{
	GtkEntry *entry = GTK_ENTRY (gsheet->entry);
	int cursor_pos = GTK_EDITABLE (entry)->current_pos;

	if (gsheet->selecting_cell)
		return TRUE;
	
	if (entry->text [0] != '=')
		return FALSE;
	if (cursor_pos == 0)
		return FALSE;
		
	switch (entry->text [cursor_pos-1]){
	case '=': case '-': case '*': case '/': case '^': 
	case '+': case '&': case '(': case '%': case '!':
		return TRUE;
	}
	
	return FALSE;
}

static void
start_cell_selection (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);
	GnomeCanvasGroup *group = GNOME_CANVAS_GROUP (canvas->root);

	g_return_if_fail (gsheet->selecting_cell == FALSE);
	
	gsheet->selecting_cell = TRUE;
	gsheet->selection = ITEM_CURSOR (gnome_canvas_item_new (
		group,
		item_cursor_get_type (),
		"Sheet", gsheet->sheet,
		"Grid",  gsheet->item_grid,
		"Style", ITEM_CURSOR_ANTED, NULL));
	item_cursor_set_bounds (ITEM_CURSOR (gsheet->selection),
				gsheet->item_cursor->start_col,
				gsheet->item_cursor->start_row,
				gsheet->item_cursor->end_col,
				gsheet->item_cursor->end_row);
				
	gsheet->sel_cursor_pos = GTK_EDITABLE (gsheet->entry)->current_pos;
	gsheet->sel_text_len = 0;
}

static void
stop_cell_selection (GnumericSheet *gsheet)
{
	if (!gsheet->selecting_cell)
		return;
	
	gsheet->selecting_cell = FALSE;
	gtk_object_destroy (GTK_OBJECT (gsheet->selection));
	gsheet->selection = NULL;
}

static void
selection_remove_selection_string (GnumericSheet *gsheet)
{
	gtk_editable_delete_text (GTK_EDITABLE (gsheet->entry),
				  gsheet->sel_cursor_pos,
				  gsheet->sel_cursor_pos+gsheet->sel_text_len);
}

static void
selection_insert_selection_string (GnumericSheet *gsheet)
{
	ItemCursor *sel = gsheet->selection;
	char buffer [20];
	int pos;

	/* Get the new selection string */
	strcpy (buffer, cell_name (sel->start_col, sel->start_row));
	if (!(sel->start_col == sel->end_col && sel->start_row == sel->end_row)){
		strcat (buffer, ":");
		strcat (buffer, cell_name (sel->end_col, sel->end_row));
	}
	gsheet->sel_text_len = strlen (buffer);

	pos = gsheet->sel_cursor_pos;
	gtk_editable_insert_text (GTK_EDITABLE (gsheet->entry),
				  buffer, strlen (buffer),
				  &pos);

	/* Set the cursor at the end.  It looks nicer */
	gtk_editable_set_position (GTK_EDITABLE (gsheet->entry),
				   gsheet->sel_cursor_pos +
				   gsheet->sel_text_len);
}

static void
selection_cursor_move_horizontal (GnumericSheet *gsheet, int dir)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);
	
	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);

	ic = gsheet->selection;
	if (dir == -1){
		if (ic->start_col == 0)
			return;
	} else {
		if (ic->end_col + 1 > (SHEET_MAX_COLS-1))
			return;
	}
	
	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				ic->start_col + dir,
				ic->start_row, 
				ic->end_col + dir,
				ic->end_row);
	selection_insert_selection_string (gsheet);
}

static void
selection_cursor_move_vertical (GnumericSheet *gsheet, int dir)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);

	ic = gsheet->selection;
	if (dir == -1){
		if (ic->start_row == 0)
			return;
	} else {
		if (ic->end_row + 1 > (SHEET_MAX_ROWS-1))
			return;
	}

	if (!gsheet->selecting_cell)
		start_cell_selection (gsheet);
	item_cursor_set_bounds (ic,
				ic->start_col, ic->start_row + dir,
				ic->end_col, ic->end_row + dir);
	selection_remove_selection_string (gsheet);
	selection_insert_selection_string (gsheet);
}

static void
selection_expand_horizontal (GnumericSheet *gsheet, int dir)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell){
		selection_cursor_move_vertical (gsheet, dir);
		return;
	}

	ic = gsheet->selection;
	if (dir == -1){
		if (ic->start_col == 0)
			return;
	} else {
		if (ic->end_col == SHEET_MAX_COLS-1)
			return;
	}

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				ic->start_col + (dir == -1 ? dir : 0),
				ic->start_row,
				ic->end_col  +  (dir == 1 ? dir : 0),
				ic->end_row);
	selection_insert_selection_string (gsheet);
}

static void
selection_expand_vertical (GnumericSheet *gsheet, int dir)
{
	ItemCursor *ic;

	g_return_if_fail (dir == -1 || dir == 1);

	if (!gsheet->selecting_cell){
		selection_cursor_move_vertical (gsheet, dir);
		return;
	}

	ic = gsheet->selection;
	
	if (dir == -1){
		if (ic->start_row == 0)
			return;
	} else {
		if (ic->end_row == SHEET_MAX_ROWS-1)
			return;
	}

	selection_remove_selection_string (gsheet);
	item_cursor_set_bounds (ic,
				ic->start_col,
				ic->start_row + (dir == -1 ? dir : 0),
				ic->end_col,
				ic->end_row + (dir == 1 ? dir : 0));	
	selection_insert_selection_string (gsheet);
}

/*
 * key press event handler for the gnumeric sheet
 */
static gint
gnumeric_sheet_key (GtkWidget *widget, GdkEventKey *event)
{
	GnumericSheet *sheet = GNUMERIC_SHEET (widget);
	Workbook *wb = sheet->sheet->parent_workbook;
	void (*movefn_horizontal) (GnumericSheet *, int);
	void (*movefn_vertical)   (GnumericSheet *, int);
	int  cursor_move = gnumeric_sheet_can_move_cursor (sheet);

	if ((event->state & GDK_SHIFT_MASK) != 0){
		if (cursor_move){
			movefn_horizontal = selection_expand_horizontal;
			movefn_vertical   = selection_expand_vertical;
		} else {
			movefn_horizontal = move_horizontal_selection;
			movefn_vertical   = move_vertical_selection;
		}
	} else {
		if (cursor_move){
			movefn_horizontal = selection_cursor_move_horizontal;
			movefn_vertical   = selection_cursor_move_vertical;
		} else {
			movefn_horizontal = move_cursor_horizontal;
			movefn_vertical = move_cursor_vertical;
		}
	}

	/* Ignore a few keys (to avoid the selection cursor to be killed
	 * in some cases
	 */
	if (cursor_move){
		switch (event->keyval){
		case GDK_Shift_L: case GDK_Shift_R:
		case GDK_Alt_L:   case GDK_Alt_R:
		case GDK_Control_L:   case GDK_Control_R:
			return 1;
		}
	}
	
	switch (event->keyval){
	case GDK_Left:
		(*movefn_horizontal)(sheet, -1);
		break;

	case GDK_Right:
		(*movefn_horizontal)(sheet, 1);
		break;

	case GDK_Up:
		(*movefn_vertical)(sheet, -1);
		break;

	case GDK_Down:
		(*movefn_vertical)(sheet, 1);
		break;

	case GDK_Return:
		g_warning ("FIXME: Should move to next cell in selection\n");
		move_cursor (sheet, sheet->cursor_col, sheet->cursor_row, 0);
		break;
		
	case GDK_F2:
		gtk_window_set_focus (GTK_WINDOW (wb->toplevel), wb->ea_input);
		/* fallback */

	default:
		if (!sheet->item_editor){
			if (event->keyval >= 0x20 && event->keyval <= 0xff)
			    start_editing_at_cursor (sheet, wb->ea_input);
		}
		stop_cell_selection (sheet);
		
		/* Forward the keystroke to the input line */
		gtk_widget_event (sheet->entry, (GdkEvent *) event);
		
	}
	return 1;
}

GtkWidget *
gnumeric_sheet_new (Sheet *sheet, ItemBar *colbar, ItemBar *rowbar)
{
	GnomeCanvasItem *item;
	GnumericSheet *gsheet;
	GnomeCanvas   *gsheet_canvas;
	GnomeCanvasGroup *gsheet_group;
	GtkWidget *widget;
	GtkWidget *entry;

	g_return_val_if_fail (sheet  != NULL, NULL);
	g_return_val_if_fail (colbar != NULL, NULL);
	g_return_val_if_fail (rowbar != NULL, NULL);
	g_return_val_if_fail (IS_ITEM_BAR (colbar), NULL);
	g_return_val_if_fail (IS_ITEM_BAR (rowbar), NULL);

	entry = sheet->parent_workbook->ea_input;
	gsheet = gnumeric_sheet_create (sheet, entry);
	gnome_canvas_set_size (GNOME_CANVAS (gsheet), 300, 100);
	/* FIXME: figure out some real size for the canvas scrolling region */
	gnome_canvas_set_scroll_region (GNOME_CANVAS (gsheet), 0, 0, 1000000, 1000000);

	/* handy shortcuts */
	gsheet_canvas = GNOME_CANVAS (gsheet);
	gsheet_group = GNOME_CANVAS_GROUP (gsheet_canvas->root);

	gsheet->colbar = colbar;
	gsheet->rowbar = rowbar;
	
	/* The grid */
	item = gnome_canvas_item_new (gsheet_group,
				      item_grid_get_type (),
				      "ItemGrid::Sheet", sheet,
				      NULL);
	gsheet->item_grid = ITEM_GRID (item);

	/* The cursor */
	item = gnome_canvas_item_new (gsheet_group,
				      item_cursor_get_type (),
				      "ItemCursor::Sheet", sheet,
				      "ItemCursor::Grid", gsheet->item_grid,
				      NULL);
	gsheet->item_cursor = ITEM_CURSOR (item);

	widget = GTK_WIDGET (gsheet);

	return widget;
}

static void
gnumeric_sheet_realize (GtkWidget *widget)
{
	if (GTK_WIDGET_CLASS (sheet_parent_class)->realize)
		(*GTK_WIDGET_CLASS (sheet_parent_class)->realize)(widget);
}

void
gnumeric_sheet_compute_visible_ranges (GnumericSheet *gsheet)
{
	GnomeCanvas   *canvas = GNOME_CANVAS (gsheet);
	int pixels, col, row;

	/* Find out the last visible col and the last full visible column */
	pixels = 0;
	col = gsheet->top_col;
	do {
		ColRowInfo *ci;
		int cb;
		
		ci = sheet_col_get_info (gsheet->sheet, col);
		cb = pixels + ci->pixels;
		
		if (cb == canvas->width){
			gsheet->last_visible_col = col;
			gsheet->last_full_col = col;
		} if (cb > canvas->width){
			gsheet->last_visible_col = col;
			if (col == gsheet->top_col)
				gsheet->last_full_col = gsheet->top_col;
			else
				gsheet->last_full_col = col - 1;
		}
		pixels = cb;
		col++;
	} while (pixels < canvas->width);

	/* Find out the last visible row and the last fully visible row */
	pixels = 0;
	row = gsheet->top_row;
	do {
		ColRowInfo *ri;
		int cb;
		
		ri = sheet_row_get_info (gsheet->sheet, row);
		cb = pixels + ri->pixels;
		
		if (cb == canvas->height){
			gsheet->last_visible_row = row;
			gsheet->last_full_row = row;
		} if (cb > canvas->height){
			gsheet->last_visible_row = row;
			if (col == gsheet->top_row)
				gsheet->last_full_row = gsheet->top_row;
			else
				gsheet->last_full_row = row - 1;
		}
		pixels = cb;
		row++;
	} while (pixels < canvas->height);
}

static int
gnumeric_sheet_set_top_row (GnumericSheet *gsheet, int new_top_row)
{
	GnomeCanvas *rowc = GNOME_CANVAS_ITEM (gsheet->rowbar)->canvas;
	Sheet *sheet = gsheet->sheet;
	int row_distance;
	
	gsheet->top_row = new_top_row;
	row_distance = sheet_row_get_distance (sheet, 0, gsheet->top_row);
	rowc->layout.vadjustment->value = row_distance;
	gtk_signal_emit_by_name (GTK_OBJECT (rowc->layout.vadjustment), "value_changed");
	
	return row_distance;
}

static int
gnumeric_sheet_set_top_col (GnumericSheet *gsheet, int new_top_col)
{
	GnomeCanvas *colc = GNOME_CANVAS_ITEM (gsheet->colbar)->canvas;
	Sheet *sheet = gsheet->sheet;
	int col_distance;

	gsheet->top_col = new_top_col;
	col_distance = sheet_col_get_distance (sheet, 0, gsheet->top_col);
	colc->layout.hadjustment->value = col_distance;
	gtk_signal_emit_by_name (GTK_OBJECT (colc->layout.hadjustment), "value_changed");
	
	return col_distance;
}

void
gnumeric_sheet_make_cell_visible (GnumericSheet *gsheet, int col, int row)
{
	GnomeCanvas *canvas;
	Sheet *sheet;
	int   did_change = 0;
	int   new_top_col, new_top_row;
	int   col_distance, row_distance;
	
	g_return_if_fail (gsheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	g_return_if_fail (col >= 0);
	g_return_if_fail (row >= 0);
	g_return_if_fail (col < SHEET_MAX_COLS);
	g_return_if_fail (row < SHEET_MAX_ROWS);

	sheet = gsheet->sheet;
	canvas = GNOME_CANVAS (gsheet);

	/* Find the new gsheet->top_col */
	if (col < gsheet->top_col){
		new_top_col = col;
	} else if (col > gsheet->last_visible_col){
		ColRowInfo *ci;
		int width = canvas->width;
		int first_col = col;
		int allocated = 0;

		ci = sheet_col_get_info (sheet, col);
		allocated = ci->pixels;
		
		do {
			first_col--;
			ci = sheet_col_get_info (sheet, first_col);
			allocated += ci->pixels;
		} while ((first_col > 0) && (width - allocated > 0));
		
		new_top_col = first_col+1;
	} else
		new_top_col = gsheet->top_col;

	/* Find the new gsheet->top_row */
	if (row < gsheet->top_row){
		new_top_row = row;
	} else if (row > gsheet->last_visible_row){
		ColRowInfo *ri;
		int width = canvas->width;
		int first_row = row;
		int allocated = 0;

		ri = sheet_row_get_info (sheet, row);
		allocated = ri->pixels;
		
		do {
			first_row--;
			ri = sheet_row_get_info (sheet, first_row);
			allocated += ri->pixels;
		} while ((first_row > 0) && (width - allocated > 0));
		
		new_top_row = first_row+1;
	} else
		new_top_row = gsheet->top_row;

	/* Determine if scrolling is required */
	col_distance = GTK_LAYOUT (gsheet)->hadjustment->value;
	row_distance = GTK_LAYOUT (gsheet)->vadjustment->value;
	
	if (gsheet->top_col != new_top_col){
		col_distance = gnumeric_sheet_set_top_col (gsheet, new_top_col);
		did_change = 1;
	}

	if (gsheet->top_row != new_top_row){
		row_distance = gnumeric_sheet_set_top_row (gsheet, new_top_row);
		did_change = 1;
	}

	if (!did_change)
		return;
	
	gnumeric_sheet_compute_visible_ranges (gsheet);

	canvas->layout.hadjustment->value = sheet_col_get_distance (sheet, 0, gsheet->top_col);
	gtk_signal_emit_by_name (GTK_OBJECT (canvas->layout.hadjustment), "value_changed");
}

static void
gnumeric_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	(*GTK_WIDGET_CLASS (sheet_parent_class)->size_allocate)(widget, allocation);

	gnumeric_sheet_compute_visible_ranges (GNUMERIC_SHEET (widget));
}

static void
gnumeric_sheet_class_init (GnumericSheetClass *class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GnomeCanvasClass *canvas_class;

	object_class = (GtkObjectClass *) class;
	widget_class = (GtkWidgetClass *) class;
	canvas_class = (GnomeCanvasClass *) class;
	
	sheet_parent_class = gtk_type_class (gnome_canvas_get_type());

	/* Method override */
	object_class->destroy = gnumeric_sheet_destroy;
	
	widget_class->realize = gnumeric_sheet_realize;
 	widget_class->size_allocate = gnumeric_size_allocate;
	widget_class->key_press_event = gnumeric_sheet_key;
}

static void
gnumeric_sheet_init (GnumericSheet *gsheet)
{
	GnomeCanvas *canvas = GNOME_CANVAS (gsheet);
	
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_FOCUS);
	GTK_WIDGET_SET_FLAGS (canvas, GTK_CAN_DEFAULT);
}

void
gnumeric_sheet_color_alloc (GnomeCanvas *canvas)
{
	static int colors_loaded;
	GdkColormap *colormap;
	
	if (colors_loaded)
		return;

	colormap = gtk_widget_get_colormap (GTK_WIDGET (canvas));
	
	gdk_color_white (colormap, &gs_white);
	gdk_color_black (colormap, &gs_black);
	gnome_canvas_get_color (canvas, "gray60", &gs_light_gray);
	gnome_canvas_get_color (canvas, "gray20", &gs_dark_gray);
	colors_loaded = 1;
}

GtkType
gnumeric_sheet_get_type (void)
{
	static GtkType gnumeric_sheet_type = 0;

	if (!gnumeric_sheet_type){
		GtkTypeInfo gnumeric_sheet_info = {
			"GnumericSheet",
			sizeof (GnumericSheet),
			sizeof (GnumericSheetClass),
			(GtkClassInitFunc) gnumeric_sheet_class_init,
			(GtkObjectInitFunc) gnumeric_sheet_init,
			NULL, /* reserved 1 */
			NULL, /* reserved 2 */
			(GtkClassInitFunc) NULL
		};

		gnumeric_sheet_type = gtk_type_unique (gnome_canvas_get_type (), &gnumeric_sheet_info);
	}

	return gnumeric_sheet_type;
}


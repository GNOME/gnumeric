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
#include "gnumeric.h"
#include "gnumeric-sheet.h"

static GnomeCanvasClass *sheet_parent_class;

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

	gnome_canvas_construct (canvas,
				gtk_widget_get_default_visual (),
				gtk_widget_get_default_colormap ());
	
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
gnumeric_sheet_accept_pending_output (GnumericSheet *sheet)
{
	Cell *cell;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (GNUMERIC_IS_SHEET (sheet));

	if (!sheet->item_editor)
		return;

	cell = sheet_cell_get (sheet->sheet, sheet->cursor_col, sheet->cursor_row);
	if (!cell)
		cell = sheet_cell_new (sheet->sheet, sheet->cursor_col, sheet->cursor_row);
	
	cell_set_text (cell, gtk_entry_get_text (GTK_ENTRY (sheet->entry)));

	gtk_object_destroy (GTK_OBJECT (sheet->item_editor));
	sheet->item_editor = NULL;
}

void
gnumeric_sheet_load_cell_val (GnumericSheet *gsheet)
{
	Sheet *sheet; 
	Workbook *wb;
	GtkWidget *entry;

	g_return_if_fail (GNUMERIC_IS_SHEET (gsheet));
	
	sheet = gsheet->sheet;
	wb = sheet->parent_workbook;
	entry = wb->ea_input;
	
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
 * gnumeric_sheet_move_cursor:
 *   @Sheet:    The sheet where the cursor is located
 *   @col:      The new column for the cursor.
 *   @row:      The new row for the cursor.
 *
 *   Moves the sheet cursor to a new location, it clears the selection,
 *   accepts any pending output on the editing line and moves the cell
 *   cursor.
 */
static void
gnumeric_sheet_move_cursor (GnumericSheet *sheet, int col, int row)
{
	ItemCursor *item_cursor = sheet->item_cursor;

	gnumeric_sheet_accept_pending_output (sheet);
	gnumeric_sheet_cursor_set (sheet, col, row);
	sheet_selection_clear (sheet->sheet);
	item_cursor_set_bounds (item_cursor, col, row, col, row);
	gnumeric_sheet_load_cell_val (sheet);
}

/*
 * gnumeric_sheet_move_cursor_horizontal:
 *  @Sheet:  The sheet name
 *  @count:  number of units to move the cursor horizontally
 *
 * Moves the cursor count columns
 */
static void
gnumeric_sheet_move_cursor_horizontal (GnumericSheet *sheet, int count)
{
	int new_left;
	
	new_left = sheet->cursor_col + count;

	if (new_left < 0)
		return;
	
	if (new_left < sheet->top_col){
		g_warning ("do scroll\n");
		return;
	}

	if (new_left < 0)
		new_left = 0;

	gnumeric_sheet_move_cursor (sheet, new_left, sheet->cursor_row);
}

/*
 * gnumeric_sheet_move_cursor_vertical:
 *  @Sheet:  The sheet name
 *  @count:  number of units to move the cursor vertically
 *
 * Moves the cursor count rows
 */
static void
gnumeric_sheet_move_cursor_vertical (GnumericSheet *sheet, int count)
{
	int new_top;

	new_top = sheet->cursor_row + count;

	if (new_top < 0)
		return;

	if (new_top < sheet->top_row){
		g_warning ("do scroll\n");
		return;
	}

	gnumeric_sheet_move_cursor (sheet, sheet->cursor_col, new_top);
}

static void
gnumeric_sheet_move_horizontal_selection (GnumericSheet *gsheet, int count)
{
	sheet_selection_extend_horizontal (gsheet->sheet, count);
}

static void
gnumeric_sheet_move_vertical_selection (GnumericSheet *gsheet, int count)
{
	sheet_selection_extend_vertical (gsheet->sheet, count);
}

static gint
gnumeric_sheet_key (GtkWidget *widget, GdkEventKey *event)
{
	GnumericSheet *sheet = GNUMERIC_SHEET (widget);
	Workbook *wb = sheet->sheet->parent_workbook;
	void (*movefn_horizontal)(GnumericSheet *, int);
	void (*movefn_vertical)(GnumericSheet *, int);
	
	if ((event->state & GDK_SHIFT_MASK) != 0){
		movefn_horizontal = gnumeric_sheet_move_horizontal_selection;
		movefn_vertical   = gnumeric_sheet_move_vertical_selection;
	} else {
		movefn_horizontal = gnumeric_sheet_move_cursor_horizontal;
		movefn_vertical = gnumeric_sheet_move_cursor_vertical;
		
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
		gnumeric_sheet_move_cursor (sheet, sheet->cursor_col, sheet->cursor_row);
		break;
		
	case GDK_F2:
		gtk_window_set_focus (GTK_WINDOW (wb->toplevel), wb->ea_input);
		/* fallback */

	default:
		if (!sheet->item_editor){
			if (event->keyval >= 0x20 && event->keyval <= 0xff)
			    start_editing_at_cursor (sheet, wb->ea_input);
		}

		/* Forward the keystroke to the input line */
		gtk_widget_event (sheet->entry, (GdkEvent *) event);
		
	}
	return 1;
}

GtkWidget *
gnumeric_sheet_new (Sheet *sheet)
{
	GnomeCanvasItem *item;
	GnumericSheet *gsheet;
	GnomeCanvas   *gsheet_canvas;
	GnomeCanvasGroup *gsheet_group;
	GtkWidget *widget;
	GtkWidget *entry = sheet->parent_workbook->ea_input;
	
	gsheet = gnumeric_sheet_create (sheet, entry);
	gnome_canvas_set_size (GNOME_CANVAS (gsheet), 300, 100);

	/* handy shortcuts */
	gsheet_canvas = GNOME_CANVAS (gsheet);
	gsheet_group = GNOME_CANVAS_GROUP (gsheet_canvas->root);
	
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

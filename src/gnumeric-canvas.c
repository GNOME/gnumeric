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

/* Signals emited by the Gnumeric Sheet widget */
enum {
	LAST_SIGNAL
};

static guint sheet_signals [LAST_SIGNAL] = { 0 };

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
	g_return_if_fail (GNUMERIC_IS_SHEET (sheet));

	sheet_cell_new_with_text (sheet->sheet, sheet->cursor_col, sheet->cursor_row,
				  gtk_entry_get_text (GTK_ENTRY (sheet->entry)));
	
	/* Destroy the object */
	if (sheet->item_editor){
		gtk_object_destroy (GTK_OBJECT (sheet->item_editor));
		sheet->item_editor = NULL;
	}
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

static void
gnumeric_sheet_move_cursor_horizontal (GnumericSheet *sheet, int count)
{
	ItemCursor *item_cursor = sheet->item_cursor;
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

	gnumeric_sheet_accept_pending_output (sheet);
	gnumeric_sheet_cursor_set (sheet, new_left, sheet->cursor_row);

	item_cursor_set_bounds (item_cursor,
				new_left, item_cursor->start_row,
				new_left, item_cursor->start_row);
	gnumeric_sheet_load_cell_val (sheet);
}

static void
gnumeric_sheet_move_cursor_vertical (GnumericSheet *sheet, int count)
{
	ItemCursor *item_cursor = sheet->item_cursor;
	int new_top;

	new_top = sheet->cursor_row + count;

	if (new_top < 0)
		return;

	if (new_top < sheet->top_row){
		g_warning ("do scroll\n");
		return;
	}

	gnumeric_sheet_accept_pending_output (sheet);
	gnumeric_sheet_cursor_set (sheet, sheet->cursor_col, new_top);
	item_cursor_set_bounds (item_cursor,
				item_cursor->start_col, new_top,
				item_cursor->start_col, new_top);
	gnumeric_sheet_load_cell_val (sheet);
}

void
gnumeric_sheet_set_selection (GnumericSheet *sheet, int start_col, int start_row, int end_col, int end_row)
{
	g_return_if_fail (sheet != NULL);
	g_return_if_fail (start_row <= end_row);
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (GNUMERIC_IS_SHEET (sheet));

	gnumeric_sheet_cursor_set (sheet, start_col, start_row);
	item_cursor_set_bounds (sheet->item_cursor,
				start_col, start_row,
				end_col, end_row);
}

static void
start_editing_at_cursor (GnumericSheet *sheet, GtkWidget *entry)
{
	GnomeCanvasItem *item;
	GnomeCanvas *canvas = GNOME_CANVAS (sheet);

	item = gnome_canvas_item_new (canvas,
				      GNOME_CANVAS_GROUP(canvas->root),
				      item_edit_get_type (),
				      "ItemEdit::Sheet",    sheet->sheet,
				      "ItemEdit::Grid",     sheet->item_grid,
				      "ItemEdit::Col",      sheet->cursor_col,
				      "ItemEdit::Row",      sheet->cursor_row,
				      "ItemEdit::GtkEntry", entry,
				      NULL);

	sheet->item_editor = ITEM_EDIT (item);
}

static gint
gnumeric_sheet_key (GtkWidget *widget, GdkEventKey *event)
{
	GnumericSheet *sheet = GNUMERIC_SHEET (widget);
	Workbook *wb = sheet->sheet->parent_workbook;

	switch (event->keyval){
	case GDK_Left:
		gnumeric_sheet_move_cursor_horizontal (sheet, -1);
		break;

	case GDK_Right:
		gnumeric_sheet_move_cursor_horizontal (sheet, 1);
		break;

	case GDK_Up:
		gnumeric_sheet_move_cursor_vertical (sheet, -1);
		break;

	case GDK_Down:
		gnumeric_sheet_move_cursor_vertical (sheet, 1);
		break;

	case GDK_F2:
		gtk_window_set_focus (GTK_WINDOW (wb->toplevel), wb->ea_input);
		/* fallback */
		
	default:
		if (!sheet->item_editor)
			start_editing_at_cursor (sheet, wb->ea_input);

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
	item = gnome_canvas_item_new (gsheet_canvas, gsheet_group,
				      item_grid_get_type (),
				      "ItemGrid::Sheet", sheet,
				      NULL);
	gsheet->item_grid = ITEM_GRID (item);

	/* The cursor */
	item = gnome_canvas_item_new (gsheet_canvas, gsheet_group,
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

static void
gnumeric_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (widget);
	GnomeCanvas   *canvas = GNOME_CANVAS (widget);
	int pixels, col, row;

	(*GTK_WIDGET_CLASS (sheet_parent_class)->size_allocate)(widget, allocation);

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
		} if (cb > canvas->width){
			gsheet->last_visible_row = row;
			if (col == gsheet->top_row)
				gsheet->last_full_row = gsheet->top_row;
			else
				gsheet->last_full_row = row - 1;
		}
		pixels = cb;
		row++;
	} while (pixels < canvas->width);

	printf ("COLS: %d %d %d\n",
		gsheet->top_col,
		gsheet->last_full_row,
		gsheet->last_visible_col);
		
	printf ("ROWS: %d %d %d\n",
		gsheet->top_row,
		gsheet->last_full_row,
		gsheet->last_visible_row);
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
/* 	widget_class->size_allocate = gnumeric_size_allocate; */
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

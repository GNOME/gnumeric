/*
 * item-edit.c : Edit facilities for worksheets.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 * (C) 1999, 2000 Miguel de Icaza
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "item-edit.h"
#include "application.h"

#define CURSOR_LEN 4
static GnomeCanvasItem *item_edit_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET,		/* The Sheet * argument */
	ARG_ITEM_GRID,		/* The ItemGrid * argument */
	ARG_GTK_ENTRY,		/* The GtkEntry * argument */
	ARG_COL,		/* The column where this should edit */
	ARG_ROW,		/* The row where this should edit */
};

/*
 * Returns the cordinates for the editor bounding box
 */
static void
item_edit_get_pixel_coords (ItemEdit *item_edit, int *x, int *y, int *w, int *h)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);
	GnumericSheet   *gsheet = GNUMERIC_SHEET (item->canvas);
	Sheet *sheet = item_edit->sheet;

	*x = gsheet->col_offset.first +
	    sheet_col_get_distance_pixels (sheet, gsheet->col.first, item_edit->col);
	*y = gsheet->row_offset.first +
	    sheet_row_get_distance_pixels (sheet, gsheet->row.first, item_edit->row);

	*w = sheet_col_get_distance_pixels (sheet, item_edit->col,
				     item_edit->col + item_edit->col_span);
	*h = sheet_row_get_distance_pixels (sheet, item_edit->row,
				     item_edit->row + item_edit->row_span);
}

static void
item_edit_draw (GnomeCanvasItem *item, GdkDrawable *drawable,
		int x, int y, int width, int height)
{
	GtkWidget *canvas = GTK_WIDGET (item->canvas);
	ItemEdit *item_edit = ITEM_EDIT (item);
	StyleFont *style_font;
	GdkFont  *font;
	ColRowInfo *ci;
	int xd, yd, wd, hd, dx, dy;
	char *text;
	int  cursor_pos, text_len, first_part_len, total_len;
	double const res = MIN(application_display_dpi_get (FALSE),
			       application_display_dpi_get (TRUE)) / 72.;

	style_font = mstyle_get_font (item_edit->mstyle,
				      item_edit->sheet->last_zoom_factor_used * res);

	font = style_font_gdk_font (style_font);
	
	text = gtk_entry_get_text (GTK_ENTRY (item_edit->editor));
	text_len = strlen (text);
	cursor_pos = GTK_EDITABLE (item_edit->editor)->current_pos;

	total_len = gdk_text_width (font, text, text_len);

	ci = sheet_col_get_info (item_edit->sheet, item_edit->col);
	total_len += ci->margin_a + ci->margin_b + CURSOR_LEN;
	
	/* Adjust the col_span if necesary */
	do {
		item_edit_get_pixel_coords (item_edit, &xd, &yd, &wd, &hd);
		
		/* FIXME : This is a temporary fix.
		 * We need a multi line editor for this case and others.
		 */
		if (item_edit->col + item_edit->col_span >= SHEET_MAX_COLS)
			break;
		if (total_len >= wd)
			item_edit->col_span++;
	} while (total_len >= wd);

	dx = xd - x;
	dy = yd - y;

	/* Do the drawing */
	gdk_draw_rectangle (drawable, canvas->style->white_gc, TRUE,
			    dx + 1, dy + 1, wd - 1, hd - 1);

	/* The margin, plus the pixel consumed by the border */
	dx += ci->margin_a + 1;

	first_part_len = gdk_text_width (font, text, cursor_pos);

	/*
	 * FIXME:
	 * The -1 from "font->descent - 1" should actually be
	 * cell->row->margin_b
	 */

	gdk_draw_text (drawable, font, canvas->style->black_gc,
		       dx, dy + hd - font->descent, text, cursor_pos);
	gdk_draw_line (drawable, canvas->style->black_gc,
		       first_part_len + dx,
		       dy + hd - font->descent,
		       first_part_len + dx,
		       dy + hd - (font->ascent + font->descent));
	gdk_draw_text (drawable, font, canvas->style->black_gc,
		       dx + first_part_len,
		       dy + hd - font->descent,
		       text + cursor_pos, text_len - cursor_pos);
	style_font_unref (style_font);
}

static double
item_edit_point (GnomeCanvasItem *item, double c_x, double c_y, int cx, int cy,
		   GnomeCanvasItem **actual_item)
{
	int x, y, w, h;
	
	item_edit_get_pixel_coords (ITEM_EDIT (item), &x, &y, &w, &h);

	*actual_item = NULL;
	if ((cx < x) || (cy < y) || (cx > x+w) || (cy > y+w))
		return 10000.0;
	
	*actual_item = item;
	return 0.0;
}

static void
item_edit_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_cursor_translate %g, %g\n", dx, dy);
}

static int
item_edit_event (GnomeCanvasItem *item, GdkEvent *event)
{
	return 0;
}

static void
item_edit_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	ItemEdit *item_edit = ITEM_EDIT (item);
	int x, y, w, h;

	if (GNOME_CANVAS_ITEM_CLASS (item_edit_parent_class)->update)
		(*GNOME_CANVAS_ITEM_CLASS(item_edit_parent_class)->update)(item, affine, clip_path, flags);
	
	item_edit_get_pixel_coords (item_edit, &x, &y, &w, &h);
	item->x1 = x;
	item->y1 = y;
	item->x2 = x + w;
	item->y2 = y + h;

	gnome_canvas_group_child_bounds (
		GNOME_CANVAS_GROUP (item->parent), item);

	if (item_edit->mstyle)
		mstyle_unref (item_edit->mstyle);
	item_edit->mstyle = sheet_style_compute (item_edit->sheet,
						 item_edit->col, item_edit->row);
}

/*
 * Instance initialization
 */
static void
item_edit_init (ItemEdit *item_edit)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_edit);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 1;
	item->y2 = 1;

	item_edit->col_span = 1;
	item_edit->row_span = 1;

	/* Set invalid values so that we know when we have been fully initialized */
	item_edit->sheet = 0;
	item_edit->col = -1;
	item_edit->row = -1;

	item_edit->mstyle = NULL;
}

static void
queue_sync (ItemEdit *item_edit)
{
	GnomeCanvas *canvas = GNOME_CANVAS_ITEM (item_edit)->canvas;
	int x, y, w, h;

	item_edit_get_pixel_coords (item_edit, &x, &y, &w, &h);
	
	gnome_canvas_request_redraw (canvas, x, y, x+w, y+h);
}

static void
entry_changed (GtkEntry *entry, void *data)
{
	queue_sync (ITEM_EDIT (data));
}

static void
item_edit_destroy (GtkObject *o)
{
	ItemEdit *item_edit = ITEM_EDIT (o);
	int x, y, w, h;

	/* Repaint the area where we had edited */
	mstyle_unref (item_edit->mstyle);
	item_edit_get_pixel_coords (item_edit, &x, &y, &w, &h);
	gnome_canvas_request_redraw (GNOME_CANVAS_ITEM (item_edit)->canvas, x, y, x+w, y+h);
	
	gtk_signal_disconnect (GTK_OBJECT (item_edit->editor), item_edit->signal);
	gtk_signal_disconnect (GTK_OBJECT (item_edit->editor), item_edit->signal2);
	
	if (GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_edit_parent_class)->destroy)(o);
}

static int
entry_event (GtkEntry *entry, GdkEvent *event, ItemEdit *item_edit)
{
	switch (event->type) {
	case GDK_KEY_PRESS:
	case GDK_KEY_RELEASE:
	case GDK_BUTTON_PRESS:
		queue_sync (item_edit);

	default:
		break;
	}
	return FALSE;
}

static void
item_edit_set_editor (ItemEdit *item_edit, void *data)
{
	item_edit->editor = GTK_WIDGET (data);
	item_edit->signal = gtk_signal_connect (
		GTK_OBJECT (item_edit->editor), "changed",
		GTK_SIGNAL_FUNC (entry_changed), item_edit);
	item_edit->signal2 = gtk_signal_connect_after (
		GTK_OBJECT (item_edit->editor), "event",
		GTK_SIGNAL_FUNC (entry_event), item_edit);
}
		      
static void
item_edit_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemEdit *item_edit;

	item = GNOME_CANVAS_ITEM (o);
	item_edit = ITEM_EDIT (o);

	switch (arg_id){
	case ARG_SHEET:
		item_edit->sheet = (Sheet *) GTK_VALUE_POINTER (*arg);
		break;
	case ARG_ITEM_GRID:
		item_edit->item_grid = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_GTK_ENTRY:
		item_edit_set_editor (item_edit, GTK_VALUE_POINTER (*arg));
		break;
	case ARG_COL:
		item_edit->col = GTK_VALUE_INT (*arg);
		break;
	case ARG_ROW:
		item_edit->row = GTK_VALUE_INT (*arg);
		break;
	}

	/* Once all of our parameters have been set, do the reconfiguration */
	if (item_edit->sheet && item_edit->col != -1 && item_edit->row != -1)
		item_edit_update (item, NULL, NULL, 0);
}

/*
 * ItemEdit class initialization
 */
static void
item_edit_class_init (ItemEditClass *item_edit_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_edit_parent_class = gtk_type_class (gnome_canvas_item_get_type ());
	
	object_class = (GtkObjectClass *) item_edit_class;
	item_class = (GnomeCanvasItemClass *) item_edit_class;

	gtk_object_add_arg_type ("ItemEdit::Sheet", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_SHEET);
	gtk_object_add_arg_type ("ItemEdit::Grid", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_ITEM_GRID);
	gtk_object_add_arg_type ("ItemEdit::GtkEntry", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GTK_ENTRY);
	gtk_object_add_arg_type ("ItemEdit::Col", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_COL);
	gtk_object_add_arg_type ("ItemEdit::Row", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_ROW);
	
	object_class->set_arg = item_edit_set_arg;
	object_class->destroy = item_edit_destroy;
	
	/* GnomeCanvasItem method overrides */
	item_class->update      = item_edit_update;
	item_class->draw        = item_edit_draw;
	item_class->point       = item_edit_point;
	item_class->translate   = item_edit_translate;
	item_class->event       = item_edit_event;
}

GtkType
item_edit_get_type (void)
{
	static GtkType item_edit_type = 0;

	if (!item_edit_type) {
		GtkTypeInfo item_edit_info = {
			"ItemEdit",
			sizeof (ItemEdit),
			sizeof (ItemEditClass),
			(GtkClassInitFunc) item_edit_class_init,
			(GtkObjectInitFunc) item_edit_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_edit_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_edit_info);
	}

	return item_edit_type;
}

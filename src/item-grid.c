/*
 * The Grid Gnome Canvas Item: Implements the grid and
 * spreadsheet information display.
 *
 * (C) 1998 The Free Software Foundation
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "item-debug.h"

static GnomeCanvasItem *item_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET,
};

static void
item_grid_destroy (GtkObject *object)
{
	ItemGrid *grid;

	grid = ITEM_GRID (object);

	if (GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)(object);
}

static GdkColor
color_alloc (GnomeCanvas *canvas, char *color_name)
{
	GdkColor color;

	color.pixel = 0;
	
	gnome_canvas_get_color (canvas, color_name, &color);
	return color;
}

static void
item_grid_realize (GnomeCanvasItem *item)
{
	ItemGrid *item_grid;
	GdkWindow *window;
	GdkGC *gc;
	
	item_grid = ITEM_GRID (item);
	window = GTK_WIDGET (item->canvas)->window;
	
	/* Configure the default grid gc */
	item_grid->grid_gc = gc = gdk_gc_new (window);
	item_grid->fill_gc = gdk_gc_new (window);
	item_grid->gc = gdk_gc_new (window);
	
	gdk_gc_set_line_attributes (gc, 1, GDK_LINE_SOLID,
				    GDK_CAP_PROJECTING, GDK_JOIN_MITER);

	/* Allocate the default colors */
	item_grid->background = color_alloc (item->canvas, "white");
	item_grid->grid_color = color_alloc (item->canvas, "gray60");
	item_grid->default_color = color_alloc (item->canvas, "black");
	
	gdk_gc_set_foreground (gc, &item_grid->grid_color);
	gdk_gc_set_background (gc, &item_grid->background);
	
	gdk_gc_set_foreground (item_grid->fill_gc, &item_grid->background);
	gdk_gc_set_background (item_grid->fill_gc, &item_grid->grid_color);
}

static void
item_grid_unrealize (GnomeCanvasItem *item)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	
	gdk_gc_unref (item_grid->grid_gc);
	item_grid->grid_gc = 0;
}

static void
item_grid_reconfigure (GnomeCanvasItem *item)
{
	item->x1 = 0;
	item->y1 = 0;
	item->x2 = INT_MAX;
	item->y2 = INT_MAX;
	
	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

/*
 * find_col: return the column where x belongs to
 */
static int
find_col (ItemGrid *item_grid, int x, int *col_origin)
{
	int col   = item_grid->left_col;
	int pixel = item_grid->left_offset;

	do {
		ColRowInfo *ci = sheet_col_get_info (item_grid->sheet, col);
		
		if (x >= pixel && x <= pixel + ci->pixels){
			if (col_origin)
				*col_origin = pixel;
			return col;
		}
		col++;
		pixel += ci->pixels;
	} while (1);
}

/*
 * find_row: return the row where y belongs to
 */
static int
find_row (ItemGrid *item_grid, int y, int *row_origin)
{
	int row   = item_grid->top_row;
	int pixel = item_grid->top_offset;

	do {
		ColRowInfo *ri = sheet_row_get_info (item_grid->sheet, row);
		
		if (y >= pixel && y <= pixel + ri->pixels){
			if (row_origin)
				*row_origin = pixel;
			return row;
		}
		row++;
		pixel += ri->pixels;
	} while (1);
}

typedef struct {
	ItemGrid      *item_grid;
	GdkDrawable   *drawable;
	GnumericSheet *gsheet;
	int   	      x_paint;		/* the offsets */
	int   	      y_paint;
} paint_data;

/*
 * Draw a cell.  It gets pixel level coordinates
 */
static void
item_grid_draw_cell (GdkDrawable *drawable, ItemGrid *item_grid,
		     int x1, int y1, int width, int height, int col, int row)
{
	GnumericSheet *gsheet = GNUMERIC_SHEET (item_grid->sheet->sheet_view);
	GnomeCanvas   *canvas = GNOME_CANVAS_ITEM (item_grid)->canvas;
	GdkFont *font;
	GdkGC *gc       = item_grid->gc;
	GdkGC *white_gc = GTK_WIDGET (canvas)->style->white_gc;
	Sheet *sheet = item_grid->sheet;
	Cell  *cell, *clip_left, *clip_right;
	Style *style;
	int   x_offset, y_offset, text_base, pixels;
	GdkRectangle rect;
	
#if 0
	item_debug_cross (drawable, gc, x1, y1, x1+width, y1+height);
#endif
	
	/* If cell is selected, draw selection */
	if (sheet_selection_is_cell_selected (sheet, col, row)){
		GdkGC *black_gc = GTK_WIDGET (canvas)->style->black_gc;
		
		if (!(gsheet->cursor_col == col && gsheet->cursor_row == row))
			gdk_draw_rectangle (drawable, black_gc, TRUE,
					    x1+1, y1+1, width - 2, height - 2);
	}

	cell = sheet_cell_get (sheet, col, row);
	if (!cell)
		return;

	/* The offsets where we start drawing the text */
	x_offset = y_offset = 0;

	/* True if we have a sibling cell in the direction where we paint */
	clip_left  = NULL;
	clip_right = NULL;
	
	style = cell->style;
	font = style->font->font;

	/* Code to test the different alignements, hardcoded for now */
	switch (col){
	case 0:
		style->halign = HALIGN_GENERAL;
		break;
	case 1:
		style->halign = HALIGN_LEFT;
		break;
	case 2:
		style->halign = HALIGN_RIGHT;
		break;
	case 3:
		style->halign = HALIGN_CENTER;
		break;
	case 4:
		style->halign = HALIGN_FILL;
		break;
	}
	switch (style->halign){
	case HALIGN_GENERAL:
		if (col < SHEET_MAX_COLS-1)
			clip_right = sheet_cell_get (sheet, col+1, row);
		x_offset = cell->col->margin_a;
		break;

	case HALIGN_LEFT:
		if (col < SHEET_MAX_COLS-1)
			clip_right = sheet_cell_get (sheet, col+1, row);
		x_offset = cell->col->margin_a;
		break;
		
	case HALIGN_RIGHT:
		if (col > 0)
			clip_left = sheet_cell_get (sheet, col-1, row);
		x_offset = cell->col->pixels - (cell->width - cell->col->margin_b);
		break;
		
	case HALIGN_CENTER:
		if (col > 0)
			clip_left = sheet_cell_get (sheet, col-1, row);
		if (col < SHEET_MAX_COLS-1)
			clip_right = sheet_cell_get (sheet, col-1, row);
		x_offset = (cell->width - cell->col->pixels)/2;
		break;
		
	case HALIGN_FILL:
		if (col < SHEET_MAX_COLS-1)
			clip_right = sheet_cell_get (sheet, col-1, row);
		clip_left = clip_right = (Cell *) TRUE;
		x_offset = cell->col->margin_a;
		break;
			
	case HALIGN_JUSTIFY:
		g_warning ("No horizontal justification supported yet\n");
		break;
	}

	if (cell->flags & CELL_COLOR_IS_SET){
		gdk_gc_set_foreground (gc, &cell->color);
	} else 
		gdk_gc_set_foreground (gc, &item_grid->default_color);

	text_base = y1 + cell->row->pixels - cell->row->margin_b - font->descent + 1;

	gdk_gc_set_foreground (gc, &item_grid->default_color);

	if (clip_left || clip_right){
		rect.x = x1;
		rect.y = y1;
		rect.width = width;
		rect.height = height;
		gdk_gc_set_clip_rectangle (gc, &rect);
	} else
		gdk_gc_set_clip_rectangle (gc, NULL);

	gdk_draw_rectangle (drawable, white_gc, TRUE,
			    x1 + cell->col->margin_a,
			    y1 + cell->row->margin_a,
			    cell->width - (cell->col->margin_a + cell->col->margin_b),
			    height - (cell->row->margin_a + cell->row->margin_b));

	pixels = 0;
	do {
		gdk_draw_text (drawable, font, gc,
			       x1 + x_offset,
			       text_base + y_offset,
			       cell->text, strlen (cell->text));
		pixels += cell->width;
	} while (style->halign == HALIGN_FILL && pixels < cell->col->pixels);
}

static void
item_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	Sheet *sheet = item_grid->sheet;
	GdkGC *grid_gc = item_grid->grid_gc;
	int end_x, end_y;
	int paint_col, paint_row, max_paint_col, max_paint_row;
	int col, row;
	int x_paint, y_paint;
	int diff_x, diff_y;
	paint_data pd;

	if (x < 0){
		g_warning ("x < 0\n");
		return;
	}

	if (y < 0){
		g_warning ("y < 0\n");
		return;
	}
	
	max_paint_col = max_paint_row = 0;
	paint_col = find_col (item_grid, x, &x_paint);
	paint_row = find_row (item_grid, y, &y_paint);

	col = paint_col;

	diff_x = x - x_paint;
	end_x = width + diff_x;
	diff_y = y - y_paint;
	end_y = height + diff_y;

	/* 1. The default background */
	gdk_draw_rectangle (drawable, item_grid->fill_gc, TRUE,
			    0, 0, width, height);
	
	/* 2. the grids */
	for (x_paint = -diff_x; x_paint < end_x; col++){
		ColRowInfo *ci;

		ci = sheet_col_get_info (sheet, col);
		g_assert (ci->pixels != 0);

		gdk_draw_line (drawable, grid_gc, x_paint, 0, x_paint, height);
		
		x_paint += ci->pixels;
		max_paint_col = col;
	}

	row = paint_row;
	for (y_paint = -diff_y; y_paint < end_y; row++){
		ColRowInfo *ri;

		ri = sheet_row_get_info (sheet, row);
		gdk_draw_line (drawable, grid_gc, 0, y_paint, width, y_paint);
		y_paint += ri->pixels;
		max_paint_row = row;
	}

	/* The cells */
	pd.x_paint   = -diff_x;
	pd.y_paint   = -diff_y;
	pd.drawable  = drawable;
	pd.item_grid = item_grid;
	pd.gsheet    = GNUMERIC_SHEET (item_grid->sheet->sheet_view);

#if 0
	printf ("Painting the (%d,%d)-(%d,%d) region\n",
		paint_col, paint_row, max_paint_col, max_paint_row);
#endif
	
	col = paint_col;
	for (x_paint = -diff_x; x_paint < end_x; col++){
		ColRowInfo *ci;

		ci = sheet_col_get_info (sheet, col);

		row = paint_row;
		for (y_paint = -diff_y; y_paint < end_y; row++){
			ColRowInfo *ri;

			ri = sheet_row_get_info (sheet, row);
			item_grid_draw_cell (drawable, item_grid,
					     x_paint, y_paint,
					     ci->pixels,
					     ri->pixels,
					     col, row);
			y_paint += ri->pixels;
		}

		x_paint += ci->pixels;
	}
	
#undef DEBUG_EXPOSES
#ifdef DEBUG_EXPOSES
	item_debug_cross (drawable, item_grid->grid_gc, 0, 0, width, height);
#endif
}

static double
item_grid_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		 GnomeCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

static void
item_grid_translate (GnomeCanvasItem *item, double dx, double dy)
{
	printf ("item_grid_translate %g, %g\n", dx, dy);
}

/*
 * Handle the selection
 */
#define convert(c,sx,sy,x,y) gnome_canvas_w2c (c,sx,sy,x,y)

static gint
item_grid_event (GnomeCanvasItem *item, GdkEvent *event)
{
	GnomeCanvas *canvas = item->canvas;
	ItemGrid *item_grid = ITEM_GRID (item);
	Sheet *sheet = item_grid->sheet;
	GnumericSheet *gsheet = GNUMERIC_SHEET (sheet->sheet_view);
	int col, row, x, y;
	int scroll_x, scroll_y;

	switch (event->type){
	case GDK_BUTTON_RELEASE:
		item_grid->selecting = 0;
		gnome_canvas_item_ungrab (item, event->button.time);
		return 1;
		
	case GDK_MOTION_NOTIFY:
		scroll_x = scroll_y = 0;
		if (event->motion.x < 0){
			event->motion.x = 0;
			scroll_x = 1;
		} if (event->motion.y < 0){
			event->motion.y = 0;
			scroll_y = 1;
		}
		convert (canvas, event->motion.x, event->motion.y, &x, &y);
		if (!item_grid->selecting)
			return 1;
		
		col = find_col (item_grid, event->motion.x, NULL);
		row = find_row (item_grid, event->motion.y, NULL);
		sheet_selection_extend_to (sheet, col, row);
		return 1;
		
	case GDK_BUTTON_PRESS:
		convert (canvas, event->button.x, event->button.y, &x, &y);
		col = find_col (item_grid, event->button.x, NULL);
		row = find_row (item_grid, event->button.y, NULL);

		gnumeric_sheet_cursor_set (gsheet, col, row);
		if (!(event->button.state & GDK_SHIFT_MASK))
			sheet_selection_clear (sheet);

		item_grid->selecting = 1;
		sheet_selection_append (sheet, col, row);
		printf ("ItemGrab:%d\n", gnome_canvas_item_grab (item,
					GDK_POINTER_MOTION_MASK |
					GDK_BUTTON_RELEASE_MASK,
					NULL,
					event->button.time));
		return 1;
		
	default:
		return 0;	
	}
}

/*
 * Instance initialization
 */
static void
item_grid_init (ItemGrid *item_grid)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_grid);
	
	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;
	
	item_grid->left_col = 0;
	item_grid->top_row  = 0;
	item_grid->top_offset = 0;
	item_grid->left_offset = 0;
	item_grid->selecting = 0;
}

static void
item_grid_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	ItemGrid *item_grid;

	item = GNOME_CANVAS_ITEM (o);
	item_grid = ITEM_GRID (o);
	
	switch (arg_id){
	case ARG_SHEET:
		item_grid->sheet = GTK_VALUE_POINTER (*arg);
		break;
	}
}

/*
 * ItemGrid class initialization
 */
static void
item_grid_class_init (ItemGridClass *item_grid_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	item_grid_parent_class = gtk_type_class (gnome_canvas_item_get_type());
	
	object_class = (GtkObjectClass *) item_grid_class;
	item_class = (GnomeCanvasItemClass *) item_grid_class;

	gtk_object_add_arg_type ("ItemGrid::Sheet", GTK_TYPE_POINTER, 
				 GTK_ARG_WRITABLE, ARG_SHEET);
	
	object_class->set_arg = item_grid_set_arg;
	object_class->destroy = item_grid_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->realize     = item_grid_realize;
	item_class->unrealize   = item_grid_unrealize;
	item_class->reconfigure = item_grid_reconfigure;
	item_class->draw        = item_grid_draw;
	item_class->point       = item_grid_point;
	item_class->translate   = item_grid_translate;
	item_class->event       = item_grid_event;
}

GtkType
item_grid_get_type (void)
{
	static GtkType item_grid_type = 0;

	if (!item_grid_type) {
		GtkTypeInfo item_grid_info = {
			"ItemGrid",
			sizeof (ItemGrid),
			sizeof (ItemGridClass),
			(GtkClassInitFunc) item_grid_class_init,
			(GtkObjectInitFunc) item_grid_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		item_grid_type = gtk_type_unique (gnome_canvas_item_get_type (), &item_grid_info);
	}

	return item_grid_type;
}

/*
 * The Grid Gnome Canvas Item: Implements the grid and
 * spreadsheet information display.
 *
 * Author:
 *     Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>

#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "item-debug.h"
#include "color.h"
#include "dialogs.h"
#include "cursors.h"

static GnomeCanvasItem *item_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_SHEET_VIEW,
};

static void
item_grid_destroy (GtkObject *object)
{
	ItemGrid *grid;

	grid = ITEM_GRID (object);

	if (GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (item_grid_parent_class)->destroy)(object);
}

static void
item_grid_realize (GnomeCanvasItem *item)
{
	GnomeCanvas *canvas = item->canvas;
	GdkVisual *visual;
	GdkWindow *window;
	ItemGrid *item_grid;
	GdkGC *gc;
	
	if (GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->realize)(item);
	
	item_grid = ITEM_GRID (item);
	window = GTK_WIDGET (item->canvas)->window;
	
	/* Configure the default grid gc */
	item_grid->grid_gc = gc = gdk_gc_new (window);
	item_grid->fill_gc = gdk_gc_new (window);
	item_grid->gc = gdk_gc_new (window);
	
	gdk_gc_set_line_attributes (gc, 1, GDK_LINE_SOLID,
				    GDK_CAP_PROJECTING, GDK_JOIN_MITER);

	/* Allocate the default colors */
	item_grid->background = gs_white;
	item_grid->grid_color = gs_light_gray;
	item_grid->default_color = gs_black;
	
	gdk_gc_set_foreground (gc, &item_grid->grid_color);
	gdk_gc_set_background (gc, &item_grid->background);
	
	gdk_gc_set_foreground (item_grid->fill_gc, &item_grid->background);
	gdk_gc_set_background (item_grid->fill_gc, &item_grid->grid_color);


	/* Find out how we need to draw the selection with the current visual */
	visual = gtk_widget_get_visual (GTK_WIDGET (canvas));

	switch (visual->type){
	case GDK_VISUAL_STATIC_GRAY:
	case GDK_VISUAL_TRUE_COLOR:
	case GDK_VISUAL_STATIC_COLOR:
		item_grid->visual_is_paletted = 0;
		break;

	default:
		item_grid->visual_is_paletted = 1;
	}
}

static void
item_grid_unrealize (GnomeCanvasItem *item)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	
	gdk_gc_unref (item_grid->grid_gc);
	item_grid->grid_gc = 0;
	
	if (GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (item_grid_parent_class)->unrealize)(item);
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
 * item_grid_find_col: return the column where x belongs to
 */
int
item_grid_find_col (ItemGrid *item_grid, int x, int *col_origin)
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
 * item_grid_find_row: return the row where y belongs to
 */
int
item_grid_find_row (ItemGrid *item_grid, int y, int *row_origin)
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

/*
 * Sets the gc appropiately for inverting the color of a region
 * in the ItemGrid.
 *
 * We are mainly interested in getting accurrated black/white
 * inversion.
 *
 * On palleted displays, we play the trick of XORing the value
 * (this will render other colors sometimes randomly, depending on the
 * pallette contents).  On non-palleted displays, we can use the
 * GDK_INVERT operation to invert the pixel value. 
 */
static void
item_grid_invert_gc (ItemGrid *item_grid)
{
	GdkGC *gc;

	gc = item_grid->gc;
	gdk_gc_set_clip_rectangle (gc, NULL);

	if (item_grid->visual_is_paletted){
		gdk_gc_set_function (gc, GDK_XOR);
		gdk_gc_set_foreground (gc, &gs_white);
	} else {
		gdk_gc_set_function (gc, GDK_INVERT);
		gdk_gc_set_foreground (gc, &gs_black);
	}
}

/*
 * Draw a cell.  It gets pixel level coordinates
 *
 * Returns the number of columns used by the cell.
 */
static int
item_grid_draw_cell (GdkDrawable *drawable, ItemGrid *item_grid, Cell *cell, int x1, int y1)
{
	GdkGC       *gc     = item_grid->gc;
	int         count;
	
	/* setup foreground */
	gdk_gc_set_foreground (gc, &item_grid->default_color);
	if (cell->render_color)
		gdk_gc_set_foreground (gc, &cell->render_color->color);
	else {
		if (cell->style->valid_flags & STYLE_FORE_COLOR)
			gdk_gc_set_foreground (gc, &cell->style->fore_color->color);
	}
	
	if (cell->style->valid_flags & STYLE_BACK_COLOR)
		gdk_gc_set_background (gc, &cell->style->back_color->color);


	if ((cell->style->valid_flags & STYLE_PATTERN) && cell->style->pattern){
		GnomeCanvasItem *item = GNOME_CANVAS_ITEM (item_grid);
		int p = cell->style->pattern - 1;
		
		gdk_gc_set_stipple (gc, GNUMERIC_SHEET (item->canvas)->patterns [p]);
		gdk_gc_set_fill (gc, GDK_STIPPLED);
		gdk_draw_rectangle (drawable, gc, TRUE,
				    x1, y1,
				    cell->col->pixels,
				    cell->row->pixels);
		gdk_gc_set_fill (gc, GDK_SOLID);
		gdk_gc_set_stipple (gc, NULL);
	}
	
	count = cell_draw (cell, item_grid->sheet_view, gc, drawable, x1, y1);

	return count;
}

static void
item_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	ItemGrid *item_grid = ITEM_GRID (item);
	Sheet *sheet = item_grid->sheet;
	GdkGC *grid_gc = item_grid->grid_gc;
	Cell  *cell;
	int end_x, end_y;
	int paint_col, paint_row, max_paint_col, max_paint_row;
	int col, row;
	int x_paint, y_paint, real_x;
	int diff_x, diff_y;

	if (x < 0){
		g_warning ("x < 0\n");
		return;
	}

	if (y < 0){
		g_warning ("y < 0\n");
		return;
	}
	
	max_paint_col = max_paint_row = 0;
	paint_col = item_grid_find_col (item_grid, x, &x_paint);
	paint_row = item_grid_find_row (item_grid, y, &y_paint);

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

	gdk_gc_set_function (item_grid->gc, GDK_COPY);

	row = paint_row;
	for (y_paint = -diff_y; y_paint < end_y; row++){
		ColRowInfo *ri;
		
		ri = sheet_row_get_info (sheet, row);
		col = paint_col;

		for (x_paint = -diff_x; x_paint < end_x; col++){
			ColRowInfo *ci;
			
			ci = sheet_col_get_info (sheet, col);
			
			cell = sheet_cell_get (sheet, col, row);

			if (cell){
				item_grid_draw_cell (drawable, item_grid, cell,
						     x_paint, y_paint);
			} else if (ri->pos != -1){
				/*
				 * If there was no cell, and the row has any cell allocated
				 * (indicated by ri->pos != -1)
				 */

				real_x = x_paint;
				cell = row_cell_get_displayed_at (ri, col);

				/*
				 * We found the cell that paints over this
				 * cell, adjust x to point to the beginning
				 * of that cell.
				 */
				if (cell){
					int i, count, end_col;

					/*
					 * Either adjust the left part
					 */
					for (i = cell->col->pos; i < col; i++){
						ColRowInfo *tci;

						tci = sheet_col_get_info (sheet, i);
						real_x -= tci->pixels;
					}

					/*
					 * Or adjust the right part
					 */
					for (i = col; i < cell->col->pos; i++){
						ColRowInfo *tci;

						tci = sheet_col_get_info (
							sheet, i);
						real_x += tci->pixels;
					}

					/*
					 * Draw the cell
					 */
					count = item_grid_draw_cell (
						drawable, item_grid, cell,
						real_x, y_paint);
					/*
					 * Optimization: advance over every
					 * cell we already painted on
					 */
					end_col = cell->col->pos + count;
					
					for (i = col+1; i < end_col; i++, col++){
						ColRowInfo *tci;
						
						tci = sheet_col_get_info (
							sheet, i);

						x_paint += tci->pixels;
					}
				} /* if cell */
			}
			x_paint += ci->pixels;
		}
		y_paint += ri->pixels;
	}

	/* Now invert any selected cells */
	item_grid_invert_gc (item_grid);
	
	row = paint_row;
	for (y_paint = -diff_y; y_paint < end_y; row++){
		ColRowInfo *ri, *ci;
		
		ri = sheet_row_get_info (sheet, row);
		col = paint_col;

		for (x_paint = -diff_x; x_paint < end_x; col++, x_paint += ci->pixels){
			ci = sheet_col_get_info (sheet, col);
			
			if (sheet->cursor_col == col && sheet->cursor_row == row)
				continue;

			if (!sheet_selection_is_cell_selected (sheet, col, row))
				continue;

			gdk_draw_rectangle (drawable, item_grid->gc, TRUE,
					    x_paint+1, y_paint+1,
					    ci->pixels-1, ri->pixels-1);
		}
		y_paint += ri->pixels;
	}
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

static void
context_destroy_menu (GtkWidget *widget)
{
	gtk_widget_destroy (gtk_widget_get_toplevel (widget));
}

static void
context_cut_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	sheet_selection_cut (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_copy_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	sheet_selection_copy (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_paste_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	Sheet *sheet = item_grid->sheet;
	
	sheet_selection_paste (sheet,
			       sheet->cursor_col,
			       sheet->cursor_row,
			       PASTE_DEFAULT);
	context_destroy_menu (widget);
}

static void
context_paste_special_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	Sheet *sheet = item_grid->sheet;
	int flags;

	flags = dialog_paste_special ();
	sheet_selection_paste (sheet,
			       sheet->cursor_col,
			       sheet->cursor_row,
			       flags);
	context_destroy_menu (widget);
}

static void
context_insert_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	dialog_insert_cells (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_delete_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	dialog_delete_cells (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_clear_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	sheet_selection_clear_content (item_grid->sheet);
	context_destroy_menu (widget);
}

static void
context_cell_format_cmd (GtkWidget *widget, ItemGrid *item_grid)
{
	dialog_cell_format (item_grid->sheet);
	context_destroy_menu (widget);
}

typedef enum {
	IG_ALWAYS,
	IG_HAVE_SELECTION,
	IG_SEPARATOR,
} popup_types;

struct {
	char         *name;
	void         (*fn)(GtkWidget *widget, ItemGrid *item_grid);
	popup_types  type;
} item_grid_context_menu [] = {
	{ N_("Cut"),           context_cut_cmd,           IG_ALWAYS         },
	{ N_("Copy"),          context_copy_cmd,          IG_ALWAYS         },
	{ N_("Paste"),         context_paste_cmd,         IG_HAVE_SELECTION },
	{ N_("Paste special"), context_paste_special_cmd, IG_HAVE_SELECTION },
	{ "",                  NULL,                      IG_SEPARATOR      },
	{ N_("Insert"),        context_insert_cmd,        IG_ALWAYS  	    },
	{ N_("Delete"),        context_delete_cmd,        IG_ALWAYS  	    },
	{ N_("Erase content"), context_clear_cmd,         IG_ALWAYS         },
	{ "",                  NULL,                      IG_SEPARATOR      },
	{ N_("Cell format"),   context_cell_format_cmd,   IG_ALWAYS         },
	{ NULL,                NULL,                      0 }
};

static GtkWidget *
create_popup_menu (ItemGrid *item_grid, int include_paste)
{
	GtkWidget *menu, *item;
	int i;

	menu = gtk_menu_new ();
	item = NULL;
	
	for (i = 0; item_grid_context_menu [i].name; i++){
		switch (item_grid_context_menu [i].type){
		case IG_SEPARATOR:
			item = gtk_menu_item_new ();
			break;
			
		case IG_HAVE_SELECTION:
			if (!include_paste)
				continue;
			/* fall down */
			
		case IG_ALWAYS:
			item = gtk_menu_item_new_with_label (
				_(item_grid_context_menu [i].name));
			break;
		default:
			g_warning ("Never reached");
		}

		gtk_signal_connect (
			GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (item_grid_context_menu [i].fn),
			item_grid);

		gtk_widget_show (item);
		gtk_menu_append (GTK_MENU (menu), item);
	}

	return menu;
}	   
		   
static void
item_grid_popup_menu (ItemGrid *item_grid, GdkEvent *event, int col, int row)
{
	GtkWidget *menu;
	int show_paste;
		
	show_paste = item_grid->sheet->workbook->clipboard_contents != NULL;

	menu = create_popup_menu (item_grid, show_paste);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->button.time);
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
	int col, row, x, y;
	int scroll_x, scroll_y;

	switch (event->type){
	case GDK_ENTER_NOTIFY: {
		int cursor;
	       
		if (sheet->mode == SHEET_MODE_SHEET)
			cursor = GNUMERIC_CURSOR_FAT_CROSS;
		else
			cursor = GNUMERIC_CURSOR_ARROW;

		cursor_set_widget (canvas, cursor);
		return TRUE;
	}
		
	case GDK_BUTTON_RELEASE:
		if (event->button.button == 1){
			item_grid->selecting = 0;
			gnome_canvas_item_ungrab (item, event->button.time);
			return 1;
		}
		break;
		
	case GDK_MOTION_NOTIFY:
		if (item_grid->selecting){
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
			
			col = item_grid_find_col (item_grid, x, NULL);
			row = item_grid_find_row (item_grid, y, NULL);
			sheet_selection_extend_to (sheet, col, row);
			return 1;
		}
		break;
		
	case GDK_BUTTON_PRESS:
		switch (event->button.button){
		case 1:
			convert (canvas, event->button.x, event->button.y, &x, &y);
			col = item_grid_find_col (item_grid, x, NULL);
			row = item_grid_find_row (item_grid, y, NULL);
			
			sheet_accept_pending_input (sheet);
			sheet_cursor_move (sheet, col, row);
			if (!(event->button.state & GDK_CONTROL_MASK))
				sheet_selection_reset_only (sheet);
			
			item_grid->selecting = 1;
			sheet_selection_append (sheet, col, row);
			gnome_canvas_item_grab (item,
						GDK_POINTER_MOTION_MASK |
						GDK_BUTTON_RELEASE_MASK,
						NULL,
						event->button.time);
			return 1;

		case 3:
			convert (canvas, event->button.x, event->button.y, &x, &y);
			col = item_grid_find_col (item_grid, x, NULL);
			row = item_grid_find_row (item_grid, y, NULL);

			item_grid_popup_menu (item_grid, event, col, row);
			return 1;
		}
	default:
		return 0;	
	}
	
	return 0;
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
	case ARG_SHEET_VIEW:
		item_grid->sheet_view = GTK_VALUE_POINTER (*arg);
		item_grid->sheet = item_grid->sheet_view->sheet;
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

	gtk_object_add_arg_type ("ItemGrid::SheetView", GTK_TYPE_POINTER, 
				 GTK_ARG_WRITABLE, ARG_SHEET_VIEW);
	
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

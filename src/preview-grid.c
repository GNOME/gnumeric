/*
 * preview-grid.c : Preview Grid Canvas Item
 *
 * Based upon "The Grid Gnome Canvas Item" a.k.a. Item-Grid
 * (item-grid.c) Created by Miguel de Icaza (miguel@kernel.org)
 *
 * Author : Almer. S. Tigelaar.
 * E-mail: almer1@dds.nl or almer-t@bigfoot.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnumeric-type-util.h"
#include "preview-grid.h"

#include "cell.h"
#include "cell-draw.h"
#include "pattern.h"
#include "portability.h"
#include "mstyle.h"
#include "sheet-style.h"
#include "style-border.h"
#include "style-color.h"

struct _PreviewGrid {
	GnomeCanvasItem canvas_item;

	GdkGC      *grid_gc;	/* Draw grid gc */
	GdkGC      *fill_gc;	/* Default background fill gc */
	GdkGC      *gc;		/* Color used for the cell */
	GdkGC      *empty_gc;	/* GC used for drawing empty cells */

	PGridGetCell get_cell_cb;

	PGridGetRowOffset get_row_offset_cb;
	PGridGetColOffset get_col_offset_cb;

	PGridGetColWidth get_col_width_cb;
	PGridGetRowHeight get_row_height_cb;

	PGridGetStyle get_style_cb;

	gpointer cb_data;

	gboolean gridlines;
};

typedef struct {
	GnomeCanvasItemClass parent_class;
} PreviewGridClass;

#define PREVIEW_GRID_CLASS(k) (GTK_CHECK_CLASS_CAST ((k), preview_grid_get_type (), PreviewGridClass))

static GnomeCanvasItemClass *preview_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,
	ARG_GET_CELL_CB,
	ARG_GET_ROW_OFFSET_CB,
	ARG_GET_ROW_HEIGHT_CB,
	ARG_GET_COL_OFFSET_CB,
	ARG_GET_COL_WIDTH_CB,
	ARG_GET_STYLE_CB,
	ARG_CB_DATA,
	ARG_RENDER_GRIDLINES,
};

/**
 * preview_grid_destroy:
 * @object: a Preview grid
 *
 * Destroy handler
 **/
static void
preview_grid_destroy (GtkObject *object)
{
	PreviewGrid *grid;

	grid = PREVIEW_GRID (object);

	if (GTK_OBJECT_CLASS (preview_grid_parent_class)->destroy)
		(*GTK_OBJECT_CLASS (preview_grid_parent_class)->destroy)(object);
}

/**
 * preview_grid_realize:
 * @item: a PreviewGrid
 *
 * Realize handler
 **/
static void
preview_grid_realize (GnomeCanvasItem *item)
{
	GdkWindow *window;
	GtkStyle  *style;
	PreviewGrid  *preview_grid;
	GdkGC     *gc;

	if (GNOME_CANVAS_ITEM_CLASS (preview_grid_parent_class)->realize)
		(*GNOME_CANVAS_ITEM_CLASS (preview_grid_parent_class)->realize)(item);

	preview_grid = PREVIEW_GRID (item);
	window = GTK_WIDGET (item->canvas)->window;

	/* Set the default background color of the canvas itself to white.
	 * This makes the redraws when the canvas scrolls flicker less.
	 */
	style = gtk_style_copy (GTK_WIDGET (item->canvas)->style);
	style->bg [GTK_STATE_NORMAL] = gs_white;
	gtk_widget_set_style (GTK_WIDGET (item->canvas), style);
	gtk_style_unref (style);

	/* Configure the default grid gc */
	preview_grid->grid_gc = gc = gdk_gc_new (window);
	preview_grid->fill_gc = gdk_gc_new (window);
	preview_grid->gc = gdk_gc_new (window);
	preview_grid->empty_gc = gdk_gc_new (window);

	gdk_gc_set_foreground (gc, &gs_light_gray);
	gdk_gc_set_background (gc, &gs_white);

	gdk_gc_set_foreground (preview_grid->fill_gc, &gs_white);
	gdk_gc_set_background (preview_grid->fill_gc, &gs_light_gray);
}

/**
 * preview_grid_unrealize:
 * @item: a PreviewGrid
 *
 * Unrealize handler
 **/
static void
preview_grid_unrealize (GnomeCanvasItem *item)
{
	PreviewGrid *preview_grid = PREVIEW_GRID (item);

	gdk_gc_unref (preview_grid->grid_gc);
	gdk_gc_unref (preview_grid->fill_gc);
	gdk_gc_unref (preview_grid->gc);
	gdk_gc_unref (preview_grid->empty_gc);
	preview_grid->grid_gc = 0;
	preview_grid->fill_gc = 0;
	preview_grid->gc = 0;
	preview_grid->empty_gc = 0;

	if (GNOME_CANVAS_ITEM_CLASS (preview_grid_parent_class)->unrealize)
		(*GNOME_CANVAS_ITEM_CLASS (preview_grid_parent_class)->unrealize)(item);
}


/**
 * preview_grid_update:
 *
 * Update handler
 **/
static void
preview_grid_update (GnomeCanvasItem *item, double *affine, ArtSVP *clip_path, int flags)
{
	if (GNOME_CANVAS_ITEM_CLASS (preview_grid_parent_class)->update)
		(* GNOME_CANVAS_ITEM_CLASS (preview_grid_parent_class)->update) (item, affine, clip_path, flags);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = INT_MAX;
	item->y2 = INT_MAX;

	gnome_canvas_group_child_bounds (GNOME_CANVAS_GROUP (item->parent), item);
}

/**
 * preview_grid_draw_background:
 *
 * Draw cell background
 **/
static void
preview_grid_draw_background (GdkDrawable *drawable, PreviewGrid *preview_grid, MStyle *mstyle,
			      int col, int row, int x, int y, int w, int h)
{
	GdkGC  * const gc = preview_grid->empty_gc;
	gboolean is_selected;

	g_return_if_fail (drawable != NULL);
	g_return_if_fail (preview_grid != NULL);
	g_return_if_fail (mstyle != NULL);

	/*
	 * Insert code here for selection drawing
	 */
	is_selected = FALSE;

	if (gnumeric_background_set_gc (mstyle, gc, preview_grid->canvas_item.canvas, is_selected))
		/* Fill the entire cell including the right & left grid line */
		gdk_draw_rectangle (drawable, gc, TRUE, x, y, w+1, h+1);
	else if (is_selected)
		/* Fill the entire cell excluding the right & left grid line */
		gdk_draw_rectangle (drawable, gc, TRUE, x+1, y+1, w-1, h-1);
}

/**
 * preview_grid_draw:
 *
 * Draw Handler
 *
 * NOTE :
 * The drawing routine does not support spanning.
 **/
static void
preview_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int draw_x, int draw_y, int width, int height)
{
 	PreviewGrid *preview_grid = PREVIEW_GRID (item);
 
 	/* To ensure that far and near borders get drawn we pretend to draw +-2
 	 * pixels around the target area which would include the surrounding
 	 * borders if necessary */
 	/* TODO : there is an opportunity to speed up the redraw loop by only
 	 * painting the borders of the edges and not the content.
 	 * However, that feels like more hassle that it is worth.  Look into this someday.
 	 */
 	int x, y, col, row;
 	int const start_col = preview_grid->get_col_offset_cb (draw_x - 2, &x, preview_grid->cb_data);
 	int end_col         = preview_grid->get_col_offset_cb (draw_x + width + 2, NULL, preview_grid->cb_data);
 	int const diff_x    = draw_x - x;
 	int start_row       = preview_grid->get_row_offset_cb (draw_y - 2, &y, preview_grid->cb_data);
 	int end_row         = preview_grid->get_row_offset_cb (draw_y + height + 2, NULL, preview_grid->cb_data);
 	int const diff_y    = draw_y - y;

 	/* Fill entire region with default background (even past far edge) */
 	gdk_draw_rectangle (drawable, preview_grid->fill_gc, TRUE,
 			    draw_x, draw_y, width, height);
 
 	for (row = start_row, y = -diff_y; row <= end_row; row++) {
 		int row_height = preview_grid->get_row_height_cb (row, preview_grid->cb_data);
 			
 		for (col = start_col, x = -diff_x; col <= end_col; col++) {
 			MStyle       *style     = preview_grid->get_style_cb (row, col, preview_grid->cb_data);
 			int           col_width = preview_grid->get_col_width_cb (col, preview_grid->cb_data);
 			Cell         *cell      = preview_grid->get_cell_cb (row, col, preview_grid->cb_data);
 
 			/*
                          * If the mstyle is NULL we are probably out of range
                          * In such cases we simply draw with the default style
                          */
			if (style == NULL)
				style = mstyle_new_default ();
			else
				mstyle_ref (style);
 
 			preview_grid_draw_background (drawable, preview_grid,
 						      style, col, row, x, y,
 						      col_width, row_height);
 
 			/*
 			 * Draw the cell contents if the cell is non null and
 			 * not empty.
 			 */
			if (cell && !cell_is_blank (cell))
				cell_draw (cell, style,
					   preview_grid->gc, drawable,
					   x, y, -1, -1, 0);

			mstyle_unref (style);
 			x += col_width;
 		}

		/*
		 * FIXME: Draw Borders
		 */
		 
		y += row_height;
 	}
}

/**
 * preview_grid_point:
 *
 * Point Handler
 *
 * Return value: point
 **/
static double
preview_grid_point (GnomeCanvasItem *item, double x, double y, int cx, int cy,
		    GnomeCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

/**
 * preview_grid_init:
 * @preview_grid: a PreviewGrid
 *
 * Instance initialization
 **/
static void
preview_grid_init (PreviewGrid *preview_grid)
{
	GnomeCanvasItem *item = GNOME_CANVAS_ITEM (preview_grid);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;
}

/**
 * preview_grid_set_arg:
 *
 * Callback to set arguments
 **/
static void
preview_grid_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	GnomeCanvasItem *item;
	PreviewGrid *preview_grid;

	item = GNOME_CANVAS_ITEM (o);
	preview_grid = PREVIEW_GRID (o);

	switch (arg_id){
	case ARG_GET_CELL_CB:
		preview_grid->get_cell_cb = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_GET_ROW_OFFSET_CB :
		preview_grid->get_row_offset_cb = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_GET_ROW_HEIGHT_CB :
		preview_grid->get_row_height_cb = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_GET_COL_OFFSET_CB :
		preview_grid->get_col_offset_cb = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_GET_COL_WIDTH_CB :
		preview_grid->get_col_width_cb = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_GET_STYLE_CB :
		preview_grid->get_style_cb = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_CB_DATA :
		preview_grid->cb_data = GTK_VALUE_POINTER (*arg);
		break;
	case ARG_RENDER_GRIDLINES :
		preview_grid->gridlines = GTK_VALUE_BOOL (*arg);
		break;
	}
}

/**
 * preview_grid_class_init:
 * @preview_grid_class: a PreviewGrid
 *
 * PreviewGrid class initialization
 **/
static void
preview_grid_class_init (PreviewGridClass *preview_grid_class)
{
	GtkObjectClass  *object_class;
	GnomeCanvasItemClass *item_class;

	preview_grid_parent_class = gtk_type_class (gnome_canvas_item_get_type ());

	object_class = (GtkObjectClass *) preview_grid_class;
	item_class = (GnomeCanvasItemClass *) preview_grid_class;

	gtk_object_add_arg_type ("PreviewGrid::GetCellCb", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GET_CELL_CB);
	gtk_object_add_arg_type ("PreviewGrid::GetRowOffsetCb", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GET_ROW_OFFSET_CB);
	gtk_object_add_arg_type ("PreviewGrid::GetRowHeightCb", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GET_ROW_HEIGHT_CB);
	gtk_object_add_arg_type ("PreviewGrid::GetColOffsetCb", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GET_COL_OFFSET_CB);
	gtk_object_add_arg_type ("PreviewGrid::GetColWidthCb", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GET_COL_WIDTH_CB);
	gtk_object_add_arg_type ("PreviewGrid::GetStyleCb", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_GET_STYLE_CB);

	gtk_object_add_arg_type ("PreviewGrid::CbData", GTK_TYPE_POINTER,
				 GTK_ARG_WRITABLE, ARG_CB_DATA);

	gtk_object_add_arg_type ("PreviewGrid::RenderGridlines", GTK_TYPE_BOOL,
				 GTK_ARG_WRITABLE, ARG_RENDER_GRIDLINES);

	object_class->set_arg = preview_grid_set_arg;
	object_class->destroy = preview_grid_destroy;

	/* GnomeCanvasItem method overrides */
	item_class->update      = preview_grid_update;
	item_class->realize     = preview_grid_realize;
	item_class->unrealize   = preview_grid_unrealize;

	item_class->draw        = preview_grid_draw;
	item_class->point       = preview_grid_point;

	/* Turn of translate and event handlers
	item_class->translate   = preview_grid_translate;
	item_class->event       = preview_grid_event;
	*/
}

GNUMERIC_MAKE_TYPE (preview_grid, "PreviewGrid", PreviewGrid,
		    preview_grid_class_init, preview_grid_init,
		    gnome_canvas_item_get_type ())

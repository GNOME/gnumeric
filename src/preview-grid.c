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

#include "border.h"
#include "cell.h"
#include "cell-draw.h"
#include "color.h"
#include "pattern.h"
#include "mstyle.h"

#include "preview-grid.h"

/*
 * Included for border drawing
 */
#include "item-grid.h"

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
	GnomeCanvas *canvas = item->canvas;
	GdkVisual *visual;
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

	/* Allocate the default colors */
	preview_grid->background = gs_white;
	preview_grid->grid_color = gs_light_gray;
	preview_grid->default_color = gs_black;

	gdk_gc_set_foreground (gc, &preview_grid->grid_color);
	gdk_gc_set_background (gc, &preview_grid->background);

	gdk_gc_set_foreground (preview_grid->fill_gc, &preview_grid->background);
	gdk_gc_set_background (preview_grid->fill_gc, &preview_grid->grid_color);

	/* Find out how we need to draw the selection with the current visual */
	visual = gtk_widget_get_visual (GTK_WIDGET (canvas));

	switch (visual->type) {
	case GDK_VISUAL_STATIC_GRAY:
	case GDK_VISUAL_TRUE_COLOR:
	case GDK_VISUAL_STATIC_COLOR:
		preview_grid->visual_is_paletted = 0;
		break;

	default:
		preview_grid->visual_is_paletted = 1;
	}
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
preview_grid_draw (GnomeCanvasItem *item, GdkDrawable *drawable, int x, int y, int width, int height)
{
	PreviewGrid *preview_grid = PREVIEW_GRID (item);
	GdkGC *grid_gc = preview_grid->grid_gc;
	int x_paint, y_paint;
	int paint_col, paint_row;
	int diff_x, diff_y;
	int row, col;

	/* 1. The default background */
	gdk_draw_rectangle (drawable, preview_grid->fill_gc, TRUE,
			    0, 0, width, height);

	/* 2. the grids */
	paint_col = preview_grid->get_col_offset_cb (x, &x_paint, preview_grid->cb_data);
	paint_row = preview_grid->get_row_offset_cb (y, &y_paint, preview_grid->cb_data);

	diff_x = x - x_paint;
	diff_y = y - y_paint;

	if (preview_grid->gridlines) {
		col = paint_col;
		x_paint = -diff_x;
		gdk_draw_line (drawable, grid_gc, x_paint, 0, x_paint, height);
		while (x_paint < width && col < SHEET_MAX_COLS) {

			x_paint += preview_grid->get_col_width_cb (col++, preview_grid->cb_data);

			gdk_draw_line (drawable, grid_gc, x_paint, 0, x_paint, height);
		}

		row = paint_row;
		y_paint = -diff_y;
		gdk_draw_line (drawable, grid_gc, 0, y_paint, width, y_paint);
		while (y_paint < height && row < SHEET_MAX_ROWS) {

			y_paint += preview_grid->get_row_height_cb (row++, preview_grid->cb_data);

			gdk_draw_line (drawable, grid_gc, 0, y_paint, width, y_paint);
		}
	}

	gdk_gc_set_function (preview_grid->gc, GDK_COPY);

	row = paint_row;
	for (y_paint = -diff_y; y_paint < height && row < SHEET_MAX_ROWS; ++row) {

		col = paint_col;
		for (x_paint = -diff_x; x_paint < width && col < SHEET_MAX_COLS; ++col) {
			MStyle *mstyle         = preview_grid->get_style_cb (row, col, preview_grid->cb_data);
			int const w            = preview_grid->get_col_width_cb (col, preview_grid->cb_data);
			int const h            = preview_grid->get_row_height_cb (row, preview_grid->cb_data);
			Cell   *cell           = preview_grid->get_cell_cb (row, col, preview_grid->cb_data);

			/*
			 * If the mstyle is NULL we are probably out of range
			 * In such cases we simply draw with the default style
			 */
			if (mstyle == NULL)
				mstyle = mstyle_new_default ();
			else
				mstyle_ref (mstyle);

			/*
			 * Draw background and border for this cell
			 */
			preview_grid_draw_background (drawable, preview_grid, mstyle,
						      col, row, x_paint, y_paint, w, h);


			/*
			 * We call upon item_grid_draw_border here from item-grid.c
			 */
			item_grid_draw_border (drawable, mstyle, x_paint, y_paint, w, h, FALSE);

			/*preview_grid_draw_border (drawable, mstyle, x_paint, y_paint, w, h);*/

			/*
			 * Draw the cell contents, if "cell" is non-null
			 */
			if (cell)
				cell_draw (cell, mstyle, preview_grid->gc, drawable,
					   x_paint, y_paint, -1, -1);

			mstyle_unref (mstyle);

			x_paint += preview_grid->get_col_width_cb (col, preview_grid->cb_data);
		}

		y_paint += preview_grid->get_row_height_cb (row, preview_grid->cb_data);
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

/**
 * preview_grid_get_type:
 *
 * Create type information
 *
 * Return value: PreviewGrid type information
 **/
GtkType
preview_grid_get_type (void)
{
	static GtkType preview_grid_type = 0;

	if (!preview_grid_type) {
		GtkTypeInfo preview_grid_info = {
			"PreviewGrid",
			sizeof (PreviewGrid),
			sizeof (PreviewGridClass),
			(GtkClassInitFunc) preview_grid_class_init,
			(GtkObjectInitFunc) preview_grid_init,
			NULL, /* reserved_1 */
			NULL, /* reserved_2 */
			(GtkClassInitFunc) NULL
		};

		preview_grid_type = gtk_type_unique (gnome_canvas_item_get_type (), &preview_grid_info);
	}

	return preview_grid_type;
}

/*
 * preview-grid-controller.c : implementation of the PreviewGridController
 *
 * The previewgridcontroller is an easy to use interface to display a
 * PreviewGrid on a canvas.
 *
 * Copyright (C) Almer. S. Tigelaar.
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
#include "style-border.h"
#include "cell.h"
#include "colrow.h"
#include "pattern.h"
#include "rendered-value.h"
#include "value.h"
#include "mstyle.h"

#include "preview-grid-controller.h"

/*
 * The previewgrid controller struct
 */
struct _PreviewGridController{
	GnomeCanvas *canvas;
	PreviewGrid *grid;
	GnomeCanvasRect *rect;
	GnomeCanvasRect *selection_rect;

	int rows;
	int cols;

	int default_row_height;
	int default_col_width;
	int canvas_height;
	int canvas_width;
	MStyle   *default_style;

	PGridCtlGetRowHeight    get_row_height_cb;
	PGridCtlGetColWidth     get_col_width_cb;
	PGridCtlGetCellContent  get_cell_content_cb;
	PGridCtlGetCellStyle    get_cell_style_cb;

	gpointer cb_data;
	gboolean gridlines;

	Cell *last_cell;
};

/**
 * get_real_row_height:
 * @controller: PreviewGridController
 * @row: row
 *
 * Retrieve the real height of @row
 *
 * Return value: height of @row
 **/
static int
get_real_row_height (PreviewGridController *controller, int row)
{
	int height;

	/*
	 * Return default height if out of range
	 */
	if (row >= controller->rows)
		return controller->default_row_height;

	height = controller->get_row_height_cb (row, controller->cb_data);

	if (height < 0)
		return controller->default_row_height;
	else
		return height;
}

/**
 * get_real_col_width:
 * @controller: PreviewGridController
 * @col: column
 *
 * Retrieve the real width of column @col
 *
 * Return value: width of @col
 **/
static int
get_real_col_width (PreviewGridController *controller, int col)
{
	int width;

	/*
	 * Return default height if out of range
	 */
	if (col >= controller->cols)
		return controller->default_col_width;

	width = controller->get_col_width_cb (col, controller->cb_data);

	if (width < 0)
		return controller->default_col_width;
	else
		return width;
}

/*************************************************************************************************
 * CALLBACKS
 *************************************************************************************************/

/**
 * cb_grid_get_cell:
 * @row: row offset
 * @col: col offset
 * @data: PreviewGridController
 *
 * Retrieve cell at coordinates @row, @col.
 * This function replicates a Cell (This is not a _real_ cell
 * bound to a sheet)
 *
 * Return value: The cell at @row, @col
 **/
static Cell*
cb_grid_get_cell (int row, int col, gpointer data)
{
	PreviewGridController *controller = (PreviewGridController *) data;
	Cell *cell;
	RenderedValue *res = NULL;
	MStyle *mstyle;

	if (col >= controller->cols || row >= controller->rows)
		return NULL;

	/*
	 * NOTE :
	 * Try to avoid using cell_blah () functions, because
	 * those functions sometimes call Sheet and cause a
	 * segfault. We are simply replicating a structure
	 * manually here.
	 * NOTE 2 :
	 * controller->last_cell contains the last replicated cell
	 * we always need to free this also when the controller itself
	 * is destroyed (see preview_grid_free ())
	 */

	cell = controller->last_cell;

	if (cell) {
		value_release (cell->value);
		rendered_value_destroy (cell->rendered_value);

		g_free (cell->col_info);
		g_free (cell->row_info);
		g_free (cell);
	}

	mstyle = controller->get_cell_style_cb (row, col, controller->cb_data);
	if (!mstyle)
		mstyle = controller->default_style;
	cell = g_new0 (Cell, 1);

	cell->row_info = g_new0 (ColRowInfo, 1);
	cell->col_info = g_new0 (ColRowInfo, 1);

	/*
	 * Eventually the row_info->pos and col_info->pos
	 * will go away
	 */
	cell->row_info->pos = row;
	cell->col_info->pos = col;
	cell->pos.row = row;
	cell->pos.col = col;

	cell->row_info->margin_a = 0;
	cell->row_info->margin_b = 0;
	cell->col_info->margin_a = 2;
	cell->col_info->margin_b = 2;

	cell->row_info->size_pixels = get_real_row_height (controller, row);
	cell->col_info->size_pixels = get_real_col_width  (controller, col);

	if (row >= controller->rows || col >= controller->cols)
		cell->value = value_new_empty ();
	else
		cell->value = controller->get_cell_content_cb (row, col, controller->cb_data);

	res = rendered_value_new (cell, mstyle, TRUE);

	cell->rendered_value = res;

	/*
	 * Rendered value needs to know text width and height to handle
	 * alignment properly
	 */
	rendered_value_calc_size_ext (cell, mstyle);

	controller->last_cell = cell;

	return cell;
}

/**
 * cb_grid_get_row_offset:
 * @y: offset
 * @row_origin: if not null the origin of the row containing pixel @y is put here
 * @data: PreviewGridController
 *
 * Return value: Row containing pixel y (and origin in @row_origin)
 **/
static int
cb_grid_get_row_offset (int y, int* row_origin, gpointer data)
{
	PreviewGridController *controller = (PreviewGridController *) data;
	int row   = 0;
	int pixel = 0;

	if (y < pixel) {
		if (row_origin)
			*row_origin = 1; /* there is a 1 pixel edge at the left */
		return 0;
	}

	do {
		if (y <= pixel + get_real_row_height (controller, row)) {
			if (row_origin)
				*row_origin = pixel;
			return row;
		}
		pixel += get_real_row_height (controller, row);
	} while (++row < SHEET_MAX_ROWS);

	if (row_origin)
		*row_origin = pixel;

	return SHEET_MAX_ROWS-1;
}

/**
 * cb_grid_get_row_height:
 * @row: row
 * @data: PreviewGridController
 *
 * Return value: The height of @row
 **/
static int
cb_grid_get_row_height (int row, gpointer data)
{
	return get_real_row_height ((PreviewGridController *) data, row);
}

/**
 * cb_grid_get_col_offset:
 * @x: offset
 * @col_origin: if not null the origin of the column containing pixel @x is put here
 * @data: PreviewGridController
 *
 * Return value: Column containing pixel x (and origin in @col_origin)
 **/
static int
cb_grid_get_col_offset (int x, int* col_origin, gpointer data)
{
	PreviewGridController *controller = (PreviewGridController *) data;
	int col   = 0;
	int pixel = 0;

	if (x < pixel) {
		if (col_origin)
			*col_origin = 1; /* there is a 1 pixel edge */
		return 0;
	}

	do {
		if (x <= pixel + get_real_col_width (controller, col)) {
			if (col_origin)
				*col_origin = pixel;
			return col;
		}
		pixel += get_real_col_width (controller, col);
	} while (++col < SHEET_MAX_COLS);

	if (col_origin)
		*col_origin = pixel;

	return SHEET_MAX_COLS-1;
}

/**
 * cb_grid_get_col_width:
 * @col: column
 * @data: PreviewGridController
 *
 * Return value: column width
 **/
static int
cb_grid_get_col_width (int col, gpointer data)
{
	return get_real_col_width ((PreviewGridController *) data, col);
}

/**
 * cb_grid_get_style:
 * @row: row offset
 * @col: col offset
 * @data: PreviewGridController
 *
 * Returns the style of the cell located at
 * @row, @col coordinates.
 *
 * Return value: MStyle
 **/
static MStyle *
cb_grid_get_style (int row, int col, gpointer data)
{
	PreviewGridController *controller = (PreviewGridController *) data;
	MStyle *mstyle;

	/*
	 * Keep a single row and column for drawing utmost bottom
	 * and right borders
	 */
	if (col > controller->cols || row > controller->rows)
		return NULL;

	mstyle = controller->get_cell_style_cb (row, col, controller->cb_data);

	if (!mstyle)
		return controller->default_style;
	else
		return mstyle;
}

/*************************************************************************************************
 * PREVIEWGRIDCONTROLLER FUNCTIONS
 *************************************************************************************************/

/**
 * preview_grid_controller_force_redraw:
 * @controller: a PreviewGridController
 *
 * Forces a redraw of the Canvas
 **/
void
preview_grid_controller_force_redraw (PreviewGridController *controller)
{
	/*
	 * We update the entire canvas
	 */
	gnome_canvas_request_redraw (controller->canvas, INT_MIN, INT_MIN,
				     INT_MAX/2, INT_MAX/2);
}

/**
 * preview_grid_controller_new:
 * @canvas: The canvas the grid should be placed on
 * @rows: Number of rows in grid
 * @cols: Number of cols in grid
 * @default_row_height: Default height of a row
 * @default_col_width: Default width of a col
 * @canvas_height: Height of @canvas, if this is -1 it will be calculated from the default row height
 * @canvas_width: Width of @canvas, if this is -1 it will be calculated from the default row width
 * @get_row_height_cb: Callback to retrieve row height
 * @get_col_width_cb: Callback to retrieve column width
 * @get_cell_text_cb: Callback to retrieve cell text
 * @get_cell_style_cb: Callback to retrieve cell style
 * @cb_data: Data passed to callback functions
 * @gridlines: Show gridlines?
 * @selected: If set a red rectangle will be drawn over the preview.
 *
 * Create a new grid controller
 *
 * Return value: new grid controller
 **/
PreviewGridController*
preview_grid_controller_new (GnomeCanvas *canvas,
			     int rows, int cols, int default_row_height, int default_col_width,
			     PGridCtlGetRowHeight get_row_height_cb,
			     PGridCtlGetColWidth get_col_width_cb,
			     PGridCtlGetCellContent get_cell_content_cb,
			     PGridCtlGetCellStyle get_cell_style_cb,
			     gpointer cb_data, gboolean gridlines, gboolean selected)
{
	PreviewGridController *controller = g_new0 (PreviewGridController, 1);

	controller->canvas             = canvas;
	controller->rows               = rows;
	controller->cols               = cols;
	controller->default_row_height = default_row_height;
	controller->default_col_width  = default_col_width;

	controller->default_style      = mstyle_new_default ();

	controller->get_row_height_cb   = get_row_height_cb;
	controller->get_col_width_cb    = get_col_width_cb;
	controller->get_cell_content_cb = get_cell_content_cb;
	controller->get_cell_style_cb   = get_cell_style_cb;

	controller->cb_data            = cb_data;
	controller->gridlines          = gridlines;

	/*
	 * This rect is used to properly center on the canvas. It covers the whole canvas area.
	 * Currently the canvas shifts the (0,0) 4,5 pixels downwards in vertical and horizontal
	 * directions. So we need (-4.5, -4.5) as the absolute top coordinate and (215.5, 85.5) for
	 * the absolute bottom of the canvas's region. Look at src/dialogs/autoformat.glade for
	 * the original canvas dimensions (look at the scrolledwindow that houses each canvas)
	 */
	controller->rect = GNOME_CANVAS_RECT (gnome_canvas_item_new (gnome_canvas_root (canvas),
								     gnome_canvas_rect_get_type (),
								     "x1", -4.5, "y1", -4.5,
								     "x2", 215.5, "y2", 85.5,
								     "width_pixels", (int) 0,
								     "fill_color", NULL,
								     NULL));

	controller->grid = PREVIEW_GRID (gnome_canvas_item_new (gnome_canvas_root (canvas),
								preview_grid_get_type (),
								"GetCellCb", cb_grid_get_cell,
								"GetRowOffsetCb", cb_grid_get_row_offset,
								"GetRowHeightCb", cb_grid_get_row_height,
								"GetColOffsetCb", cb_grid_get_col_offset,
								"GetColWidthCb", cb_grid_get_col_width,
								"GetStyleCb", cb_grid_get_style,
								"CbData", controller,
								"RenderGridlines", gridlines,
								NULL));

	/*
	 * The numbers used here are a little less then the ones used for the centering
	 * rect above. This rect is only drawn when the grid is 'selected'
	 */
	if (selected)
		controller->selection_rect = GNOME_CANVAS_RECT (gnome_canvas_item_new (gnome_canvas_root (canvas),
										       gnome_canvas_rect_get_type (),
										       "x1", -7.0, "y1", -2.5,
										       "x2", 219.0, "y2", 84.5,
										       "width_pixels", (int) 2,
										       "outline_color", "red",
										       "fill_color", NULL,
										       NULL));
	/*
	 * Set the scroll region to a nice value
	 */
	gnome_canvas_set_scroll_region (canvas, 0, 0,
					cols * default_col_width, rows * default_row_height);

	preview_grid_controller_force_redraw (controller);

	return controller;
}

/**
 * preview_grid_controller_free:
 * @controller:
 *
 * Free a previously made grid controller
 **/
void
preview_grid_controller_free (PreviewGridController *controller)
{

	/*
	 * Free last replicated cell
	 */
	if (controller->last_cell) {
		Cell *cell = controller->last_cell;

		value_release (cell->value);
		rendered_value_destroy (cell->rendered_value);

		g_free (cell->col_info);
		g_free (cell->row_info);
		g_free (cell);
	}

	gtk_object_destroy (GTK_OBJECT (controller->grid));

	gnome_canvas_set_scroll_region (controller->canvas, 0, 0, 0, 0);

	mstyle_unref (controller->default_style);

	g_free (controller);

}

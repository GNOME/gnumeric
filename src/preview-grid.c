/*
 * preview-grid.c : Preview Grid Canvas Item
 *
 * Based upon "The Grid Gnome Canvas Item" a.k.a. Item-Grid
 * (item-grid.c) Created by Miguel de Icaza (miguel@kernel.org)
 *
 * Author : Almer S. Tigelaar <almer@gnome.org>
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

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "preview-grid.h"

#include "cell.h"
#include "cell-draw.h"
#include "colrow.h"
#include "pattern.h"
#include "mstyle.h"
#include "rendered-value.h"
#include "sheet-style.h"
#include "style-border.h"
#include "style-color.h"
#include "value.h"
#include "gnm-marshalers.h"

#include <gsf/gsf-impl-utils.h>

struct _PreviewGrid {
	FooCanvasItem canvas_item;

	struct { /* Gc's */
		GdkGC *fill;	/* Default background fill gc */
		GdkGC *cell;	/* Color used for the cell */
		GdkGC *empty;	/* GC used for drawing empty cells */
	} gc;
	struct { /* Defaults */
		MStyle *style;
		int     row_height;
		int     col_width;
	} def;

	gboolean gridlines;
};

typedef struct {
	FooCanvasItemClass parent_class;

	int      (* get_row_height) (PreviewGrid *pg, int row);
	int      (* get_col_width)  (PreviewGrid *pg, int col);
	MStyle * (* get_cell_style) (PreviewGrid *pg, int row, int col);
	Value *  (* get_cell_value) (PreviewGrid *pg, int row, int col);
} PreviewGridClass;

#define PREVIEW_GRID_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), preview_grid_get_type (), PreviewGridClass))

static FooCanvasItemClass *preview_grid_parent_class;

/* The arguments we take */
enum {
	ARG_0,

	/* Options */
	ARG_RENDER_GRIDLINES,
	ARG_DEFAULT_ROW_HEIGHT,
	ARG_DEFAULT_COL_WIDTH
};

/* Signals */
enum {
	GET_ROW_HEIGHT,
	GET_COL_WIDTH,
	GET_CELL_STYLE,
	GET_CELL_VALUE,
	LAST_SIGNAL
};

static guint pg_signals [LAST_SIGNAL] = { 0 };

typedef gpointer (*GtkSignal_POINTER__INT_INT) (GtkObject * object,
						gint arg1,
						gint arg2,
						gpointer user_data);
/*****************************************************************************/

static int
pg_get_row_height (PreviewGrid *pg, int const row)
{
	int height;

	g_return_val_if_fail (row >= 0 && row < SHEET_MAX_ROWS, 1);

	g_signal_emit (G_OBJECT (pg), pg_signals [GET_ROW_HEIGHT], 0,
		       row, &height);
	if (height > 0)
		return height;
	return pg->def.row_height;
}

static int
pg_get_col_width (PreviewGrid *pg, int const col)
{
	int width;

	g_return_val_if_fail (col >= 0 && col < SHEET_MAX_COLS, 1);

	g_signal_emit (G_OBJECT (pg), pg_signals [GET_COL_WIDTH], 0,
		       col, &width);
	if (width > 0)
		return width;
	return pg->def.col_width;
}

/**
 * pg_get_row_offset:
 * pg:
 * @y: offset
 * @row_origin: if not null the origin of the row containing pixel @y is put here
 *
 * Return value: Row containing pixel y (and origin in @row_origin)
 **/
static int
pg_get_row_offset (PreviewGrid *pg, int const y, int *row_origin)
{
	int row   = 0;
	int pixel = 0;

	g_return_val_if_fail (pg != NULL, 0);

	if (y < pixel) {
		if (row_origin)
			*row_origin = 1; /* there is a 1 pixel edge at the left */
		return 0;
	}

	do {
		if (y <= pixel + pg_get_row_height (pg, row)) {
			if (row_origin)
				*row_origin = pixel;
			return row;
		}
		pixel += pg_get_row_height (pg, row);
	} while (++row < SHEET_MAX_ROWS);

	if (row_origin)
		*row_origin = pixel;

	return SHEET_MAX_ROWS - 1;
}

/**
 * pg_get_col_offset:
 * @x: offset
 * @col_origin: if not null the origin of the column containing pixel @x is put here
 *
 * Return value: Column containing pixel x (and origin in @col_origin)
 **/
static int
pg_get_col_offset (PreviewGrid *pg, int const x, int *col_origin)
{
	int col   = 0;
	int pixel = 0;

	g_return_val_if_fail (pg != NULL, 0);

	if (x < pixel) {
		if (col_origin)
			*col_origin = 1; /* there is a 1 pixel edge */
		return 0;
	}

	do {
		if (x <= pixel + pg_get_col_width (pg, col)) {
			if (col_origin)
				*col_origin = pixel;
			return col;
		}
		pixel += pg_get_col_width (pg, col);
	} while (++col < SHEET_MAX_COLS);

	if (col_origin)
		*col_origin = pixel;

	return SHEET_MAX_COLS - 1;
}

static MStyle *
pg_get_style (PreviewGrid *pg, int const row, int const col)
{
	MStyle *style = NULL;

	g_return_val_if_fail (pg != NULL, 0);
	g_return_val_if_fail (row >= 0 && row < SHEET_MAX_ROWS, 0);
	g_return_val_if_fail (col >= 0 && col < SHEET_MAX_COLS, 0);

	g_signal_emit (G_OBJECT (pg), pg_signals [GET_CELL_STYLE], 0,
		       row, col, &style, NULL);

	/* If no style was returned, use the default style */
	if (style == NULL)
		return pg->def.style;
	else
		return style;
}

static Cell *
pg_construct_cell (PreviewGrid *pg, int const row, int const col)
{
	Cell          *cell  = NULL;
	RenderedValue *res   = NULL;
	MStyle        *style = NULL;
	PangoContext  *context;

	g_return_val_if_fail (pg != NULL, 0);
	g_return_val_if_fail (row >= 0 && row < SHEET_MAX_ROWS, 0);
	g_return_val_if_fail (col >= 0 && col < SHEET_MAX_COLS, 0);

	/*
	 * We are going to manually replicate a cell
	 * structure here, please remember that this is not
	 * the equivalent of a real cell.  In particular we g_new
	 * it and it must be g_free'd.
	 */
	cell = g_new0 (Cell, 1);

	cell->row_info = g_new0 (ColRowInfo, 1);
	cell->col_info = g_new0 (ColRowInfo, 1);

	style = pg_get_style (pg, row, col);

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

	cell->row_info->size_pixels = pg_get_row_height (pg, row);
	cell->col_info->size_pixels = pg_get_col_width  (pg, col);

	cell->value = NULL;
	g_signal_emit (G_OBJECT (pg), pg_signals [GET_CELL_VALUE], 0,
		       row, col, &cell->value, NULL);
	if (cell->value == NULL)
		cell->value = value_new_empty ();

	/* FIXME: probably not here.  */
	context = gdk_pango_context_get ();
	/* FIXME: barf!  */
	gdk_pango_context_set_colormap (context,
					gtk_widget_get_default_colormap ());

	res = rendered_value_new (cell, style, TRUE, context);
	g_object_unref (G_OBJECT (context));
	cell->rendered_value = res;

	return cell;
}

static void
pg_destruct_cell (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	value_release (cell->value);
	rendered_value_destroy (cell->rendered_value);

	g_free (cell->col_info);
	g_free (cell->row_info);

	g_free (cell);
}
/*****************************************************************************/

/**
 * preview_grid_destroy:
 * @object: a Preview grid
 *
 * Destroy handler
 **/
static void
preview_grid_destroy (GtkObject *object)
{
	PreviewGrid *pg;

	pg = PREVIEW_GRID (object);

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
preview_grid_realize (FooCanvasItem *item)
{
	GdkWindow   *window;
	GtkStyle    *style;
	PreviewGrid *pg;

	if (FOO_CANVAS_ITEM_CLASS (preview_grid_parent_class)->realize)
		(*FOO_CANVAS_ITEM_CLASS (preview_grid_parent_class)->realize)(item);

	pg = PREVIEW_GRID (item);
	window = GTK_WIDGET (item->canvas)->window;

	/* Set the default background color of the canvas itself to white.
	 * This makes the redraws when the canvas scrolls flicker less.
	 */
	style = gtk_style_copy (GTK_WIDGET (item->canvas)->style);
	style->bg [GTK_STATE_NORMAL] = gs_white;
	gtk_widget_set_style (GTK_WIDGET (item->canvas), style);
	gtk_style_unref (style);

	/* Configure the default grid gc */
	pg->gc.fill  = gdk_gc_new (window);
	pg->gc.cell  = gdk_gc_new (window);
	pg->gc.empty = gdk_gc_new (window);

	gdk_gc_set_foreground (pg->gc.fill, &gs_white);
	gdk_gc_set_background (pg->gc.fill, &gs_light_gray);
	gdk_gc_set_fill (pg->gc.cell, GDK_SOLID);

	/* Initialize hard-coded defaults */
	pg->def.style = mstyle_new_default ();
}

/**
 * preview_grid_unrealize:
 * @item: a PreviewGrid
 *
 * Unrealize handler
 **/
static void
preview_grid_unrealize (FooCanvasItem *item)
{
	PreviewGrid *pg = PREVIEW_GRID (item);

	g_object_unref (G_OBJECT (pg->gc.fill));
	g_object_unref (G_OBJECT (pg->gc.cell));
	g_object_unref (G_OBJECT (pg->gc.empty));
	pg->gc.fill  = 0;
	pg->gc.cell  = 0;
	pg->gc.empty = 0;

	mstyle_unref (pg->def.style);

	if (FOO_CANVAS_ITEM_CLASS (preview_grid_parent_class)->unrealize)
		(*FOO_CANVAS_ITEM_CLASS (preview_grid_parent_class)->unrealize)(item);
}

/**
 * preview_grid_update:
 *
 * Update handler
 **/
static void
preview_grid_update (FooCanvasItem *item,  double i2w_dx, double i2w_dy, int flags)
{
	if (FOO_CANVAS_ITEM_CLASS (preview_grid_parent_class)->update)
		(* FOO_CANVAS_ITEM_CLASS (preview_grid_parent_class)->update) (item, i2w_dx, i2w_dy, flags);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = INT_MAX/2;
	item->y2 = INT_MAX/2;
}

/**
 * preview_grid_draw_background:
 *
 * Draw cell background
 **/
static void
preview_grid_draw_background (GdkDrawable *drawable, PreviewGrid const *pg, MStyle const *mstyle,
			      int col, int row, int x, int y, int w, int h)
{
	GdkGC *gc = pg->gc.empty;

	if (gnumeric_background_set_gc (mstyle, gc, pg->canvas_item.canvas, FALSE))
		/* Fill the entire cell (API excludes far pixel) */
		gdk_draw_rectangle (drawable, gc, TRUE, x, y, w+1, h+1);

	style_border_draw_diag (mstyle, drawable, x, y, x+w, y+h);
}

#define border_null(b)	((b) == none || (b) == NULL)
static void
pg_style_get_row (PreviewGrid *pg, StyleRow *sr)
{
	StyleBorder const *top, *bottom, *none = style_border_none ();
	StyleBorder const *left, *right;
	int const end = sr->end_col, row = sr->row;
	int col = sr->start_col;

	sr->vertical [col] = none;
	while (col <= end) {
		MStyle const * style = pg_get_style (pg, row, col);

		sr->styles [col] = style;

		top = mstyle_get_border (style, MSTYLE_BORDER_TOP);
		bottom = mstyle_get_border (style, MSTYLE_BORDER_BOTTOM);
		left = mstyle_get_border (style, MSTYLE_BORDER_LEFT);
		right = mstyle_get_border (style, MSTYLE_BORDER_RIGHT);

		/* Cancel grids if there is a background */
		if (sr->hide_grid || mstyle_get_pattern (style) > 0) {
			if (top == none)
				top = NULL;
			if (bottom == none)
				bottom = NULL;
			if (left == none)
				left = NULL;
			if (right == none)
				right = NULL;
		}
		if (top != none && border_null (sr->top [col]))
			sr->top [col] = top;
		sr->bottom [col] = bottom;

		if (left != none && border_null (sr->vertical [col]))
			sr->vertical [col] = left;
		sr->vertical [++col] = right;
	}
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
preview_grid_draw (FooCanvasItem *item, GdkDrawable *drawable,
		   GdkEventExpose *expose)
{
	gint draw_x = expose->area.x;
	gint draw_y = expose->area.y;
	gint width  = expose->area.width;
	gint height = expose->area.height;
 	PreviewGrid *pg = PREVIEW_GRID (item);

 	/* To ensure that far and near borders get drawn we pretend to draw +-2
 	 * pixels around the target area which would include the surrounding
 	 * borders if necessary */
 	/* TODO : there is an opportunity to speed up the redraw loop by only
 	 * painting the borders of the edges and not the content.
 	 * However, that feels like more hassle that it is worth.  Look into this someday.
 	 */
 	int x, y, col, row, n;
 	int const start_col = pg_get_col_offset (pg, draw_x - 2, &x);
 	int end_col         = pg_get_col_offset (pg, draw_x + width + 2, NULL);
 	int const diff_x    = draw_x;
 	int start_row       = pg_get_row_offset (pg, draw_y - 2, &y);
 	int end_row         = pg_get_row_offset (pg, draw_y + height + 2, NULL);
 	int const diff_y    = draw_y;

	StyleRow sr, next_sr;
	MStyle const **styles;
	StyleBorder const **borders, **prev_vert;
	StyleBorder const *none = pg->gridlines ? style_border_none () : NULL;

	int *colwidths = NULL;

	style_border_none_set_color (style_color_grid ());

 	/* Fill entire region with default background (even past far edge) */
 	gdk_draw_rectangle (drawable, pg->gc.fill, TRUE,
 			    draw_x, draw_y, width, height);

	/*
	 * allocate a single blob of memory for all 8 arrays of pointers.
	 * 	- 6 arrays of n StyleBorder const *
	 * 	- 2 arrays of n MStyle const *
	 */
	n = end_col - start_col + 3; /* 1 before, 1 after, 1 fencepost */
	style_row_init (&prev_vert, &sr, &next_sr, start_col, end_col,
			g_alloca (n * 8 * sizeof (gpointer)), !pg->gridlines);

	/* load up the styles for the first row */
	next_sr.row = sr.row = row = start_row;
	pg_style_get_row (pg, &sr);

	/* Collect the column widths */
	colwidths = g_alloca (n * sizeof (int));
	colwidths -= start_col;
	for (col = start_col; col <= end_col; col++)
		colwidths[col] = pg_get_col_width (pg, col);

	for (y = -diff_y; row <= end_row; row = sr.row = next_sr.row) {
 		int row_height = pg_get_row_height (pg, row);

		if (++next_sr.row > end_row) {
			for (col = start_col ; col <= end_col; ++col)
				next_sr.vertical [col] =
				next_sr.bottom [col] = none;
		} else
			pg_style_get_row (pg, &next_sr);

		for (col = start_col, x = -diff_x; col <= end_col; col++) {
 			Cell         *cell  = pg_construct_cell (pg, row, col);
			MStyle const *style = sr.styles [col];

 			preview_grid_draw_background (drawable, pg,
 						      style, col, row, x, y,
 						      colwidths [col], row_height);

			if (!cell_is_blank (cell))
				cell_draw (cell, pg->gc.cell, drawable,
					   x, y, -1, -1, -1);

			pg_destruct_cell (cell);
 			x += colwidths [col];
 		}

		style_borders_row_draw (prev_vert, &sr,
					drawable, -diff_x, y, y+row_height,
					colwidths, TRUE);

		/* roll the pointers */
		borders = prev_vert; prev_vert = sr.vertical;
		sr.vertical = next_sr.vertical; next_sr.vertical = borders;
		borders = sr.top; sr.top = sr.bottom;
		sr.bottom = next_sr.top = next_sr.bottom; next_sr.bottom = borders;
		styles = sr.styles; sr.styles = next_sr.styles; next_sr.styles = styles;

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
preview_grid_point (FooCanvasItem *item, double x, double y, int cx, int cy,
		    FooCanvasItem **actual_item)
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
	FooCanvasItem *item = FOO_CANVAS_ITEM (preview_grid);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;

	/* Some sensible hardcoded defaults */
	preview_grid->gridlines      = FALSE;
	preview_grid->def.col_width  = 64;
	preview_grid->def.row_height = 17;
}

/**
 * preview_grid_set_arg:
 *
 * Callback to set arguments
 **/
static void
preview_grid_set_arg (GtkObject *o, GtkArg *arg, guint arg_id)
{
	FooCanvasItem *item;
	PreviewGrid *pg;

	item = FOO_CANVAS_ITEM (o);
	pg = PREVIEW_GRID (o);

	switch (arg_id){
	case ARG_RENDER_GRIDLINES :
		pg->gridlines = GTK_VALUE_BOOL (*arg);
		break;
	case ARG_DEFAULT_ROW_HEIGHT :
		pg->def.row_height = GTK_VALUE_INT (*arg);
		break;
	case ARG_DEFAULT_COL_WIDTH :
		pg->def.col_width = GTK_VALUE_INT (*arg);
		break;
	default :
		g_warning ("Unknown argument");
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
	GtkObjectClass  *klass;
	FooCanvasItemClass *item_class;

	preview_grid_parent_class = g_type_class_peek (foo_canvas_item_get_type ());

	klass = (GtkObjectClass *) preview_grid_class;
	item_class = (FooCanvasItemClass *) preview_grid_class;

	/* Manipulation */
	gtk_object_add_arg_type ("PreviewGrid::RenderGridlines", GTK_TYPE_BOOL,
				 GTK_ARG_WRITABLE, ARG_RENDER_GRIDLINES);
	gtk_object_add_arg_type ("PreviewGrid::DefaultRowHeight", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_DEFAULT_ROW_HEIGHT);
	gtk_object_add_arg_type ("PreviewGrid::DefaultColWidth", GTK_TYPE_INT,
				 GTK_ARG_WRITABLE, ARG_DEFAULT_COL_WIDTH);

	klass->set_arg = preview_grid_set_arg;
	klass->destroy = preview_grid_destroy;

	/* FooCanvasItem method overrides */
	item_class->update      = preview_grid_update;
	item_class->realize     = preview_grid_realize;
	item_class->unrealize   = preview_grid_unrealize;

	item_class->draw        = preview_grid_draw;
	item_class->point       = preview_grid_point;

	/* Create all the signals */
	pg_signals [GET_ROW_HEIGHT] = g_signal_new ( "get_row_height",
		preview_grid_get_type (),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (PreviewGridClass, get_row_height),
		(GSignalAccumulator) NULL, NULL,
		gnm__INT__INT,
		G_TYPE_INT, 1, G_TYPE_INT);
	pg_signals [GET_COL_WIDTH] = g_signal_new ( "get_col_width",
		preview_grid_get_type (),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (PreviewGridClass, get_col_width),
		(GSignalAccumulator) NULL, NULL,
		gnm__INT__INT,
		G_TYPE_INT, 1, G_TYPE_INT);
	pg_signals [GET_CELL_STYLE] = g_signal_new ( "get_cell_style",
		preview_grid_get_type (),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (PreviewGridClass, get_cell_value),
		(GSignalAccumulator) NULL, NULL,
		gnm__POINTER__INT_INT,
		G_TYPE_POINTER, 2, G_TYPE_INT, G_TYPE_INT);
	pg_signals [GET_CELL_VALUE] = g_signal_new ( "get_cell_value",
		preview_grid_get_type (),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (PreviewGridClass, get_cell_value),
		(GSignalAccumulator) NULL, NULL,
		gnm__POINTER__INT_INT,
		G_TYPE_POINTER, 2, G_TYPE_INT, G_TYPE_INT);
}

GSF_CLASS (PreviewGrid, preview_grid,
	   preview_grid_class_init, preview_grid_init,
	   FOO_TYPE_CANVAS_ITEM);

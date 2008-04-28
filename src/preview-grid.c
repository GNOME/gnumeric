/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
#include "preview-grid-impl.h"

#include "cell.h"
#include "sheet.h"
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

static FooCanvasItemClass *parent_klass;
enum {
	PREVIEW_GRID_PROP_0,
	PREVIEW_GRID_PROP_RENDER_GRIDLINES,
	PREVIEW_GRID_PROP_DEFAULT_COL_WIDTH,
	PREVIEW_GRID_PROP_DEFAULT_ROW_HEIGHT,
	PREVIEW_GRID_PROP_DEFAULT_STYLE,
	PREVIEW_GRID_PROP_DEFAULT_VALUE
};

/*****************************************************************************/

static GnmStyle *
pg_get_style (PreviewGrid *pg, int col, int row)
{
	PreviewGridClass *klass = PREVIEW_GRID_GET_CLASS (pg);
	GnmStyle *style;

	g_return_val_if_fail (col >= 0 && col < gnm_sheet_get_max_cols (pg->sheet), NULL);
	g_return_val_if_fail (row >= 0 && row < gnm_sheet_get_max_rows (pg->sheet), NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	if (klass->get_cell_style != NULL) {
		style = (klass->get_cell_style) (pg, col, row);
		if (style != NULL)
			return style;
	}

	return pg->defaults.style;
}

static GnmCell *
pg_fetch_cell (PreviewGrid *pg, int col, int row, PangoContext *context,
	       GnmStyle const *style)
{
	PreviewGridClass *klass = PREVIEW_GRID_GET_CLASS (pg);
	GnmCell  *cell;
	GnmValue *v = NULL;

	g_return_val_if_fail (klass != NULL, NULL);
	g_return_val_if_fail (pg != NULL, NULL);
	g_return_val_if_fail (col >= 0 && col < gnm_sheet_get_max_cols (pg->sheet), NULL);
	g_return_val_if_fail (row >= 0 && row < gnm_sheet_get_max_rows (pg->sheet), NULL);

	if (NULL != klass->get_cell_value)
		v = (klass->get_cell_value) (pg, col, row);
	if (NULL == v)
		v = value_dup (pg->defaults.value);

	cell = sheet_cell_fetch (pg->sheet, col, row);
	gnm_cell_set_value (cell, v);
	cell->rendered_value = gnm_rendered_value_new (cell, style,
		TRUE, context, pg->sheet->last_zoom_factor_used);

	return cell;
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
	int pixel = 1;
	int const h = pg->defaults.row_height;

	g_return_val_if_fail (pg != NULL, 0);

	do {
		if (y <= (pixel + h) || h == 0) {
			if (row_origin)
				*row_origin = pixel;
			return row;
		}
		pixel += h;
	} while (++row < gnm_sheet_get_max_rows (pg->sheet));

	if (row_origin)
		*row_origin = pixel;

	return gnm_sheet_get_max_rows (pg->sheet) - 1;
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
	int pixel = 1;
	int w;

	g_return_val_if_fail (pg != NULL, 0);

	do {
		w = pg->defaults.col_width;
		if (x <= (pixel + w) || w == 0) {
			if (col_origin)
				*col_origin = pixel;
			return col;
		}
		pixel += w;
	} while (++col < gnm_sheet_get_max_cols (pg->sheet));

	if (col_origin)
		*col_origin = pixel;

	return gnm_sheet_get_max_cols (pg->sheet) - 1;
}

static void
preview_grid_realize (FooCanvasItem *item)
{
	GtkStyle    *style;
	GdkWindow   *window = GTK_WIDGET (item->canvas)->window;
	PreviewGrid *pg = PREVIEW_GRID (item);


	if (parent_klass->realize)
		(*parent_klass->realize) (item);

	/* Set the default background color of the canvas itself to white.
	 * This makes the redraws when the canvas scrolls flicker less.
	 */
	style = gtk_style_copy (GTK_WIDGET (item->canvas)->style);
	style->bg[GTK_STATE_NORMAL] = style->white;
	gtk_widget_set_style (GTK_WIDGET (item->canvas), style);
	g_object_unref (style);

	/* Configure the default grid gc */
	pg->gc.fill  = gdk_gc_new (window);
	pg->gc.cell  = gdk_gc_new (window);
	pg->gc.empty = gdk_gc_new (window);

	gdk_gc_set_rgb_fg_color (pg->gc.fill, &gs_white);
	gdk_gc_set_rgb_bg_color (pg->gc.fill, &gs_light_gray);
	gdk_gc_set_fill (pg->gc.cell, GDK_SOLID);
}

static void
preview_grid_unrealize (FooCanvasItem *item)
{
	PreviewGrid *pg = PREVIEW_GRID (item);
	g_object_unref (pg->gc.fill);  pg->gc.fill  = NULL;
	g_object_unref (pg->gc.cell);  pg->gc.cell  = NULL;
	g_object_unref (pg->gc.empty); pg->gc.empty = NULL;
	if (parent_klass->unrealize)
		(*parent_klass->unrealize) (item);
}

static void
preview_grid_update (FooCanvasItem *item,  double i2w_dx, double i2w_dy, int flags)
{
	FooCanvasGroup *group = FOO_CANVAS_GROUP (item);
	if (parent_klass->update)
		(*parent_klass->update) (item, i2w_dx, i2w_dy, flags);

	item->x1 = group->xpos - 2;
	item->y1 = group->ypos - 2;
	item->x2 = INT_MAX/2;	/* FIXME add some num cols/rows abilities */
	item->y2 = INT_MAX/2;	/* FIXME and some flags to decide how to adapt */

	foo_canvas_item_request_redraw (item);
}

static void
preview_grid_draw_background (GdkDrawable *drawable, PreviewGrid const *pg, GnmStyle const *mstyle,
			      int col, int row, int x, int y, int w, int h)
{
	GdkGC *gc = pg->gc.empty;

	if (gnumeric_background_set_gc (mstyle, gc, pg->base.item.canvas, FALSE))
		/* Fill the entire cell (API excludes far pixel) */
		gdk_draw_rectangle (drawable, gc, TRUE, x, y, w+1, h+1);

	gnm_style_border_draw_diag (mstyle, drawable, x, y, x+w, y+h);
}

#define border_null(b)	((b) == none || (b) == NULL)
static void
pg_style_get_row (PreviewGrid *pg, GnmStyleRow *sr)
{
	GnmBorder const *top, *bottom, *none = gnm_style_border_none ();
	GnmBorder const *left, *right;
	int const end = sr->end_col, row = sr->row;
	int col = sr->start_col;

	sr->vertical [col] = none;
	while (col <= end) {
		GnmStyle const * style = pg_get_style (pg, col, row);

		sr->styles [col] = style;

		top = gnm_style_get_border (style, MSTYLE_BORDER_TOP);
		bottom = gnm_style_get_border (style, MSTYLE_BORDER_BOTTOM);
		left = gnm_style_get_border (style, MSTYLE_BORDER_LEFT);
		right = gnm_style_get_border (style, MSTYLE_BORDER_RIGHT);

		/* Cancel grids if there is a background */
		if (sr->hide_grid || gnm_style_get_pattern (style) > 0) {
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

/* no spans or merges */
static void
preview_grid_draw (FooCanvasItem *item, GdkDrawable *drawable,
		   GdkEventExpose *expose)
{
	gint draw_x = expose->area.x;
	gint draw_y = expose->area.y;
	gint width  = expose->area.width;
	gint height = expose->area.height;
	PreviewGrid *pg = PREVIEW_GRID (item);
	PangoContext *context = gtk_widget_get_pango_context
		(gtk_widget_get_toplevel (GTK_WIDGET (item->canvas)));

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
	int diff_x    = x;
	int start_row       = pg_get_row_offset (pg, draw_y - 2, &y);
	int end_row         = pg_get_row_offset (pg, draw_y + height + 2, NULL);
	int diff_y    = y;
	int row_height = pg->defaults.row_height;

	GnmStyleRow sr, next_sr;
	GnmStyle const **styles;
	GnmBorder const **borders, **prev_vert;
	GnmBorder const *none = pg->gridlines ? gnm_style_border_none () : NULL;

	int *colwidths = NULL;

	gnm_style_border_none_set_color (style_color_grid ());

	/*
	 * allocate a single blob of memory for all 8 arrays of pointers.
	 *	- 6 arrays of n GnmBorder const *
	 *	- 2 arrays of n GnmStyle const *
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
		colwidths[col] = pg->defaults.col_width;

	foo_canvas_w2c (item->canvas, diff_x, diff_y, &diff_x, &diff_y);
	/* Fill entire region with default background (even past far edge) */
	gdk_draw_rectangle (drawable, pg->gc.fill, TRUE,
			    diff_x, diff_y, width, height);

	for (y = diff_y; row <= end_row; row = sr.row = next_sr.row) {
		if (++next_sr.row > end_row) {
			for (col = start_col ; col <= end_col; ++col)
				next_sr.vertical [col] =
				next_sr.bottom [col] = none;
		} else
			pg_style_get_row (pg, &next_sr);

		for (col = start_col, x = diff_x; col <= end_col; col++) {
			GnmStyle const *style = sr.styles [col];
			GnmCell const  *cell  = pg_fetch_cell (pg,
				col, row, context, style);

			preview_grid_draw_background (drawable, pg,
						      style, col, row, x, y,
						      colwidths [col], row_height);

			if (!gnm_cell_is_empty (cell))
				cell_draw (cell, pg->gc.cell, drawable,
					   x, y, colwidths [col], row_height, -1);

			x += colwidths [col];
		}

		gnm_style_borders_row_draw (prev_vert, &sr,
					drawable, diff_x, y, y+row_height,
					colwidths, TRUE, 1 /* cheat dir == 1 for now */);

		/* roll the pointers */
		borders = prev_vert; prev_vert = sr.vertical;
		sr.vertical = next_sr.vertical; next_sr.vertical = borders;
		borders = sr.top; sr.top = sr.bottom;
		sr.bottom = next_sr.top = next_sr.bottom; next_sr.bottom = borders;
		styles = sr.styles; sr.styles = next_sr.styles; next_sr.styles = styles;

		y += row_height;
	}
}

static double
preview_grid_point (FooCanvasItem *item, double x, double y, int cx, int cy,
		    FooCanvasItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

static void
preview_grid_set_property (GObject *obj, guint param_id,
			   GValue const *value, GParamSpec *pspec)
{
	PreviewGrid *pg = PREVIEW_GRID (obj);

	switch (param_id){
	case PREVIEW_GRID_PROP_RENDER_GRIDLINES :
		pg->gridlines = g_value_get_boolean (value);
		break;
	case PREVIEW_GRID_PROP_DEFAULT_COL_WIDTH :
		pg->defaults.col_width = g_value_get_uint (value);
		break;
	case PREVIEW_GRID_PROP_DEFAULT_ROW_HEIGHT :
		pg->defaults.row_height = g_value_get_uint (value);
		break;
	case PREVIEW_GRID_PROP_DEFAULT_STYLE : { /* add a  ref */
		GnmStyle *style = g_value_get_pointer (value);
		g_return_if_fail (style != NULL);
		gnm_style_ref (style);
		gnm_style_unref (pg->defaults.style);
		pg->defaults.style = style;
		break;
	}
	case PREVIEW_GRID_PROP_DEFAULT_VALUE : { /* steal ownership */
		GnmValue *val = g_value_get_pointer (value);
		g_return_if_fail (val != NULL);
		if (pg->defaults.value != val) {
			value_release (pg->defaults.value);
			pg->defaults.value = val;
		}
		break;
	}
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return; /* NOTE : RETURN */
	}

	foo_canvas_item_request_update (FOO_CANVAS_ITEM (obj));
}

static void
preview_grid_dispose (GObject *obj)
{
	PreviewGrid *pg = PREVIEW_GRID (obj);

	if (pg->defaults.style != NULL) {
		gnm_style_unref (pg->defaults.style);
		pg->defaults.style = NULL;
	}
	if (pg->defaults.value != NULL) {
		value_release (pg->defaults.value);
		pg->defaults.value = NULL;
	}

	if (pg->sheet) {
		g_object_unref (pg->sheet);
		pg->sheet = NULL;
	}

	G_OBJECT_CLASS (parent_klass)->dispose (obj);
}

static void
preview_grid_init (PreviewGrid *pg)
{
	FooCanvasItem *item = FOO_CANVAS_ITEM (pg);

	item->x1 = 0;
	item->y1 = 0;
	item->x2 = 0;
	item->y2 = 0;

	pg->sheet = g_object_new (GNM_SHEET_TYPE, NULL);
	pg->sheet->index_in_wb = -1;
	pg->sheet->workbook = NULL;
	pg->gridlines = FALSE;
	pg->defaults.col_width = 64;
	pg->defaults.row_height = 17;
	pg->defaults.style = gnm_style_new_default ();
	pg->defaults.value = value_new_empty ();
}

static void
preview_grid_class_init (GObjectClass *gobject_klass)
{
	FooCanvasItemClass *item_klass = (FooCanvasItemClass *)gobject_klass;

	parent_klass = g_type_class_peek_parent (gobject_klass);

	gobject_klass->set_property = preview_grid_set_property;
	gobject_klass->dispose = preview_grid_dispose;
	g_object_class_install_property (gobject_klass, PREVIEW_GRID_PROP_RENDER_GRIDLINES,
		g_param_spec_boolean ("render-gridlines", NULL, NULL,
			FALSE,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));
        g_object_class_install_property (gobject_klass, PREVIEW_GRID_PROP_DEFAULT_COL_WIDTH,
                 g_param_spec_uint ("default-col-width", NULL, NULL,
			0, G_MAXUINT, 0,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));
        g_object_class_install_property (gobject_klass, PREVIEW_GRID_PROP_DEFAULT_ROW_HEIGHT,
                 g_param_spec_uint ("default-row-height", NULL, NULL,
			0, G_MAXUINT, 0,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));
        g_object_class_install_property (gobject_klass, PREVIEW_GRID_PROP_DEFAULT_STYLE,
                 g_param_spec_pointer ("default-style", NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));
        g_object_class_install_property (gobject_klass, PREVIEW_GRID_PROP_DEFAULT_VALUE,
                 g_param_spec_pointer ("default-value", NULL, NULL,
			GSF_PARAM_STATIC | G_PARAM_WRITABLE));

	item_klass->update      = preview_grid_update;
	item_klass->realize     = preview_grid_realize;
	item_klass->unrealize   = preview_grid_unrealize;
	item_klass->draw        = preview_grid_draw;
	item_klass->point       = preview_grid_point;
}

GSF_CLASS (PreviewGrid, preview_grid,
	   preview_grid_class_init, preview_grid_init,
	   FOO_TYPE_CANVAS_GROUP);

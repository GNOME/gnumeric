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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <preview-grid-impl.h>

#include <cell.h>
#include <sheet.h>
#include <cell-draw.h>
#include <colrow.h>
#include <pattern.h>
#include <mstyle.h>
#include <rendered-value.h>
#include <sheet-style.h>
#include <style-border.h>
#include <style-color.h>
#include <value.h>
#include <gnm-marshalers.h>

#include <gsf/gsf-impl-utils.h>

static GocItemClass *parent_klass;
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
pg_get_style (GnmPreviewGrid *pg, int col, int row)
{
	GnmPreviewGridClass *klass = GNM_PREVIEW_GRID_GET_CLASS (pg);
	GnmStyle *style;

	g_return_val_if_fail (col >= 0 && col < gnm_sheet_get_max_cols (pg->sheet), NULL);
	g_return_val_if_fail (row >= 0 && row < gnm_sheet_get_max_rows (pg->sheet), NULL);
	g_return_val_if_fail (klass != NULL, NULL);

	if (klass->get_cell_style) {
		style = klass->get_cell_style (pg, col, row);
		if (style != NULL)
			return style;
	}

	return pg->defaults.style;
}

static GnmCell *
pg_fetch_cell (GnmPreviewGrid *pg, int col, int row)
{
	GnmPreviewGridClass *klass = GNM_PREVIEW_GRID_GET_CLASS (pg);
	GnmCell *cell;
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

	gnm_cell_render_value (cell, TRUE);

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
pg_get_row_offset (GnmPreviewGrid *pg, int const y, int *row_origin)
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

	return gnm_sheet_get_last_row (pg->sheet);
}

/**
 * pg_get_col_offset:
 * @x: offset
 * @col_origin: if not null the origin of the column containing pixel @x is put here
 *
 * Return value: Column containing pixel x (and origin in @col_origin)
 **/
static int
pg_get_col_offset (GnmPreviewGrid *pg, int const x, int *col_origin)
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

	return gnm_sheet_get_last_col (pg->sheet);
}

static void
preview_grid_update_bounds (GocItem *item)
{
	item->x0 = -2;
	item->y0 = -2;
	item->x1 = INT_MAX/2;	/* FIXME add some num cols/rows abilities */
	item->y1 = INT_MAX/2;	/* FIXME and some flags to decide how to adapt */
}

static void
preview_grid_draw_background (cairo_t *cr, GnmPreviewGrid const *pg, GnmStyle const *mstyle,
			      int col, int row, int x, int y, int w, int h)
{
	if (gnm_pattern_background_set (mstyle, cr, FALSE, NULL)) {
		cairo_rectangle (cr, x, y, w+1, h+1);
		cairo_fill (cr);
	}
	gnm_style_border_draw_diag (mstyle, cr, x, y, x+w, y+h);
}

static void
pg_style_get_row (GnmPreviewGrid *pg, GnmStyleRow *sr)
{
	int const row = sr->row;
	int col;

	for (col = sr->start_col; col <= sr->end_col; col++) {
		GnmStyle const *style = pg_get_style (pg, col, row);
		sheet_style_set_pos (pg->sheet, col, row,
				     gnm_style_dup (style));
	}

	sheet_style_get_row (pg->sheet, sr);
}

/* no spans or merges */
static gboolean
preview_grid_draw_region (GocItem const *item, cairo_t *cr,
			  double x0, double y0, double x1, double y1)
{
	GnmPreviewGrid *pg = GNM_PREVIEW_GRID (item);

	/* To ensure that far and near borders get drawn we pretend to draw +-2
	 * pixels around the target area which would include the surrounding
	 * borders if necessary */
	/* TODO : there is an opportunity to speed up the redraw loop by only
	 * painting the borders of the edges and not the content.
	 * However, that feels like more hassle that it is worth.  Look into this someday.
	 */
	int x, y, col, row, n;
	int const start_col = pg_get_col_offset (pg, x0 - 2, &x);
	int end_col         = pg_get_col_offset (pg, x1 + 2, NULL);
	int diff_x    = x;
	int start_row       = pg_get_row_offset (pg, y0 - 2, &y);
	int end_row         = pg_get_row_offset (pg, y1 + 2, NULL);
	int diff_y    = y;
	int row_height = pg->defaults.row_height;

	GnmStyleRow sr, next_sr;
	GnmStyle const **styles;
	GnmBorder const **borders, **prev_vert;
	GnmBorder const *none = pg->gridlines ? gnm_style_border_none () : NULL;
	gpointer *sr_array_data;

	int *colwidths = NULL;

	gnm_style_border_none_set_color (style_color_grid (goc_item_get_style_context (item)));

	/*
	 * allocate a single blob of memory for all 8 arrays of pointers.
	 *	- 6 arrays of n GnmBorder const *
	 *	- 2 arrays of n GnmStyle const *
	 */
	n = end_col - start_col + 3; /* 1 before, 1 after, 1 fencepost */
	sr_array_data = g_new (gpointer, n * 8);
	style_row_init (&prev_vert, &sr, &next_sr, start_col, end_col,
			sr_array_data, !pg->gridlines);

	/* load up the styles for the first row */
	next_sr.row = sr.row = row = start_row;
	pg_style_get_row (pg, &sr);

	/* Collect the column widths */
	colwidths = g_new (int, n);
	colwidths -= start_col;
	for (col = start_col; col <= end_col; col++)
		colwidths[col] = pg->defaults.col_width;

	/* Fill entire region with default background (even past far edge) */
	gtk_render_background (goc_item_get_style_context (item),
			       cr, diff_x, diff_y, x1 - x0, y1 - y0);

	for (y = diff_y; row <= end_row; row = sr.row = next_sr.row) {
		if (++next_sr.row > end_row) {
			for (col = start_col ; col <= end_col; ++col)
				next_sr.vertical[col] =
				next_sr.bottom[col] = none;
		} else
			pg_style_get_row (pg, &next_sr);

		for (col = start_col, x = diff_x; col <= end_col; col++) {
			GnmStyle const *style = sr.styles[col];
			GnmCell const *cell = pg_fetch_cell (pg, col, row);

			preview_grid_draw_background (cr, pg,
						      style, col, row, x, y,
						      colwidths[col], row_height);

			if (!gnm_cell_is_empty (cell))
				cell_draw (cell, cr,
					   x, y, colwidths[col], row_height,
					   -1, FALSE, NULL);

			x += colwidths[col];
		}

		gnm_style_borders_row_draw
			(prev_vert, &sr, cr,
			 diff_x, y, y + row_height,
			 colwidths, TRUE, 1 /* cheat dir == 1 for now */);

		/* roll the pointers */
		borders = prev_vert; prev_vert = sr.vertical;
		sr.vertical = next_sr.vertical; next_sr.vertical = borders;
		borders = sr.top; sr.top = sr.bottom;
		sr.bottom = next_sr.top = next_sr.bottom; next_sr.bottom = borders;
		styles = sr.styles; sr.styles = next_sr.styles; next_sr.styles = styles;

		y += row_height;
	}

	g_free (sr_array_data);
	g_free (colwidths + start_col); // Offset reverts -= from above
	return TRUE;
}

static double
preview_grid_distance (GocItem *item, double cx, double cy,
		       GocItem **actual_item)
{
	*actual_item = item;
	return 0.0;
}

static void
preview_grid_set_property (GObject *obj, guint param_id,
			   GValue const *value, GParamSpec *pspec)
{
	GnmPreviewGrid *pg = GNM_PREVIEW_GRID (obj);

	switch (param_id){
	case PREVIEW_GRID_PROP_RENDER_GRIDLINES:
		pg->gridlines = g_value_get_boolean (value);
		break;
	case PREVIEW_GRID_PROP_DEFAULT_COL_WIDTH:
		pg->defaults.col_width = g_value_get_uint (value);
		break;
	case PREVIEW_GRID_PROP_DEFAULT_ROW_HEIGHT:
		pg->defaults.row_height = g_value_get_uint (value);
		break;
	case PREVIEW_GRID_PROP_DEFAULT_STYLE : {
		GnmStyle *style = g_value_dup_boxed (value);
		g_return_if_fail (style != NULL);
		gnm_style_unref (pg->defaults.style);
		pg->defaults.style = style;
		break;
	}
	case PREVIEW_GRID_PROP_DEFAULT_VALUE: {
		GnmValue *val = g_value_dup_boxed (value);
		g_return_if_fail (val != NULL);
		value_release (pg->defaults.value);
		pg->defaults.value = val;
		break;
	}
	default: G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, param_id, pspec);
		return; /* NOTE : RETURN */
	}

	goc_item_invalidate (GOC_ITEM (obj));
}

static void
preview_grid_dispose (GObject *obj)
{
	GnmPreviewGrid *pg = GNM_PREVIEW_GRID (obj);

	if (pg->defaults.style != NULL) {
		gnm_style_unref (pg->defaults.style);
		pg->defaults.style = NULL;
	}
	value_release (pg->defaults.value);
	pg->defaults.value = NULL;

	g_clear_object (&pg->sheet);

	G_OBJECT_CLASS (parent_klass)->dispose (obj);
}

static void
gnm_preview_grid_init (GnmPreviewGrid *pg)
{
	pg->sheet = g_object_new (GNM_SHEET_TYPE,
				  "rows", 256,
				  "columns", 256,
				  NULL);
	pg->gridlines = FALSE;
	pg->defaults.col_width = 64;
	pg->defaults.row_height = 17;
	pg->defaults.style = gnm_style_new_default ();
	pg->defaults.value = value_new_empty ();
}

static void
gnm_preview_grid_class_init (GObjectClass *gobject_klass)
{
	GocItemClass *item_klass = (GocItemClass *)gobject_klass;

	parent_klass = g_type_class_peek_parent (gobject_klass);

	gobject_klass->set_property = preview_grid_set_property;
	gobject_klass->dispose = preview_grid_dispose;
	g_object_class_install_property
		(gobject_klass, PREVIEW_GRID_PROP_RENDER_GRIDLINES,
		 g_param_spec_boolean ("render-gridlines", NULL, NULL,
				       FALSE,
				       GSF_PARAM_STATIC | G_PARAM_WRITABLE));
        g_object_class_install_property
		(gobject_klass, PREVIEW_GRID_PROP_DEFAULT_COL_WIDTH,
                 g_param_spec_uint ("default-col-width", NULL, NULL,
				    0, G_MAXUINT, 0,
				    GSF_PARAM_STATIC | G_PARAM_WRITABLE));
        g_object_class_install_property
		(gobject_klass, PREVIEW_GRID_PROP_DEFAULT_ROW_HEIGHT,
                 g_param_spec_uint ("default-row-height", NULL, NULL,
				    0, G_MAXUINT, 0,
				    GSF_PARAM_STATIC | G_PARAM_WRITABLE));
        g_object_class_install_property
		(gobject_klass, PREVIEW_GRID_PROP_DEFAULT_STYLE,
                 g_param_spec_boxed ("default-style", NULL, NULL,
				      gnm_style_get_type (),
				      GSF_PARAM_STATIC | G_PARAM_WRITABLE));
        g_object_class_install_property
		(gobject_klass, PREVIEW_GRID_PROP_DEFAULT_VALUE,
                 g_param_spec_boxed ("default-value", NULL, NULL,
				     gnm_value_get_type (),
				     GSF_PARAM_STATIC | G_PARAM_WRITABLE));

	item_klass->update_bounds = preview_grid_update_bounds;
	item_klass->draw_region = preview_grid_draw_region;
	item_klass->distance    = preview_grid_distance;
}

GSF_CLASS (GnmPreviewGrid, gnm_preview_grid,
	   gnm_preview_grid_class_init, gnm_preview_grid_init,
	   GOC_TYPE_GROUP)

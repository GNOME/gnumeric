/*
 * sheet.c: Implements the sheet management and per-sheet storage
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 1997-1999 Miguel de Icaza (miguel@kernel.org)
 * Copyright (C) 1999-2009 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <sheet.h>

#include <sheet-view.h>
#include <command-context.h>
#include <sheet-control.h>
#include <sheet-style.h>
#include <sheet-conditions.h>
#include <workbook-priv.h>
#include <workbook-control.h>
#include <workbook-view.h>
#include <parse-util.h>
#include <dependent.h>
#include <value.h>
#include <number-match.h>
#include <clipboard.h>
#include <selection.h>
#include <ranges.h>
#include <print-info.h>
#include <mstyle.h>
#include <style-color.h>
#include <style-font.h>
#include <application.h>
#include <commands.h>
#include <cellspan.h>
#include <cell.h>
#include <sheet-merge.h>
#include <sheet-private.h>
#include <expr-name.h>
#include <expr.h>
#include <rendered-value.h>
#include <gnumeric-conf.h>
#include <sheet-object-impl.h>
#include <sheet-object-cell-comment.h>
#include <tools/gnm-solver.h>
#include <hlink.h>
#include <sheet-filter.h>
#include <sheet-filter-combo.h>
#include <gnm-sheet-slicer.h>
#include <tools/scenarios.h>
#include <cell-draw.h>
#include <sort.h>
#include <gutils.h>
#include <goffice/goffice.h>

#include <gnm-i18n.h>
#include <gsf/gsf-impl-utils.h>
#include <stdlib.h>
#include <string.h>

static gboolean debug_redraw;

static GnmSheetSize *
gnm_sheet_size_copy (GnmSheetSize *size)
{
	GnmSheetSize *res = g_new (GnmSheetSize, 1);
	*res = *size;
	return res;
}

GType
gnm_sheet_size_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmSheetSize",
			 (GBoxedCopyFunc)gnm_sheet_size_copy,
			 (GBoxedFreeFunc)g_free);
	}
	return t;
}

enum {
	DETACHED_FROM_WORKBOOK,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

typedef struct {
	GObjectClass parent;

	void (*detached_from_workbook) (Sheet *, Workbook *wb);
} GnmSheetClass;
typedef Sheet GnmSheet;

enum {
	PROP_0,
	PROP_SHEET_TYPE,
	PROP_WORKBOOK,
	PROP_NAME,
	PROP_RTL,
	PROP_VISIBILITY,
	PROP_DISPLAY_FORMULAS,
	PROP_DISPLAY_ZEROS,
	PROP_DISPLAY_GRID,
	PROP_DISPLAY_COLUMN_HEADER,
	PROP_DISPLAY_ROW_HEADER,
	PROP_DISPLAY_OUTLINES,
	PROP_DISPLAY_OUTLINES_BELOW,
	PROP_DISPLAY_OUTLINES_RIGHT,

	PROP_PROTECTED,
	PROP_PROTECTED_ALLOW_EDIT_OBJECTS,
	PROP_PROTECTED_ALLOW_EDIT_SCENARIOS,
	PROP_PROTECTED_ALLOW_CELL_FORMATTING,
	PROP_PROTECTED_ALLOW_COLUMN_FORMATTING,
	PROP_PROTECTED_ALLOW_ROW_FORMATTING,
	PROP_PROTECTED_ALLOW_INSERT_COLUMNS,
	PROP_PROTECTED_ALLOW_INSERT_ROWS,
	PROP_PROTECTED_ALLOW_INSERT_HYPERLINKS,
	PROP_PROTECTED_ALLOW_DELETE_COLUMNS,
	PROP_PROTECTED_ALLOW_DELETE_ROWS,
	PROP_PROTECTED_ALLOW_SELECT_LOCKED_CELLS,
	PROP_PROTECTED_ALLOW_SORT_RANGES,
	PROP_PROTECTED_ALLOW_EDIT_AUTO_FILTERS,
	PROP_PROTECTED_ALLOW_EDIT_PIVOTTABLE,
	PROP_PROTECTED_ALLOW_SELECT_UNLOCKED_CELLS,

	PROP_CONVENTIONS,
	PROP_USE_R1C1,

	PROP_TAB_FOREGROUND,
	PROP_TAB_BACKGROUND,
	PROP_ZOOM_FACTOR,

	PROP_COLUMNS,
	PROP_ROWS
};

static void gnm_sheet_finalize (GObject *obj);

static GObjectClass *parent_class;

static void
col_row_collection_resize (ColRowCollection *infos, int size)
{
	int end_idx = COLROW_SEGMENT_INDEX (size);
	int i = infos->info->len - 1;

	while (i >= end_idx) {
		ColRowSegment *segment = g_ptr_array_index (infos->info, i);
		if (segment) {
			g_free (segment);
			g_ptr_array_index (infos->info, i) = NULL;
		}
		i--;
	}

	g_ptr_array_set_size (infos->info, end_idx);
}

static void
sheet_set_direction (Sheet *sheet, gboolean text_is_rtl)
{
	GnmRange r;

	text_is_rtl = !!text_is_rtl;
	if (text_is_rtl == sheet->text_is_rtl)
		return;

	sheet_mark_dirty (sheet);

	sheet->text_is_rtl = text_is_rtl;
	sheet->priv->reposition_objects.col = 0;
	sheet_range_calc_spans (sheet,
				range_init_full_sheet (&r, sheet),
				GNM_SPANCALC_RE_RENDER);
}

static void
sheet_set_visibility (Sheet *sheet, GnmSheetVisibility visibility)
{
	if (sheet->visibility == visibility)
		return;

	sheet->visibility = visibility;
	sheet_mark_dirty (sheet);
}

static void
cb_re_render_formulas (G_GNUC_UNUSED gpointer unused,
		       GnmCell *cell,
		       G_GNUC_UNUSED gpointer user)
{
	if (gnm_cell_has_expr (cell)) {
		gnm_cell_unrender (cell);
		sheet_cell_queue_respan (cell);
	}
}

static void
re_render_formulas (Sheet const *sheet)
{
	sheet_cell_foreach (sheet, (GHFunc)cb_re_render_formulas, NULL);
}

static void
sheet_set_conventions (Sheet *sheet, GnmConventions const *convs)
{
	if (sheet->convs == convs)
		return;
	gnm_conventions_unref (sheet->convs);
	sheet->convs = gnm_conventions_ref (convs);
	if (sheet->display_formulas)
		re_render_formulas (sheet);
	SHEET_FOREACH_VIEW (sheet, sv,
		sv->edit_pos_changed.content = TRUE;);
	sheet_mark_dirty (sheet);
}

GnmConventions const *
sheet_get_conventions (Sheet const *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), gnm_conventions_default);

	return sheet->convs;
}

static void
cb_sheet_set_hide_zeros (G_GNUC_UNUSED gpointer unused,
			 GnmCell *cell,
			 G_GNUC_UNUSED gpointer user)
{
	if (gnm_cell_is_zero (cell))
		gnm_cell_unrender (cell);
}

static void
sheet_set_hide_zeros (Sheet *sheet, gboolean hide)
{
	hide = !!hide;
	if (sheet->hide_zero == hide)
		return;

	sheet->hide_zero = hide;
	sheet_mark_dirty (sheet);

	sheet_cell_foreach (sheet, (GHFunc)cb_sheet_set_hide_zeros, NULL);
}

static void
sheet_set_name (Sheet *sheet, char const *new_name)
{
	Workbook *wb = sheet->workbook;
	gboolean attached;
	Sheet *sucker;
	char *new_name_unquoted;

	g_return_if_fail (new_name != NULL);

	/* No change whatsoever.  */
	if (go_str_compare (sheet->name_unquoted, new_name) == 0)
		return;

	/* Mark the sheet dirty unless this is the initial name.  */
	if (sheet->name_unquoted)
		sheet_mark_dirty (sheet);

	sucker = wb ? workbook_sheet_by_name (wb, new_name) : NULL;
	if (sucker && sucker != sheet) {
		/*
		 * Prevent a name clash.  With this you can swap names by
		 * setting just the two names.
		 */
		char *sucker_name = workbook_sheet_get_free_name (wb, new_name, TRUE, FALSE);
#if 0
		g_warning ("Renaming %s to %s to avoid clash.\n", sucker->name_unquoted, sucker_name);
#endif
		g_object_set (sucker, "name", sucker_name, NULL);
		g_free (sucker_name);
	}

	attached = wb != NULL &&
		sheet->index_in_wb != -1 &&
		sheet->name_case_insensitive;

	/* FIXME: maybe have workbook_sheet_detach_internal for this.  */
	if (attached)
		g_hash_table_remove (wb->sheet_hash_private,
				     sheet->name_case_insensitive);

	/* Copy before free.  */
	new_name_unquoted = g_strdup (new_name);

	g_free (sheet->name_unquoted);
	g_free (sheet->name_quoted);
	g_free (sheet->name_unquoted_collate_key);
	g_free (sheet->name_case_insensitive);
	sheet->name_unquoted = new_name_unquoted;
	sheet->name_quoted = g_string_free
		(gnm_expr_conv_quote (sheet->convs, new_name_unquoted),
		 FALSE);
	sheet->name_unquoted_collate_key =
		g_utf8_collate_key (new_name_unquoted, -1);
	sheet->name_case_insensitive =
		g_utf8_casefold (new_name_unquoted, -1);

	/* FIXME: maybe have workbook_sheet_attach_internal for this.  */
	if (attached)
		g_hash_table_insert (wb->sheet_hash_private,
				     sheet->name_case_insensitive,
				     sheet);

	if (!sheet->being_constructed &&
	    sheet->sheet_type == GNM_SHEET_DATA) {
		/* We have to fix the Sheet_Title name */
		GnmNamedExpr *nexpr;
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, sheet);
		nexpr = expr_name_lookup (&pp, "Sheet_Title");
		if (nexpr) {
			GnmExprTop const *texpr =
				gnm_expr_top_new_constant
				(value_new_string (sheet->name_unquoted));
			expr_name_set_expr (nexpr, texpr);
		}
	}
}

struct resize_colrow {
	Sheet *sheet;
	gboolean is_cols;
	double scale;
};

static gboolean
cb_colrow_compute_pixels_from_pts (GnmColRowIter const *iter,
				   gpointer data_)
{
	struct resize_colrow *data = data_;
	colrow_compute_pixels_from_pts ((ColRowInfo *)iter->cri,
					data->sheet, data->is_cols,
					data->scale);
	return FALSE;
}

static void
cb_clear_rendered_cells (G_GNUC_UNUSED gpointer ignored, GnmCell *cell)
{
	if (gnm_cell_get_rendered_value (cell) != NULL) {
		sheet_cell_queue_respan (cell);
		gnm_cell_unrender (cell);
	}
}

/**
 * sheet_range_unrender:
 * @sheet: sheet to change
 * @r: (nullable): range to unrender
 *
 * Unrenders all cells in the given range.  If @r is %NULL, the all cells
 * in the sheet are unrendered.
 */
void
sheet_range_unrender (Sheet *sheet, GnmRange const *r)
{
	GPtrArray *cells = sheet_cells (sheet, r);
	unsigned ui;

	for (ui = 0; ui < cells->len; ui++) {
		GnmCell *cell = g_ptr_array_index (cells, ui);
		gnm_cell_unrender (cell);
	}

	g_ptr_array_unref (cells);
}


static void
sheet_scale_changed (Sheet *sheet, gboolean cols_rescaled, gboolean rows_rescaled)
{
	g_return_if_fail (cols_rescaled || rows_rescaled);

	/* Then every column and row */
	if (cols_rescaled) {
		struct resize_colrow closure;

		closure.sheet = sheet;
		closure.is_cols = TRUE;
		closure.scale = colrow_compute_pixel_scale (sheet, TRUE);

		colrow_compute_pixels_from_pts (&sheet->cols.default_style,
						sheet, TRUE, closure.scale);
		sheet_colrow_foreach (sheet, TRUE, 0, -1,
				      cb_colrow_compute_pixels_from_pts,
				      &closure);
		gnm_sheet_mark_colrow_changed (sheet, 0, TRUE);
	}
	if (rows_rescaled) {
		struct resize_colrow closure;

		closure.sheet = sheet;
		closure.is_cols = FALSE;
		closure.scale = colrow_compute_pixel_scale (sheet, FALSE);

		colrow_compute_pixels_from_pts (&sheet->rows.default_style,
						sheet, FALSE, closure.scale);
		sheet_colrow_foreach (sheet, FALSE, 0, -1,
				      cb_colrow_compute_pixels_from_pts,
				      &closure);
		gnm_sheet_mark_colrow_changed (sheet, 0, FALSE);
	}

	sheet_cell_foreach (sheet, (GHFunc)&cb_clear_rendered_cells, NULL);
	SHEET_FOREACH_CONTROL (sheet, view, control, sc_scale_changed (control););
}

static void
sheet_set_display_formulas (Sheet *sheet, gboolean display)
{
	display = !!display;
	if (sheet->display_formulas == display)
		return;

	sheet->display_formulas = display;
	sheet_mark_dirty (sheet);
	if (!sheet->being_constructed)
		sheet_scale_changed (sheet, TRUE, FALSE);
}

static void
sheet_set_zoom_factor (Sheet *sheet, double factor)
{
	if (fabs (factor - sheet->last_zoom_factor_used) < 1e-6)
		return;
	sheet->last_zoom_factor_used = factor;
	if (!sheet->being_constructed)
		sheet_scale_changed (sheet, TRUE, TRUE);
}

static void
gnm_sheet_set_property (GObject *object, guint property_id,
			GValue const *value, GParamSpec *pspec)
{
	Sheet *sheet = (Sheet *)object;

	switch (property_id) {
	case PROP_SHEET_TYPE:
		/* Construction-time only */
		sheet->sheet_type = g_value_get_enum (value);
		break;
	case PROP_WORKBOOK:
		/* Construction-time only */
		sheet->workbook = g_value_get_object (value);
		break;
	case PROP_NAME:
		sheet_set_name (sheet, g_value_get_string (value));
		break;
	case PROP_RTL:
		sheet_set_direction (sheet, g_value_get_boolean (value));
		break;
	case PROP_VISIBILITY:
		sheet_set_visibility (sheet, g_value_get_enum (value));
		break;
	case PROP_DISPLAY_FORMULAS:
		sheet_set_display_formulas (sheet, g_value_get_boolean (value));
		break;
	case PROP_DISPLAY_ZEROS:
		sheet_set_hide_zeros (sheet, !g_value_get_boolean (value));
		break;
	case PROP_DISPLAY_GRID:
		sheet->hide_grid = !g_value_get_boolean (value);
		break;
	case PROP_DISPLAY_COLUMN_HEADER:
		sheet->hide_col_header = !g_value_get_boolean (value);
		break;
	case PROP_DISPLAY_ROW_HEADER:
		sheet->hide_row_header = !g_value_get_boolean (value);
		break;
	case PROP_DISPLAY_OUTLINES:
		sheet->display_outlines = !!g_value_get_boolean (value);
		break;
	case PROP_DISPLAY_OUTLINES_BELOW:
		sheet->outline_symbols_below = !!g_value_get_boolean (value);
		break;
	case PROP_DISPLAY_OUTLINES_RIGHT:
		sheet->outline_symbols_right = !!g_value_get_boolean (value);
		break;

	case PROP_PROTECTED:
		sheet->is_protected = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_OBJECTS:
		sheet->protected_allow.edit_objects = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_SCENARIOS:
		sheet->protected_allow.edit_scenarios = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_CELL_FORMATTING:
		sheet->protected_allow.cell_formatting = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_COLUMN_FORMATTING:
		sheet->protected_allow.column_formatting = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_ROW_FORMATTING:
		sheet->protected_allow.row_formatting = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_COLUMNS:
		sheet->protected_allow.insert_columns = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_ROWS:
		sheet->protected_allow.insert_rows = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_HYPERLINKS:
		sheet->protected_allow.insert_hyperlinks = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_DELETE_COLUMNS:
		sheet->protected_allow.delete_columns = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_DELETE_ROWS:
		sheet->protected_allow.delete_rows = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_SELECT_LOCKED_CELLS:
		sheet->protected_allow.select_locked_cells = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_SORT_RANGES:
		sheet->protected_allow.sort_ranges = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_AUTO_FILTERS:
		sheet->protected_allow.edit_auto_filters = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_PIVOTTABLE:
		sheet->protected_allow.edit_pivottable = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_SELECT_UNLOCKED_CELLS:
		sheet->protected_allow.select_unlocked_cells = !!g_value_get_boolean (value);
		break;

	case PROP_CONVENTIONS:
		sheet_set_conventions (sheet, g_value_get_boxed (value));
		break;
	case PROP_USE_R1C1: /* convenience api */
		sheet_set_conventions (sheet, !!g_value_get_boolean (value)
			? gnm_conventions_xls_r1c1 : gnm_conventions_default);
		break;

	case PROP_TAB_FOREGROUND: {
		GnmColor *color = g_value_dup_boxed (value);
		style_color_unref (sheet->tab_text_color);
		sheet->tab_text_color = color;
		sheet_mark_dirty (sheet);
		break;
	}
	case PROP_TAB_BACKGROUND: {
		GnmColor *color = g_value_dup_boxed (value);
		style_color_unref (sheet->tab_color);
		sheet->tab_color = color;
		sheet_mark_dirty (sheet);
		break;
	}
	case PROP_ZOOM_FACTOR:
		sheet_set_zoom_factor (sheet, g_value_get_double (value));
		break;
	case PROP_COLUMNS:
		/* Construction-time only */
		sheet->size.max_cols = g_value_get_int (value);
		break;
	case PROP_ROWS:
		/* Construction-time only */
		sheet->size.max_rows = g_value_get_int (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sheet_get_property (GObject *object, guint property_id,
			GValue *value, GParamSpec *pspec)
{
	Sheet *sheet = (Sheet *)object;

	switch (property_id) {
	case PROP_SHEET_TYPE:
		g_value_set_enum (value, sheet->sheet_type);
		break;
	case PROP_WORKBOOK:
		g_value_set_object (value, sheet->workbook);
		break;
	case PROP_NAME:
		g_value_set_string (value, sheet->name_unquoted);
		break;
	case PROP_RTL:
		g_value_set_boolean (value, sheet->text_is_rtl);
		break;
	case PROP_VISIBILITY:
		g_value_set_enum (value, sheet->visibility);
		break;
	case PROP_DISPLAY_FORMULAS:
		g_value_set_boolean (value, sheet->display_formulas);
		break;
	case PROP_DISPLAY_ZEROS:
		g_value_set_boolean (value, !sheet->hide_zero);
		break;
	case PROP_DISPLAY_GRID:
		g_value_set_boolean (value, !sheet->hide_grid);
		break;
	case PROP_DISPLAY_COLUMN_HEADER:
		g_value_set_boolean (value, !sheet->hide_col_header);
		break;
	case PROP_DISPLAY_ROW_HEADER:
		g_value_set_boolean (value, !sheet->hide_row_header);
		break;
	case PROP_DISPLAY_OUTLINES:
		g_value_set_boolean (value, sheet->display_outlines);
		break;
	case PROP_DISPLAY_OUTLINES_BELOW:
		g_value_set_boolean (value, sheet->outline_symbols_below);
		break;
	case PROP_DISPLAY_OUTLINES_RIGHT:
		g_value_set_boolean (value, sheet->outline_symbols_right);
		break;

	case PROP_PROTECTED:
		g_value_set_boolean (value, sheet->is_protected);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_OBJECTS:
		g_value_set_boolean (value, sheet->protected_allow.edit_objects);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_SCENARIOS:
		g_value_set_boolean (value, sheet->protected_allow.edit_scenarios);
		break;
	case PROP_PROTECTED_ALLOW_CELL_FORMATTING:
		g_value_set_boolean (value, sheet->protected_allow.cell_formatting);
		break;
	case PROP_PROTECTED_ALLOW_COLUMN_FORMATTING:
		g_value_set_boolean (value, sheet->protected_allow.column_formatting);
		break;
	case PROP_PROTECTED_ALLOW_ROW_FORMATTING:
		g_value_set_boolean (value, sheet->protected_allow.row_formatting);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_COLUMNS:
		g_value_set_boolean (value, sheet->protected_allow.insert_columns);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_ROWS:
		g_value_set_boolean (value, sheet->protected_allow.insert_rows);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_HYPERLINKS:
		g_value_set_boolean (value, sheet->protected_allow.insert_hyperlinks);
		break;
	case PROP_PROTECTED_ALLOW_DELETE_COLUMNS:
		g_value_set_boolean (value, sheet->protected_allow.delete_columns);
		break;
	case PROP_PROTECTED_ALLOW_DELETE_ROWS:
		g_value_set_boolean (value, sheet->protected_allow.delete_rows);
		break;
	case PROP_PROTECTED_ALLOW_SELECT_LOCKED_CELLS:
		g_value_set_boolean (value, sheet->protected_allow.select_locked_cells);
		break;
	case PROP_PROTECTED_ALLOW_SORT_RANGES:
		g_value_set_boolean (value, sheet->protected_allow.sort_ranges);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_AUTO_FILTERS:
		g_value_set_boolean (value, sheet->protected_allow.edit_auto_filters);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_PIVOTTABLE:
		g_value_set_boolean (value, sheet->protected_allow.edit_pivottable);
		break;
	case PROP_PROTECTED_ALLOW_SELECT_UNLOCKED_CELLS:
		g_value_set_boolean (value, sheet->protected_allow.select_unlocked_cells);
		break;

	case PROP_CONVENTIONS:
		g_value_set_boxed (value, sheet->convs);
		break;
	case PROP_USE_R1C1: /* convenience api */
		g_value_set_boolean (value, sheet->convs->r1c1_addresses);
		break;

	case PROP_TAB_FOREGROUND:
		g_value_set_boxed (value, sheet->tab_text_color);
		break;
	case PROP_TAB_BACKGROUND:
		g_value_set_boxed (value, sheet->tab_color);
		break;
	case PROP_ZOOM_FACTOR:
		g_value_set_double (value, sheet->last_zoom_factor_used);
		break;
	case PROP_COLUMNS:
		g_value_set_int (value, sheet->size.max_cols);
		break;
	case PROP_ROWS:
		g_value_set_int (value, sheet->size.max_rows);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sheet_constructed (GObject *obj)
{
	Sheet *sheet = SHEET (obj);
	int ht;
	GnmStyle *style;

	if (parent_class->constructed)
		parent_class->constructed (obj);

	/* Now sheet_type, max_cols, and max_rows have been set.  */
	sheet->being_constructed = FALSE;

	col_row_collection_resize (&sheet->cols, sheet->size.max_cols);
	col_row_collection_resize (&sheet->rows, sheet->size.max_rows);

	sheet->priv->reposition_objects.col = sheet->size.max_cols;
	sheet->priv->reposition_objects.row = sheet->size.max_rows;

	range_init_full_sheet (&sheet->priv->unhidden_region, sheet);
	sheet_style_init (sheet);

	sheet_conditions_init (sheet);

	sheet->deps = gnm_dep_container_new (sheet);

	switch (sheet->sheet_type) {
	case GNM_SHEET_XLM:
		sheet->display_formulas = TRUE;
		break;
	case GNM_SHEET_OBJECT:
		sheet->hide_grid = TRUE;
		sheet->hide_col_header = sheet->hide_row_header = TRUE;
		colrow_compute_pixels_from_pts (&sheet->rows.default_style,
						sheet, FALSE, -1);
		colrow_compute_pixels_from_pts (&sheet->cols.default_style,
						sheet, TRUE, -1);
		break;
	case GNM_SHEET_DATA: {
		/* We have to add permanent names */
		GnmExprTop const *texpr;

		if (sheet->name_unquoted)
			texpr =	gnm_expr_top_new_constant
				(value_new_string (sheet->name_unquoted));
		else
			texpr = gnm_expr_top_new_constant
				(value_new_error_REF (NULL));
		expr_name_perm_add (sheet, "Sheet_Title",
				    texpr, FALSE);

		texpr = gnm_expr_top_new_constant
				(value_new_error_REF (NULL));
		expr_name_perm_add (sheet, "Print_Area",
				    texpr, FALSE);
		break;
	}
	default:
		g_assert_not_reached ();
	}

	style = sheet_style_default (sheet);
	ht = gnm_style_get_pango_height (style,
					 sheet->rendered_values->context, 1);
	gnm_style_unref (style);
	ht += GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
	if (ht > sheet_row_get_default_size_pixels (sheet)) {
		sheet_row_set_default_size_pixels (sheet, ht);
		sheet_col_set_default_size_pixels (sheet, ht * 9 / 2);
	}

	sheet_scale_changed (sheet, TRUE, TRUE);
}

static guint
cell_set_hash (GnmCell const *key)
{
	guint32 r = key->pos.row;
	guint32 c = key->pos.col;
	guint32 h;

	h = r;
	h *= (guint32)123456789;
	h ^= c;
	h *= (guint32)123456789;

	return h;
}

static gint
cell_set_equal (GnmCell const *a, GnmCell const *b)
{
	return (a->pos.row == b->pos.row && a->pos.col == b->pos.col);
}

static void
gnm_sheet_init (Sheet *sheet)
{
	PangoContext *context;

	sheet->priv = g_new0 (SheetPrivate, 1);
	sheet->being_constructed = TRUE;

	sheet->sheet_views = g_ptr_array_new ();

	/* Init, focus, and load handle setting these if/when necessary */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;

	sheet->is_protected = FALSE;
	sheet->protected_allow.edit_scenarios		= FALSE;
	sheet->protected_allow.cell_formatting		= FALSE;
	sheet->protected_allow.column_formatting	= FALSE;
	sheet->protected_allow.row_formatting		= FALSE;
	sheet->protected_allow.insert_columns		= FALSE;
	sheet->protected_allow.insert_rows		= FALSE;
	sheet->protected_allow.insert_hyperlinks	= FALSE;
	sheet->protected_allow.delete_columns		= FALSE;
	sheet->protected_allow.delete_rows		= FALSE;
	sheet->protected_allow.select_locked_cells	= TRUE;
	sheet->protected_allow.sort_ranges		= FALSE;
	sheet->protected_allow.edit_auto_filters	= FALSE;
	sheet->protected_allow.edit_pivottable		= FALSE;
	sheet->protected_allow.select_unlocked_cells	= TRUE;

	sheet->hide_zero = FALSE;
	sheet->display_outlines = TRUE;
	sheet->outline_symbols_below = TRUE;
	sheet->outline_symbols_right = TRUE;
	sheet->tab_color = NULL;
	sheet->tab_text_color = NULL;
	sheet->visibility = GNM_SHEET_VISIBILITY_VISIBLE;
#ifdef GNM_WITH_GTK
	sheet->text_is_rtl = (gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL);
#else
	sheet->text_is_rtl = FALSE;
#endif

	sheet->sheet_objects = NULL;
	sheet->max_object_extent.col = sheet->max_object_extent.row = 0;

	sheet->solver_parameters = gnm_solver_param_new (sheet);

	sheet->cols.max_used = -1;
	sheet->cols.info = g_ptr_array_new ();
	sheet_col_set_default_size_pts (sheet, 48);

	sheet->rows.max_used = -1;
	sheet->rows.info = g_ptr_array_new ();
	// 12.75 might be overwritten later
	sheet_row_set_default_size_pts (sheet, 12.75);

	sheet->print_info = gnm_print_information_new (FALSE);

	sheet->filters = NULL;
	sheet->scenarios = NULL;
	sheet->sort_setups = NULL;
	sheet->list_merged = NULL;
	sheet->hash_merged = g_hash_table_new ((GHashFunc)&gnm_cellpos_hash,
					       (GCompareFunc)&gnm_cellpos_equal);

	sheet->cell_hash = g_hash_table_new ((GHashFunc)&cell_set_hash,
					     (GCompareFunc)&cell_set_equal);

	/* Init preferences */
	sheet->convs = gnm_conventions_ref (gnm_conventions_default);

	/* FIXME: probably not here.  */
	/* See also gtk_widget_create_pango_context ().  */
	sheet->last_zoom_factor_used = -1;  /* Overridden later */
	context = gnm_pango_context_get ();
	sheet->rendered_values = gnm_rvc_new (context, 5000);
	g_object_unref (context);

	/* Init menu states */
	sheet->priv->enable_showhide_detail = TRUE;

	sheet->names = gnm_named_expr_collection_new ();
	sheet->style_data = NULL;

	sheet->index_in_wb = -1;

	sheet->pending_redraw = g_array_new (FALSE, FALSE, sizeof (GnmRange));
	sheet->pending_redraw_src = 0;
	debug_redraw = gnm_debug_flag ("redraw-ranges");
}

static Sheet the_invalid_sheet;
Sheet *invalid_sheet = &the_invalid_sheet;

static void
gnm_sheet_class_init (GObjectClass *gobject_class)
{
	if (GNM_MAX_COLS > 364238) {
		/* Oh, yeah?  */
		g_warning (_("This is a special version of Gnumeric.  It has been compiled\n"
			     "with support for a very large number of columns.  Access to the\n"
			     "column named TRUE may conflict with the constant of the same\n"
			     "name.  Expect weirdness."));
	}

	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property	= gnm_sheet_set_property;
	gobject_class->get_property	= gnm_sheet_get_property;
	gobject_class->finalize         = gnm_sheet_finalize;
	gobject_class->constructed      = gnm_sheet_constructed;

        g_object_class_install_property (gobject_class, PROP_SHEET_TYPE,
		 g_param_spec_enum ("sheet-type",
				    P_("Sheet Type"),
				    P_("Which type of sheet this is."),
				    GNM_SHEET_TYPE_TYPE,
				    GNM_SHEET_DATA,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE |
				    G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (gobject_class, PROP_WORKBOOK,
		g_param_spec_object ("workbook",
				     P_("Parent workbook"),
				     P_("The workbook in which this sheet lives"),
				     GNM_WORKBOOK_TYPE,
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_string ("name",
				      P_("Name"),
				      P_("The name of the sheet."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, PROP_RTL,
		 g_param_spec_boolean ("text-is-rtl",
				       P_("text-is-rtl"),
				       P_("Text goes from right to left."),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, PROP_VISIBILITY,
		 g_param_spec_enum ("visibility",
				    P_("Visibility"),
				    P_("How visible the sheet is."),
				    GNM_SHEET_VISIBILITY_TYPE,
				    GNM_SHEET_VISIBILITY_VISIBLE,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_FORMULAS,
		 g_param_spec_boolean ("display-formulas",
				       P_("Display Formul\303\246"),
				       P_("Control whether formul\303\246 are shown instead of values."),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_ZEROS,
		 g_param_spec_boolean ("display-zeros", _("Display Zeros"),
				       _("Control whether zeros are shown are blanked out."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_GRID,
		 g_param_spec_boolean ("display-grid", _("Display Grid"),
				       _("Control whether the grid is shown."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_COLUMN_HEADER,
		 g_param_spec_boolean ("display-column-header",
				       P_("Display Column Headers"),
				       P_("Control whether column headers are shown."),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_ROW_HEADER,
		 g_param_spec_boolean ("display-row-header",
				       P_("Display Row Headers"),
				       P_("Control whether row headers are shown."),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_OUTLINES,
		 g_param_spec_boolean ("display-outlines",
				       P_("Display Outlines"),
				       P_("Control whether outlines are shown."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_OUTLINES_BELOW,
		 g_param_spec_boolean ("display-outlines-below",
				       P_("Display Outlines Below"),
				       P_("Control whether outline symbols are shown below."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_OUTLINES_RIGHT,
		 g_param_spec_boolean ("display-outlines-right",
				       P_("Display Outlines Right"),
				       P_("Control whether outline symbols are shown to the right."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));

        g_object_class_install_property (gobject_class, PROP_PROTECTED,
		 g_param_spec_boolean ("protected",
				       P_("Protected"),
				       P_("Sheet is protected."),
				       FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_EDIT_OBJECTS,
		g_param_spec_boolean ("protected-allow-edit-objects",
				      P_("Protected Allow Edit objects"),
				      P_("Allow objects to be edited while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_EDIT_SCENARIOS,
		g_param_spec_boolean ("protected-allow-edit-scenarios",
				      P_("Protected allow edit scenarios"),
				      P_("Allow scenarios to be edited while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_CELL_FORMATTING,
		g_param_spec_boolean ("protected-allow-cell-formatting",
				      P_("Protected allow cell formatting"),
				      P_("Allow cell format changes while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_COLUMN_FORMATTING,
		g_param_spec_boolean ("protected-allow-column-formatting",
				      P_("Protected allow column formatting"),
				      P_("Allow column formatting while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_ROW_FORMATTING,
		g_param_spec_boolean ("protected-allow-row-formatting",
				      P_("Protected allow row formatting"),
				      P_("Allow row formatting while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_INSERT_COLUMNS,
		g_param_spec_boolean ("protected-allow-insert-columns",
				      P_("Protected allow insert columns"),
				      P_("Allow columns to be inserted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_INSERT_ROWS,
		g_param_spec_boolean ("protected-allow-insert-rows",
				      P_("Protected allow insert rows"),
				      P_("Allow rows to be inserted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_INSERT_HYPERLINKS,
		g_param_spec_boolean ("protected-allow-insert-hyperlinks",
				      P_("Protected allow insert hyperlinks"),
				      P_("Allow hyperlinks to be inserted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_DELETE_COLUMNS,
		g_param_spec_boolean ("protected-allow-delete-columns",
				      P_("Protected allow delete columns"),
				      P_("Allow columns to be deleted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_DELETE_ROWS,
		g_param_spec_boolean ("protected-allow-delete-rows",
				      P_("Protected allow delete rows"),
				      P_("Allow rows to be deleted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_SELECT_LOCKED_CELLS,
		g_param_spec_boolean ("protected-allow-select-locked-cells",
				      P_("Protected allow select locked cells"),
				      P_("Allow the user to select locked cells while a sheet is protected"),
				      TRUE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_SORT_RANGES,
		g_param_spec_boolean ("protected-allow-sort-ranges",
				      P_("Protected allow sort ranges"),
				      P_("Allow ranges to be sorted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_EDIT_AUTO_FILTERS,
		g_param_spec_boolean ("protected-allow-edit-auto-filters",
				      P_("Protected allow edit auto filters"),
				      P_("Allow auto filters to be edited while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_EDIT_PIVOTTABLE,
		g_param_spec_boolean ("protected-allow-edit-pivottable",
				      P_("Protected allow edit pivottable"),
				      P_("Allow pivottable to be edited while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_SELECT_UNLOCKED_CELLS,
		g_param_spec_boolean ("protected-allow-select-unlocked-cells",
				      P_("Protected allow select unlocked cells"),
				      P_("Allow the user to select unlocked cells while a sheet is protected"),
				      TRUE, GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property
		(gobject_class, PROP_CONVENTIONS,
		 g_param_spec_boxed ("conventions",
				     P_("Display convention for expressions (default Gnumeric A1)"),
				     P_("How to format displayed expressions, (A1 vs R1C1, function names, ...)"),
				     gnm_conventions_get_type (),
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_USE_R1C1, /* convenience wrapper to CONVENTIONS */
		g_param_spec_boolean ("use-r1c1",
				      P_("Display convention for expressions as XLS_R1C1 vs default"),
				      P_("How to format displayed expressions, (a convenience api)"),
				      FALSE,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_TAB_FOREGROUND,
		g_param_spec_boxed ("tab-foreground",
				    P_("Tab Foreground"),
				    P_("The foreground color of the tab."),
				    GNM_COLOR_TYPE,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_TAB_BACKGROUND,
		g_param_spec_boxed ("tab-background",
				    P_("Tab Background"),
				    P_("The background color of the tab."),
				    GNM_COLOR_TYPE,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));

	/* What is this doing in sheet?  */
	g_object_class_install_property (gobject_class, PROP_ZOOM_FACTOR,
		g_param_spec_double ("zoom-factor",
				     P_("Zoom Factor"),
				     P_("The level of zoom used for this sheet."),
				     0.1, 5.0,
				     1.0,
				     GSF_PARAM_STATIC |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_COLUMNS,
		g_param_spec_int ("columns",
				  P_("Columns"),
				  P_("Columns number in the sheet"),
				  0, GNM_MAX_COLS, GNM_DEFAULT_COLS,
				  GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (gobject_class, PROP_ROWS,
		g_param_spec_int ("rows",
				  P_("Rows"),
				  P_("Rows number in the sheet"),
				  0, GNM_MAX_ROWS, GNM_DEFAULT_ROWS,
				  GSF_PARAM_STATIC | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals[DETACHED_FROM_WORKBOOK] = g_signal_new
		("detached_from_workbook",
		 GNM_SHEET_TYPE,
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET (GnmSheetClass, detached_from_workbook),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__OBJECT,
		 G_TYPE_NONE, 1, GNM_WORKBOOK_TYPE);

}

GSF_CLASS (GnmSheet, gnm_sheet,
	   gnm_sheet_class_init, gnm_sheet_init, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

GType
gnm_sheet_type_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
	  static const GEnumValue values[] = {
		  { GNM_SHEET_DATA,   "GNM_SHEET_DATA",   "data"   },
		  { GNM_SHEET_OBJECT, "GNM_SHEET_OBJECT", "object" },
		  { GNM_SHEET_XLM,    "GNM_SHEET_XLM",    "xlm"    },
		  { 0, NULL, NULL }
	  };
	  etype = g_enum_register_static ("GnmSheetType", values);
  }
  return etype;
}

GType
gnm_sheet_visibility_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
	  static GEnumValue const values[] = {
		  { GNM_SHEET_VISIBILITY_VISIBLE, "GNM_SHEET_VISIBILITY_VISIBLE", "visible" },
		  { GNM_SHEET_VISIBILITY_HIDDEN, "GNM_SHEET_VISIBILITY_HIDDEN", "hidden" },
		  { GNM_SHEET_VISIBILITY_VERY_HIDDEN, "GNM_SHEET_VISIBILITY_VERY_HIDDEN", "very-hidden" },
		  { 0, NULL, NULL }
	  };
	  etype = g_enum_register_static ("GnmSheetVisibility", values);
  }
  return etype;
}

/* ------------------------------------------------------------------------- */

static gboolean
powerof_2 (int i)
{
	return i > 0 && (i & (i - 1)) == 0;
}

gboolean
gnm_sheet_valid_size (int cols, int rows)
{
	return (cols >= GNM_MIN_COLS &&
		cols <= GNM_MAX_COLS &&
		powerof_2 (cols) &&
		rows >= GNM_MIN_ROWS &&
		rows <= GNM_MAX_ROWS &&
		powerof_2 (rows)
#if 0
       	&& 0x80000000u / (unsigned)(cols / 2) >= (unsigned)rows
#endif
		);
}

/**
 * gnm_sheet_suggest_size:
 * @cols: (inout): number of columns
 * @rows: (inout): number of rows
 *
 * This function produces a valid sheet size that is reasonable for data
 * of @cols columns by @rows rows.  If possible, this will be a size bigger
 * in both dimensions.  However, that is not always possible and when it is
 * not, the suggested will be smaller in one or both directions.
 */
void
gnm_sheet_suggest_size (int *cols, int *rows)
{
	int c = GNM_DEFAULT_COLS;
	int r = GNM_DEFAULT_ROWS;

	while (c < *cols && c < GNM_MAX_COLS)
		c *= 2;

	while (r < *rows && r < GNM_MAX_ROWS)
		r *= 2;

	while (!gnm_sheet_valid_size (c, r)) {
		/* Darn!  Too large.  */
		if (*cols >= GNM_MIN_COLS && c > GNM_MIN_COLS)
			c /= 2;
		else if (*rows >= GNM_MIN_ROWS && r > GNM_MIN_ROWS)
			r /= 2;
		else if (c > GNM_MIN_COLS)
			c /= 2;
		else
			r /= 2;
	}

	*cols = c;
	*rows = r;
}

static void gnm_sheet_resize_main (Sheet *sheet, int cols, int rows,
				   GOCmdContext *cc, GOUndo **pundo);

static void
cb_sheet_resize (Sheet *sheet, const GnmSheetSize *data, GOCmdContext *cc)
{
	gnm_sheet_resize_main (sheet, data->max_cols, data->max_rows,
			       cc, NULL);
}

static void
gnm_sheet_resize_main (Sheet *sheet, int cols, int rows,
		       GOCmdContext *cc, GOUndo **pundo)
{
	int old_cols, old_rows;
	GPtrArray *common_col_styles = NULL;
	GPtrArray *common_row_styles = NULL;

	if (pundo) *pundo = NULL;

	old_cols = gnm_sheet_get_max_cols (sheet);
	old_rows = gnm_sheet_get_max_rows (sheet);
	if (old_cols == cols && old_rows == rows)
		return;

	if (gnm_debug_flag ("sheet-resize"))
		g_printerr ("Resize %dx%d -> %dx%d\n",
			    old_cols, old_rows, cols, rows);

	sheet->workbook->sheet_size_cached = FALSE;

	/* ---------------------------------------- */
	/* Gather styles we want to copy into new areas.  */

	if (cols > old_cols)
		common_row_styles = sheet_style_most_common (sheet, FALSE);
	if (rows > old_rows)
		common_col_styles = sheet_style_most_common (sheet, TRUE);

	/* ---------------------------------------- */
	/* Remove the columns and rows that will disappear.  */

	if (cols < old_cols) {
		GOUndo *u = NULL;
		gboolean err;

		err = sheet_delete_cols (sheet, cols, G_MAXINT,
					 pundo ? &u : NULL, cc);
		if (pundo)
			*pundo = go_undo_combine (*pundo, u);
		if (err)
			goto handle_error;
	}

	if (rows < old_rows) {
		GOUndo *u = NULL;
		gboolean err;

		err = sheet_delete_rows (sheet, rows, G_MAXINT,
					 pundo ? &u : NULL, cc);
		if (pundo)
			*pundo = go_undo_combine (*pundo, u);
		if (err)
			goto handle_error;
	}

	/* ---------------------------------------- */
	/* Restrict selection.  (Not undone.)  */

	SHEET_FOREACH_VIEW (sheet, sv,
		{
			GnmRange new_full;
			GSList *l;
			GSList *sel = selection_get_ranges (sv, TRUE);
			gboolean any = FALSE;
			GnmCellPos vis;
			sv_selection_reset (sv);
			range_init (&new_full, 0, 0, cols - 1, rows - 1);
			vis = new_full.start;
			for (l = sel; l; l = l->next) {
				GnmRange *r = l->data;
				GnmRange newr;
				if (range_intersection (&newr, r, &new_full)) {
					sv_selection_add_range (sv, &newr);
					vis = newr.start;
					any = TRUE;
				}
				g_free (r);
			}
			g_slist_free (sel);
			if (!any)
				sv_selection_add_pos (sv, 0, 0,
						      GNM_SELECTION_MODE_ADD);
			gnm_sheet_view_make_cell_visible (sv, vis.col, vis.row, FALSE);
		});

	/* ---------------------------------------- */
	/* Resize column and row containers.  */

	col_row_collection_resize (&sheet->cols, cols);
	col_row_collection_resize (&sheet->rows, rows);

	/* ---------------------------------------- */
	/* Resize the dependency containers.  */

	{
		GSList *l, *linked = NULL;
		/* FIXME: what about dependents in other workbooks?  */
		WORKBOOK_FOREACH_DEPENDENT
			(sheet->workbook, dep,

			 if (dependent_is_linked (dep)) {
				 dependent_unlink (dep);
				 linked = g_slist_prepend (linked, dep);
			 });

		gnm_dep_container_resize (sheet->deps, rows);

		for (l = linked; l; l = l->next) {
			GnmDependent *dep = l->data;
			dependent_link (dep);
		}

		g_slist_free (linked);

		workbook_queue_all_recalc (sheet->workbook);
	}

	/* ---------------------------------------- */
	/* Resize the styles.  */

	sheet_style_resize (sheet, cols, rows);

	/* ---------------------------------------- */
	/* Actually change the properties.  */

	sheet->size.max_cols = cols;
	sheet->cols.max_used = MIN (sheet->cols.max_used, cols - 1);
	sheet->size.max_rows = rows;
	sheet->rows.max_used = MIN (sheet->rows.max_used, rows - 1);

	if (old_cols != cols)
		g_object_notify (G_OBJECT (sheet), "columns");
	if (old_rows != rows)
		g_object_notify (G_OBJECT (sheet), "rows");

	if (pundo) {
		GnmSheetSize *data = g_new (GnmSheetSize, 1);
		GOUndo *u;

		data->max_cols = old_cols;
		data->max_rows = old_rows;
		u = go_undo_binary_new (sheet, data,
					(GOUndoBinaryFunc)cb_sheet_resize,
					NULL, g_free);
		*pundo = go_undo_combine (*pundo, u);
	}

	range_init_full_sheet (&sheet->priv->unhidden_region, sheet);

	/* ---------------------------------------- */
	/* Apply styles to new areas.  */

	if (cols > old_cols) {
		int r = 0;
		int end_r = MIN (old_rows, rows);
		while (r < end_r) {
			int r2 = r;
			GnmStyle *mstyle = g_ptr_array_index (common_row_styles, r);
			GnmRange rng;
			while (r2 + 1 < end_r &&
			       mstyle == g_ptr_array_index (common_row_styles, r2 + 1))
				r2++;
			range_init (&rng, old_cols, r, cols - 1, r2);
			gnm_style_ref (mstyle);
			sheet_apply_style (sheet, &rng, mstyle);
			r = r2 + 1;
		}
	}

	if (rows > old_rows) {
		int c = 0;
		int end_c = MIN (old_cols, cols);
		while (c < end_c) {
			int c2 = c;
			GnmStyle *mstyle = g_ptr_array_index (common_col_styles, c);
			GnmRange rng;
			while (c2 + 1 < end_c &&
			       mstyle == g_ptr_array_index (common_col_styles, c2 + 1))
				c2++;
			range_init (&rng, c, old_rows, c2, rows - 1);
			gnm_style_ref (mstyle);
			sheet_apply_style (sheet, &rng, mstyle);
			c = c2 + 1;
		}

		if (cols > old_cols) {
			/*
			 * Expanded in both directions.  One could argue about
			 * what style to use down here, but we choose the
			 * last column style.
			 */
			GnmStyle *mstyle = g_ptr_array_index (common_col_styles, old_cols - 1);
			GnmRange rng;

			range_init (&rng,
				    old_cols, old_rows,
				    cols - 1, rows - 1);
			gnm_style_ref (mstyle);
			sheet_apply_style (sheet, &rng, mstyle);
		}
	}

	if (common_row_styles)
		g_ptr_array_free (common_row_styles, TRUE);
	if (common_col_styles)
		g_ptr_array_free (common_col_styles, TRUE);

	/* ---------------------------------------- */

	sheet_redraw_all (sheet, TRUE);
	return;

 handle_error:
	if (pundo) {
		go_undo_undo_with_data (*pundo, cc);
		g_object_unref (*pundo);
		*pundo = NULL;
	}
}

/**
 * gnm_sheet_resize:
 * @sheet: #Sheet
 * @cols: the new columns number.
 * @rows: the new rows number.
 * @cc: #GOCmdContext.
 * @perr: (out): will be %TRUE on error.
 *
 * Returns: (transfer full): the newly allocated #GOUndo.
 **/
GOUndo *
gnm_sheet_resize (Sheet *sheet, int cols, int rows,
		  GOCmdContext *cc, gboolean *perr)
{
	GOUndo *undo = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (gnm_sheet_valid_size (cols, rows), NULL);

	if (cols < sheet->size.max_cols || rows < sheet->size.max_rows) {
		GSList *overlap, *l;
		gboolean bad = FALSE;
		GnmRange r;

		r.start.col = r.start.row = 0;
		r.end.col = MIN (cols, sheet->size.max_cols) - 1;
		r.end.row = MIN (rows, sheet->size.max_rows) - 1;

		overlap = gnm_sheet_merge_get_overlap (sheet, &r);
		for (l = overlap; l && !bad; l = l->next) {
			GnmRange const *m = l->data;
			if (!range_contained (m, &r)) {
				bad = TRUE;
				gnm_cmd_context_error_splits_merge (cc, m);
			}
		}
		g_slist_free (overlap);
		if (bad) {
			*perr = TRUE;
			return NULL;
		}
	}

	gnm_sheet_resize_main (sheet, cols, rows, cc, &undo);

	*perr = FALSE;
	return undo;
}


/**
 * sheet_new_with_type:
 * @wb: #Workbook
 * @name: An unquoted name
 * @type: @GnmSheetType
 * @columns: The number of columns for the sheet
 * @rows: The number of rows for the sheet
 *
 * Create a new Sheet of type @type, and associate it with @wb.
 * The type cannot be changed later.
 *
 * Returns: (transfer full): the newly allocated sheet.
 **/
Sheet *
sheet_new_with_type (Workbook *wb, char const *name, GnmSheetType type,
		     int columns, int rows)
{
	Sheet *sheet;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (gnm_sheet_valid_size (columns, rows), NULL);

	sheet = g_object_new (GNM_SHEET_TYPE,
			      "workbook", wb,
			      "sheet-type", type,
			      "columns", columns,
			      "rows", rows,
			      "name", name,
			      "zoom-factor", gnm_conf_get_core_gui_window_zoom (),
			      NULL);

	if (type == GNM_SHEET_OBJECT)
		print_info_set_paper_orientation (sheet->print_info, GTK_PAGE_ORIENTATION_LANDSCAPE);

	return sheet;
}

/**
 * sheet_new:
 * @wb: #Workbook
 * @name: The name for the sheet (unquoted).
 * @columns: The requested columns number.
 * @rows: The requested rows number.
 *
 * Create a new Sheet of type SHEET_DATA, and associate it with @wb.
 * The type cannot be changed later
 *
 * Returns: (transfer full): the newly allocated sheet.
 **/
Sheet *
sheet_new (Workbook *wb, char const *name, int columns, int rows)
{
	return sheet_new_with_type (wb, name, GNM_SHEET_DATA, columns, rows);
}

/****************************************************************************/

void
sheet_redraw_all (Sheet const *sheet, gboolean headers)
{
	/* We potentially do a lot of recalcs as part of this, so make sure
	   stuff that caches sub-computations see the whole thing instead
	   of clearing between cells.  */
	gnm_app_recalc_start ();
	SHEET_FOREACH_CONTROL (sheet, view, control,
		sc_redraw_all (control, headers););
	gnm_app_recalc_finish ();
}

static GnmValue *
cb_clear_rendered_values (GnmCellIter const *iter, G_GNUC_UNUSED gpointer user)
{
	gnm_cell_unrender (iter->cell);
	return NULL;
}

/**
 * sheet_range_calc_spans:
 * @sheet: The sheet,
 * @r:     the region to update.
 * @flags:
 *
 * This is used to re-calculate cell dimensions and re-render
 * a cell's text. eg. if a format has changed we need to re-render
 * the cached version of the rendered text in the cell.
 **/
void
sheet_range_calc_spans (Sheet *sheet, GnmRange const *r, GnmSpanCalcFlags flags)
{
	if (flags & GNM_SPANCALC_RE_RENDER)
		sheet_foreach_cell_in_range
			(sheet, CELL_ITER_IGNORE_NONEXISTENT, r,
			 cb_clear_rendered_values, NULL);
	sheet_queue_respan (sheet, r->start.row, r->end.row);

	/* Redraw the new region in case the span changes */
	sheet_redraw_range (sheet, r);
}

static void
sheet_redraw_partial_row (Sheet const *sheet, int const row,
			  int const start_col, int const end_col)
{
	GnmRange r;
	range_init (&r, start_col, row, end_col, row);
	SHEET_FOREACH_CONTROL (sheet, view, control,
		sc_redraw_range (control, &r););
}

static void
sheet_redraw_cell (GnmCell const *cell)
{
	CellSpanInfo const * span;
	int start_col, end_col, row;
	GnmRange const *merged;
	Sheet *sheet;
	ColRowInfo *ri;

	g_return_if_fail (cell != NULL);

	sheet = cell->base.sheet;
	merged = gnm_sheet_merge_is_corner (sheet, &cell->pos);
	if (merged != NULL) {
		SHEET_FOREACH_CONTROL (sheet, view, control,
			sc_redraw_range (control, merged););
		return;
	}

	row = cell->pos.row;
	start_col = end_col = cell->pos.col;
	ri = sheet_row_get (sheet, row);
	span = row_span_get (ri, start_col);

	if (span) {
		start_col = span->left;
		end_col = span->right;
	}

	sheet_redraw_partial_row (sheet, row, start_col, end_col);
}

static void
sheet_cell_calc_span (GnmCell *cell, GnmSpanCalcFlags flags)
{
	CellSpanInfo const * span;
	int left, right;
	int min_col, max_col, row;
	gboolean render = (flags & GNM_SPANCALC_RE_RENDER) != 0;
	gboolean const resize = (flags & GNM_SPANCALC_RESIZE) != 0;
	gboolean existing = FALSE;
	GnmRange const *merged;
	Sheet *sheet;
	ColRowInfo *ri;

	g_return_if_fail (cell != NULL);

	sheet = cell->base.sheet;
	row = cell->pos.row;

	/* Render and Size any unrendered cells */
	if ((flags & GNM_SPANCALC_RENDER) && gnm_cell_get_rendered_value (cell) == NULL)
		render = TRUE;

	if (render) {
		if (!gnm_cell_has_expr (cell))
			gnm_cell_render_value ((GnmCell *)cell, TRUE);
		else
			gnm_cell_unrender (cell);
	} else if (resize) {
		/* FIXME: what was wanted here?  */
		/* rendered_value_calc_size (cell); */
	}

	/* Is there an existing span ? clear it BEFORE calculating new one */
	ri = sheet_row_get (sheet, row);
	span = row_span_get (ri, cell->pos.col);
	if (span != NULL) {
		GnmCell const * const other = span->cell;

		min_col = span->left;
		max_col = span->right;

		/* A different cell used to span into this cell, respan that */
		if (cell != other) {
			int other_left, other_right;

			cell_unregister_span (other);
			cell_calc_span (other, &other_left, &other_right);
			if (min_col > other_left)
				min_col = other_left;
			if (max_col < other_right)
				max_col = other_right;

			if (other_left != other_right)
				cell_register_span (other, other_left, other_right);
		} else
			existing = TRUE;
	} else
		min_col = max_col = cell->pos.col;

	merged = gnm_sheet_merge_is_corner (sheet, &cell->pos);
	if (NULL != merged) {
		if (existing) {
			if (min_col > merged->start.col)
				min_col = merged->start.col;
			if (max_col < merged->end.col)
				max_col = merged->end.col;
		} else {
			sheet_redraw_cell (cell);
			return;
		}
	} else {
		/* Calculate the span of the cell */
		cell_calc_span (cell, &left, &right);
		if (min_col > left)
			min_col = left;
		if (max_col < right)
			max_col = right;

		/* This cell already had an existing span */
		if (existing) {
			/* If it changed, remove the old one */
			if (left != span->left || right != span->right)
				cell_unregister_span (cell);
			else
				/* unchanged, short curcuit adding the span again */
				left = right;
		}

		if (left != right)
			cell_register_span (cell, left, right);
	}

	sheet_redraw_partial_row (sheet, row, min_col, max_col);
}

/**
 * sheet_apply_style: (skip)
 * @sheet: the sheet in which can be found
 * @range: the range to which should be applied
 * @style: (transfer full): A #GnmStyle partial style
 *
 * A mid level routine that applies the supplied partial style @style to the
 * target @range and performs the necessary respanning and redrawing.
 **/
void
sheet_apply_style (Sheet       *sheet,
		   GnmRange const *range,
		   GnmStyle      *style)
{
	GnmSpanCalcFlags spanflags = gnm_style_required_spanflags (style);
	sheet_style_apply_range (sheet, range, style);
	/* This also redraws the range: */
	sheet_range_calc_spans (sheet, range, spanflags);
}

/**
 * sheet_apply_style_gi: (rename-to sheet_apply_style)
 * @sheet: the sheet in which can be found
 * @range: the range to which should be applied
 * @style: A #GnmStyle partial style
 *
 * A mid level routine that applies the supplied partial style @style to the
 * target @range and performs the necessary respanning and redrawing.
 **/
void
sheet_apply_style_gi (Sheet *sheet, GnmRange const *range, GnmStyle *style)
{
	GnmSpanCalcFlags spanflags = gnm_style_required_spanflags (style);
	gnm_style_ref (style);
	sheet_style_apply_range (sheet, range, style);
	/* This also redraws the range: */
	sheet_range_calc_spans (sheet, range, spanflags);
}

static void
sheet_apply_style_cb (GnmSheetRange *sr,
		      GnmStyle      *style)
{
	gnm_style_ref (style);
	sheet_apply_style (sr->sheet, &sr->range, style);
	sheet_flag_style_update_range (sr->sheet, &sr->range);
}

/**
 * sheet_apply_style_undo:
 * @sr: (transfer full): #GnmSheetRange
 * @style: (transfer none): #GnmStyle
 *
 * Returns: (transfer full): the new #GOUndo.
 **/
GOUndo *
sheet_apply_style_undo (GnmSheetRange *sr,
			GnmStyle      *style)
{
	gnm_style_ref (style);
	return go_undo_binary_new
		(sr, (gpointer)style,
		 (GOUndoBinaryFunc) sheet_apply_style_cb,
		 (GFreeFunc) gnm_sheet_range_free,
		 (GFreeFunc) gnm_style_unref);
}


/**
 * sheet_apply_border:
 * @sheet: #Sheet to change
 * @range: #GnmRange around which to place borders
 * @borders: (array fixed-size=8): Border styles to set.
 */
void
sheet_apply_border (Sheet *sheet,
		    GnmRange const *range,
		    GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX])
{
	GnmSpanCalcFlags spanflags = GNM_SPANCALC_RE_RENDER | GNM_SPANCALC_RESIZE;
	sheet_style_apply_border (sheet, range, borders);
	/* This also redraws the range: */
	sheet_range_calc_spans (sheet, range, spanflags);
}

/****************************************************************************/

static ColRowInfo *
sheet_row_new (Sheet *sheet)
{
	ColRowInfo *ri;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	ri = col_row_info_new ();
	*ri = sheet->rows.default_style;
	ri->is_default = FALSE;
	ri->needs_respan = TRUE;

	return ri;
}

static ColRowInfo *
sheet_col_new (Sheet *sheet)
{
	ColRowInfo *ci;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	ci = col_row_info_new ();
	*ci = sheet->cols.default_style;
	ci->is_default = FALSE;

	return ci;
}

static void
sheet_colrow_add (Sheet *sheet, ColRowInfo *cp, gboolean is_cols, int n)
{
	ColRowCollection *info = is_cols ? &sheet->cols : &sheet->rows;
	ColRowSegment **psegment = (ColRowSegment **)&COLROW_GET_SEGMENT (info, n);

	g_return_if_fail (n >= 0);
	g_return_if_fail (n < colrow_max (is_cols, sheet));

	if (*psegment == NULL)
		*psegment = g_new0 (ColRowSegment, 1);
	colrow_free ((*psegment)->info[COLROW_SUB_INDEX (n)]);
	(*psegment)->info[COLROW_SUB_INDEX (n)] = cp;

	if (cp->outline_level > info->max_outline_level)
		info->max_outline_level = cp->outline_level;
	if (n > info->max_used) {
		info->max_used = n;
		sheet->priv->resize_scrollbar = TRUE;
	}
}

static void
sheet_reposition_objects (Sheet const *sheet, GnmCellPos const *pos)
{
	GSList *ptr;
	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next )
		sheet_object_update_bounds (GNM_SO (ptr->data), pos);
}

/**
 * sheet_flag_status_update_cell:
 * @cell: The cell that has changed.
 *
 *    flag the sheet as requiring an update to the status display
 *    if the supplied cell location is the edit cursor, or part of the
 *    selected region.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 **/
void
sheet_flag_status_update_cell (GnmCell const *cell)
{
	SHEET_FOREACH_VIEW (cell->base.sheet, sv,
		gnm_sheet_view_flag_status_update_pos (sv, &cell->pos););
}

/**
 * sheet_flag_status_update_range:
 * @sheet:
 * @range: (nullable): GnmRange, or %NULL for full sheet
 *
 *    flag the sheet as requiring an update to the status display
 *    if the supplied cell location contains the edit cursor, or intersects of
 *    the selected region.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 **/
void
sheet_flag_status_update_range (Sheet const *sheet, GnmRange const *range)
{
	SHEET_FOREACH_VIEW (sheet, sv,
		gnm_sheet_view_flag_status_update_range (sv, range););
}

/**
 * sheet_flag_style_update_range:
 * @sheet: The sheet being changed
 * @range: the range that is changing.
 *
 * Flag format changes that will require updating the format indicators.
 **/
void
sheet_flag_style_update_range (Sheet const *sheet, GnmRange const *range)
{
	SHEET_FOREACH_VIEW (sheet, sv,
		gnm_sheet_view_flag_style_update_range (sv, range););
}

/**
 * sheet_flag_recompute_spans:
 * @sheet:
 *
 * Flag the sheet as requiring a full span recomputation the next time
 * sheet_update is called.
 **/
void
sheet_flag_recompute_spans (Sheet const *sheet)
{
	sheet->priv->recompute_spans = TRUE;
}

static gboolean
cb_outline_level (GnmColRowIter const *iter, gpointer data)
{
	int *outline_level = data;
	if (*outline_level < iter->cri->outline_level)
		*outline_level  = iter->cri->outline_level;
	return FALSE;
}

/**
 * sheet_colrow_fit_gutter:
 * @sheet: Sheet to change.
 * @is_cols: %TRUE for columns, %FALSE for rows.
 *
 * Find the current max outline level.
 **/
static int
sheet_colrow_fit_gutter (Sheet const *sheet, gboolean is_cols)
{
	int outline_level = 0;
	sheet_colrow_foreach (sheet, is_cols, 0, -1,
			      cb_outline_level, &outline_level);
	return outline_level;
}

/**
 * sheet_objects_max_extent:
 * @sheet:
 *
 * Utility routine to calculate the maximum extent of objects in this sheet.
 */
static void
sheet_objects_max_extent (Sheet *sheet)
{
	GnmCellPos max_pos = { 0, 0 };
	GSList *ptr;

	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next ) {
		SheetObject *so = GNM_SO (ptr->data);

		if (max_pos.col < so->anchor.cell_bound.end.col)
			max_pos.col = so->anchor.cell_bound.end.col;
		if (max_pos.row < so->anchor.cell_bound.end.row)
			max_pos.row = so->anchor.cell_bound.end.row;
	}

	if (sheet->max_object_extent.col != max_pos.col ||
	    sheet->max_object_extent.row != max_pos.row) {
		sheet->max_object_extent = max_pos;
		sheet_scrollbar_config (sheet);
	}
}

/**
 * sheet_update_only_grid:
 * @sheet: #Sheet
 *
 * Should be called after a logical command has finished processing
 * to request redraws for any pending events
 **/
void
sheet_update_only_grid (Sheet const *sheet)
{
	SheetPrivate *p;

	g_return_if_fail (IS_SHEET (sheet));

	p = sheet->priv;

	if (p->objects_changed) {
		p->objects_changed = FALSE;
		sheet_objects_max_extent ((Sheet *)sheet);
	}

	/* be careful these can toggle flags */
	if (p->recompute_max_col_group) {
		sheet_colrow_gutter ((Sheet *)sheet, TRUE,
			sheet_colrow_fit_gutter (sheet, TRUE));
		sheet->priv->recompute_max_col_group = FALSE;
	}
	if (p->recompute_max_row_group) {
		sheet_colrow_gutter ((Sheet *)sheet, FALSE,
			sheet_colrow_fit_gutter (sheet, FALSE));
		sheet->priv->recompute_max_row_group = FALSE;
	}

	SHEET_FOREACH_VIEW (sheet, sv, {
		if (sv->reposition_selection) {
			sv->reposition_selection = FALSE;

			/* when moving we cleared the selection before
			 * arriving in here.
			 */
			if (sv->selections != NULL)
				sv_selection_set (sv, &sv->edit_pos_real,
						  sv->cursor.base_corner.col,
						  sv->cursor.base_corner.row,
						  sv->cursor.move_corner.col,
						  sv->cursor.move_corner.row);
		}
	});

	if (p->recompute_spans) {
		p->recompute_spans = FALSE;
		/* FIXME : I would prefer to use GNM_SPANCALC_RENDER rather than
		 * RE_RENDER.  It only renders those cells which are not
		 * rendered.  The trouble is that when a col changes size we
		 * need to rerender, but currently nothing marks that.
		 *
		 * hmm, that suggests an approach.  maybe I can install a per
		 * col flag.  Then add a flag clearing loop after the
		 * sheet_calc_span.
		 */
#if 0
		sheet_calc_spans (sheet, GNM_SPANCALC_RESIZE|GNM_SPANCALC_RE_RENDER |
				  (p->recompute_visibility ?
				   SPANCALC_NO_DRAW : GNM_SPANCALC_SIMPLE));
#endif
		sheet_queue_respan (sheet, 0, gnm_sheet_get_last_row (sheet));
	}

	if (p->reposition_objects.row < gnm_sheet_get_max_rows (sheet) ||
	    p->reposition_objects.col < gnm_sheet_get_max_cols (sheet)) {
		SHEET_FOREACH_VIEW (sheet, sv, {
			if (!p->resize && gnm_sheet_view_is_frozen (sv)) {
				if (p->reposition_objects.col < sv->unfrozen_top_left.col ||
				    p->reposition_objects.row < sv->unfrozen_top_left.row) {
					gnm_sheet_view_resize (sv, FALSE);
				}
			}
		});
		sheet_reposition_objects (sheet, &p->reposition_objects);
		p->reposition_objects.row = gnm_sheet_get_max_rows (sheet);
		p->reposition_objects.col = gnm_sheet_get_max_cols (sheet);
	}

	if (p->resize) {
		p->resize = FALSE;
		SHEET_FOREACH_VIEW (sheet, sv, { gnm_sheet_view_resize (sv, FALSE); });
	}

	if (p->recompute_visibility) {
		/* TODO : There is room for some opimization
		 * We only need to force complete visibility recalculation
		 * (which we do in sheet_compute_visible_region)
		 * if a row or col before the start of the visible region.
		 * If we are REALLY smart we could even accumulate the size differential
		 * and use that.
		 */
		p->recompute_visibility = FALSE;
		p->resize_scrollbar = FALSE; /* compute_visible_region does this */
		SHEET_FOREACH_CONTROL(sheet, view, control,
			sc_recompute_visible_region (control, TRUE););
		sheet_redraw_all (sheet, TRUE);
	}

	if (p->resize_scrollbar) {
		sheet_scrollbar_config (sheet);
		p->resize_scrollbar = FALSE;
	}

	if (p->filters_changed) {
		p->filters_changed = FALSE;
		SHEET_FOREACH_CONTROL (sheet, sv, sc,
			wb_control_menu_state_update (sc_wbc (sc), MS_ADD_VS_REMOVE_FILTER););
	}
}

/**
 * sheet_update:
 * @sheet: #Sheet
 *
 * Should be called after a logical command has finished processing to request
 * redraws for any pending events, and to update the various status regions
 **/
void
sheet_update (Sheet const *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	sheet_update_only_grid (sheet);

	SHEET_FOREACH_VIEW (sheet, sv, gnm_sheet_view_update (sv););
}

/**
 * sheet_cell_get:
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Returns: (nullable): a #GnmCell, or %NULL if the cell does not exist
 **/
GnmCell *
sheet_cell_get (Sheet const *sheet, int col, int row)
{
	GnmCell *cell;
	GnmCell key;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	key.pos.col = col;
	key.pos.row = row;
	cell = g_hash_table_lookup (sheet->cell_hash, &key);

	return cell;
}

/**
 * sheet_cell_fetch:
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Returns: a #GnmCell containing at (@col,@row).
 * If no cell existed at that location before, it is created.
 **/
GnmCell *
sheet_cell_fetch (Sheet *sheet, int col, int row)
{
	GnmCell *cell;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cell = sheet_cell_get (sheet, col, row);
	if (!cell)
		cell = sheet_cell_create (sheet, col, row);

	return cell;
}

/**
 * sheet_colrow_can_group:
 * @sheet: #Sheet
 * @r: A #GnmRange
 * @is_cols: %TRUE for columns, %FALSE for rows.
 *
 * Returns: %TRUE if the cols/rows in @r.start -> @r.end can be grouped,
 * %FALSE otherwise. You can invert the result if you need to find out if a
 * group can be ungrouped.
 **/
gboolean
sheet_colrow_can_group (Sheet *sheet, GnmRange const *r, gboolean is_cols)
{
	ColRowInfo const *start_cri, *end_cri;
	int start, end;

	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (is_cols) {
		start = r->start.col;
		end = r->end.col;
	} else {
		start = r->start.row;
		end = r->end.row;
	}
	start_cri = sheet_colrow_fetch (sheet, start, is_cols);
	end_cri = sheet_colrow_fetch (sheet, end, is_cols);

	/* Groups on outline level 0 (no outline) may always be formed */
	if (start_cri->outline_level == 0 || end_cri->outline_level == 0)
		return TRUE;

	/* We just won't group a group that already exists (or doesn't), it's useless */
	return (colrow_find_outline_bound (sheet, is_cols, start, start_cri->outline_level, FALSE) != start ||
		colrow_find_outline_bound (sheet, is_cols, end, end_cri->outline_level, TRUE) != end);
}

gboolean
sheet_colrow_group_ungroup (Sheet *sheet, GnmRange const *r,
			    gboolean is_cols, gboolean group)
{
	int i, new_max, start, end;
	int const step = group ? 1 : -1;

	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	/* Can we group/ungroup ? */
	if (group != sheet_colrow_can_group (sheet, r, is_cols))
		return FALSE;

	if (is_cols) {
		start = r->start.col;
		end = r->end.col;
	} else {
		start = r->start.row;
		end = r->end.row;
	}

	/* Set new outline for each col/row and find highest outline level */
	new_max = (is_cols ? &sheet->cols : &sheet->rows)->max_outline_level;
	for (i = start; i <= end; i++) {
		ColRowInfo *cri = sheet_colrow_fetch (sheet, i, is_cols);
		int const new_level = cri->outline_level + step;

		if (new_level >= 0) {
			colrow_info_set_outline (cri, new_level, FALSE);
			if (new_max < new_level)
				new_max = new_level;
		}
	}

	if (!group)
		new_max = sheet_colrow_fit_gutter (sheet, is_cols);

	sheet_colrow_gutter (sheet, is_cols, new_max);
	SHEET_FOREACH_VIEW (sheet, sv,
		gnm_sheet_view_redraw_headers (sv, is_cols, !is_cols, NULL););

	return TRUE;
}

/**
 * sheet_colrow_gutter:
 * @sheet:
 * @is_cols: %TRUE for columns, %FALSE for rows.
 * @max_outline:
 *
 * Set the maximum outline levels for cols or rows.
 */
void
sheet_colrow_gutter (Sheet *sheet, gboolean is_cols, int max_outline)
{
	ColRowCollection *infos;

	g_return_if_fail (IS_SHEET (sheet));

	infos = is_cols ? &(sheet->cols) : &(sheet->rows);
	if (infos->max_outline_level != max_outline) {
		sheet->priv->resize = TRUE;
		infos->max_outline_level = max_outline;
	}
}

struct sheet_extent_data {
	GnmRange range;
	gboolean spans_and_merges_extend;
	gboolean ignore_empties;
	gboolean include_hidden;
};

static void
cb_sheet_get_extent (G_GNUC_UNUSED gpointer ignored, gpointer value, gpointer data)
{
	GnmCell const *cell = (GnmCell const *) value;
	struct sheet_extent_data *res = data;
	Sheet *sheet = cell->base.sheet;
	ColRowInfo *ri = NULL;

	if (res->ignore_empties && gnm_cell_is_empty (cell))
		return;
	if (!res->include_hidden) {
		ri = sheet_col_get (sheet, cell->pos.col);
		if (!ri->visible)
			return;
		ri = sheet_row_get (sheet, cell->pos.row);
		if (!ri->visible)
			return;
	}

	/* Remember the first cell is the min and max */
	if (res->range.start.col > cell->pos.col)
		res->range.start.col = cell->pos.col;
	if (res->range.end.col < cell->pos.col)
		res->range.end.col = cell->pos.col;
	if (res->range.start.row > cell->pos.row)
		res->range.start.row = cell->pos.row;
	if (res->range.end.row < cell->pos.row)
		res->range.end.row = cell->pos.row;

	if (!res->spans_and_merges_extend)
		return;

	/* Cannot span AND merge */
	if (gnm_cell_is_merged (cell)) {
		GnmRange const *merged =
			gnm_sheet_merge_is_corner (sheet, &cell->pos);
		res->range = range_union (&res->range, merged);
	} else {
		CellSpanInfo const *span;
		if (ri == NULL)
			ri = sheet_row_get (sheet, cell->pos.row);
		if (ri->needs_respan)
			row_calc_spans (ri, cell->pos.row, sheet);
		span = row_span_get (ri, cell->pos.col);
		if (NULL != span) {
			if (res->range.start.col > span->left)
				res->range.start.col = span->left;
			if (res->range.end.col < span->right)
				res->range.end.col = span->right;
		}
	}
}

/**
 * sheet_get_extent:
 * @sheet: the sheet
 * @spans_and_merges_extend: optionally extend region for spans and merges.
 * @include_hidden: whether to include the content of hidden cells.
 *
 * calculates the area occupied by cell data.
 *
 * NOTE: When spans_and_merges_extend is %TRUE, this function will calculate
 * all spans.  That might be expensive.
 *
 * NOTE: This refers to *visible* contents.  Cells with empty values, including
 * formulas with such values, are *ignored.
 *
 * Returns: the range.
 **/
GnmRange
sheet_get_extent (Sheet const *sheet, gboolean spans_and_merges_extend, gboolean include_hidden)
{
	static GnmRange const dummy = { { 0,0 }, { 0,0 } };
	struct sheet_extent_data closure;
	GSList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), dummy);

	closure.range.start.col = gnm_sheet_get_last_col (sheet) + 1;
	closure.range.start.row = gnm_sheet_get_last_row (sheet) + 1;
	closure.range.end.col   = 0;
	closure.range.end.row   = 0;
	closure.spans_and_merges_extend = spans_and_merges_extend;
	closure.include_hidden = include_hidden;
	closure.ignore_empties = TRUE;

	sheet_cell_foreach (sheet, &cb_sheet_get_extent, &closure);

	for (ptr = sheet->sheet_objects; ptr; ptr = ptr->next) {
		SheetObject *so = GNM_SO (ptr->data);

		closure.range.start.col = MIN (so->anchor.cell_bound.start.col,
					       closure.range.start.col);
		closure.range.start.row = MIN (so->anchor.cell_bound.start.row,
					       closure.range.start.row);
		closure.range.end.col = MAX (so->anchor.cell_bound.end.col,
					     closure.range.end.col);
		closure.range.end.row = MAX (so->anchor.cell_bound.end.row,
					     closure.range.end.row);
	}

	if (closure.range.start.col > gnm_sheet_get_last_col (sheet))
		closure.range.start.col = 0;
	if (closure.range.start.row > gnm_sheet_get_last_row (sheet))
		closure.range.start.row = 0;
	if (closure.range.end.col < 0)
		closure.range.end.col = 0;
	if (closure.range.end.row < 0)
		closure.range.end.row = 0;

	return closure.range;
}

/**
 * sheet_get_cells_extent:
 * @sheet: the sheet
 *
 * Calculates the area occupied by cells, including empty cells.
 *
 * Returns: the range.
 **/
GnmRange
sheet_get_cells_extent (Sheet const *sheet)
{
	static GnmRange const dummy = { { 0,0 }, { 0,0 } };
	struct sheet_extent_data closure;

	g_return_val_if_fail (IS_SHEET (sheet), dummy);

	closure.range.start.col = gnm_sheet_get_last_col (sheet);
	closure.range.start.row = gnm_sheet_get_last_row (sheet);
	closure.range.end.col   = 0;
	closure.range.end.row   = 0;
	closure.spans_and_merges_extend = FALSE;
	closure.include_hidden = TRUE;
	closure.ignore_empties = FALSE;

	sheet_cell_foreach (sheet, &cb_sheet_get_extent, &closure);

	return closure.range;
}


GnmRange *
sheet_get_nominal_printarea (Sheet const *sheet)
{
	GnmNamedExpr *nexpr;
	GnmValue *val;
	GnmParsePos pos;
	GnmRange *r;
	GnmRangeRef const *r_ref;
	gint max_rows;
	gint max_cols;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	parse_pos_init_sheet (&pos, sheet);
	nexpr = expr_name_lookup (&pos, "Print_Area");
	if (nexpr == NULL)
		return NULL;

	val = gnm_expr_top_get_range (nexpr->texpr);
	r_ref = val ? value_get_rangeref (val) : NULL;
	if (r_ref == NULL) {
		value_release (val);
		return NULL;
	}

	r = g_new0 (GnmRange, 1);
	range_init_rangeref (r, r_ref);
	value_release (val);

	if (r->end.col >= (max_cols = gnm_sheet_get_max_cols (sheet)))
		r->end.col = max_cols - 1;
	if (r->end.row >= (max_rows = gnm_sheet_get_max_rows (sheet)))
		r->end.row = max_rows - 1;
	if (r->start.col < 0)
		r->start.col = 0;
	if (r->start.row < 0)
		r->start.row = 0;

	return r;
}

GnmRange
sheet_get_printarea (Sheet const *sheet,
		     gboolean include_styles,
		     gboolean ignore_printarea)
{
	static GnmRange const dummy = { { 0,0 }, { 0,0 } };
	GnmRange print_area;

	g_return_val_if_fail (IS_SHEET (sheet), dummy);

	if (gnm_export_range_for_sheet (sheet, &print_area) >= 0)
		return print_area;

	if (!ignore_printarea) {
		GnmRange *r = sheet_get_nominal_printarea (sheet);
		if (r != NULL) {
			print_area = *r;
			g_free (r);
			return print_area;
		}
	}

	print_area = sheet_get_extent (sheet, TRUE, FALSE);
	if (include_styles)
		sheet_style_get_extent (sheet, &print_area);

	return print_area;
}

struct cb_fit {
	int max;
	gboolean ignore_strings;
	gboolean only_when_needed;
};

/* find the maximum width in a range.  */
static GnmValue *
cb_max_cell_width (GnmCellIter const *iter, struct cb_fit *data)
{
	int width;
	GnmCell *cell = iter->cell;
	GnmRenderedValue *rv;

	if (gnm_cell_is_merged (cell))
		return NULL;

	/*
	 * Special handling for manual recalc.  We need to eval newly
	 * entered expressions.  gnm_cell_render_value will do that for us,
	 * but we want to short-circuit some strings early.
	 */
	if (cell->base.flags & GNM_CELL_HAS_NEW_EXPR)
		gnm_cell_eval (cell);

	if (data->ignore_strings && VALUE_IS_STRING (cell->value))
		return NULL;

	/* Variable width cell must be re-rendered */
	rv = gnm_cell_get_rendered_value (cell);

	if (rv == NULL || rv->variable_width) {
		if (data->only_when_needed && VALUE_IS_FLOAT (cell->value)) {
			// A numeric cell that already fits does not cause
			// a column to be widened.

			gnm_float aval = gnm_abs (value_get_as_float (cell->value));
			GOFormat const *fmt = gnm_cell_get_format (cell);
			gboolean overflowed;

			if (!rv)
				rv = gnm_cell_render_value (cell, TRUE);
			cell_finish_layout (cell, NULL, iter->ci->size_pixels, FALSE);

			overflowed = rv->numeric_overflow;
			if (go_format_is_general (fmt) &&
			    aval < GNM_const(1e8) && aval >= GNM_const(0.001) &&
			    (strchr (gnm_rendered_value_get_text (rv), 'E') ||
			     strchr (gnm_rendered_value_get_text (rv), 'e')))
				overflowed = TRUE;

			if (!overflowed)
				return NULL;
		}

		gnm_cell_render_value (cell, FALSE);
	}

	/* Make sure things are as-if drawn.  */
	cell_finish_layout (cell, NULL, iter->ci->size_pixels, TRUE);

	width = gnm_cell_rendered_width (cell) + gnm_cell_rendered_offset (cell);
	if (width > data->max)
		data->max = width;

	return NULL;
}

/**
 * sheet_col_size_fit_pixels:
 * @sheet: The sheet
 * @col: the column that we want to query
 * @srow: starting row.
 * @erow: ending row.
 * @ignore_strings: skip cells containing string values.  Currently this
 * flags doubles as an indicator that numeric cells should only cause a
 * widening when they would otherwise cause "####" to be displayed.
 *
 * This routine computes the ideal size for the column to make the contents all
 * cells in the column visible.
 *
 * Returns: Maximum size in pixels INCLUDING margins and grid lines
 *          or 0 if there are no cells.
 **/
int
sheet_col_size_fit_pixels (Sheet *sheet, int col, int srow, int erow,
			   gboolean ignore_strings)
{
	struct cb_fit data;
	ColRowInfo *ci = sheet_col_get (sheet, col);
	if (ci == NULL)
		return 0;

	data.max = -1;
	data.ignore_strings = ignore_strings;
	data.only_when_needed = ignore_strings; // Close enough
	sheet_foreach_cell_in_region (sheet,
		CELL_ITER_IGNORE_NONEXISTENT |
		CELL_ITER_IGNORE_HIDDEN |
		CELL_ITER_IGNORE_FILTERED,
		col, srow, col, erow,
		(CellIterFunc)&cb_max_cell_width, &data);

	/* Reset to the default width if the column was empty */
	if (data.max <= 0)
		return 0;

	/* GnmCell width does not include margins or far grid line*/
	return data.max + GNM_COL_MARGIN + GNM_COL_MARGIN + 1;
}

/* find the maximum height in a range. */
static GnmValue *
cb_max_cell_height (GnmCellIter const *iter, struct cb_fit *data)
{
	int height;
	GnmCell *cell = iter->cell;

	if (gnm_cell_is_merged (cell))
		return NULL;

	/*
	 * Special handling for manual recalc.  We need to eval newly
	 * entered expressions.  gnm_cell_render_value will do that for us,
	 * but we want to short-circuit some strings early.
	 */
	if (cell->base.flags & GNM_CELL_HAS_NEW_EXPR)
		gnm_cell_eval (cell);

	if (data->ignore_strings && VALUE_IS_STRING (cell->value))
		return NULL;

	if (!VALUE_IS_STRING (cell->value)) {
		/*
		 * Mildly cheating to avoid performance problems, See bug
		 * 359392.  This assumes that non-strings do not wrap and
		 * that they are all the same height, more or less.
		 */
		Sheet const *sheet = cell->base.sheet;
		height =  gnm_style_get_pango_height (gnm_cell_get_effective_style (cell),
						      sheet->rendered_values->context,
						      sheet->last_zoom_factor_used);
	} else {
		(void)gnm_cell_fetch_rendered_value (cell, TRUE);

		/* Make sure things are as-if drawn.  Inhibit #####s.  */
		cell_finish_layout (cell, NULL, iter->ci->size_pixels, FALSE);

		height = gnm_cell_rendered_height (cell);
	}

	if (height > data->max)
		data->max = height;

	return NULL;
}

/**
 * sheet_row_size_fit_pixels:
 * @sheet: The sheet
 * @row: the row that we want to query
 * @scol: starting column.
 * @ecol: ending column.
 * @ignore_strings: skip cells containing string values.
 *
 * This routine computes the ideal size for the row to make all data fit
 * properly.
 *
 * Returns: Maximum size in pixels INCLUDING margins and grid lines
 *          or 0 if there are no cells.
 **/
int
sheet_row_size_fit_pixels (Sheet *sheet, int row, int scol, int ecol,
			   gboolean ignore_strings)
{
	struct cb_fit data;
	ColRowInfo const *ri = sheet_row_get (sheet, row);
	if (ri == NULL)
		return 0;

	data.max = -1;
	data.ignore_strings = ignore_strings;
	sheet_foreach_cell_in_region (sheet,
		CELL_ITER_IGNORE_NONEXISTENT |
		CELL_ITER_IGNORE_HIDDEN |
		CELL_ITER_IGNORE_FILTERED,
		scol, row,
		ecol, row,
		(CellIterFunc)&cb_max_cell_height, &data);

	/* Reset to the default width if the column was empty */
	if (data.max <= 0)
		return 0;

	/* GnmCell height does not include margins or bottom grid line */
	return data.max + GNM_ROW_MARGIN + GNM_ROW_MARGIN + 1;
}

struct recalc_span_closure {
	Sheet *sheet;
	int col;
};

static gboolean
cb_recalc_spans_in_col (GnmColRowIter const *iter, gpointer user)
{
	struct recalc_span_closure *closure = user;
	int const col = closure->col;
	int left, right;
	CellSpanInfo const *span = row_span_get (iter->cri, col);

	if (span) {
		/* If there is an existing span see if it changed */
		GnmCell const * const cell = span->cell;
		cell_calc_span (cell, &left, &right);
		if (left != span->left || right != span->right) {
			cell_unregister_span (cell);
			cell_register_span (cell, left, right);
		}
	} else {
		/* If there is a cell see if it started to span */
		GnmCell const * const cell = sheet_cell_get (closure->sheet, col, iter->pos);
		if (cell) {
			cell_calc_span (cell, &left, &right);
			if (left != right)
				cell_register_span (cell, left, right);
		}
	}

	return FALSE;
}

/**
 * sheet_recompute_spans_for_col:
 * @sheet: the sheet
 * @col:   The column that changed
 *
 * This routine recomputes the column span for the cells that touches
 * the column.
 */
void
sheet_recompute_spans_for_col (Sheet *sheet, int col)
{
	struct recalc_span_closure closure;
	closure.sheet = sheet;
	closure.col = col;

	sheet_colrow_foreach (sheet, FALSE, 0, -1,
			      &cb_recalc_spans_in_col, &closure);
}

/****************************************************************************/
typedef struct {
	GnmValue *val;
	GnmExprTop const *texpr;
	GnmRange expr_bound;
} closure_set_cell_value;

static GnmValue *
cb_set_cell_content (GnmCellIter const *iter, closure_set_cell_value *info)
{
	GnmExprTop const *texpr = info->texpr;
	GnmCell *cell;

	cell = iter->cell;
	if (!cell)
		cell = sheet_cell_create (iter->pp.sheet,
					  iter->pp.eval.col,
					  iter->pp.eval.row);

	/*
	 * If we are overwriting an array, we need to clear things here
	 * or gnm_cell_set_expr/gnm_cell_set_value will complain.
	 */
	if (cell->base.texpr && gnm_expr_top_is_array (cell->base.texpr))
		gnm_cell_cleanout (cell);

	if (texpr != NULL) {
		if (!range_contains (&info->expr_bound,
				     iter->pp.eval.col, iter->pp.eval.row)) {
			GnmExprRelocateInfo rinfo;

			rinfo.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;
			rinfo.pos = iter->pp;
			rinfo.origin.start = iter->pp.eval;
			rinfo.origin.end   = iter->pp.eval;
			rinfo.origin_sheet = iter->pp.sheet;
			rinfo.target_sheet = iter->pp.sheet;
			rinfo.col_offset = 0;
			rinfo.row_offset = 0;
			texpr = gnm_expr_top_relocate (texpr, &rinfo, FALSE);
		}

		gnm_cell_set_expr (cell, texpr);
	} else
		gnm_cell_set_value (cell, value_dup (info->val));
	return NULL;
}

static GnmValue *
cb_clear_non_corner (GnmCellIter const *iter, GnmRange const *merged)
{
	if (merged->start.col != iter->pp.eval.col ||
	    merged->start.row != iter->pp.eval.row)
		gnm_cell_set_value (iter->cell, value_new_empty ());
	return NULL;
}

/**
 * sheet_range_set_expr_cb:
 * @sr: #GnmSheetRange
 * @texpr: #GnmExprTop
 *
 *
 * Does NOT check for array division.
 **/
static void
sheet_range_set_expr_cb (GnmSheetRange const *sr, GnmExprTop const *texpr)
{
	closure_set_cell_value	closure;
	GSList *merged, *ptr;

	g_return_if_fail (sr != NULL);
	g_return_if_fail (texpr != NULL);

	closure.texpr = texpr;
	gnm_expr_top_get_boundingbox (closure.texpr,
				      sr->sheet,
				      &closure.expr_bound);

	sheet_region_queue_recalc (sr->sheet, &sr->range);
	/* Store the parsed result creating any cells necessary */
	sheet_foreach_cell_in_range
		(sr->sheet, CELL_ITER_ALL, &sr->range,
		 (CellIterFunc)&cb_set_cell_content, &closure);

	merged = gnm_sheet_merge_get_overlap (sr->sheet, &sr->range);
	for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange const *tmp = ptr->data;
		sheet_foreach_cell_in_range
			(sr->sheet, CELL_ITER_IGNORE_BLANK, tmp,
			 (CellIterFunc)&cb_clear_non_corner,
			 (gpointer)tmp);
	}
	g_slist_free (merged);

	sheet_region_queue_recalc (sr->sheet, &sr->range);
	sheet_flag_status_update_range (sr->sheet, &sr->range);
	sheet_queue_respan (sr->sheet, sr->range.start.row,
			    sr->range.end.row);
	sheet_redraw_range (sr->sheet, &sr->range);
}

/**
 * sheet_range_set_expr_undo:
 * @sr: (transfer full): #GnmSheetRange
 * @texpr: (transfer none): #GnmExprTop
 *
 * Returns: (transfer full): the newly created #GOUndo.
 **/
GOUndo *
sheet_range_set_expr_undo (GnmSheetRange *sr, GnmExprTop const  *texpr)
{
	gnm_expr_top_ref (texpr);
	return go_undo_binary_new
		(sr, (gpointer)texpr,
		 (GOUndoBinaryFunc) sheet_range_set_expr_cb,
		 (GFreeFunc) gnm_sheet_range_free,
		 (GFreeFunc) gnm_expr_top_unref);
}


/**
 * sheet_range_set_text:
 * @pos: The position from which to parse an expression.
 * @r:  The range to fill
 * @str: The text to be parsed and assigned.
 *
 * Does NOT check for array division.
 * Does NOT redraw
 * Does NOT generate spans.
 **/
void
sheet_range_set_text (GnmParsePos const *pos, GnmRange const *r, char const *str)
{
	closure_set_cell_value	closure;
	GSList *merged, *ptr;
	Sheet *sheet;

	g_return_if_fail (pos != NULL);
	g_return_if_fail (r != NULL);
	g_return_if_fail (str != NULL);

	sheet = pos->sheet;

	parse_text_value_or_expr (pos, str,
				  &closure.val, &closure.texpr);

	if (closure.texpr)
		gnm_expr_top_get_boundingbox (closure.texpr,
					      sheet,
					      &closure.expr_bound);

	/* Store the parsed result creating any cells necessary */
	sheet_foreach_cell_in_range (sheet, CELL_ITER_ALL, r,
		(CellIterFunc)&cb_set_cell_content, &closure);

	merged = gnm_sheet_merge_get_overlap (sheet, r);
	for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange const *tmp = ptr->data;
		sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_BLANK, tmp,
			(CellIterFunc)&cb_clear_non_corner, (gpointer)tmp);
	}
	g_slist_free (merged);

	sheet_region_queue_recalc (sheet, r);

	value_release (closure.val);
	if (closure.texpr)
		gnm_expr_top_unref (closure.texpr);

	sheet_flag_status_update_range (sheet, r);
}

static void
sheet_range_set_text_cb (GnmSheetRange const *sr, gchar const *text)
{
	GnmParsePos pos;

	pos.eval = sr->range.start;
	pos.sheet = sr->sheet;
	pos.wb = sr->sheet->workbook;

	sheet_range_set_text (&pos, &sr->range, text);
	sheet_region_queue_recalc (sr->sheet, &sr->range);
	sheet_flag_status_update_range (sr->sheet, &sr->range);
	sheet_queue_respan (sr->sheet, sr->range.start.row,
			    sr->range.end.row);
	sheet_redraw_range (sr->sheet, &sr->range);
}

/**
 * sheet_range_set_text_undo:
 * @sr: (transfer full): #GnmSheetRange
 * @text: (transfer none): text for range
 *
 * Returns: (transfer full): the newly created #GOUndo.
 **/
GOUndo *
sheet_range_set_text_undo (GnmSheetRange *sr,
			   char const *text)
{
	return go_undo_binary_new
		(sr, g_strdup (text),
		 (GOUndoBinaryFunc) sheet_range_set_text_cb,
		 (GFreeFunc) gnm_sheet_range_free,
		 (GFreeFunc) g_free);
}


static GnmValue *
cb_set_markup (GnmCellIter const *iter, PangoAttrList *markup)
{
	GnmCell *cell;

	cell = iter->cell;
	if (!cell)
		return NULL;

	if (VALUE_IS_STRING (cell->value)) {
		GOFormat *fmt;
		GnmValue *val = value_dup (cell->value);

		fmt = go_format_new_markup (markup, TRUE);
		value_set_fmt (val, fmt);
		go_format_unref (fmt);

		gnm_cell_cleanout (cell);
		gnm_cell_assign_value (cell, val);
	}
	return NULL;
}

static void
sheet_range_set_markup_cb (GnmSheetRange const *sr, PangoAttrList *markup)
{
	sheet_foreach_cell_in_range
		(sr->sheet, CELL_ITER_ALL, &sr->range,
		 (CellIterFunc)&cb_set_markup, markup);

	sheet_region_queue_recalc (sr->sheet, &sr->range);
	sheet_flag_status_update_range (sr->sheet, &sr->range);
	sheet_queue_respan (sr->sheet, sr->range.start.row,
			    sr->range.end.row);
}

/**
 * sheet_range_set_markup_undo:
 * @sr: (transfer full): #GnmSheetRange
 * @markup: (transfer none) (nullable): #PangoAttrList
 *
 * Returns: (transfer full) (nullable): the newly created #GOUndo.
 **/
GOUndo *
sheet_range_set_markup_undo (GnmSheetRange *sr, PangoAttrList *markup)
{
	if (markup == NULL)
		return NULL;
	return go_undo_binary_new
		(sr, pango_attr_list_ref (markup),
		 (GOUndoBinaryFunc) sheet_range_set_markup_cb,
		 (GFreeFunc) gnm_sheet_range_free,
		 (GFreeFunc) pango_attr_list_unref);
}

/**
 * sheet_cell_get_value:
 * @sheet: Sheet
 * @col: Source column
 * @row: Source row
 *
 * Returns: (transfer none) (nullable): the cell's current value.  The return
 * value will be %NULL only when the cell does not exist.
 **/
GnmValue const *
sheet_cell_get_value (Sheet *sheet, int const col, int const row)
{
	GnmCell *cell;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	cell = sheet_cell_get (sheet, col, row);

	return cell ? cell->value : NULL;
}

/**
 * sheet_cell_set_text:
 * @cell: A cell.
 * @str: the text to set.
 * @markup: (allow-none): an optional PangoAttrList.
 *
 * Marks the sheet as dirty
 * Clears old spans.
 * Flags status updates
 * Queues recalcs
 */
void
sheet_cell_set_text (GnmCell *cell, char const *text, PangoAttrList *markup)
{
	GnmExprTop const *texpr;
	GnmValue *val;
	GnmParsePos pp;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (!gnm_cell_is_nonsingleton_array (cell));

	parse_text_value_or_expr (parse_pos_init_cell (&pp, cell),
		text, &val, &texpr);

	/* Queue a redraw before in case the span changes */
	sheet_redraw_cell (cell);

	if (texpr != NULL) {
		gnm_cell_set_expr (cell, texpr);
		gnm_expr_top_unref (texpr);

		/*
		 * Queue recalc before spanning.  Otherwise spanning may
		 * create a bogus rendered value, see #495879.
		 */
		cell_queue_recalc (cell);

		/* Clear spans from _other_ cells */
		sheet_cell_calc_span (cell, GNM_SPANCALC_SIMPLE);
	} else {
		g_return_if_fail (val != NULL);

		if (markup != NULL && VALUE_IS_STRING (val)) {
			gboolean quoted = (text[0] == '\'');
			PangoAttrList *adj_markup;
			GOFormat *fmt;

			if (quoted) {
				/* We ate the quote.  Adjust.  Ugh.  */
				adj_markup = pango_attr_list_copy (markup);
				go_pango_attr_list_erase (adj_markup, 0, 1);
			} else
				adj_markup = markup;

			fmt = go_format_new_markup (adj_markup, TRUE);
			value_set_fmt (val, fmt);
			go_format_unref (fmt);
			if (quoted)
				pango_attr_list_unref (adj_markup);
		}

		gnm_cell_set_value (cell, val);

		/* Queue recalc before spanning, see above.  */
		cell_queue_recalc (cell);

		sheet_cell_calc_span (cell, GNM_SPANCALC_RESIZE | GNM_SPANCALC_RENDER);
	}

	sheet_flag_status_update_cell (cell);
}

/**
 * sheet_cell_set_text_gi: (rename-to sheet_cell_set_text)
 * @sheet: #Sheet
 * @col: Column number
 * @row: Row number
 * @str: the text to set.
 *
 * Sets the contents of a cell.
 */
void
sheet_cell_set_text_gi (Sheet *sheet, int col, int row, char const *str)
{
	sheet_cell_set_text (sheet_cell_fetch (sheet, col, row), str, NULL);
}


/**
 * sheet_cell_set_expr:
 * @cell: #GnmCell
 * @texpr: New expression for @cell.
 *
 * Marks the sheet as dirty
 * Clears old spans.
 * Flags status updates
 * Queues recalcs
 */
void
sheet_cell_set_expr (GnmCell *cell, GnmExprTop const *texpr)
{
	gnm_cell_set_expr (cell, texpr);

	/* clear spans from _other_ cells */
	sheet_cell_calc_span (cell, GNM_SPANCALC_SIMPLE);

	cell_queue_recalc (cell);
	sheet_flag_status_update_cell (cell);
}

/**
 * sheet_cell_set_value: (skip)
 * @cell: #GnmCell
 * @v: (transfer full): #GnmValue
 *
 * Stores, without copying, the supplied value.  It marks the
 * sheet as dirty.
 *
 * The value is rendered and spans are calculated.  It queues a redraw
 * and checks to see if the edit region or selection content changed.
 *
 * NOTE : This DOES check for array partitioning.
 */
void
sheet_cell_set_value (GnmCell *cell, GnmValue *v)
{
	/* TODO : if the value is unchanged do not assign it */
	gnm_cell_set_value (cell, v);
	sheet_cell_calc_span (cell, GNM_SPANCALC_RESIZE | GNM_SPANCALC_RENDER);
	cell_queue_recalc (cell);
	sheet_flag_status_update_cell (cell);
}

/**
 * sheet_cell_set_value_gi: (rename-to sheet_cell_set_value)
 * @sheet: #Sheet
 * @col: Column number
 * @row: Row number
 * @v: #GnmValue
 *
 * Set the value of the cell at (@col,@row) to @v.
 *
 * The value is rendered and spans are calculated.  It queues a redraw
 * and checks to see if the edit region or selection content changed.
 */
void
sheet_cell_set_value_gi (Sheet *sheet, int col, int row, GnmValue *v)
{
	// This version exists because not all versions of pygobject
	// understand transfer-full parameters
	sheet_cell_set_value (sheet_cell_fetch (sheet, col, row),
			      value_dup (v));
}

/****************************************************************************/

/*
 * This routine is used to queue the redraw regions for the
 * cell region specified.
 *
 * It is usually called before a change happens to a region,
 * and after the change has been done to queue the regions
 * for the old contents and the new contents.
 *
 * It intelligently handles spans and merged ranges
 */
void
sheet_range_bounding_box (Sheet const *sheet, GnmRange *bound)
{
	GSList *ptr;
	int row;
	GnmRange r = *bound;

	g_return_if_fail (range_is_sane	(bound));

	/* Check the first and last columns for spans and extend the region to
	 * include the maximum extent.
	 */
	for (row = r.start.row; row <= r.end.row; row++){
		ColRowInfo const *ri = sheet_row_get (sheet, row);

		if (ri != NULL) {
			CellSpanInfo const * span0;

			if (ri->needs_respan)
				row_calc_spans ((ColRowInfo *)ri, row, sheet);

			span0 = row_span_get (ri, r.start.col);

			if (span0 != NULL) {
				if (bound->start.col > span0->left)
					bound->start.col = span0->left;
				if (bound->end.col < span0->right)
					bound->end.col = span0->right;
			}
			if (r.start.col != r.end.col) {
				CellSpanInfo const * span1 =
					row_span_get (ri, r.end.col);

				if (span1 != NULL) {
					if (bound->start.col > span1->left)
						bound->start.col = span1->left;
					if (bound->end.col < span1->right)
						bound->end.col = span1->right;
				}
			}
			/* skip segments with no cells */
		} else if (row == COLROW_SEGMENT_START (row)) {
			ColRowSegment const * const segment =
				COLROW_GET_SEGMENT (&(sheet->rows), row);
			if (segment == NULL)
				row = COLROW_SEGMENT_END (row);
		}
	}

	/* TODO : this may get expensive if there are alot of merged ranges */
	/* no need to iterate, one pass is enough */
	for (ptr = sheet->list_merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange const * const test = ptr->data;
		if (r.start.row <= test->end.row || r.end.row >= test->start.row) {
			if (bound->start.col > test->start.col)
				bound->start.col = test->start.col;
			if (bound->end.col < test->end.col)
				bound->end.col = test->end.col;
			if (bound->start.row > test->start.row)
				bound->start.row = test->start.row;
			if (bound->end.row < test->end.row)
				bound->end.row = test->end.row;
		}
	}
}

void
sheet_redraw_region (Sheet const *sheet,
		     int start_col, int start_row,
		     int end_col,   int end_row)
{
	GnmRange r;
	g_return_if_fail (IS_SHEET (sheet));

	range_init (&r, start_col, start_row, end_col, end_row);
	sheet_redraw_range (sheet, &r);
}


/**
 * sheet_redraw_range:
 * @sheet: sheet to redraw
 * @range: range to redraw
 *
 * Redraw the indicated range, or at least the visible parts of it.
 */
void
sheet_redraw_range (Sheet const *sheet, GnmRange const *range)
{
	GnmRange bound;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);

	// We potentially do a lot of recalcs as part of this, so make sure
	// stuff that caches sub-computations see the whole thing instead
	// of clearing between cells.
	gnm_app_recalc_start ();

	bound = *range;
	sheet_range_bounding_box (sheet, &bound);

	SHEET_FOREACH_CONTROL (sheet, view, control,
		sc_redraw_range (control, &bound););

	gnm_app_recalc_finish ();
}

static gboolean
cb_pending_redraw_handler (Sheet *sheet)
{
	unsigned ui, len;
	GArray *arr = sheet->pending_redraw;

	// It's possible that more redraws will arrive as we process these
	// so be careful only to touch the right ones.

	if (debug_redraw)
		g_printerr ("Entering redraw with %u ranges\n", arr->len);
	if (arr->len >= 2) {
		gnm_range_simplify (arr);
		if (debug_redraw)
			g_printerr ("Down to %u ranges\n", arr->len);
	}

	// Lock down the length we handle here
	len = arr->len;
	for (ui = 0; ui < len; ui++) {
		GnmRange const *r = &g_array_index (arr, GnmRange, ui);
		if (debug_redraw)
			g_printerr ("Redrawing %s\n", range_as_string (r));
		sheet_redraw_range (sheet, r);
	}
	g_array_remove_range (arr, 0, len);

	if (arr->len == 0) {
		sheet->pending_redraw_src = 0;
		return FALSE;
	} else
		return TRUE;
}

/**
 * sheet_queue_redraw_range:
 * @sheet: sheet to redraw
 * @range: range to redraw
 *
 * This queues a redraw for the indicated range.  The redraw will happen
 * when Gnumeric returns to the gui main loop.
 */
void
sheet_queue_redraw_range (Sheet *sheet, GnmRange const *range)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);

	if (sheet->workbook->being_loaded) {
		if (debug_redraw)
			g_printerr ("Ignoring redraw of %s during loading\n", range_as_string (range));
		return;
	}

	if (debug_redraw)
		g_printerr ("Adding redraw %s\n", range_as_string (range));

	g_array_append_val (sheet->pending_redraw, *range);

	if (sheet->pending_redraw_src == 0)
		sheet->pending_redraw_src =
			g_timeout_add (0,
				       (GSourceFunc)cb_pending_redraw_handler,
				       sheet);
}


/****************************************************************************/

gboolean
sheet_col_is_hidden (Sheet const *sheet, int col)
{
	ColRowInfo const * const res = sheet_col_get (sheet, col);
	return (res != NULL && !res->visible);
}

gboolean
sheet_row_is_hidden (Sheet const *sheet, int row)
{
	ColRowInfo const * const res = sheet_row_get (sheet, row);
	return (res != NULL && !res->visible);
}


/**
 * sheet_find_boundary_horizontal:
 * @sheet: The Sheet
 * @col: The column from which to begin searching.
 * @move_row: The row in which to search for the edge of the range.
 * @base_row: The height of the area being moved.
 * @count:      units to extend the selection vertically
 * @jump_to_boundaries: Jump to range boundaries.
 *
 * Calculate the column index for the column which is @count units
 * from @start_col doing bounds checking.  If @jump_to_boundaries is
 * %TRUE then @count must be 1 and the jump is to the edge of the logical range.
 *
 * This routine implements the logic necessary for ctrl-arrow style
 * movement.  That is more complicated than simply finding the last in a list
 * of cells with content.  If you are at the end of a range it will find the
 * start of the next.  Make sure that is the sort of behavior you want before
 * calling this.
 *
 * Returns: the column index.
 **/
int
sheet_find_boundary_horizontal (Sheet *sheet, int start_col, int move_row,
				int base_row, int count,
				gboolean jump_to_boundaries)
{
	gboolean find_nonblank = sheet_is_cell_empty (sheet, start_col, move_row);
	gboolean keep_looking = FALSE;
	int new_col, prev_col, lagged_start_col, max_col = gnm_sheet_get_last_col (sheet);
	int iterations = 0;
	GnmRange check_merge;
	GnmRange const * const bound = &sheet->priv->unhidden_region;

	/* Jumping to bounds requires steping cell by cell */
	g_return_val_if_fail (count == 1 || count == -1 || !jump_to_boundaries, start_col);
	g_return_val_if_fail (IS_SHEET (sheet), start_col);

	if (move_row < base_row) {
		check_merge.start.row = move_row;
		check_merge.end.row = base_row;
	} else {
		check_merge.end.row = move_row;
		check_merge.start.row = base_row;
	}

	do {
		GSList *merged, *ptr;

		lagged_start_col = check_merge.start.col = check_merge.end.col = start_col;
		merged = gnm_sheet_merge_get_overlap (sheet, &check_merge);
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
			GnmRange const * const r = ptr->data;
			if (count > 0) {
				if (start_col < r->end.col)
					start_col = r->end.col;
			} else {
				if (start_col > r->start.col)
					start_col = r->start.col;
			}
		}
		g_slist_free (merged);
	} while (start_col != lagged_start_col);
	new_col = prev_col = start_col;

	do {
		new_col += count;
		++iterations;

		if (new_col < bound->start.col)
			return MIN (bound->start.col, max_col);
		if (new_col > bound->end.col)
			return MIN (bound->end.col, max_col);

		keep_looking = sheet_col_is_hidden (sheet, new_col);
		if (jump_to_boundaries) {
			if (new_col > sheet->cols.max_used) {
				if (count > 0)
					return (find_nonblank || iterations == 1)?
						MIN (bound->end.col, max_col):
						MIN (prev_col, max_col);
				new_col = sheet->cols.max_used;
			}

			keep_looking |= (sheet_is_cell_empty (sheet, new_col, move_row) == find_nonblank);
			if (keep_looking)
				prev_col = new_col;
			else if (!find_nonblank) {
				/*
				 * Handle special case where we are on the last
				 * non-NULL cell
				 */
				if (iterations == 1)
					keep_looking = find_nonblank = TRUE;
				else
					new_col = prev_col;
			}
		}
	} while (keep_looking);

	return MIN (new_col, max_col);
}

/**
 * sheet_find_boundary_vertical:
 * @sheet: The Sheet
 * @move_col: The col in which to search for the edge of the range.
 * @row: The row from which to begin searching.
 * @base_col: The width of the area being moved.
 * @count: units to extend the selection vertically
 * @jump_to_boundaries: Jump to range boundaries.
 *
 * Calculate the row index for the row which is @count units
 * from @start_row doing bounds checking.  If @jump_to_boundaries is
 * %TRUE then @count must be 1 and the jump is to the edge of the logical range.
 *
 * This routine implements the logic necessary for ctrl-arrow style
 * movement.  That is more complicated than simply finding the last in a list
 * of cells with content.  If you are at the end of a range it will find the
 * start of the next.  Make sure that is the sort of behavior you want before
 * calling this.
 *
 * Returns: the row index.
 **/
int
sheet_find_boundary_vertical (Sheet *sheet, int move_col, int start_row,
			      int base_col, int count,
			      gboolean jump_to_boundaries)
{
	gboolean find_nonblank = sheet_is_cell_empty (sheet, move_col, start_row);
	gboolean keep_looking = FALSE;
	int new_row, prev_row, lagged_start_row, max_row = gnm_sheet_get_last_row (sheet);
	int iterations = 0;
	GnmRange check_merge;
	GnmRange const * const bound = &sheet->priv->unhidden_region;

	/* Jumping to bounds requires steping cell by cell */
	g_return_val_if_fail (count == 1 || count == -1 || !jump_to_boundaries, start_row);
	g_return_val_if_fail (IS_SHEET (sheet), start_row);

	if (move_col < base_col) {
		check_merge.start.col = move_col;
		check_merge.end.col = base_col;
	} else {
		check_merge.end.col = move_col;
		check_merge.start.col = base_col;
	}

	do {
		GSList *merged, *ptr;

		lagged_start_row = check_merge.start.row = check_merge.end.row = start_row;
		merged = gnm_sheet_merge_get_overlap (sheet, &check_merge);
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
			GnmRange const * const r = ptr->data;
			if (count > 0) {
				if (start_row < r->end.row)
					start_row = r->end.row;
			} else {
				if (start_row > r->start.row)
					start_row = r->start.row;
			}
		}
		g_slist_free (merged);
	} while (start_row != lagged_start_row);
	new_row = prev_row = start_row;

	do {
		new_row += count;
		++iterations;

		if (new_row < bound->start.row)
			return MIN (bound->start.row, max_row);
		if (new_row > bound->end.row)
			return MIN (bound->end.row, max_row);

		keep_looking = sheet_row_is_hidden (sheet, new_row);
		if (jump_to_boundaries) {
			if (new_row > sheet->rows.max_used) {
				if (count > 0)
					return (find_nonblank || iterations == 1)?
						MIN (bound->end.row, max_row):
						MIN (prev_row, max_row);
				new_row = sheet->rows.max_used;
			}

			keep_looking |= (sheet_is_cell_empty (sheet, move_col, new_row) == find_nonblank);
			if (keep_looking)
				prev_row = new_row;
			else if (!find_nonblank) {
				/*
				 * Handle special case where we are on the last
				 * non-NULL cell
				 */
				if (iterations == 1)
					keep_looking = find_nonblank = TRUE;
				else
					new_row = prev_row;
			}
		}
	} while (keep_looking);

	return MIN (new_row, max_row);
}

typedef enum {
	CHECK_AND_LOAD_START = 1,
	CHECK_END = 2,
	LOAD_END  = 4
} ArrayCheckFlags;

typedef struct {
	Sheet const *sheet;
	int flags;
	int start, end;
	GnmRange const *ignore;

	GnmRange	error;
} ArrayCheckData;

static gboolean
cb_check_array_horizontal (GnmColRowIter const *iter, gpointer data_)
{
	ArrayCheckData *data = data_;
	gboolean is_array = FALSE;

	if (data->flags & CHECK_AND_LOAD_START  &&	/* Top */
	    (is_array = gnm_cell_array_bound (
			sheet_cell_get (data->sheet, iter->pos, data->start),
			&data->error)) &&
	    data->error.start.row < data->start &&
	    (data->ignore == NULL ||
	     !range_contained (&data->error, data->ignore)))
	    return TRUE;

	if (data->flags & LOAD_END)
		is_array = gnm_cell_array_bound (
			sheet_cell_get (data->sheet, iter->pos, data->end),
			&data->error);

	return (data->flags & CHECK_END &&
		is_array &&
		data->error.end.row > data->end &&	/* Bottom */
		(data->ignore == NULL ||
		 !range_contained (&data->error, data->ignore)));
}

static gboolean
cb_check_array_vertical (GnmColRowIter const *iter, gpointer data_)
{
	ArrayCheckData *data = data_;
	gboolean is_array = FALSE;

	if (data->flags & CHECK_AND_LOAD_START &&	/* Left */
	    (is_array = gnm_cell_array_bound (
			sheet_cell_get (data->sheet, data->start, iter->pos),
			&data->error)) &&
	    data->error.start.col < data->start &&
	    (data->ignore == NULL ||
	     !range_contained (&data->error, data->ignore)))
	    return TRUE;

	if (data->flags & LOAD_END)
		is_array = gnm_cell_array_bound (
			sheet_cell_get (data->sheet, data->end, iter->pos),
			&data->error);

	return (data->flags & CHECK_END &&
		is_array &&
		data->error.end.col > data->end &&	/* Right */
		(data->ignore == NULL ||
		 !range_contained (&data->error, data->ignore)));
}

/**
 * sheet_range_splits_array:
 * @sheet: The sheet.
 * @r: The range to check
 * @ignore: (nullable): a range in which it is ok to have an array.
 * @cc: (nullable): place to report an error.
 * @cmd: (nullable): cmd name used with @cc.
 *
 * Check the outer edges of range @sheet!@r to ensure that if an array is
 * within it then the entire array is within the range.  @ignore is useful when
 * src and dest ranges may overlap.
 *
 * Returns: %TRUE if an array would be split.
 **/
gboolean
sheet_range_splits_array (Sheet const *sheet,
			  GnmRange const *r, GnmRange const *ignore,
			  GOCmdContext *cc, char const *cmd)
{
	ArrayCheckData closure;

	g_return_val_if_fail (r->start.col <= r->end.col, FALSE);
	g_return_val_if_fail (r->start.row <= r->end.row, FALSE);

	closure.sheet = sheet;
	closure.ignore = ignore;

	closure.start = r->start.row;
	closure.end = r->end.row;
	if (closure.start <= 0) {
		closure.flags = (closure.end < sheet->rows.max_used)
			? CHECK_END | LOAD_END
			: 0;
	} else if (closure.end < sheet->rows.max_used)
		closure.flags = (closure.start == closure.end)
			? CHECK_AND_LOAD_START | CHECK_END
			: CHECK_AND_LOAD_START | CHECK_END | LOAD_END;
	else
		closure.flags = CHECK_AND_LOAD_START;

	if (closure.flags &&
	    sheet_colrow_foreach (sheet, TRUE,
				  r->start.col, r->end.col,
				  cb_check_array_horizontal, &closure)) {
		if (cc)
			gnm_cmd_context_error_splits_array (cc,
				cmd, &closure.error);
		return TRUE;
	}

	closure.start = r->start.col;
	closure.end = r->end.col;
	if (closure.start <= 0) {
		closure.flags = (closure.end < sheet->cols.max_used)
			? CHECK_END | LOAD_END
			: 0;
	} else if (closure.end < sheet->cols.max_used)
		closure.flags = (closure.start == closure.end)
			? CHECK_AND_LOAD_START | CHECK_END
			: CHECK_AND_LOAD_START | CHECK_END | LOAD_END;
	else
		closure.flags = CHECK_AND_LOAD_START;

	if (closure.flags &&
	    sheet_colrow_foreach (sheet, FALSE,
				  r->start.row, r->end.row,
				  cb_check_array_vertical, &closure)) {
		if (cc)
			gnm_cmd_context_error_splits_array (cc,
				cmd, &closure.error);
		return TRUE;
	}
	return FALSE;
}

/**
 * sheet_range_splits_region:
 * @sheet: the sheet.
 * @r: The range whose boundaries are checked
 * @ignore: An optional range in which it is ok to have arrays and merges
 * @cc: The context that issued the command
 * @cmd: The translated command name.
 *
 * A utility to see whether moving the range @r will split any arrays
 * or merged regions.
 * Returns: whether any arrays or merged regions will be split.
 */
gboolean
sheet_range_splits_region (Sheet const *sheet,
			   GnmRange const *r, GnmRange const *ignore,
			   GOCmdContext *cc, char const *cmd_name)
{
	GSList *merged;

	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	/* Check for array subdivision */
	if (sheet_range_splits_array (sheet, r, ignore, cc, cmd_name))
		return TRUE;

	merged = gnm_sheet_merge_get_overlap (sheet, r);
	if (merged) {
		GSList *ptr;

		for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
			GnmRange const *m = ptr->data;
			if (ignore != NULL && range_contained (m, ignore))
				continue;
			if (!range_contained (m, r))
				break;
		}
		g_slist_free (merged);

		if (cc != NULL && ptr != NULL) {
			go_cmd_context_error_invalid (cc, cmd_name,
				_("Target region contains merged cells"));
			return TRUE;
		}
	}
	return FALSE;
}

/**
 * sheet_ranges_split_region:
 * @sheet: the sheet.
 * @ranges: (element-type GnmRange): A list of ranges to check.
 * @cc: The context that issued the command
 * @cmd: The translated command name.
 *
 * A utility to see whether moving any of the ranges @ranges will split any
 * arrays or merged regions.
 * Returns: whether any arrays or merged regions will be split.
 */
gboolean
sheet_ranges_split_region (Sheet const * sheet, GSList const *ranges,
			   GOCmdContext *cc, char const *cmd)
{
	GSList const *l;

	/* Check for array subdivision */
	for (l = ranges; l != NULL; l = l->next) {
		GnmRange const *r = l->data;
		if (sheet_range_splits_region (sheet, r, NULL, cc, cmd))
			return TRUE;
	}
	return FALSE;
}

static GnmValue *
cb_cell_is_array (GnmCellIter const *iter, G_GNUC_UNUSED gpointer user)
{
	return gnm_cell_is_array (iter->cell) ? VALUE_TERMINATE : NULL;
}

/**
 * sheet_range_contains_merges_or_arrays:
 * @sheet: The sheet
 * @r: the range to check.
 * @cc: an optional place to report errors.
 * @cmd:
 * @merges: if %TRUE, check for merges.
 * @arrays: if %TRUE, check for arrays.
 *
 * Check to see if the target region @sheet!@r contains any merged regions or
 * arrays.  Report an error to the @cc if it is supplied.
 * Returns: %TRUE if the target region @sheet!@r contains any merged regions or
 * arrays.
 **/
gboolean
sheet_range_contains_merges_or_arrays (Sheet const *sheet, GnmRange const *r,
				       GOCmdContext *cc, char const *cmd,
				       gboolean merges, gboolean arrays)
{
	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	if (merges) {
		GSList *merged = gnm_sheet_merge_get_overlap (sheet, r);
		if (merged != NULL) {
			if (cc != NULL)
				go_cmd_context_error_invalid
					(cc, cmd,
					 _("cannot operate on merged cells"));
			g_slist_free (merged);
			return TRUE;
		}
	}

	if (arrays) {
		if (sheet_foreach_cell_in_range (
			    (Sheet *)sheet, CELL_ITER_IGNORE_NONEXISTENT, r,
			    cb_cell_is_array, NULL)) {
			if (cc != NULL)
				go_cmd_context_error_invalid
					(cc, cmd,
					 _("cannot operate on array formul\303\246"));
			return TRUE;
		}
	}

	return FALSE;
}

/***************************************************************************/

/**
 * sheet_colrow_get_default:
 * @sheet:
 * @is_cols: %TRUE for columns, %FALSE for rows.
 *
 * Returns: (transfer none): the default #ColRowInfo.
 */
ColRowInfo const *
sheet_colrow_get_default (Sheet const *sheet, gboolean is_cols)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	return is_cols ? &sheet->cols.default_style : &sheet->rows.default_style;
}

static void
sheet_colrow_optimize1 (int max, int max_used, ColRowCollection *collection)
{
	int i;
	int first_unused = max_used + 1;

	for (i = COLROW_SEGMENT_START (first_unused);
	     i < max;
	     i += COLROW_SEGMENT_SIZE) {
		ColRowSegment *segment = COLROW_GET_SEGMENT (collection, i);
		int j;
		gboolean any = FALSE;

		if (!segment)
			continue;
		for (j = 0; j < COLROW_SEGMENT_SIZE; j++) {
			ColRowInfo *info = segment->info[j];
			if (!info)
				continue;
			if (i + j >= first_unused &&
			    col_row_info_equal (&collection->default_style, info)) {
				colrow_free (info);
				segment->info[j] = NULL;
			} else {
				any = TRUE;
				if (i + j >= first_unused)
					max_used = i + j;
			}
		}

		if (!any) {
			g_free (segment);
			COLROW_GET_SEGMENT (collection, i) = NULL;
		}
	}

	collection->max_used = max_used;
}

void
sheet_colrow_optimize (Sheet *sheet)
{
	GnmRange extent;

	g_return_if_fail (IS_SHEET (sheet));

	extent = sheet_get_cells_extent (sheet);

	sheet_colrow_optimize1 (gnm_sheet_get_max_cols (sheet),
				extent.end.col,
				&sheet->cols);
	sheet_colrow_optimize1 (gnm_sheet_get_max_rows (sheet),
				extent.end.row,
				&sheet->rows);
}

/**
 * sheet_col_get:
 * @sheet: The sheet to query
 * @col: Column number
 *
 * Returns: (transfer none) (nullable): A #ColRowInfo for the column, or %NULL
 * if none has been allocated yet.
 */
ColRowInfo *
sheet_col_get (Sheet const *sheet, int col)
{
	ColRowSegment *segment;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (col < gnm_sheet_get_max_cols (sheet), NULL);
	g_return_val_if_fail (col >= 0, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->cols), col);
	if (segment != NULL)
		return segment->info[COLROW_SUB_INDEX (col)];
	return NULL;
}

/**
 * sheet_row_get:
 * @sheet: The sheet to query
 * @row: Row number
 *
 * Returns: (transfer none) (nullable): A #ColRowInfo for the row, or %NULL
 * if none has been allocated yet.
 */
ColRowInfo *
sheet_row_get (Sheet const *sheet, int row)
{
	ColRowSegment *segment;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (row < gnm_sheet_get_max_rows (sheet), NULL);
	g_return_val_if_fail (row >= 0, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->rows), row);
	if (segment != NULL)
		return segment->info[COLROW_SUB_INDEX (row)];
	return NULL;
}

ColRowInfo *
sheet_colrow_get (Sheet const *sheet, int colrow, gboolean is_cols)
{
	return is_cols
		? sheet_col_get (sheet, colrow)
		: sheet_row_get (sheet, colrow);
}

/**
 * sheet_col_fetch:
 * @sheet: The sheet to query
 * @col: Column number
 *
 * Returns: (transfer none): The #ColRowInfo for column @col.  This result
 * will not be the default #ColRowInfo and may be changed.
 */
ColRowInfo *
sheet_col_fetch (Sheet *sheet, int pos)
{
	ColRowInfo *cri = sheet_col_get (sheet, pos);
	if (NULL == cri && NULL != (cri = sheet_col_new (sheet)))
		sheet_colrow_add (sheet, cri, TRUE, pos);
	return cri;
}

/**
 * sheet_row_fetch:
 * @sheet: The sheet to query
 * @row: Row number
 *
 * Returns: (transfer none): The #ColRowInfo for row @row.  This result
 * will not be the default #ColRowInfo and may be changed.
 */
ColRowInfo *
sheet_row_fetch (Sheet *sheet, int pos)
{
	ColRowInfo *cri = sheet_row_get (sheet, pos);
	if (NULL == cri && NULL != (cri = sheet_row_new (sheet)))
		sheet_colrow_add (sheet, cri, FALSE, pos);
	return cri;
}

ColRowInfo *
sheet_colrow_fetch (Sheet *sheet, int colrow, gboolean is_cols)
{
	return is_cols
		? sheet_col_fetch (sheet, colrow)
		: sheet_row_fetch (sheet, colrow);
}

/**
 * sheet_col_get_info:
 * @sheet: The sheet to query
 * @col: Column number
 *
 * Returns: (transfer none): The #ColRowInfo for column @col.  This may be
 * the default #ColRowInfo for columns and should not be changed.
 */
ColRowInfo const *
sheet_col_get_info (Sheet const *sheet, int col)
{
	ColRowInfo *ci = sheet_col_get (sheet, col);

	if (ci != NULL)
		return ci;
	return &sheet->cols.default_style;
}

/**
 * sheet_row_get_info:
 * @sheet: The sheet to query
 * @row: column number
 *
 * Returns: (transfer none): The #ColRowInfo for row @row.  This may be
 * the default #ColRowInfo for rows and should not be changed.
 */
ColRowInfo const *
sheet_row_get_info (Sheet const *sheet, int row)
{
	ColRowInfo *ri = sheet_row_get (sheet, row);

	if (ri != NULL)
		return ri;
	return &sheet->rows.default_style;
}

ColRowInfo const *
sheet_colrow_get_info (Sheet const *sheet, int colrow, gboolean is_cols)
{
	return is_cols
		? sheet_col_get_info (sheet, colrow)
		: sheet_row_get_info (sheet, colrow);
}

void
gnm_sheet_mark_colrow_changed (Sheet *sheet, int colrow, gboolean is_cols)
{
	ColRowCollection *infos = is_cols ? &sheet->cols : &sheet->rows;
	int ix = COLROW_SEGMENT_INDEX (colrow);

	if (gnm_debug_flag ("colrow-pixel-start")) {
		if (is_cols)
			g_printerr ("Changed column %s onwards\n",
				    col_name (colrow));
		else
			g_printerr ("Changed row %s onwards\n",
				    row_name (colrow));
	}

	// Mark anything from ix onwards as invalid
	infos->last_valid_pixel_start =
		MIN (infos->last_valid_pixel_start, ix - 1);
}

void
sheet_colrow_copy_info (Sheet *sheet, int colrow, gboolean is_cols,
			ColRowInfo const *cri)
{
	ColRowInfo *dst = sheet_colrow_fetch (sheet, colrow, is_cols);

	dst->size_pts      = cri->size_pts;
	dst->size_pixels   = cri->size_pixels;
	dst->outline_level = cri->outline_level;
	dst->is_collapsed  = cri->is_collapsed;
	dst->hard_size     = cri->hard_size;
	dst->visible       = cri->visible;

	gnm_sheet_mark_colrow_changed (sheet, colrow, is_cols);
}


/**
 * sheet_colrow_foreach:
 * @sheet: #Sheet
 * @is_cols: %TRUE for columns, %FALSE for rows.
 * @first:	start position (inclusive)
 * @last:	stop position (inclusive), -1 meaning end-of-sheet
 * @callback: (scope call): A callback function which should return %TRUE
 *    to stop the iteration.
 * @user_data:	A baggage pointer.
 *
 * Iterates through the existing rows or columns within the range supplied.
 * If a callback returns %TRUE, iteration stops.
 **/
gboolean
sheet_colrow_foreach (Sheet const *sheet,
		      gboolean is_cols,
		      int first, int last,
		      ColRowHandler callback,
		      gpointer user_data)
{
	ColRowCollection const *infos;
	GnmColRowIter iter;
	ColRowSegment const *segment;
	int sub, inner_last, i;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	if (last == -1)
		last = colrow_max (is_cols, sheet) - 1;
	infos = is_cols ? &sheet->cols : &sheet->rows;

	/* clip */
	if (last > infos->max_used)
		last = infos->max_used;

	for (i = first; i <= last ; ) {
		segment = COLROW_GET_SEGMENT (infos, i);
		sub = COLROW_SUB_INDEX(i);
		inner_last = (COLROW_SEGMENT_INDEX (last) == COLROW_SEGMENT_INDEX (i))
			? COLROW_SUB_INDEX (last)+1 : COLROW_SEGMENT_SIZE;
		iter.pos = i;
		i += COLROW_SEGMENT_SIZE - sub;
		if (segment == NULL)
			continue;

		for (; sub < inner_last; sub++, iter.pos++) {
			iter.cri = segment->info[sub];
			if (iter.cri != NULL && (*callback)(&iter, user_data))
				return TRUE;
		}
	}

	return FALSE;
}


/*****************************************************************************/

static gint
cell_ordering (gconstpointer a_, gconstpointer b_)
{
	GnmCell const *a = *(GnmCell **)a_;
	GnmCell const *b = *(GnmCell **)b_;

	if (a->pos.row != b->pos.row)
		return a->pos.row - b->pos.row;

	return a->pos.col - b->pos.col;
}

/**
 * sheet_cells:
 * @sheet: a #Sheet
 * @r: (nullable): a #GnmRange
 *
 * Retrieves an array of all cells inside @r.
 * Returns: (element-type GnmCell) (transfer container): the cells array.
 **/
GPtrArray *
sheet_cells (Sheet *sheet, const GnmRange *r)
{
	GPtrArray *res = g_ptr_array_new ();
	GHashTableIter hiter;
	gpointer value;

	g_hash_table_iter_init (&hiter, sheet->cell_hash);
	while (g_hash_table_iter_next (&hiter, NULL, &value)) {
		GnmCell *cell = value;
		if (!r || range_contains (r, cell->pos.col, cell->pos.row))
			g_ptr_array_add (res, cell);
	}
	g_ptr_array_sort (res, cell_ordering);

	return res;
}



#define SWAP_INT(a,b) do { int t; t = a; a = b; b = t; } while (0)

/**
 * sheet_foreach_cell_in_range:
 * @sheet: #Sheet
 * @flags:
 * @r: #GnmRange
 * @callback: (scope call): #CellFilterFunc
 * @closure: user data.
 *
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is passed, then
 * callbacks are only invoked for existing cells.
 *
 * Note: this function does not honour the CELL_ITER_IGNORE_SUBTOTAL flag.
 *
 * Returns: (transfer none): the value returned by the callback, which can be:
 *    non-%NULL on error, or VALUE_TERMINATE if some invoked routine requested
 *    to stop (by returning non-%NULL).
 *
 * NOTE: between 0.56 and 0.57, the traversal order changed.  The order is now
 *
 *        1    2    3
 *        4    5    6
 *        7    8    9
 *
 * (This appears to be the order in which XL looks at the values of ranges.)
 * If your code depends on any particular ordering, please add a very visible
 * comment near the call.
 */
GnmValue *
sheet_foreach_cell_in_range (Sheet *sheet, CellIterFlags flags,
			     GnmRange const *r,
			     CellIterFunc callback,
			     gpointer     closure)
{
	return sheet_foreach_cell_in_region (sheet, flags,
					     r->start.col, r->start.row,
					     r->end.col, r->end.row,
					     callback, closure);
}


/**
 * sheet_foreach_cell_in_region:
 * @sheet: #Sheet
 * @flags:
 * @start_col: Starting column
 * @start_row: Starting row
 * @end_col: Ending column, -1 meaning last
 * @end_row: Ending row, -1 meaning last
 * @callback: (scope call): #CellFilterFunc
 * @closure: user data.
 *
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is passed, then
 * callbacks are only invoked for existing cells.
 *
 * Note: this function does not honour the CELL_ITER_IGNORE_SUBTOTAL flag.
 *
 * Returns: (transfer none): the value returned by the callback, which can be:
 *    non-%NULL on error, or VALUE_TERMINATE if some invoked routine requested
 *    to stop (by returning non-%NULL).
 *
 * NOTE: between 0.56 and 0.57, the traversal order changed.  The order is now
 *
 *        1    2    3
 *        4    5    6
 *        7    8    9
 *
 * (This appears to be the order in which XL looks at the values of ranges.)
 * If your code depends on any particular ordering, please add a very visible
 * comment near the call.
 */
GnmValue *
sheet_foreach_cell_in_region (Sheet *sheet, CellIterFlags flags,
			     int start_col, int start_row,
			     int end_col,   int end_row,
			     CellIterFunc callback, void *closure)
{
	GnmValue *cont;
	GnmCellIter iter;
	gboolean const visibility_matters = (flags & CELL_ITER_IGNORE_HIDDEN) != 0;
	gboolean const ignore_filtered = (flags & CELL_ITER_IGNORE_FILTERED) != 0;
	gboolean const only_existing = (flags & CELL_ITER_IGNORE_NONEXISTENT) != 0;
	gboolean const ignore_empty = (flags & CELL_ITER_IGNORE_EMPTY) != 0;
	gboolean ignore;
	gboolean use_celllist;
	guint64 range_size;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (callback != NULL, NULL);

	// For convenience
	if (end_col == -1) end_col = gnm_sheet_get_last_col (sheet);
	if (end_row == -1) end_row = gnm_sheet_get_last_row (sheet);

	iter.pp.sheet = sheet;
	iter.pp.wb = sheet->workbook;

	if (start_col > end_col)
		SWAP_INT (start_col, end_col);
	if (end_col < 0 || start_col > gnm_sheet_get_last_col (sheet))
		return NULL;
	start_col = MAX (0, start_col);
	end_col = MIN (end_col, gnm_sheet_get_last_col (sheet));

	if (start_row > end_row)
		SWAP_INT (start_row, end_row);
	if (end_row < 0 || start_row > gnm_sheet_get_last_row (sheet))
		return NULL;
	start_row = MAX (0, start_row);
	end_row = MIN (end_row, gnm_sheet_get_last_row (sheet));

	range_size = (guint64)(end_row - start_row + 1) * (end_col - start_col + 1);
	use_celllist =
		only_existing &&
		range_size > g_hash_table_size (sheet->cell_hash) + 1000;
	if (use_celllist) {
		GPtrArray *all_cells;
		int last_row = -1, last_col = -1;
		GnmValue *res = NULL;
		unsigned ui;
		GnmRange r;

		if (gnm_debug_flag ("sheet-foreach"))
			g_printerr ("Using celllist for area of size %d\n",
				    (int)range_size);

		range_init (&r, start_col, start_row, end_col, end_row);
		all_cells = sheet_cells (sheet, &r);

		for (ui = 0; ui < all_cells->len; ui++) {
			GnmCell *cell = g_ptr_array_index (all_cells, ui);

			iter.cell = cell;
			iter.pp.eval.row = cell->pos.row;
			iter.pp.eval.col = cell->pos.col;

			if (iter.pp.eval.row != last_row) {
				last_row = iter.pp.eval.row;
				iter.ri = sheet_row_get (iter.pp.sheet, last_row);
			}
			if (iter.ri == NULL) {
				g_critical ("Cell without row data -- please report");
				continue;
			}
			if (visibility_matters && !iter.ri->visible)
				continue;
			if (ignore_filtered && iter.ri->in_filter && !iter.ri->visible)
				continue;

			if (iter.pp.eval.col != last_col) {
				last_col = iter.pp.eval.col;
				iter.ci = sheet_col_get (iter.pp.sheet, last_col);
			}
			if (iter.ci == NULL) {
				g_critical ("Cell without column data -- please report");
				continue;
			}
			if (visibility_matters && !iter.ci->visible)
				continue;

			ignore = (ignore_empty &&
				  VALUE_IS_EMPTY (cell->value) &&
				  !gnm_cell_needs_recalc (cell));
			if (ignore)
				continue;

			res = (*callback) (&iter, closure);
			if (res != NULL)
				break;
		}

		g_ptr_array_free (all_cells, TRUE);
		return res;
	}

	for (iter.pp.eval.row = start_row;
	     iter.pp.eval.row <= end_row;
	     ++iter.pp.eval.row) {
		iter.ri = sheet_row_get (iter.pp.sheet, iter.pp.eval.row);

		/* no need to check visibility, that would require a colinfo to exist */
		if (iter.ri == NULL) {
			if (only_existing) {
				/* skip segments with no cells */
				if (iter.pp.eval.row == COLROW_SEGMENT_START (iter.pp.eval.row)) {
					ColRowSegment const *segment =
						COLROW_GET_SEGMENT (&(sheet->rows), iter.pp.eval.row);
					if (segment == NULL)
						iter.pp.eval.row = COLROW_SEGMENT_END (iter.pp.eval.row);
				}
			} else {
				iter.cell = NULL;
				for (iter.pp.eval.col = start_col; iter.pp.eval.col <= end_col; ++iter.pp.eval.col) {
					cont = (*callback) (&iter, closure);
					if (cont != NULL)
						return cont;
				}
			}

			continue;
		}

		if (visibility_matters && !iter.ri->visible)
			continue;
		if (ignore_filtered && iter.ri->in_filter && !iter.ri->visible)
			continue;

		for (iter.pp.eval.col = start_col; iter.pp.eval.col <= end_col; ++iter.pp.eval.col) {
			iter.ci = sheet_col_get (sheet, iter.pp.eval.col);
			if (iter.ci != NULL) {
				if (visibility_matters && !iter.ci->visible)
					continue;
				iter.cell = sheet_cell_get (sheet,
					iter.pp.eval.col, iter.pp.eval.row);
			} else
				iter.cell = NULL;

			ignore = (iter.cell == NULL)
				? (only_existing || ignore_empty)
				: (ignore_empty && VALUE_IS_EMPTY (iter.cell->value) &&
				   !gnm_cell_needs_recalc (iter.cell));

			if (ignore) {
				if (iter.pp.eval.col == COLROW_SEGMENT_START (iter.pp.eval.col)) {
					ColRowSegment const *segment =
						COLROW_GET_SEGMENT (&(sheet->cols), iter.pp.eval.col);
					if (segment == NULL)
						iter.pp.eval.col = COLROW_SEGMENT_END (iter.pp.eval.col);
				}
				continue;
			}

			cont = (*callback) (&iter, closure);
			if (cont != NULL)
				return cont;
		}
	}
	return NULL;
}

/**
 * sheet_cell_foreach:
 * @sheet: #Sheet
 * @callback: (scope call):
 * @data:
 *
 * Call @callback with an argument of @data for each cell in the sheet
 **/
void
sheet_cell_foreach (Sheet const *sheet, GHFunc callback, gpointer data)
{
	g_return_if_fail (IS_SHEET (sheet));

	g_hash_table_foreach (sheet->cell_hash, callback, data);
}

/**
 * sheet_cells_count:
 * @sheet: #Sheet
 *
 * Returns the number of cells with content in the current workbook.
 **/
unsigned
sheet_cells_count (Sheet const *sheet)
{
	return g_hash_table_size (sheet->cell_hash);
}

static void
cb_sheet_cells_collect (G_GNUC_UNUSED gpointer unused,
			GnmCell const *cell,
			GPtrArray *cells)
{
	GnmEvalPos *ep = eval_pos_init_cell (g_new (GnmEvalPos, 1), cell);
	g_ptr_array_add (cells, ep);
}

/**
 * sheet_cell_positions:
 * @sheet: The sheet to find cells in.
 * @comments: If true, include cells with only comments also.
 *
 * Collects a GPtrArray of GnmEvalPos pointers for all cells in a sheet.
 * No particular order should be assumed.
 * Returns: (element-type GnmEvalPos) (transfer full): the newly created array
 **/
GPtrArray *
sheet_cell_positions (Sheet *sheet, gboolean comments)
{
	GPtrArray *cells = g_ptr_array_new ();

	g_return_val_if_fail (IS_SHEET (sheet), cells);

	sheet_cell_foreach (sheet, (GHFunc)cb_sheet_cells_collect, cells);

	if (comments) {
		GnmRange r;
		GSList *scomments, *ptr;

		range_init_full_sheet (&r, sheet);
		scomments = sheet_objects_get (sheet, &r, GNM_CELL_COMMENT_TYPE);
		for (ptr = scomments; ptr; ptr = ptr->next) {
			GnmComment *c = ptr->data;
			GnmRange const *loc = sheet_object_get_range (GNM_SO (c));
			GnmCell *cell = sheet_cell_get (sheet, loc->start.col, loc->start.row);
			if (!cell) {
				/* If cell does not exist, we haven't seen it...  */
				GnmEvalPos *ep = g_new (GnmEvalPos, 1);
				ep->sheet = sheet;
				ep->eval.col = loc->start.col;
				ep->eval.row = loc->start.row;
				g_ptr_array_add (cells, ep);
			}
		}
		g_slist_free (scomments);
	}

	return cells;
}


static GnmValue *
cb_fail_if_exist (GnmCellIter const *iter, G_GNUC_UNUSED gpointer user)
{
	return gnm_cell_is_empty (iter->cell) ? NULL : VALUE_TERMINATE;
}

/**
 * sheet_is_region_empty:
 * @sheet: sheet to check
 * @r: region to check
 *
 * Returns: %TRUE if the specified region of the @sheet does not
 * contain any cells
 */
gboolean
sheet_is_region_empty (Sheet *sheet, GnmRange const *r)
{
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	return sheet_foreach_cell_in_range (
		sheet, CELL_ITER_IGNORE_BLANK, r,
		cb_fail_if_exist, NULL) == NULL;
}

gboolean
sheet_is_cell_empty (Sheet *sheet, int col, int row)
{
	GnmCell const *cell = sheet_cell_get (sheet, col, row);
	return gnm_cell_is_empty (cell);
}

/**
 * sheet_cell_add_to_hash:
 * @sheet The sheet where the cell is inserted
 * @cell  The cell, it should already have col/pos pointers
 *        initialized pointing to the correct ColRowInfo
 *
 * GnmCell::pos must be valid before this is called.  The position is used as the
 * hash key.
 */
static void
sheet_cell_add_to_hash (Sheet *sheet, GnmCell *cell)
{
	g_return_if_fail (cell->pos.col < gnm_sheet_get_max_cols (sheet));
	g_return_if_fail (cell->pos.row < gnm_sheet_get_max_rows (sheet));

	cell->base.flags |= GNM_CELL_IN_SHEET_LIST;
	/* NOTE:
	 *   fetching the col/row here serve 3 functions
	 *   1) obsolete: we used to store the pointer in the cell
	 *   2) Expanding col/row.max_used
	 *   3) Creating an entry in the COLROW_SEGMENT.  Lots and lots of
	 *	things use those to help limit iteration
	 *
	 * For now just call col_fetch even though it is not necessary to
	 * ensure that 2,3 still happen.  Alot will need rewriting to avoid
	 * these requirements.
	 **/
	(void)sheet_col_fetch (sheet, cell->pos.col);
	(void)sheet_row_fetch (sheet, cell->pos.row);

	gnm_cell_unrender (cell);

	g_hash_table_insert (sheet->cell_hash, cell, cell);

	if (gnm_sheet_merge_is_corner (sheet, &cell->pos))
		cell->base.flags |= GNM_CELL_IS_MERGED;
}

#undef USE_CELL_POOL

#ifdef USE_CELL_POOL
/* The pool from which all cells are allocated.  */
static GOMemChunk *cell_pool;
#else
static int cell_allocations = 0;
#endif

static GnmCell *
cell_new (void)
{
	GnmCell *cell =
#ifdef USE_CELL_POOL
		go_mem_chunk_alloc0 (cell_pool)
#else
		(cell_allocations++, g_slice_new0 (GnmCell))
#endif
	;

	cell->base.flags = DEPENDENT_CELL;
	return cell;
}


static void
cell_free (GnmCell *cell)
{
	g_return_if_fail (cell != NULL);

	gnm_cell_cleanout (cell);
#ifdef USE_CELL_POOL
	go_mem_chunk_free (cell_pool, cell);
#else
	cell_allocations--, g_slice_free1 (sizeof (*cell), cell);
#endif
}

/**
 * gnm_sheet_cell_init: (skip)
 */
void
gnm_sheet_cell_init (void)
{
#ifdef USE_CELL_POOL
	cell_pool = go_mem_chunk_new ("cell pool",
				       sizeof (GnmCell),
				       128 * 1024 - 128);
#endif
}

#ifdef USE_CELL_POOL
static void
cb_cell_pool_leak (gpointer data, G_GNUC_UNUSED gpointer user)
{
	GnmCell const *cell = data;
	g_printerr ("Leaking cell %p at %s\n", (void *)cell, cell_name (cell));
}
#endif

/**
 * gnm_sheet_cell_shutdown: (skip)
 */
void
gnm_sheet_cell_shutdown (void)
{
#ifdef USE_CELL_POOL
	go_mem_chunk_foreach_leak (cell_pool, cb_cell_pool_leak, NULL);
	go_mem_chunk_destroy (cell_pool, FALSE);
	cell_pool = NULL;
#else
	if (cell_allocations)
		g_printerr ("Leaking %d cells.\n", cell_allocations);
#endif
}

/****************************************************************************/

/**
 * sheet_cell_create:
 * @sheet: #Sheet
 * @col:
 * @row:
 *
 * Creates a new cell and adds it to the sheet hash.
 **/
GnmCell *
sheet_cell_create (Sheet *sheet, int col, int row)
{
	GnmCell *cell;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (col >= 0, NULL);
	g_return_val_if_fail (col < gnm_sheet_get_max_cols (sheet), NULL);
	g_return_val_if_fail (row >= 0, NULL);
	g_return_val_if_fail (row < gnm_sheet_get_max_rows (sheet), NULL);

	cell = cell_new ();
	cell->base.sheet = sheet;
	cell->pos.col = col;
	cell->pos.row = row;
	cell->value = value_new_empty ();

	sheet_cell_add_to_hash (sheet, cell);
	return cell;
}

/**
 * sheet_cell_remove_from_hash:
 * @sheet:
 * @cell:
 *
 * Removes a cell from the sheet hash, clears any spans, and unlinks it from
 * the dependent collection.
 */
static void
sheet_cell_remove_from_hash (Sheet *sheet, GnmCell *cell)
{
	cell_unregister_span (cell);
	if (gnm_cell_expr_is_linked (cell))
		dependent_unlink (GNM_CELL_TO_DEP (cell));
	g_hash_table_remove (sheet->cell_hash, cell);
	cell->base.flags &= ~(GNM_CELL_IN_SHEET_LIST|GNM_CELL_IS_MERGED);
}

/**
 * sheet_cell_destroy:
 * @sheet:
 * @cell:
 * @queue_recalc:
 *
 * Remove the cell from the web of dependencies of a
 *        sheet.  Do NOT redraw.
 **/
static void
sheet_cell_destroy (Sheet *sheet, GnmCell *cell, gboolean queue_recalc)
{
	if (gnm_cell_expr_is_linked (cell)) {
		/* if it needs recalc then its depends are already queued
		 * check recalc status before we unlink
		 */
		queue_recalc &= !gnm_cell_needs_recalc (cell);
		dependent_unlink (GNM_CELL_TO_DEP (cell));
	}

	if (queue_recalc)
		cell_foreach_dep (cell, (GnmDepFunc)dependent_queue_recalc, NULL);

	sheet_cell_remove_from_hash (sheet, cell);
	cell_free (cell);
}

/**
 * sheet_cell_remove:
 * @sheet:
 * @cell:
 * @redraw:
 * @queue_recalc:
 *
 * Remove the cell from the web of dependencies of a
 *        sheet.  Do NOT free the cell, optionally redraw it, optionally
 *        queue it for recalc.
 **/
void
sheet_cell_remove (Sheet *sheet, GnmCell *cell,
		   gboolean redraw, gboolean queue_recalc)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	/* Queue a redraw on the region used by the cell being removed */
	if (redraw) {
		sheet_redraw_region (sheet,
				     cell->pos.col, cell->pos.row,
				     cell->pos.col, cell->pos.row);
		sheet_flag_status_update_cell (cell);
	}

	sheet_cell_destroy (sheet, cell, queue_recalc);
}

static GnmValue *
cb_free_cell (GnmCellIter const *iter, G_GNUC_UNUSED gpointer user)
{
	sheet_cell_destroy (iter->pp.sheet, iter->cell, FALSE);
	return NULL;
}

/**
 * sheet_col_destroy:
 * @sheet:
 * @col:
 * @free_cells:
 *
 * Destroys a ColRowInfo from the Sheet with all of its cells
 */
static void
sheet_col_destroy (Sheet *sheet, int const col, gboolean free_cells)
{
	ColRowSegment **segment = (ColRowSegment **)&COLROW_GET_SEGMENT (&(sheet->cols), col);
	int const sub = COLROW_SUB_INDEX (col);
	ColRowInfo *ci = NULL;

	if (*segment == NULL)
		return;
	ci = (*segment)->info[sub];
	if (ci == NULL)
		return;

	if (sheet->cols.max_outline_level > 0 &&
	    sheet->cols.max_outline_level == ci->outline_level)
		sheet->priv->recompute_max_col_group = TRUE;

	if (free_cells)
		sheet_foreach_cell_in_region (sheet, CELL_ITER_IGNORE_NONEXISTENT,
					      col, 0, col, -1,
					      &cb_free_cell, NULL);

	(*segment)->info[sub] = NULL;
	colrow_free (ci);

	/* Use >= just in case things are screwed up */
	if (col >= sheet->cols.max_used) {
		int i = col;
		while (--i >= 0 && sheet_col_get (sheet, i) == NULL)
		    ;
		sheet->cols.max_used = i;
	}
}

/*
 * Destroys a row ColRowInfo
 */
static void
sheet_row_destroy (Sheet *sheet, int const row, gboolean free_cells)
{
	ColRowSegment **segment = (ColRowSegment **)&COLROW_GET_SEGMENT (&(sheet->rows), row);
	int const sub = COLROW_SUB_INDEX (row);
	ColRowInfo *ri = NULL;

	if (*segment == NULL)
		return;
	ri = (*segment)->info[sub];
	if (ri == NULL)
		return;

	if (sheet->rows.max_outline_level > 0 &&
	    sheet->rows.max_outline_level == ri->outline_level)
		sheet->priv->recompute_max_row_group = TRUE;

	if (free_cells)
		sheet_foreach_cell_in_region (sheet, CELL_ITER_IGNORE_NONEXISTENT,
					      0, row, -1, row,
					      &cb_free_cell, NULL);

	/* Rows have span lists, destroy them too */
	row_destroy_span (ri);

	(*segment)->info[sub] = NULL;
	colrow_free (ri);

	/* Use >= just in case things are screwed up */
	if (row >= sheet->rows.max_used) {
		int i = row;
		while (--i >= 0 && sheet_row_get (sheet, i) == NULL)
		    ;
		sheet->rows.max_used = i;
	}
}

static void
cb_remove_allcells (G_GNUC_UNUSED gpointer ignore0, GnmCell *cell, G_GNUC_UNUSED gpointer ignore1)
{
	cell->base.flags &= ~GNM_CELL_IN_SHEET_LIST;
	cell_free (cell);
}

void
sheet_destroy_contents (Sheet *sheet)
{
	GSList *filters;
	int i;

	/* By the time we reach here dependencies should have been shut down */
	g_return_if_fail (sheet->deps == NULL);

	/* A simple test to see if this has already been run. */
	if (sheet->hash_merged == NULL)
		return;

	{
		GSList *tmp = sheet->slicers;
		sheet->slicers = NULL;
		g_slist_free_full (tmp, (GDestroyNotify)gnm_sheet_slicer_clear_sheet);
	}

	/* These contain SheetObjects, remove them first */
	filters = g_slist_copy (sheet->filters);
	g_slist_foreach (filters, (GFunc)gnm_filter_remove, NULL);
	g_slist_foreach (filters, (GFunc)gnm_filter_unref, NULL);
	g_slist_free (filters);

	if (sheet->sheet_objects) {
		/* The list is changed as we remove */
		GSList *objs = g_slist_copy (sheet->sheet_objects);
		GSList *ptr;
		for (ptr = objs; ptr != NULL ; ptr = ptr->next) {
			SheetObject *so = GNM_SO (ptr->data);
			if (so != NULL)
				sheet_object_clear_sheet (so);
		}
		g_slist_free (objs);
		if (sheet->sheet_objects != NULL)
			g_warning ("There is a problem with sheet objects");
	}

	/* The memory is managed by Sheet::list_merged */
	g_hash_table_destroy (sheet->hash_merged);
	sheet->hash_merged = NULL;

	g_slist_free_full (sheet->list_merged, g_free);
	sheet->list_merged = NULL;

	/* Clear the row spans 1st */
	for (i = sheet->rows.max_used; i >= 0 ; --i)
		row_destroy_span (sheet_row_get (sheet, i));

	/* Remove all the cells */
	sheet_cell_foreach (sheet, (GHFunc) &cb_remove_allcells, NULL);
	g_hash_table_destroy (sheet->cell_hash);
	sheet->cell_hash = NULL;

	/* Delete in ascending order to avoid decrementing max_used each time */
	for (i = 0; i <= sheet->cols.max_used; ++i)
		sheet_col_destroy (sheet, i, FALSE);

	for (i = 0; i <= sheet->rows.max_used; ++i)
		sheet_row_destroy (sheet, i, FALSE);

	/* Free segments too */
	col_row_collection_resize (&sheet->cols, 0);
	g_ptr_array_free (sheet->cols.info, TRUE);
	sheet->cols.info = NULL;

	col_row_collection_resize (&sheet->rows, 0);
	g_ptr_array_free (sheet->rows.info, TRUE);
	sheet->rows.info = NULL;

	g_clear_object (&sheet->solver_parameters);
}

/**
 * sheet_destroy:
 * @sheet: the sheet to destroy
 *
 * Please note that you need to detach this sheet before
 * calling this routine or you will get a warning.
 */
static void
sheet_destroy (Sheet *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->sheet_views->len > 0)
		g_warning ("Unexpected left over views");

	if (sheet->print_info) {
		gnm_print_info_free (sheet->print_info);
		sheet->print_info = NULL;
	}

	style_color_unref (sheet->tab_color);
	sheet->tab_color = NULL;
	style_color_unref (sheet->tab_text_color);
	sheet->tab_text_color = NULL;

	gnm_app_clipboard_invalidate_sheet (sheet);
}

static void
gnm_sheet_finalize (GObject *obj)
{
	Sheet *sheet = SHEET (obj);
	gboolean debug_FMR = gnm_debug_flag ("sheet-fmr");

	sheet_destroy (sheet);

	g_clear_object (&sheet->solver_parameters);

	gnm_conventions_unref (sheet->convs);
	sheet->convs = NULL;

	g_list_free_full (sheet->scenarios, g_object_unref);
	sheet->scenarios = NULL;

	if (sheet->sort_setups != NULL)
		g_hash_table_unref (sheet->sort_setups);

	dependents_invalidate_sheet (sheet, TRUE);

	sheet_destroy_contents (sheet);

	if (sheet->slicers != NULL) {
		g_warning ("DataSlicer list should be NULL");
	}
	if (sheet->filters != NULL) {
		g_warning ("Filter list should be NULL");
	}
	if (sheet->sheet_objects != NULL) {
		g_warning ("Sheet object list should be NULL");
	}
	if (sheet->list_merged != NULL) {
		g_warning ("Merged list should be NULL");
	}
	if (sheet->hash_merged != NULL) {
		g_warning ("Merged hash should be NULL");
	}

	sheet_style_shutdown (sheet);
	sheet_conditions_uninit (sheet);

	if (sheet->pending_redraw_src) {
		g_source_remove (sheet->pending_redraw_src);
		sheet->pending_redraw_src = 0;
	}
	g_array_free (sheet->pending_redraw, TRUE);

	if (debug_FMR) {
		g_printerr ("Sheet %p is %s\n", sheet, sheet->name_quoted);
	}
	g_free (sheet->name_quoted);
	g_free (sheet->name_unquoted);
	g_free (sheet->name_unquoted_collate_key);
	g_free (sheet->name_case_insensitive);
	/* Poison */
	sheet->name_quoted = (char *)0xdeadbeef;
	sheet->name_unquoted = (char *)0xdeadbeef;
	g_free (sheet->priv);
	g_ptr_array_free (sheet->sheet_views, TRUE);

	gnm_rvc_free (sheet->rendered_values);

	if (debug_FMR) {
		/* Keep object around. */
		return;
	}

	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/*****************************************************************************/

/*
 * cb_empty_cell: A callback for sheet_foreach_cell_in_region
 *     removes/clear all of the cells in the specified region.
 *     Does NOT queue a redraw.
 *
 * WARNING : This does NOT regenerate spans that were interrupted by
 * this cell and can now continue.
 */
static GnmValue *
cb_empty_cell (GnmCellIter const *iter, gpointer user)
{
	int clear_flags = GPOINTER_TO_INT (user);
#if 0
	/* TODO : here and other places flag a need to update the
	 * row/col maxima.
	 */
	if (row >= sheet->rows.max_used || col >= sheet->cols.max_used) { }
#endif

	sheet_cell_remove (iter->pp.sheet, iter->cell, FALSE,
		(clear_flags & CLEAR_RECALC_DEPS) &&
		iter->pp.wb->recursive_dirty_enabled);

	return NULL;
}

/**
 * sheet_clear_region:
 * @sheet: the sheet being changed
 * @start_col: Starting column
 * @start_row: Starting row
 * @end_col: Ending column
 * @end_row: Ending row
 * @clear_flags: flags indicating what to clear
 * @cc: (nullable): command context for error reporting
 *
 * Clears a region of cells, formats, object, etc. as indicated by
 * @clear_flags.
 */
void
sheet_clear_region (Sheet *sheet,
		    int start_col, int start_row,
		    int end_col, int end_row,
		    SheetClearFlags clear_flags,
		    GOCmdContext *cc)
{
	GnmRange r;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (start_col <= end_col);
	g_return_if_fail (start_row <= end_row);

	r.start.col = start_col;
	r.start.row = start_row;
	r.end.col = end_col;
	r.end.row = end_row;

	if (clear_flags & CLEAR_VALUES && !(clear_flags & CLEAR_NOCHECKARRAY) &&
	    sheet_range_splits_array (sheet, &r, NULL, cc, _("Clear")))
		return;

	/* Queue a redraw for cells being modified */
	if (clear_flags & (CLEAR_VALUES|CLEAR_FORMATS))
		sheet_redraw_region (sheet,
				     start_col, start_row,
				     end_col, end_row);

	/* Clear the style in the region (new_default will ref the style for us). */
	if (clear_flags & CLEAR_FORMATS) {
		sheet_style_set_range (sheet, &r, sheet_style_default (sheet));
		sheet_range_calc_spans (sheet, &r, GNM_SPANCALC_RE_RENDER|GNM_SPANCALC_RESIZE);
		rows_height_update (sheet, &r, TRUE);
	}

	if (clear_flags & CLEAR_OBJECTS)
		sheet_objects_clear (sheet, &r, G_TYPE_NONE, NULL);
	else if (clear_flags & CLEAR_COMMENTS)
		sheet_objects_clear (sheet, &r, GNM_CELL_COMMENT_TYPE, NULL);

	/* TODO : how to handle objects ? */
	if (clear_flags & CLEAR_VALUES) {
		/* Remove or empty the cells depending on
		 * whether or not there are comments
		 */
		sheet_foreach_cell_in_region (sheet, CELL_ITER_IGNORE_NONEXISTENT,
			start_col, start_row, end_col, end_row,
			&cb_empty_cell, GINT_TO_POINTER (clear_flags));

		if (!(clear_flags & CLEAR_NORESPAN)) {
			sheet_queue_respan (sheet, start_row, end_row);
			sheet_flag_status_update_range (sheet, &r);
		}
	}

	if (clear_flags & CLEAR_MERGES) {
		GSList *merged, *ptr;
		merged = gnm_sheet_merge_get_overlap (sheet, &r);
		for (ptr = merged ; ptr != NULL ; ptr = ptr->next)
			gnm_sheet_merge_remove (sheet, ptr->data);
		g_slist_free (merged);
	}

	if (clear_flags & CLEAR_RECALC_DEPS)
		sheet_region_queue_recalc (sheet, &r);

	/* Always redraw */
	sheet_redraw_all (sheet, FALSE);
}

static void
sheet_clear_region_cb (GnmSheetRange *sr, int *flags)
{
	sheet_clear_region (sr->sheet,
			  sr->range.start.col, sr->range.start.row,
			  sr->range.end.col, sr->range.end.row,
			  *flags | CLEAR_NOCHECKARRAY, NULL);
}


/**
 * sheet_clear_region_undo:
 * @sr: #GnmSheetRange
 * @clear_flags: flags.
 *
 * Returns: (transfer full): the new #GOUndo.
 **/
GOUndo *sheet_clear_region_undo (GnmSheetRange *sr, int clear_flags)
{
	int *flags = g_new(int, 1);
	*flags = clear_flags;
	return go_undo_binary_new
		(sr, (gpointer)flags,
		 (GOUndoBinaryFunc) sheet_clear_region_cb,
		 (GFreeFunc) gnm_sheet_range_free,
		 (GFreeFunc) g_free);
}


/*****************************************************************************/

void
sheet_mark_dirty (Sheet *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->workbook)
		workbook_mark_dirty (sheet->workbook);
}

/****************************************************************************/

static void
sheet_cells_deps_move (GnmExprRelocateInfo *rinfo)
{
	Sheet *sheet = rinfo->origin_sheet;
	GPtrArray *deps = sheet_cells (sheet, &rinfo->origin);
	unsigned ui;

	/* Phase 1: collect all cells and remove them from hash.  */
	for (ui = 0; ui < deps->len; ui++) {
		GnmCell *cell = g_ptr_array_index (deps, ui);
		gboolean needs_recalc = gnm_cell_needs_recalc (cell);
		sheet_cell_remove_from_hash (sheet, cell);
		if (needs_recalc) /* Do we need this now? */
			cell->base.flags |= DEPENDENT_NEEDS_RECALC;
	}

	/* Phase 2: add all non-cell deps with positions */
	SHEET_FOREACH_DEPENDENT
		(sheet, dep, {
			GnmCellPos const *pos;
			if (!dependent_is_cell (dep) &&
			    dependent_has_pos (dep) &&
			    (pos = dependent_pos (dep)) &&
			    range_contains (&rinfo->origin, pos->col, pos->row)) {
				dependent_unlink (dep);
				g_ptr_array_add (deps, dep);
			}
		});

	/* Phase 3: move everything and add cells to hash.  */
	for (ui = 0; ui < deps->len; ui++) {
		GnmDependent *dep = g_ptr_array_index (deps, ui);

		dependent_move (dep, rinfo->col_offset, rinfo->row_offset);

		if (dependent_is_cell (dep))
			sheet_cell_add_to_hash (sheet, GNM_DEP_TO_CELL (dep));

		if (dep->texpr)
			dependent_link (dep);
	}

	g_ptr_array_free (deps, TRUE);
}

/* Moves the headers to their new location */
static void
sheet_colrow_move (Sheet *sheet, gboolean is_cols,
		   int const old_pos, int const new_pos)
{
	ColRowSegment *segment = COLROW_GET_SEGMENT (is_cols ? &sheet->cols : &sheet->rows, old_pos);
	ColRowInfo *info = segment
		? segment->info[COLROW_SUB_INDEX (old_pos)]
		: NULL;

	g_return_if_fail (old_pos >= 0);
	g_return_if_fail (new_pos >= 0);

	if (info == NULL)
		return;

	/* Update the position */
	segment->info[COLROW_SUB_INDEX (old_pos)] = NULL;
	sheet_colrow_add (sheet, info, is_cols, new_pos);
}

static void
sheet_colrow_set_collapse (Sheet *sheet, gboolean is_cols, int pos)
{
	ColRowInfo *cri;
	ColRowInfo const *vs = NULL;

	if (pos < 0 || pos >= colrow_max (is_cols, sheet))
		return;

	/* grab the next or previous col/row */
	if ((is_cols ? sheet->outline_symbols_right : sheet->outline_symbols_below)) {
		if (pos > 0)
			vs = sheet_colrow_get (sheet, pos-1, is_cols);
	} else if ((pos+1) < colrow_max (is_cols, sheet))
		vs = sheet_colrow_get (sheet, pos+1, is_cols);

	/* handle the case where an empty col/row should be marked collapsed */
	cri = sheet_colrow_get (sheet, pos, is_cols);
	if (cri != NULL)
		cri->is_collapsed = (vs != NULL && !vs->visible &&
				     vs->outline_level > cri->outline_level);
	else if (vs != NULL && !vs->visible && vs->outline_level > 0) {
		cri = sheet_colrow_fetch (sheet, pos, is_cols);
		cri->is_collapsed = TRUE;
	}
}

static void
combine_undo (GOUndo **pundo, GOUndo *u)
{
	if (pundo)
		*pundo = go_undo_combine (*pundo, u);
	else
		g_object_unref (u);
}

typedef gboolean (*ColRowInsDelFunc) (Sheet *sheet, int idx, int count,
				      GOUndo **pundo, GOCmdContext *cc);

typedef struct {
	ColRowInsDelFunc func;
	Sheet *sheet;
	gboolean is_cols;
	int pos;
	int count;
	ColRowStateList *states;
	int state_start;
} ColRowInsDelData;

static void
cb_undo_insdel (ColRowInsDelData *data)
{
	data->func (data->sheet, data->pos, data->count, NULL, NULL);
	colrow_set_states (data->sheet, data->is_cols,
			   data->state_start, data->states);
}

static void
cb_undo_insdel_free (ColRowInsDelData *data)
{
	colrow_state_list_destroy (data->states);
	g_free (data);
}

static gboolean
sheet_insdel_colrow (Sheet *sheet, int pos, int count,
		     GOUndo **pundo, GOCmdContext *cc,
		     gboolean is_cols, gboolean is_insert,
		     const char *description,
		     ColRowInsDelFunc opposite)
{

	GnmRange kill_zone;    /* The range whose contents will be lost.  */
	GnmRange move_zone;    /* The range whose contents will be moved.  */
	GnmRange change_zone;  /* The union of kill_zone and move_zone.  */
	int i, last_pos, max_used_pos;
	int kill_start, kill_end, move_start, move_end;
	int scount = is_insert ? count : -count;
	ColRowStateList *states = NULL;
	GnmExprRelocateInfo reloc_info;
	GSList *l;
	gboolean sticky_end = TRUE;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count > 0, TRUE);

	/*
	 * The main undo for an insert col/row is delete col/row and vice versa.
	 * In addition to that, we collect undo information that the main undo
	 * operation will not restore -- for example the contents of the kill
	 * zone.
	 */
	if (pundo) *pundo = NULL;

	last_pos = colrow_max (is_cols, sheet) - 1;
	max_used_pos = is_cols ? sheet->cols.max_used : sheet->rows.max_used;
	if (is_insert) {
		kill_start = last_pos - (count - 1);
		kill_end = last_pos;
		move_start = pos;
		move_end = kill_start - 1;
	} else {
		int max_count = last_pos + 1 - pos;
		if (count > max_count) {
			sticky_end = FALSE;
			count = max_count;
		}
		kill_start = pos;
		kill_end = pos + (count - 1);
		move_start = kill_end + 1;
		move_end = last_pos;
	}
	(is_cols ? range_init_cols : range_init_rows)
		(&kill_zone, sheet, kill_start, kill_end);
	(is_cols ? range_init_cols : range_init_rows)
		(&move_zone, sheet, move_start, move_end);
	change_zone = range_union (&kill_zone, &move_zone);

	/* 0. Check displaced/deleted region and ensure arrays aren't divided. */
	if (sheet_range_splits_array (sheet, &kill_zone, NULL, cc, description))
		return TRUE;
	if (move_start <= move_end &&
	    sheet_range_splits_array (sheet, &move_zone, NULL, cc, description))
		return TRUE;

	/*
	 * At this point we're committed.  Anything that can go wrong should
	 * have been ruled out already.
	 */

	if (0) {
		g_printerr ("Action = %s at %d count %d\n", description, pos, count);
		g_printerr ("Kill zone: %s\n", range_as_string (&kill_zone));
	}

	/* 1. Delete all columns/rows in the kill zone */
	if (pundo) {
		combine_undo (pundo, clipboard_copy_range_undo (sheet, &kill_zone));
		states = colrow_get_states (sheet, is_cols, kill_start, kill_end);
	}
	for (i = MIN (max_used_pos, kill_end); i >= kill_start; --i)
		(is_cols ? sheet_col_destroy : sheet_row_destroy)
			(sheet, i, TRUE);
	/* Brutally discard auto filter objects.  Collect the rest for undo.  */
	sheet_objects_clear (sheet, &kill_zone, GNM_FILTER_COMBO_TYPE, NULL);
	sheet_objects_clear (sheet, &kill_zone, G_TYPE_NONE, pundo);

	reloc_info.reloc_type = is_cols ? GNM_EXPR_RELOCATE_COLS : GNM_EXPR_RELOCATE_ROWS;
	reloc_info.sticky_end = sticky_end;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	parse_pos_init_sheet (&reloc_info.pos, sheet);

	// 2. Get rid of style dependents, see #741197.
	sheet_conditions_link_unlink_dependents (sheet, &change_zone, FALSE);

	/* 3. Invalidate references to kill zone.  */
	if (is_insert) {
		/* Done in the next step. */
	} else {
		reloc_info.origin = kill_zone;
		/* Force invalidation: */
		reloc_info.col_offset = is_cols ? last_pos + 1 : 0;
		reloc_info.row_offset = is_cols ? 0 : last_pos + 1;
		combine_undo (pundo, dependents_relocate (&reloc_info));
	}

	/* 4. Fix references to the cells which are moving */
	reloc_info.origin = is_insert ? change_zone : move_zone;
	reloc_info.col_offset = is_cols ? scount : 0;
	reloc_info.row_offset = is_cols ? 0 : scount;
	combine_undo (pundo, dependents_relocate (&reloc_info));

	/* 5. Move the cells */
	sheet_cells_deps_move (&reloc_info);

	/* 6. Move the columns/rows to their new location.  */
	if (is_insert) {
		/* From right to left */
		for (i = max_used_pos; i >= pos ; --i)
			sheet_colrow_move (sheet, is_cols, i, i + count);
	} else {
		/* From left to right */
		for (i = pos + count ; i <= max_used_pos; ++i)
			sheet_colrow_move (sheet, is_cols, i, i - count);
	}
	sheet_colrow_set_collapse (sheet, is_cols, pos);
	sheet_colrow_set_collapse (sheet, is_cols,
				   is_insert ? pos + count : last_pos - (count - 1));

	/* 7. Move formatting.  */
	sheet_style_insdel_colrow (&reloc_info);
	sheet_conditions_link_unlink_dependents (sheet, NULL, TRUE);

	/* 8. Move objects.  */
	sheet_objects_relocate (&reloc_info, FALSE, pundo);

	/* 9. Move merges.  */
	gnm_sheet_merge_relocate (&reloc_info, pundo);

	/* 10. Move filters.  */
	gnm_sheet_filter_insdel_colrow (sheet, is_cols, is_insert, pos, count, pundo);

	/* Notify sheet of pending updates */
	sheet_mark_dirty (sheet);
	sheet->priv->recompute_visibility = TRUE;
	sheet_flag_recompute_spans (sheet);
	sheet_flag_status_update_range (sheet, &change_zone);
	if (is_cols)
		sheet->priv->reposition_objects.col = pos;
	else
		sheet->priv->reposition_objects.row = pos;

	/* WARNING WARNING WARNING
	 * This is bad practice and should not really be here.
	 * However, we need to ensure that update is run before
	 * gnm_sheet_view_panes_insdel_colrow plays with frozen panes, updating those can
	 * trigger redraws before sheet_update has been called. */
	sheet_update (sheet);

	SHEET_FOREACH_VIEW (sheet, sv,
			    gnm_sheet_view_panes_insdel_colrow (sv, is_cols, is_insert, pos, count););

	/* The main undo is the opposite operation.  */
	if (pundo) {
		ColRowInsDelData *data;
		GOUndo *u;

		data = g_new (ColRowInsDelData, 1);
		data->func = opposite;
		data->sheet = sheet;
		data->is_cols = is_cols;
		data->pos = pos;
		data->count = count;
		data->states = states;
		data->state_start = kill_start;

		u = go_undo_unary_new (data, (GOUndoUnaryFunc)cb_undo_insdel,
				       (GFreeFunc)cb_undo_insdel_free);

		combine_undo (pundo, u);
	}

	/* Reapply all filters.  */
	for (l = sheet->filters; l; l = l->next) {
		GnmFilter *filter = l->data;
		gnm_filter_reapply (filter);
	}

	return FALSE;
}

/**
 * sheet_insert_cols:
 * @sheet: #Sheet
 * @col: At which position we want to insert
 * @count: The number of columns to be inserted
 * @pundo: (out): (transfer full): (allow-none): undo closure
 * @cc: The command context
 **/
gboolean
sheet_insert_cols (Sheet *sheet, int col, int count,
		   GOUndo **pundo, GOCmdContext *cc)
{
	return sheet_insdel_colrow (sheet, col, count, pundo, cc,
				    TRUE, TRUE,
				    _("Insert Columns"),
				    sheet_delete_cols);
}

/**
 * sheet_delete_cols:
 * @sheet: The sheet
 * @col:     At which position we want to start deleting columns
 * @count:   The number of columns to be deleted
 * @pundo: (out): (transfer full): (allow-none): undo closure
 * @cc: The command context
 */
gboolean
sheet_delete_cols (Sheet *sheet, int col, int count,
		   GOUndo **pundo, GOCmdContext *cc)
{
	return sheet_insdel_colrow (sheet, col, count, pundo, cc,
				    TRUE, FALSE,
				    _("Delete Columns"),
				    sheet_insert_cols);
}

/**
 * sheet_insert_rows:
 * @sheet: The sheet
 * @row: At which position we want to insert
 * @count: The number of rows to be inserted
 * @pundo: (out): (transfer full): (allow-none): undo closure
 * @cc: The command context
 */
gboolean
sheet_insert_rows (Sheet *sheet, int row, int count,
		   GOUndo **pundo, GOCmdContext *cc)
{
	return sheet_insdel_colrow (sheet, row, count, pundo, cc,
				    FALSE, TRUE,
				    _("Insert Rows"),
				    sheet_delete_rows);
}

/**
 * sheet_delete_rows:
 * @sheet: The sheet
 * @row: At which position we want to start deleting rows
 * @count: The number of rows to be deleted
 * @pundo: (out): (transfer full): (allow-none): undo closure
 * @cc: The command context
 */
gboolean
sheet_delete_rows (Sheet *sheet, int row, int count,
		   GOUndo **pundo, GOCmdContext *cc)
{
	return sheet_insdel_colrow (sheet, row, count, pundo, cc,
				    FALSE, FALSE,
				    _("Delete Rows"),
				    sheet_insert_rows);
}

/*
 * Callback for sheet_foreach_cell_in_region to remove a cell from the sheet
 * hash, unlink from the dependent collection and put it in a temporary list.
 */
static GnmValue *
cb_collect_cell (GnmCellIter const *iter, gpointer user)
{
	GList ** l = user;
	GnmCell *cell = iter->cell;
	gboolean needs_recalc = gnm_cell_needs_recalc (cell);

	sheet_cell_remove_from_hash (iter->pp.sheet, cell);
	*l = g_list_prepend (*l, cell);
	if (needs_recalc)
		cell->base.flags |= DEPENDENT_NEEDS_RECALC;
	return NULL;
}

/**
 * sheet_move_range:
 * @cc: The command context
 * @rinfo:
 * @pundo: (out) (optional) (transfer full): undo object
 *
 * Move a range as specified in @rinfo report warnings to @cc.
 * if @pundo is non-%NULL, invalidate references to the
 * target region that are being cleared, and store the undo information
 * in @pundo.  If it is %NULL do NOT INVALIDATE.
 **/
void
sheet_move_range (GnmExprRelocateInfo const *rinfo,
		  GOUndo **pundo, GOCmdContext *cc)
{
	GList *cells = NULL;
	GnmCell  *cell;
	GnmRange  dst;
	gboolean out_of_range;

	g_return_if_fail (rinfo != NULL);
	g_return_if_fail (IS_SHEET (rinfo->origin_sheet));
	g_return_if_fail (IS_SHEET (rinfo->target_sheet));
	g_return_if_fail (rinfo->origin_sheet != rinfo->target_sheet ||
			  rinfo->col_offset != 0 ||
			  rinfo->row_offset != 0);

	dst = rinfo->origin;
	out_of_range = range_translate (&dst, rinfo->target_sheet,
					rinfo->col_offset, rinfo->row_offset);

	/* Redraw the src region in case anything was spanning */
	sheet_redraw_range (rinfo->origin_sheet, &rinfo->origin);

	// 0. Get rid of style dependents
	sheet_conditions_link_unlink_dependents (rinfo->origin_sheet, &rinfo->origin, FALSE);

	/* 1. invalidate references to any cells in the destination range that
	 * are not shared with the src.  This must be done before the references
	 * to from the src range are adjusted because they will point into
	 * the destination.
	 */
	if (pundo != NULL) {
		*pundo = NULL;
		if (!out_of_range) {
			GSList *invalid;
			GnmExprRelocateInfo reloc_info;

			/* We need to be careful about invalidating references
			 * to the old content of the destination region.  We
			 * only invalidate references to regions that are
			 * actually lost.  However, this care is only necessary
			 * if the source and target sheets are the same.
			 *
			 * Handle dst cells being pasted over
			 */
			if (rinfo->origin_sheet == rinfo->target_sheet &&
			    range_overlap (&rinfo->origin, &dst))
				invalid = range_split_ranges (&rinfo->origin, &dst);
			else
				invalid = g_slist_append (NULL, gnm_range_dup (&dst));

			reloc_info.origin_sheet = reloc_info.target_sheet = rinfo->target_sheet;

			/* send to infinity to invalidate, but try to assist
			 * the relocation heuristics only move in 1
			 * dimension if possible to give us a chance to be
			 * smart about partial invalidations */
			reloc_info.col_offset = gnm_sheet_get_max_cols (rinfo->target_sheet);
			reloc_info.row_offset = gnm_sheet_get_max_rows (rinfo->target_sheet);
			reloc_info.sticky_end = TRUE;
			if (rinfo->col_offset == 0) {
				reloc_info.col_offset = 0;
				reloc_info.reloc_type = GNM_EXPR_RELOCATE_ROWS;
			} else if (rinfo->row_offset == 0) {
				reloc_info.row_offset = 0;
				reloc_info.reloc_type = GNM_EXPR_RELOCATE_COLS;
			} else
				reloc_info.reloc_type = GNM_EXPR_RELOCATE_MOVE_RANGE;

			parse_pos_init_sheet (&reloc_info.pos,
					      rinfo->origin_sheet);

			while (invalid) {
				GnmRange *r = invalid->data;
				invalid = g_slist_remove (invalid, r);
				if (!range_overlap (r, &rinfo->origin)) {
					reloc_info.origin = *r;
					combine_undo (pundo,
						      dependents_relocate (&reloc_info));
				}
				g_free (r);
			}

			/*
			 * DO NOT handle src cells moving out the bounds.
			 * that is handled elsewhere.
			 */
		}

		/* 2. Fix references to and from the cells which are moving */
		combine_undo (pundo, dependents_relocate (rinfo));
	}

	/* 3. Collect the cells */
	sheet_foreach_cell_in_range (rinfo->origin_sheet,
				     CELL_ITER_IGNORE_NONEXISTENT,
				     &rinfo->origin,
				     &cb_collect_cell, &cells);

	/* Reverse list so that we start at the top left (simplifies arrays). */
	cells = g_list_reverse (cells);

	/* 4. Clear the target area and invalidate references to it */
	if (!out_of_range)
		/* we can clear content but not styles from the destination
		 * region without worrying if it overlaps with the source,
		 * because we have already extracted the content.  However,
		 * we do need to queue anything that depends on the region for
		 * recalc. */
		sheet_clear_region (rinfo->target_sheet,
				    dst.start.col, dst.start.row,
				    dst.end.col, dst.end.row,
				    CLEAR_VALUES|CLEAR_RECALC_DEPS, cc);

	/* 5. Slide styles BEFORE the cells so that spans get computed properly */
	sheet_style_relocate (rinfo);

	/* 6. Insert the cells back */
	for (; cells != NULL ; cells = g_list_remove (cells, cell)) {
		cell = cells->data;

		/* check for out of bounds and delete if necessary */
		if ((cell->pos.col + rinfo->col_offset) >= gnm_sheet_get_max_cols (rinfo->target_sheet) ||
		    (cell->pos.row + rinfo->row_offset) >= gnm_sheet_get_max_rows (rinfo->target_sheet)) {
			cell_free (cell);
			continue;
		}

		/* Update the location */
		cell->base.sheet = rinfo->target_sheet;
		cell->pos.col += rinfo->col_offset;
		cell->pos.row += rinfo->row_offset;
		sheet_cell_add_to_hash (rinfo->target_sheet, cell);
		if (gnm_cell_has_expr (cell))
			dependent_link (GNM_CELL_TO_DEP (cell));
	}

	/* 7. Move objects in the range */
	sheet_objects_relocate (rinfo, TRUE, pundo);
	gnm_sheet_merge_relocate (rinfo, pundo);

	/* 8. Notify sheet of pending update */
	sheet_flag_recompute_spans (rinfo->origin_sheet);
	sheet_flag_status_update_range (rinfo->origin_sheet, &rinfo->origin);
}

static void
sheet_colrow_default_calc (Sheet *sheet, double units,
			   gboolean is_cols, gboolean is_pts)
{
	ColRowInfo *cri = is_cols
		? &sheet->cols.default_style
		: &sheet->rows.default_style;

	g_return_if_fail (units > 0.);

	if (gnm_debug_flag ("colrow")) {
		g_printerr ("Setting default %s size to %g%s\n",
			    is_cols ? "column" : "row",
			    units,
			    is_pts ? "pts" : "px");
	}

	cri->is_default	= TRUE;
	cri->hard_size	= FALSE;
	cri->visible	= TRUE;
	cri->spans	= NULL;

	if (is_pts) {
		cri->size_pts = units;
		colrow_compute_pixels_from_pts (cri, sheet, is_cols, -1);
	} else {
		cri->size_pixels = units;
		colrow_compute_pts_from_pixels (cri, sheet, is_cols, -1);
	}

	gnm_sheet_mark_colrow_changed (sheet, 0, is_cols); // All, really
}

static gint64
sheet_colrow_segment_pixels (ColRowCollection const *collection,
			     int ix, int six0, int six1)
{
	ColRowSegment *segment = COLROW_GET_SEGMENT_INDEX (collection, ix);
	gint64 pixels = 0;

	if (segment == NULL)
		return collection->default_style.size_pixels * (six1 - six0);

	for (int i = six0; i < six1; i++) {
		ColRowInfo const *cri = segment->info[i];
		if (cri == NULL)
			pixels += collection->default_style.size_pixels;
		else if (cri->visible)
			pixels += cri->size_pixels;
	}
	return pixels;
}

/**
 * sheet_colrow_get_distance_pixels:
 * @is_cols: %TRUE for columns, %FALSE for rows.
 * @sheet: The sheet
 * @from: Starting column/row
 * @to: Ending column/row, not inclusive
 *
 * Returns: the number of pixels between columns/rows @from and @to
 * measured from the upper left corner.
 */
gint64
sheet_colrow_get_distance_pixels (Sheet const *sheet, gboolean is_cols,
				  int from, int to)
{
	ColRowCollection *collection;
	int max, ix, ixv, ix0;
	gint64 start, dflt;

	g_return_val_if_fail (IS_SHEET (sheet), 1);
	g_return_val_if_fail (from >= 0 && to >= 0, 1);

	if (from >= to) {
		if (from == to)
			return 0;
		return -sheet_colrow_get_distance_pixels
			(sheet, is_cols, to, from);
	}

	collection = (ColRowCollection *)(is_cols ? &sheet->cols : &sheet->rows);
	dflt = collection->default_style.size_pixels;
	ix = COLROW_SEGMENT_INDEX (to);

	if (ix == COLROW_SEGMENT_INDEX (from)) {
		// Single-segment optimization.  Not essential.
		return sheet_colrow_segment_pixels
			(collection, ix,
			 COLROW_SUB_INDEX (from), COLROW_SUB_INDEX (to));
	}

	if (from > 0)
		return sheet_colrow_get_distance_pixels (sheet, is_cols, 0, to) -
			sheet_colrow_get_distance_pixels (sheet, is_cols, 0, from);

	max = colrow_max (is_cols, sheet);
	if (to == max) {
		int six = COLROW_SUB_INDEX (to - 1) + 1;
		start = sheet_colrow_get_distance_pixels
			(sheet, is_cols, 0, to - six);
		start +=
			sheet_colrow_segment_pixels
			(collection, COLROW_SEGMENT_INDEX (to) - 1,
			 0, six);
		return start;
	}

	g_return_val_if_fail (to < max, 1);

	// At this point, 0 <= from < to < max

	// Find the highest ix0 for which we have a valid pixel_start,
	// but no larger than ix
	ix0 = ixv = MAX (0, MIN (ix, collection->last_valid_pixel_start));
	while (ix0 > 0 && COLROW_GET_SEGMENT_INDEX (collection, ix0) == NULL)
		ix0--;

	// Find start and adjust for default segments
	start = (ix0 == 0)
		? 0
		: (((ColRowSegment *)COLROW_GET_SEGMENT_INDEX (collection, ix0))
		   ->pixel_start);
	start += dflt * COLROW_SEGMENT_SIZE * (ixv - ix0);

	while (ix > ixv) {
		ColRowSegment *segment;
		gint64 w = sheet_colrow_segment_pixels
			(collection, ixv, 0, COLROW_SEGMENT_SIZE);

		start += w;
		ixv++;
		segment = COLROW_GET_SEGMENT_INDEX (collection, ixv);
		if (segment) {
			segment->pixel_start = start;
			collection->last_valid_pixel_start = ixv;
		}
	}

	start += sheet_colrow_segment_pixels
		(collection, ix, 0, COLROW_SUB_INDEX (to));

	return start;
}

/************************************************************************/
// Col width support routines.


/**
 * sheet_col_get_distance_pts:
 * @sheet: The sheet
 * @from: Starting column
 * @to: Ending column, not inclusive
 *
 * Returns: the number of points between columns @from and @to
 * measured from the upper left corner.
 */
double
sheet_col_get_distance_pts (Sheet const *sheet, int from, int to)
{
	ColRowInfo const *ci;
	double dflt, pts = 0., sign = 1.;
	int i;

	g_return_val_if_fail (IS_SHEET (sheet), 1.);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1.;
	}

	g_return_val_if_fail (from >= 0, 1.);
	g_return_val_if_fail (to <= gnm_sheet_get_max_cols (sheet), 1.);

	/* Do not use sheet_colrow_foreach, it ignores empties */
	dflt = sheet->cols.default_style.size_pts;
	for (i = from ; i < to ; ++i) {
		if (NULL == (ci = sheet_col_get (sheet, i)))
			pts += dflt;
		else if (ci->visible)
			pts += ci->size_pts;
	}

	if (sheet->display_formulas)
		pts *= 2.;

	return pts * sign;
}

/**
 * sheet_col_get_distance_pixels:
 * @sheet: The sheet
 * @from: Starting column
 * @to: Ending column, not inclusive
 *
 * Returns: the number of pixels between columns @from and @to
 * measured from the upper left corner.
 */
gint64
sheet_col_get_distance_pixels (Sheet const *sheet, int from, int to)
{
	return sheet_colrow_get_distance_pixels (sheet, TRUE, from, to);
}

/**
 * sheet_col_set_size_pts:
 * @sheet: The sheet
 * @col: The col
 * @width_pts: The desired width in pts
 * @set_by_user: %TRUE if this was done by a user (ie, user manually
 *               set the width)
 *
 * Sets width of a col in pts, INCLUDING left and right margins, and the far
 * grid line.  This is a low level internal routine.  It does NOT redraw,
 * or reposition objects.
 **/
void
sheet_col_set_size_pts (Sheet *sheet, int col, double width_pts,
			gboolean set_by_user)
{
	ColRowInfo *ci;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (width_pts > 0.0);

	ci = sheet_col_fetch (sheet, col);
	ci->hard_size = set_by_user;
	if (ci->size_pts == width_pts)
		return;

	ci->size_pts = width_pts;
	colrow_compute_pixels_from_pts (ci, sheet, TRUE, -1);
	gnm_sheet_mark_colrow_changed (sheet, col, TRUE);

	sheet->priv->recompute_visibility = TRUE;
	sheet_flag_recompute_spans (sheet);
	if (sheet->priv->reposition_objects.col > col)
		sheet->priv->reposition_objects.col = col;
}

void
sheet_col_set_size_pixels (Sheet *sheet, int col, int width_pixels,
			   gboolean set_by_user)
{
	ColRowInfo *ci;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (width_pixels > 0.0);

	ci = sheet_col_fetch (sheet, col);
	ci->hard_size = set_by_user;
	if (ci->size_pixels == width_pixels)
		return;

	ci->size_pixels = width_pixels;
	colrow_compute_pts_from_pixels (ci, sheet, TRUE, -1);
	gnm_sheet_mark_colrow_changed (sheet, col, TRUE);

	sheet->priv->recompute_visibility = TRUE;
	sheet_flag_recompute_spans (sheet);
	if (sheet->priv->reposition_objects.col > col)
		sheet->priv->reposition_objects.col = col;
}

/**
 * sheet_col_get_default_size_pts:
 * @sheet: The sheet
 *
 * Returns: the default number of pts in a column, including margins.
 * This function returns the raw sum, no rounding etc.
 */
double
sheet_col_get_default_size_pts (Sheet const *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), 1.);
	return sheet->cols.default_style.size_pts;
}

int
sheet_col_get_default_size_pixels (Sheet const *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), 1.);
	return sheet->cols.default_style.size_pixels;
}

void
sheet_col_set_default_size_pts (Sheet *sheet, double width_pts)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (width_pts > 0.);

	sheet_colrow_default_calc (sheet, width_pts, TRUE, TRUE);
	sheet->priv->recompute_visibility = TRUE;
	sheet_flag_recompute_spans (sheet);
	sheet->priv->reposition_objects.col = 0;
}
void
sheet_col_set_default_size_pixels (Sheet *sheet, int width_pixels)
{
	g_return_if_fail (IS_SHEET (sheet));

	sheet_colrow_default_calc (sheet, width_pixels, TRUE, FALSE);
	sheet->priv->recompute_visibility = TRUE;
	sheet_flag_recompute_spans (sheet);
	sheet->priv->reposition_objects.col = 0;
}

/**************************************************************************/
// Row height support routines


/**
 * sheet_row_get_distance_pts:
 * @sheet: The sheet
 * @from: Starting row
 * @to: Ending row, not inclusive
 *
 * Return: the number of points between rows @from and @to
 * measured from the upper left corner.
 */
double
sheet_row_get_distance_pts (Sheet const *sheet, int from, int to)
{
	ColRowSegment const *segment;
	ColRowInfo const *ri;
	double const default_size = sheet->rows.default_style.size_pts;
	double pts = 0., sign = 1.;
	int i;

	g_return_val_if_fail (IS_SHEET (sheet), 1.);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1.;
	}

	g_return_val_if_fail (from >= 0, 1.);
	g_return_val_if_fail (to <= gnm_sheet_get_max_rows (sheet), 1.);

	/* Do not use sheet_colrow_foreach, it ignores empties.
	 * Optimize this so that long jumps are not quite so horrific
	 * for performance.
	 */
	for (i = from ; i < to ; ++i) {
		segment = COLROW_GET_SEGMENT (&(sheet->rows), i);

		if (segment != NULL) {
			ri = segment->info[COLROW_SUB_INDEX (i)];
			if (ri == NULL)
				pts += default_size;
			else if (ri->visible)
				pts += ri->size_pts;
		} else {
			int segment_end = COLROW_SEGMENT_END (i)+1;
			if (segment_end > to)
				segment_end = to;
			pts += default_size * (segment_end - i);
			i = segment_end-1;
		}
	}

	return pts*sign;
}

/**
 * sheet_row_get_distance_pixels:
 * @sheet: The sheet
 * @from: Starting row
 * @to: Ending row, not inclusive
 *
 * Return: the number of pixels between rows @from and @to
 * measured from the upper left corner.
 */
gint64
sheet_row_get_distance_pixels (Sheet const *sheet, int from, int to)
{
	return sheet_colrow_get_distance_pixels (sheet, FALSE, from, to);
}

/**
 * sheet_row_set_size_pts:
 * @sheet:	 The sheet
 * @row:	 The row
 * @height_pts:	 The desired height in pts
 * @set_by_user: %TRUE if this was done by a user (ie, user manually
 *               set the height)
 *
 * Sets height of a row in pts, INCLUDING top and bottom margins, and the lower
 * grid line.  This is a low level internal routine.  It does NOT redraw,
 * or reposition objects.
 */
void
sheet_row_set_size_pts (Sheet *sheet, int row, double height_pts,
			gboolean set_by_user)
{
	ColRowInfo *ri;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (height_pts > 0.0);

	ri = sheet_row_fetch (sheet, row);
	ri->hard_size = set_by_user;
	if (ri->size_pts == height_pts)
		return;

	ri->size_pts = height_pts;
	colrow_compute_pixels_from_pts (ri, sheet, FALSE, -1);
	gnm_sheet_mark_colrow_changed (sheet, row, FALSE);

	sheet->priv->recompute_visibility = TRUE;
	if (sheet->priv->reposition_objects.row > row)
		sheet->priv->reposition_objects.row = row;
}

/**
 * sheet_row_set_size_pixels:
 * @sheet:	 The sheet
 * @row:	 The row
 * @height_pixels: The desired height
 * @set_by_user: %TRUE if this was done by a user (ie, user manually
 *                      set the width)
 *
 * Sets height of a row in pixels, INCLUDING top and bottom margins, and the lower
 * grid line.
 */
void
sheet_row_set_size_pixels (Sheet *sheet, int row, int height_pixels,
			   gboolean set_by_user)
{
	ColRowInfo *ri;

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (height_pixels > 0);

	ri = sheet_row_fetch (sheet, row);
	ri->hard_size = set_by_user;
	if (ri->size_pixels == height_pixels)
		return;

	ri->size_pixels = height_pixels;
	colrow_compute_pts_from_pixels (ri, sheet, FALSE, -1);
	gnm_sheet_mark_colrow_changed (sheet, row, FALSE);

	sheet->priv->recompute_visibility = TRUE;
	if (sheet->priv->reposition_objects.row > row)
		sheet->priv->reposition_objects.row = row;
}

/**
 * sheet_row_get_default_size_pts:
 * @sheet: The sheet
 *
 * Return: the default number of units in a row, including margins.
 * This function returns the raw sum, no rounding etc.
 */
double
sheet_row_get_default_size_pts (Sheet const *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), 1.);
	return sheet->rows.default_style.size_pts;
}

int
sheet_row_get_default_size_pixels (Sheet const *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), 1.);
	return sheet->rows.default_style.size_pixels;
}

void
sheet_row_set_default_size_pts (Sheet *sheet, double height_pts)
{
	g_return_if_fail (IS_SHEET (sheet));

	sheet_colrow_default_calc (sheet, height_pts, FALSE, TRUE);
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->reposition_objects.row = 0;
}

void
sheet_row_set_default_size_pixels (Sheet *sheet, int height_pixels)
{
	g_return_if_fail (IS_SHEET (sheet));

	sheet_colrow_default_calc (sheet, height_pixels, FALSE, FALSE);
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->reposition_objects.row = 0;
}

/****************************************************************************/

void
sheet_scrollbar_config (Sheet const *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	SHEET_FOREACH_CONTROL (sheet, view, control,
		sc_scrollbar_config (control););
}

/*****************************************************************************/
typedef struct
{
	gboolean is_column;
	Sheet *sheet;
} closure_clone_colrow;

static gboolean
sheet_clone_colrow_info_item (GnmColRowIter const *iter, void *user_data)
{
	closure_clone_colrow const *closure = user_data;
	sheet_colrow_copy_info (closure->sheet, iter->pos, closure->is_column,
				iter->cri);
	return FALSE;
}

static void
sheet_dup_colrows (Sheet const *src, Sheet *dst)
{
	closure_clone_colrow closure;
	int max_col = MIN (gnm_sheet_get_max_cols (src), gnm_sheet_get_max_cols (dst)),
	    max_row = MIN (gnm_sheet_get_max_rows (src), gnm_sheet_get_max_rows (dst));

	closure.sheet = dst;
	closure.is_column = TRUE;
	sheet_colrow_foreach  (src, TRUE, 0, max_col - 1,
			       &sheet_clone_colrow_info_item, &closure);
	closure.is_column = FALSE;
	sheet_colrow_foreach (src, FALSE, 0, max_row - 1,
			      &sheet_clone_colrow_info_item, &closure);

	sheet_col_set_default_size_pixels (dst,
		sheet_col_get_default_size_pixels (src));
	sheet_row_set_default_size_pixels (dst,
		sheet_row_get_default_size_pixels (src));

	dst->cols.max_outline_level = src->cols.max_outline_level;
	dst->rows.max_outline_level = src->rows.max_outline_level;
}

static void
sheet_dup_styles (Sheet const *src, Sheet *dst)
{
	static GnmCellPos const	corner = { 0, 0 };
	GnmRange	 r;
	GnmStyleList	*styles;

	sheet_style_set_auto_pattern_color (
		dst, sheet_style_get_auto_pattern_color (src));

	styles = sheet_style_get_range (src, range_init_full_sheet (&r, src));
	sheet_style_set_list (dst, &corner, styles, NULL, NULL);
	style_list_free	(styles);
}

static void
sheet_dup_merged_regions (Sheet const *src, Sheet *dst)
{
	GSList *ptr;

	for (ptr = src->list_merged ; ptr != NULL ; ptr = ptr->next)
		gnm_sheet_merge_add (dst, ptr->data, FALSE, NULL);
}

static void
sheet_dup_names (Sheet const *src, Sheet *dst)
{
	GSList *names = gnm_named_expr_collection_list (src->names);
	GSList *l;
	GnmParsePos dst_pp;

	if (names == NULL)
		return;

	parse_pos_init_sheet (&dst_pp, dst);

	/* Pass 1: add placeholders.  */
	for (l = names; l; l = l->next) {
		GnmNamedExpr *src_nexpr = l->data;
		char const *name = expr_name_name (src_nexpr);
		GnmNamedExpr *dst_nexpr =
			gnm_named_expr_collection_lookup (dst->names, name);
		GnmExprTop const *texpr;

		if (dst_nexpr)
			continue;

		texpr = gnm_expr_top_new_constant (value_new_empty ());
		expr_name_add (&dst_pp, name, texpr , NULL, TRUE, NULL);
	}

	/* Pass 2: assign the right expression.  */
	for (l = names; l; l = l->next) {
		GnmNamedExpr *src_nexpr = l->data;
		char const *name = expr_name_name (src_nexpr);
		GnmNamedExpr *dst_nexpr =
			gnm_named_expr_collection_lookup (dst->names, name);
		GnmExprTop const *texpr;

		if (!dst_nexpr) {
			g_warning ("Trouble while duplicating name %s", name);
			continue;
		}

		if (!dst_nexpr->is_editable)
			continue;

		texpr = gnm_expr_top_relocate_sheet (src_nexpr->texpr, src, dst);
		expr_name_set_expr (dst_nexpr, texpr);
	}

	g_slist_free (names);
}

static void
cb_sheet_cell_copy (G_GNUC_UNUSED gpointer unused, gpointer key, gpointer new_sheet_param)
{
	GnmCell const *cell = key;
	Sheet *dst = new_sheet_param;
	Sheet *src;
	GnmExprTop const *texpr;

	g_return_if_fail (dst != NULL);
	g_return_if_fail (cell != NULL);

	src = cell->base.sheet;
	texpr = cell->base.texpr;

	if (texpr && gnm_expr_top_is_array_corner (texpr)) {
		int cols, rows;

		texpr = gnm_expr_top_relocate_sheet (texpr, src, dst);
		gnm_expr_top_get_array_size (texpr, &cols, &rows);

		gnm_cell_set_array_formula (dst,
			cell->pos.col, cell->pos.row,
			cell->pos.col + cols - 1,
			cell->pos.row + rows - 1,
			gnm_expr_top_new (gnm_expr_copy (gnm_expr_top_get_array_expr (texpr))));

		gnm_expr_top_unref (texpr);
	} else if (texpr && gnm_expr_top_is_array_elem (texpr, NULL, NULL)) {
		/* Not a corner -- ignore.  */
	} else {
		GnmCell *new_cell = sheet_cell_create (dst, cell->pos.col, cell->pos.row);
		if (gnm_cell_has_expr (cell)) {
			texpr = gnm_expr_top_relocate_sheet (texpr, src, dst);
			gnm_cell_set_expr_and_value (new_cell, texpr, value_new_empty (), TRUE);
			gnm_expr_top_unref (texpr);
		} else
			gnm_cell_set_value (new_cell, value_dup (cell->value));
	}
}

static void
sheet_dup_cells (Sheet const *src, Sheet *dst)
{
	sheet_cell_foreach (src, &cb_sheet_cell_copy, dst);
	sheet_region_queue_recalc (dst, NULL);
}

static void
sheet_dup_filters (Sheet const *src, Sheet *dst)
{
	GSList *ptr;
	for (ptr = src->filters ; ptr != NULL ; ptr = ptr->next)
		gnm_filter_dup (ptr->data, dst);
	dst->filters = g_slist_reverse (dst->filters);
}

/**
 * sheet_dup:
 * @source_sheet: #Sheet
 *
 * Create a duplicate sheet.
 *
 * Returns: (transfer full): the newly allocated #Sheet.
 **/
Sheet *
sheet_dup (Sheet const *src)
{
	Workbook *wb;
	Sheet *dst;
	char *name;
	GList *l;

	g_return_val_if_fail (IS_SHEET (src), NULL);
	g_return_val_if_fail (src->workbook != NULL, NULL);

	wb = src->workbook;
	name = workbook_sheet_get_free_name (wb, src->name_unquoted,
					     TRUE, TRUE);
	dst = sheet_new_with_type (wb, name, src->sheet_type,
				   src->size.max_cols, src->size.max_rows);
	g_free (name);

	dst->protected_allow = src->protected_allow;
	g_object_set (dst,
		"zoom-factor",		    src->last_zoom_factor_used,
		"text-is-rtl",		    src->text_is_rtl,
		"visibility",		    src->visibility,
		"protected",		    src->is_protected,
		"display-formulas",	    src->display_formulas,
		"display-zeros",	   !src->hide_zero,
		"display-grid",		   !src->hide_grid,
		"display-column-header",   !src->hide_col_header,
		"display-row-header",	   !src->hide_row_header,
		"display-outlines",	    src->display_outlines,
		"display-outlines-below",   src->outline_symbols_below,
		"display-outlines-right",   src->outline_symbols_right,
		"conventions",		    src->convs,
		"tab-foreground",	    src->tab_text_color,
		"tab-background",	    src->tab_color,
		NULL);

	gnm_print_info_free (dst->print_info);
	dst->print_info = gnm_print_info_dup (src->print_info);

	sheet_dup_styles         (src, dst);
	sheet_dup_merged_regions (src, dst);
	sheet_dup_colrows	 (src, dst);
	sheet_dup_names		 (src, dst);
	sheet_dup_cells		 (src, dst);
	sheet_objects_dup	 (src, dst, NULL);
	sheet_dup_filters	 (src, dst); /* must be after objects */

#warning selection is in view
#warning freeze/thaw is in view

	g_object_unref (dst->solver_parameters);
	dst->solver_parameters = gnm_solver_param_dup (src->solver_parameters, dst);

	for (l = src->scenarios; l; l = l->next) {
		GnmScenario *src_sc = l->data;
		GnmScenario *dst_sc = gnm_scenario_dup (src_sc, dst);
		dst->scenarios = g_list_prepend (dst->scenarios, dst_sc);
	}
	dst->scenarios = g_list_reverse (dst->scenarios);

	sheet_mark_dirty (dst);
	sheet_redraw_all (dst, TRUE);

	return dst;
}

/**
 * sheet_set_outline_direction:
 * @sheet: the sheet
 * @is_cols: %TRUE for columns, %FALSE for rows.
 *
 * When changing the placement of outline collapse markers the flags
 * need to be recomputed.
 **/
void
sheet_set_outline_direction (Sheet *sheet, gboolean is_cols)
{
	unsigned i;
	g_return_if_fail (IS_SHEET (sheet));

	/* not particularly efficient, but this is not a hot spot */
	for (i = colrow_max (is_cols, sheet); i-- > 0 ; )
		sheet_colrow_set_collapse (sheet, is_cols, i);
}

/**
 * sheet_get_view:
 * @sheet: The sheet
 * @wbv:
 *
 * Find the SheetView corresponding to the supplied @wbv.
 * Returns: (transfer none): the view.
 */
SheetView *
sheet_get_view (Sheet const *sheet, WorkbookView const *wbv)
{
	if (sheet == NULL)
		return NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	SHEET_FOREACH_VIEW (sheet, view, {
		if (sv_wbv (view) == wbv)
			return view;
	});
	return NULL;
}

void
sheet_freeze_object_views (Sheet const *sheet, gboolean qfreeze)
{
	SHEET_FOREACH_CONTROL
		(sheet, view, control,
		 sc_freeze_object_view (control, qfreeze););
}


static gboolean
cb_queue_respan (GnmColRowIter const *iter, void *user_data)
{
	((ColRowInfo *)(iter->cri))->needs_respan = TRUE;
	return FALSE;
}

/**
 * sheet_queue_respan:
 * @sheet: The sheet
 * @start_row:
 * @end_row:
 *
 * queues a span generation for the selected rows.
 * the caller is responsible for queuing a redraw
 **/
void
sheet_queue_respan (Sheet const *sheet, int start_row, int end_row)
{
	sheet_colrow_foreach (sheet, FALSE, start_row, end_row,
			      cb_queue_respan, NULL);
}

void
sheet_cell_queue_respan (GnmCell *cell)
{
	ColRowInfo *ri = sheet_row_get (cell->base.sheet, cell->pos.row);
	ri->needs_respan = TRUE;
}


/**
 * sheet_get_comment:
 * @sheet: The sheet
 * @pos: #GnmCellPos const *
 *
 * If there is a cell comment at @pos in @sheet return it.
 *
 * Caller does get a reference to the object if it exists.
 * Returns: (transfer full): the comment or %NULL.
 **/
GnmComment *
sheet_get_comment (Sheet const *sheet, GnmCellPos const *pos)
{
	GnmRange r;
	GSList *comments;
	GnmComment *res;

	GnmRange const *mr;

	mr = gnm_sheet_merge_contains_pos (sheet, pos);

	if (mr)
		comments = sheet_objects_get (sheet, mr, GNM_CELL_COMMENT_TYPE);
	else {
		r.start = r.end = *pos;
		comments = sheet_objects_get (sheet, &r, GNM_CELL_COMMENT_TYPE);
	}
	if (!comments)
		return NULL;

	/* This assumes just one comment per cell.  */
	res = comments->data;
	g_slist_free (comments);
	return res;
}

static GnmValue *
cb_find_extents (GnmCellIter const *iter, GnmCellPos *extent)
{
	if (extent->col < iter->pp.eval.col)
		extent->col = iter->pp.eval.col;
	if (extent->row < iter->pp.eval.row)
		extent->row = iter->pp.eval.row;
	return NULL;
}

/**
 * sheet_range_trim:
 * @sheet: sheet cells are contained on
 * @r:	   range to trim empty cells from
 * @cols:  trim from right
 * @rows:  trim from bottom
 *
 * This removes empty rows/cols from the
 * right hand or bottom edges of the range
 * depending on the value of @cols or @rows.
 *
 * Returns: %TRUE if the range was totally empty.
 **/
gboolean
sheet_range_trim (Sheet const *sheet, GnmRange *r,
		  gboolean cols, gboolean rows)
{
	GnmCellPos extent = { -1, -1 };

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (r != NULL, TRUE);

	sheet_foreach_cell_in_range (
		(Sheet *)sheet, CELL_ITER_IGNORE_BLANK, r,
		(CellIterFunc) cb_find_extents, &extent);

	if (extent.col < 0 || extent.row < 0)
		return TRUE;
	if (cols)
		r->end.col = extent.col;
	if (rows)
		r->end.row = extent.row;
	return FALSE;
}

/**
 * sheet_range_has_heading:
 * @sheet: Sheet to check
 * @src: GnmRange to check
 * @top: Flag
 *
 * Checks for a header row in @sheet!@src.  If top is true it looks for a
 * header row from the top and if false it looks for a header col from the
 * left
 *
 * Returns: %TRUE if @src seems to have a heading
 **/
gboolean
sheet_range_has_heading (Sheet const *sheet, GnmRange const *src,
			gboolean top, gboolean ignore_styles)
{
	GnmCell const *a, *b;
	int length, i;

	/* There is only one row or col */
	if (top) {
		if (src->end.row <= src->start.row)
			return FALSE;
		length = src->end.col - src->start.col + 1;
	} else {
		if (src->end.col <= src->start.col)
			return FALSE;
		length = src->end.row - src->start.row + 1;
	}

	for (i = 0; i < length; i++) {
		if (top) {
			a = sheet_cell_get (sheet,
				src->start.col + i, src->start.row);
			b = sheet_cell_get (sheet,
				src->start.col + i, src->start.row + 1);
		} else {
			a = sheet_cell_get (sheet,
				src->start.col, src->start.row + i);
			b = sheet_cell_get (sheet,
				src->start.col + 1, src->start.row + i);
		}

		/* be anal */
		if (a == NULL || a->value == NULL || b == NULL || b->value == NULL)
			continue;

		if (VALUE_IS_NUMBER (a->value)) {
			if (!VALUE_IS_NUMBER (b->value))
				return TRUE;
			/* check for style differences */
		} else if (a->value->v_any.type != b->value->v_any.type)
			return TRUE;

		/* Look for style differences */
		if (!ignore_styles &&
		    !gnm_style_equal_header (gnm_cell_get_style (a),
					     gnm_cell_get_style (b), top))
			return TRUE;
	}

	return FALSE;
}

/**
 * gnm_sheet_foreach_name:
 * @sheet: #Sheet
 * @func: (scope call): #GHFunc
 * @data: user data.
 *
 * Executes @func for each name in @sheet.
 **/
void
gnm_sheet_foreach_name (Sheet const *sheet, GHFunc func, gpointer data)
{
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->names)
		gnm_named_expr_collection_foreach (sheet->names, func, data);
}

/**
 * gnm_sheet_get_size:
 * @sheet: #Sheet
 *
 * Returns: (transfer none): the sheet size.
 **/
GnmSheetSize const *
gnm_sheet_get_size (Sheet const *sheet)
{
	static const GnmSheetSize default_size = {
		GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS
	};

	if (G_UNLIKELY (!sheet)) {
		g_warning ("NULL sheet in gnm_sheet_get_size!");
		/* FIXME: This needs to go.  */
		return &default_size;
	}

	if (G_UNLIKELY (sheet->being_constructed))
		g_warning ("Access to sheet size during construction!");

	return &sheet->size;
}

/**
 * gnm_sheet_get_size2:
 * @sheet: (nullable): The sheet
 * @wb: (nullable): workbook.
 *
 * Determines the sheet size, either of @sheet if that is non-%NULL or
 * of @wb's default sheet.  One of @sheet and @wb must be non-%NULL.
 *
 * Returns: (transfer none): the sheet size.
 **/
GnmSheetSize const *
gnm_sheet_get_size2 (Sheet const *sheet, Workbook const *wb)
{
	return sheet
		? gnm_sheet_get_size (sheet)
		: workbook_get_sheet_size (wb);
}

void
gnm_sheet_set_solver_params (Sheet *sheet, GnmSolverParameters *param)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (GNM_IS_SOLVER_PARAMETERS (param));

	g_object_ref (param);
	g_object_unref (sheet->solver_parameters);
	sheet->solver_parameters = param;
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_sheet_scenario_new: (skip)
 * @sheet: #Sheet
 * @name: the new scenario name.
 *
 * Returns: (transfer full): the newly created #GnmScenario.
 **/
GnmScenario *
gnm_sheet_scenario_new (Sheet *sheet, const char *name)
{
	GnmScenario *sc;
	char *actual_name;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	/* Check if a scenario having the same name already exists. */
	if (gnm_sheet_scenario_find (sheet, name)) {
		GString *str = g_string_new (NULL);
		gchar   *tmp;
		int     i, j, len;

		len = strlen (name);
		if (len > 1 && name [len - 1] == ']') {
			for (i = len - 2; i > 0; i--) {
				if (! g_ascii_isdigit (name [i]))
					break;
			}

			tmp = g_strdup (name);
			if (i > 0 && name [i] == '[')
				tmp [i] = '\0';
		} else
			tmp = g_strdup (name);

		for (j = 1; ; j++) {
			g_string_printf (str, "%s [%d]", tmp, j);
			if (!gnm_sheet_scenario_find (sheet, str->str)) {
				actual_name = g_string_free (str, FALSE);
				str = NULL;
				break;
			}
		}
		if (str)
			g_string_free (str, TRUE);
		g_free (tmp);
	} else
		actual_name = g_strdup (name);

	sc = gnm_scenario_new (actual_name, sheet);

	g_free (actual_name);

	return sc;
}

/**
 * gnm_sheet_scenario_find:
 * @sheet: #Sheet
 * @name: the scenario name.
 *
 * Returns: (transfer none) (nullable): the found scenario, or %NULL.
 **/
GnmScenario *
gnm_sheet_scenario_find (Sheet *sheet, const char *name)
{
	GList *l;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (l = sheet->scenarios; l; l = l->next) {
		GnmScenario *sc = l->data;
		if (strcmp (name, sc->name) == 0)
			return sc;
	}

	return NULL;
}

/**
 * gnm_sheet_scenario_add:
 * @sheet: #Sheet
 * @sc: (transfer full): #GnmScenario
 *
 **/
void
gnm_sheet_scenario_add (Sheet *sheet, GnmScenario *sc)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (GNM_IS_SCENARIO (sc));

	/* We take ownership of the ref.  */
	sheet->scenarios = g_list_append (sheet->scenarios, sc);
}

void
gnm_sheet_scenario_remove (Sheet *sheet, GnmScenario *sc)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (GNM_IS_SCENARIO (sc));

	sheet->scenarios = g_list_remove (sheet->scenarios, sc);
	g_object_unref (sc);
}

/* ------------------------------------------------------------------------- */

/**
 * gnm_sheet_get_sort_setups:
 * @sheet: #Sheet
 *
 * Returns: (transfer none): the sort setups for @sheet.
 **/
GHashTable *
gnm_sheet_get_sort_setups (Sheet *sheet)
{
	GHashTable *hash = sheet->sort_setups;

	if (hash == NULL)
		hash = sheet->sort_setups =
			g_hash_table_new_full
			(g_str_hash, g_str_equal,
			 g_free, (GDestroyNotify)gnm_sort_data_destroy);

	return hash;
}

void
gnm_sheet_add_sort_setup (Sheet *sheet, char *key, gpointer setup)
{
	GHashTable *hash = gnm_sheet_get_sort_setups (sheet);

	g_hash_table_insert (hash, key, setup);
}

/**
 * gnm_sheet_find_sort_setup:
 * @sheet: #Sheet
 * @key:
 *
 * Returns: (transfer none) (nullable): the found sort setup or %NULL.
 **/
gconstpointer
gnm_sheet_find_sort_setup (Sheet *sheet, char const *key)
{
	if (sheet->sort_setups == NULL)
		return NULL;
	return g_hash_table_lookup (sheet->sort_setups, key);
}

/**
 * sheet_date_conv:
 * @sheet: #Sheet
 *
 * Returns: (transfer none): the date conventions in effect for the sheet.
 * This is purely a convenience function to access the conventions used
 * for the workbook.  All sheets in a workbook share the same date
 * conventions.
 **/
GODateConventions const *
sheet_date_conv (Sheet const *sheet)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	return workbook_date_conv (sheet->workbook);
}

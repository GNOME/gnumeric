/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * sheet.c: Implements the sheet management and per-sheet storage
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 1997-1999 Miguel de Icaza (miguel@kernel.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include "gnumeric.h"
#include "sheet.h"

#include "sheet-view.h"
#include "command-context.h"
#include "sheet-control.h"
#include "sheet-style.h"
#include "workbook-priv.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "parse-util.h"
#include "dependent.h"
#include "value.h"
#include "number-match.h"
#include "clipboard.h"
#include "selection.h"
#include "ranges.h"
#include "print-info.h"
#include "mstyle.h"
#include "style-color.h"
#include "style-font.h"
#include "application.h"
#include "commands.h"
#include "cellspan.h"
#include "cell.h"
#include "sheet-merge.h"
#include "sheet-private.h"
#include "expr-name.h"
#include "expr-impl.h"
#include "rendered-value.h"
#include "gnumeric-gconf.h"
#include "sheet-object-impl.h"
#include "sheet-object-cell-comment.h"
#include "solver.h"
#include "hlink.h"
#include "sheet-filter.h"
#include "scenarios.h"
#include "cell-draw.h"
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-pango-extras.h>
#include <goffice/utils/go-format.h>

#include <glib/gi18n-lib.h>
#include <gsf/gsf-impl-utils.h>
#include <stdlib.h>
#include <string.h>

enum {
	DETACHED_FROM_WORKBOOK,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

typedef struct {
	GObjectClass parent;

	void (*detached_from_workbook) (Sheet *, Workbook *wb);
} GnmSheetClass;
typedef Sheet GnmSheet;

enum {
	PROP_0,
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
	PROP_ZOOM_FACTOR
};

static void gnm_sheet_finalize (GObject *obj);

static GObjectClass *parent_class;

static void
sheet_set_direction (Sheet *sheet, gboolean text_is_rtl)
{
	GnmRange r;

	text_is_rtl = !!text_is_rtl;
	if (text_is_rtl == sheet->text_is_rtl)
		return;

	sheet->text_is_rtl = text_is_rtl;
	sheet->priv->reposition_objects.col = 0;
	sheet_range_calc_spans (sheet, range_init_full_sheet (&r), GNM_SPANCALC_RE_RENDER);
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
		if (cell->rendered_value != NULL) {
			gnm_rendered_value_destroy (cell->rendered_value);
			cell->rendered_value = NULL;
		}
		if (cell->row_info != NULL)
			cell->row_info->needs_respan = TRUE;
	}
}

static void
re_render_formulas (Sheet const *sheet)
{
	sheet_cell_foreach (sheet, (GHFunc)cb_re_render_formulas, NULL);
}

static void
sheet_set_display_formulas (Sheet *sheet, gboolean display)
{
	display = !!display;
	if (sheet->display_formulas == display)
		return;
	sheet->display_formulas = display;
	re_render_formulas (sheet);
}

static void
sheet_set_conventions (Sheet *sheet, GnmConventions const *convs)
{
	if (sheet->convs == convs)
		return;
	sheet->convs = convs;
	if (sheet->display_formulas)
		re_render_formulas (sheet);
	SHEET_FOREACH_VIEW (sheet, sv,
		sv->edit_pos_changed.content = TRUE;);
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
	if (cell->rendered_value && gnm_cell_is_zero (cell))
		gnm_cell_render_value (cell, TRUE);
}

static void
sheet_set_hide_zeros (Sheet *sheet, gboolean hide)
{
	hide = !!hide;
	if (sheet->hide_zero == hide)
		return;
	sheet->hide_zero = hide;

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

	attached = wb != NULL && /* not strictly needed */
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
	sheet->name_quoted = g_string_free (gnm_expr_conv_quote (
		gnm_conventions_default, new_name_unquoted), FALSE);
	sheet->name_unquoted_collate_key =
		g_utf8_collate_key (new_name_unquoted, -1);
	sheet->name_case_insensitive =
		g_utf8_casefold (new_name_unquoted, -1);

	/* FIXME: maybe have workbook_sheet_attach_internal for this.  */
	if (attached)
		g_hash_table_insert (wb->sheet_hash_private,
				     sheet->name_case_insensitive,
				     sheet);

	if (sheet->sheet_type == GNM_SHEET_DATA) {
		/* We have to fix the Sheet_Title name */
		GnmNamedExpr *nexpr;
		GnmParsePos pp;

		parse_pos_init_sheet (&pp, sheet);
		nexpr = expr_name_lookup (&pp, "Sheet_Title");

		if (nexpr != NULL)
			expr_name_set_expr (nexpr,
					    gnm_expr_top_new_constant
					    (value_new_string
					     (sheet->name_unquoted)));
	}
}

struct resize_colrow {
	Sheet *sheet;
	gboolean is_cols;
};

static gboolean
cb_colrow_compute_pixels_from_pts (GnmColRowIter const *iter,
				   struct resize_colrow *data)
{
	colrow_compute_pixels_from_pts ((ColRowInfo *)iter->cri,
		data->sheet, data->is_cols);
	return FALSE;
}

static void
cb_clear_rendered_cells (gpointer ignored, GnmCell *cell)
{
	if (cell->rendered_value != NULL) {
		cell->row_info->needs_respan = TRUE;
		gnm_rendered_value_destroy (cell->rendered_value);
		cell->rendered_value = NULL;
	}
}

static void
sheet_set_zoom_factor (Sheet *sheet, double factor)
{
	struct resize_colrow closure;

	if (fabs (factor - sheet->last_zoom_factor_used) < 1e-6)
		return;
	sheet->last_zoom_factor_used = factor;

	/* First, the default styles */
	colrow_compute_pixels_from_pts (&sheet->rows.default_style, sheet, FALSE);
	colrow_compute_pixels_from_pts (&sheet->cols.default_style, sheet, TRUE);

	/* Then every column and row */
	closure.sheet = sheet;
	closure.is_cols = TRUE;
	colrow_foreach (&sheet->cols, 0, gnm_sheet_get_max_cols (sheet) - 1,
		(ColRowHandler)&cb_colrow_compute_pixels_from_pts, &closure);
	closure.is_cols = FALSE;
	colrow_foreach (&sheet->rows, 0, gnm_sheet_get_max_rows (sheet) - 1,
		(ColRowHandler)&cb_colrow_compute_pixels_from_pts, &closure);

	sheet_cell_foreach (sheet, (GHFunc)&cb_clear_rendered_cells, NULL);
	SHEET_FOREACH_CONTROL (sheet, view, control, sc_scale_changed (control););
}


static void
gnm_sheet_set_property (GObject *object, guint property_id,
			GValue const *value, GParamSpec *pspec)
{
	Sheet *sheet = (Sheet *)object;

	switch (property_id) {
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

	case PROP_PROTECTED :
		sheet->is_protected = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_OBJECTS :
		sheet->protected_allow.edit_objects = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_SCENARIOS :
		sheet->protected_allow.edit_scenarios = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_CELL_FORMATTING :
		sheet->protected_allow.cell_formatting = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_COLUMN_FORMATTING :
		sheet->protected_allow.column_formatting = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_ROW_FORMATTING :
		sheet->protected_allow.row_formatting = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_COLUMNS :
		sheet->protected_allow.insert_columns = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_ROWS :
		sheet->protected_allow.insert_rows = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_HYPERLINKS :
		sheet->protected_allow.insert_hyperlinks = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_DELETE_COLUMNS :
		sheet->protected_allow.delete_columns = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_DELETE_ROWS :
		sheet->protected_allow.delete_rows = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_SELECT_LOCKED_CELLS :
		sheet->protected_allow.select_locked_cells = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_SORT_RANGES :
		sheet->protected_allow.sort_ranges = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_AUTO_FILTERS :
		sheet->protected_allow.edit_auto_filters = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_PIVOTTABLE :
		sheet->protected_allow.edit_pivottable = !!g_value_get_boolean (value);
		break;
	case PROP_PROTECTED_ALLOW_SELECT_UNLOCKED_CELLS :
		sheet->protected_allow.select_unlocked_cells = !!g_value_get_boolean (value);
		break;

	case PROP_CONVENTIONS:
		sheet_set_conventions (sheet, g_value_get_pointer (value));
		break;
	case PROP_USE_R1C1: /* convenience api */
		sheet_set_conventions (sheet, !!g_value_get_boolean (value)
			? gnm_conventions_xls_r1c1 : gnm_conventions_default);
		break;

	case PROP_TAB_FOREGROUND: {
		GnmColor *color = g_value_dup_boxed (value);
		style_color_unref (sheet->tab_text_color);
		sheet->tab_text_color = color;
		break;
	}
	case PROP_TAB_BACKGROUND: {
		GnmColor *color = g_value_dup_boxed (value);
		style_color_unref (sheet->tab_color);
		sheet->tab_color = color;
		break;
	}
	case PROP_ZOOM_FACTOR:
		sheet_set_zoom_factor (sheet, g_value_get_double (value));
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

	case PROP_PROTECTED :
		g_value_set_boolean (value, sheet->is_protected);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_OBJECTS :
		g_value_set_boolean (value, sheet->protected_allow.edit_objects);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_SCENARIOS :
		g_value_set_boolean (value, sheet->protected_allow.edit_scenarios);
		break;
	case PROP_PROTECTED_ALLOW_CELL_FORMATTING :
		g_value_set_boolean (value, sheet->protected_allow.cell_formatting);
		break;
	case PROP_PROTECTED_ALLOW_COLUMN_FORMATTING :
		g_value_set_boolean (value, sheet->protected_allow.column_formatting);
		break;
	case PROP_PROTECTED_ALLOW_ROW_FORMATTING :
		g_value_set_boolean (value, sheet->protected_allow.row_formatting);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_COLUMNS :
		g_value_set_boolean (value, sheet->protected_allow.insert_columns);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_ROWS :
		g_value_set_boolean (value, sheet->protected_allow.insert_rows);
		break;
	case PROP_PROTECTED_ALLOW_INSERT_HYPERLINKS :
		g_value_set_boolean (value, sheet->protected_allow.insert_hyperlinks);
		break;
	case PROP_PROTECTED_ALLOW_DELETE_COLUMNS :
		g_value_set_boolean (value, sheet->protected_allow.delete_columns);
		break;
	case PROP_PROTECTED_ALLOW_DELETE_ROWS :
		g_value_set_boolean (value, sheet->protected_allow.delete_rows);
		break;
	case PROP_PROTECTED_ALLOW_SELECT_LOCKED_CELLS :
		g_value_set_boolean (value, sheet->protected_allow.select_locked_cells);
		break;
	case PROP_PROTECTED_ALLOW_SORT_RANGES :
		g_value_set_boolean (value, sheet->protected_allow.sort_ranges);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_AUTO_FILTERS :
		g_value_set_boolean (value, sheet->protected_allow.edit_auto_filters);
		break;
	case PROP_PROTECTED_ALLOW_EDIT_PIVOTTABLE :
		g_value_set_boolean (value, sheet->protected_allow.edit_pivottable);
		break;
	case PROP_PROTECTED_ALLOW_SELECT_UNLOCKED_CELLS :
		g_value_set_boolean (value, sheet->protected_allow.select_unlocked_cells);
		break;

	case PROP_CONVENTIONS:
		g_value_set_pointer (value, (gpointer)sheet->convs);
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sheet_init (Sheet *sheet)
{
	sheet->priv = g_new0 (SheetPrivate, 1);
	sheet->sheet_views = g_ptr_array_new ();

	/* Init, focus, and load handle setting these if/when necessary */
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->recompute_spans = TRUE;
	sheet->priv->reposition_objects.row = gnm_sheet_get_max_rows (sheet);
	sheet->priv->reposition_objects.col = gnm_sheet_get_max_cols (sheet);

	range_init_full_sheet (&sheet->priv->unhidden_region);

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
	sheet->protected_allow.select_locked_cells	=TRUE;
	sheet->protected_allow.sort_ranges		= FALSE;
	sheet->protected_allow.edit_auto_filters	= FALSE;
	sheet->protected_allow.edit_pivottable		= FALSE;
	sheet->protected_allow.select_unlocked_cells	=TRUE;

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

	sheet->solver_parameters = solver_param_new ();

	sheet->cols.max_used = -1;
	g_ptr_array_set_size (sheet->cols.info = g_ptr_array_new (),
			      COLROW_SEGMENT_INDEX (gnm_sheet_get_max_cols (sheet) - 1) + 1);
	sheet_col_set_default_size_pts (sheet, 48);

	sheet->rows.max_used = -1;
	g_ptr_array_set_size (sheet->rows.info = g_ptr_array_new (),
			      COLROW_SEGMENT_INDEX (gnm_sheet_get_max_rows (sheet) - 1) + 1);
	sheet_row_set_default_size_pts (sheet, 12.75);

	sheet->print_info = print_info_new (FALSE);

	sheet->filters = NULL;
	sheet->scenarios = NULL;
	sheet->list_merged = NULL;
	sheet->hash_merged = g_hash_table_new ((GHashFunc)&gnm_cellpos_hash,
					       (GCompareFunc)&gnm_cellpos_equal);

	sheet->cell_hash = g_hash_table_new ((GHashFunc)&gnm_cellpos_hash,
					     (GCompareFunc)&gnm_cellpos_equal);

	/* Init preferences */
	sheet->convs = gnm_conventions_default;
	sheet->hide_zero = FALSE;

	/* FIXME: probably not here.  */
	/* See also gtk_widget_create_pango_context ().  */
	sheet->context = gnm_pango_context_get ();

	/* Init menu states */
	sheet->priv->enable_showhide_detail = TRUE;

	sheet->names = NULL;
	sheet->convs = gnm_conventions_default;

	sheet_style_init (sheet);

	/*
	 * "zoom-factor" is a construction parameter and will thus
	 * override this.
	 */
	sheet->last_zoom_factor_used = -1;

	sheet->deps	 = gnm_dep_container_new (sheet);
}

static void
gnm_sheet_class_init (GObjectClass *gobject_class)
{
	parent_class = g_type_class_peek_parent (gobject_class);

	gobject_class->set_property	= gnm_sheet_set_property;
	gobject_class->get_property	= gnm_sheet_get_property;
	gobject_class->finalize         = gnm_sheet_finalize;

        g_object_class_install_property (gobject_class, PROP_NAME,
		 g_param_spec_string ("name", _("Name"),
				      _("The name of the sheet."),
				      NULL,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, PROP_RTL,
		 g_param_spec_boolean ("text-is-rtl", _("text-is-rtl"),
				       _("Text goes from right to left."),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class, PROP_VISIBILITY,
		 g_param_spec_enum ("visibility", _("Visibility"),
				    _("How visible the sheet is."),
				    GNM_SHEET_VISIBILITY_TYPE,
				    GNM_SHEET_VISIBILITY_VISIBLE,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_FORMULAS,
		 g_param_spec_boolean ("display-formulas", _("Display Formulas"),
				       _("Control whether formulas are shown instead of values."),
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
		 g_param_spec_boolean ("display-column-header", _("Display Column Headers"),
				       _("Control whether column headers are shown."),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_ROW_HEADER,
		 g_param_spec_boolean ("display-row-header", _("Display Row Headers"),
				       _("Control whether row headers are shown."),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_OUTLINES,
		 g_param_spec_boolean ("display-outlines", _("Display Outlines"),
				       _("Control whether outlines are shown."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_OUTLINES_BELOW,
		 g_param_spec_boolean ("display-outlines-below", _("Display Outlines Below"),
				       _("Control whether outline symbols are shown below."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_DISPLAY_OUTLINES_RIGHT,
		 g_param_spec_boolean ("display-outlines-right", _("Display Outlines Right"),
				       _("Control whether outline symbols are shown to the right."),
				       TRUE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));

        g_object_class_install_property (gobject_class, PROP_PROTECTED,
		 g_param_spec_boolean ("protected", _("Protected"),
				       _("Sheet is protected."),
				       FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_EDIT_OBJECTS,
		g_param_spec_boolean ("protected-allow-edit-objects", _("Protected Allow Edit objects"),
				      _("Allow objects to be edited while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_EDIT_SCENARIOS,
		g_param_spec_boolean ("protected-allow-edit-scenarios", _("Protected allow edit scenarios"),
				      _("Allow scenarios to be edited while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_CELL_FORMATTING,
		g_param_spec_boolean ("protected-allow-cell-formatting", _("Protected allow cell formatting"),
				      _("Allow cell format changes while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_COLUMN_FORMATTING,
		g_param_spec_boolean ("protected-allow-column-formatting", _("Protected allow column formatting"),
				      _("Allow column formatting while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_ROW_FORMATTING,
		g_param_spec_boolean ("protected-allow-row-formatting", _("Protected allow row formatting"),
				      _("Allow row formatting while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_INSERT_COLUMNS,
		g_param_spec_boolean ("protected-allow-insert-columns", _("Protected allow insert columns"),
				      _("Allow columns to be inserted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_INSERT_ROWS,
		g_param_spec_boolean ("protected-allow-insert-rows", _("Protected allow insert rows"),
				      _("Allow rows to be inserted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_INSERT_HYPERLINKS,
		g_param_spec_boolean ("protected-allow-insert-hyperlinks", _("Protected allow insert hyperlinks"),
				      _("Allow hyperlinks to be inserted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_DELETE_COLUMNS,
		g_param_spec_boolean ("protected-allow-delete-columns", _("Protected allow delete columns"),
				      _("Allow columns to be deleted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_DELETE_ROWS,
		g_param_spec_boolean ("protected-allow-delete-rows", _("Protected allow delete rows"),
				      _("Allow rows to be deleted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_SELECT_LOCKED_CELLS,
		g_param_spec_boolean ("protected-allow-select-locked-cells", _("Protected allow select locked cells"),
				      _("Allow the user to select locked cells while a sheet is protected"),
				      TRUE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_SORT_RANGES,
		g_param_spec_boolean ("protected-allow-sort-ranges", _("Protected allow sort ranges"),
				      _("Allow ranges to be sorted while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_EDIT_AUTO_FILTERS,
		g_param_spec_boolean ("protected-allow-edit-auto-filters", _("Protected allow edit auto filters"),
				      _("Allow auto filters to be edited while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_EDIT_PIVOTTABLE,
		g_param_spec_boolean ("protected-allow-edit-pivottable", _("Protected allow edit pivottable"),
				      _("Allow pivottable to be edited  while a sheet is protected"),
				      FALSE, GSF_PARAM_STATIC | G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_PROTECTED_ALLOW_SELECT_UNLOCKED_CELLS,
		g_param_spec_boolean ("protected-allow-select-unlocked-cells", _("Protected allow select unlocked cells"),
				      _("Allow the user to select unlocked cells while a sheet is protected"),
				      TRUE, GSF_PARAM_STATIC | G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_CONVENTIONS,
		 g_param_spec_pointer ("conventions", _("Display convention for expressions (default Gnumeric A1)"),
				       _("How to format displayed expressions, (A1 vs R1C1, function names, ...)"),
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_USE_R1C1, /* convenience wrapper to CONVENTIONS */
		 g_param_spec_boolean ("use-r1c1", _("Display convention for expressions as XLS_R1C1 vs default"),
				       _("How to format displayed expressions, (a convenience api)"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_TAB_FOREGROUND,
		 g_param_spec_boxed ("tab-foreground", _("Tab Foreground"),
				     _("The foreground color of the tab."),
				     GNM_STYLE_COLOR_TYPE,
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class, PROP_TAB_BACKGROUND,
		 g_param_spec_boxed ("tab-background", _("Tab Background"),
				     _("The background color of the tab."),
				     GNM_STYLE_COLOR_TYPE,
				     GSF_PARAM_STATIC |
				     G_PARAM_READWRITE));

	/* What is this doing in sheet?  */
	g_object_class_install_property (gobject_class, PROP_ZOOM_FACTOR,
		 g_param_spec_double ("zoom-factor", _("Zoom Factor"),
				      _("The level of zoom used for this sheet."),
				      0.1, 5.0,
				      1.0,
				      GSF_PARAM_STATIC |
				      G_PARAM_CONSTRUCT |
				      G_PARAM_READWRITE));

	signals[DETACHED_FROM_WORKBOOK] = g_signal_new
		("detached_from_workbook",
		 GNM_SHEET_TYPE,
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET (GnmSheetClass, detached_from_workbook),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__OBJECT,
		 G_TYPE_NONE, 1, WORKBOOK_TYPE);

}

GSF_CLASS (GnmSheet, gnm_sheet,
	   gnm_sheet_class_init, gnm_sheet_init, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

GType
gnm_sheet_visibility_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
	  static GEnumValue const values[] = {
		  { GNM_SHEET_VISIBILITY_VISIBLE, (char*)"GNM_SHEET_VISIBILITY_VISIBLE", (char*)"visible" },
		  { GNM_SHEET_VISIBILITY_HIDDEN, (char*)"GNM_SHEET_VISIBILITY_HIDDEN", (char*)"hidden" },
		  { GNM_SHEET_VISIBILITY_VERY_HIDDEN, (char*)"GNM_SHEET_VISIBILITY_VERY_HIDDEN", (char*)"very-hidden" },
		  { 0, NULL, NULL }
	  };
	  etype = g_enum_register_static ("GnmSheetVisibility", values);
  }
  return etype;
}

/* ------------------------------------------------------------------------- */
/**
 * sheet_new_with_type :
 * @wb    : #Workbook
 * @name  : An unquoted name in utf8
 * @type  : @GnmSheetType
 *
 * Create a new Sheet of type @type, and associate it with @wb.
 * The type can not be changed later
 **/
Sheet *
sheet_new_with_type (Workbook *wb, char const *name, GnmSheetType type)
{
	Sheet  *sheet;

	g_return_val_if_fail (wb != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	sheet = g_object_new (GNM_SHEET_TYPE,
			      "zoom-factor", (double)gnm_app_prefs->zoom,
			      NULL);

	sheet->index_in_wb = -1;
	sheet->workbook = wb;
	sheet->name_unquoted = g_strdup (name);
	sheet->name_quoted = g_string_free (gnm_expr_conv_quote (
		gnm_conventions_default, name), FALSE);
	sheet->name_unquoted_collate_key =
		g_utf8_collate_key (sheet->name_unquoted, -1);
	sheet->name_case_insensitive =
		g_utf8_casefold (sheet->name_unquoted, -1);
	sheet->sheet_type = type;

	sheet->display_formulas = (type == GNM_SHEET_XLM);
	sheet->hide_grid =
	sheet->hide_col_header =
	sheet->hide_row_header = (type == GNM_SHEET_OBJECT);

	if (type == GNM_SHEET_OBJECT) {
		colrow_compute_pixels_from_pts (&sheet->rows.default_style, sheet, FALSE);
		colrow_compute_pixels_from_pts (&sheet->cols.default_style, sheet, TRUE);
	}

	if (type == GNM_SHEET_DATA) {
		/* We have to add permanent names */
		{
			expr_name_perm_add (sheet, "Sheet_Title",
				    gnm_expr_top_new_constant (value_new_string (sheet->name_unquoted)),
				    FALSE);
		}
		{
			GnmRange r;
			range_init_full_sheet (&r);
			expr_name_perm_add (sheet, "Print_Area",
				gnm_expr_top_new_constant (value_new_cellrange_r (sheet, &r)),
				TRUE);
		}
	}

	return sheet;
}

/**
 * sheet_new :
 * @wb    : #Workbook
 * @name  : An unquoted name in utf8
 *
 * Create a new Sheet of type SHEET_DATA, and associate it with @wb.
 * The type can not be changed later
 **/
Sheet *
sheet_new (Workbook *wb, char const *name)
{
	return sheet_new_with_type (wb, name, GNM_SHEET_DATA);
}

/****************************************************************************/

void
sheet_redraw_all (Sheet const *sheet, gboolean headers)
{
	SHEET_FOREACH_CONTROL (sheet, view, control,
		sc_redraw_all (control, headers););
}

static GnmValue *
cb_clear_rendered_values (GnmCellIter const *iter, G_GNUC_UNUSED gpointer user)
{
	if (iter->cell->rendered_value != NULL) {
		gnm_rendered_value_destroy (iter->cell->rendered_value);
		iter->cell->rendered_value = NULL;
	}
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
	sheet_mark_dirty (sheet);
	if (flags & GNM_SPANCALC_RE_RENDER)
		sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_NONEXISTENT,
			r->start.col, r->start.row, r->end.col, r->end.row,
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

void
sheet_cell_calc_span (GnmCell *cell, GnmSpanCalcFlags flags)
{
	CellSpanInfo const * span;
	int left, right;
	int min_col, max_col;
	gboolean render = (flags & GNM_SPANCALC_RE_RENDER);
	gboolean const resize = (flags & GNM_SPANCALC_RESIZE);
	gboolean existing = FALSE;
	GnmRange const *merged;

	g_return_if_fail (cell != NULL);

	/* Render & Size any unrendered cells */
	if ((flags & GNM_SPANCALC_RENDER) && cell->rendered_value == NULL)
		render = TRUE;

	if (render) {
		if (!gnm_cell_has_expr (cell))
			gnm_cell_render_value ((GnmCell *)cell, TRUE);
		else if (cell->rendered_value) {
			gnm_rendered_value_destroy (cell->rendered_value);
			cell->rendered_value = NULL;
		}
	} else if (resize) {
		/* FIXME: what was wanted here?  */
		/* rendered_value_calc_size (cell); */
	}

	/* Is there an existing span ? clear it BEFORE calculating new one */
	span = row_span_get (cell->row_info, cell->pos.col);
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

	merged = gnm_sheet_merge_is_corner (cell->base.sheet, &cell->pos);
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

	sheet_redraw_partial_row (cell->base.sheet,
		cell->pos.row, min_col, max_col);
}

/**
 * sheet_apply_style :
 * @sheet: the sheet in which can be found
 * @range: the range to which should be applied
 * @style: the style
 *
 * A mid level routine that applies the supplied partial style @style to the
 * target @range and performs the necessary respanning and redrawing.
 *
 * It absorbs the style reference.
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

void
sheet_apply_border (Sheet       *sheet,
		    GnmRange const *range,
		    GnmBorder **borders)
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
	ColRowInfo *ri = g_new (ColRowInfo, 1);

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	*ri = sheet->rows.default_style;
	ri->is_default = FALSE;
	ri->needs_respan = TRUE;

	return ri;
}

static ColRowInfo *
sheet_col_new (Sheet *sheet)
{
	ColRowInfo *ci = g_new (ColRowInfo, 1);

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	*ci = sheet->cols.default_style;
	ci->is_default = FALSE;

	return ci;
}

static void
sheet_col_add (Sheet *sheet, ColRowInfo *cp, int col)
{
	ColRowSegment **segment = (ColRowSegment **)&COLROW_GET_SEGMENT (&(sheet->cols), col);

	g_return_if_fail (col >= 0);
	g_return_if_fail (col < gnm_sheet_get_max_cols (sheet));

	if (*segment == NULL)
		*segment = g_new0 (ColRowSegment, 1);
	(*segment)->info[COLROW_SUB_INDEX (col)] = cp;

	if (cp->outline_level > sheet->cols.max_outline_level)
		sheet->cols.max_outline_level = cp->outline_level;
	if (col > sheet->cols.max_used) {
		sheet->cols.max_used = col;
		sheet->priv->resize_scrollbar = TRUE;
	}
}

static void
sheet_row_add (Sheet *sheet, ColRowInfo *rp, int row)
{
	ColRowSegment **segment = (ColRowSegment **)&COLROW_GET_SEGMENT (&(sheet->rows), row);

	g_return_if_fail (row >= 0);
	g_return_if_fail (row < gnm_sheet_get_max_rows (sheet));

	if (*segment == NULL)
		*segment = g_new0 (ColRowSegment, 1);
	(*segment)->info[COLROW_SUB_INDEX (row)] = rp;

	if (rp->outline_level > sheet->rows.max_outline_level)
		sheet->rows.max_outline_level = rp->outline_level;
	if (row > sheet->rows.max_used) {
		sheet->rows.max_used = row;
		sheet->priv->resize_scrollbar = TRUE;
	}
}

static void
sheet_reposition_objects (Sheet const *sheet, GnmCellPos const *pos)
{
	GSList *ptr;
	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next )
		sheet_object_update_bounds (SHEET_OBJECT (ptr->data), pos);
}

/**
 * sheet_flag_status_update_cell:
 *    flag the sheet as requiring an update to the status display
 *    if the supplied cell location is the edit cursor, or part of the
 *    selected region.
 *
 * @cell : The cell that has changed.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 **/
void
sheet_flag_status_update_cell (GnmCell const *cell)
{
	SHEET_FOREACH_VIEW (cell->base.sheet, sv,
		sv_flag_status_update_pos (sv, &cell->pos););
}

/**
 * sheet_flag_status_update_range:
 *    flag the sheet as requiring an update to the status display
 *    if the supplied cell location contains the edit cursor, or intersects of
 *    the selected region.
 *
 * @sheet :
 * @range : If NULL then force an update.
 *
 * Will cause the format toolbar, the edit area, and the auto expressions to be
 * updated if appropriate.
 **/
void
sheet_flag_status_update_range (Sheet const *sheet, GnmRange const *range)
{
	SHEET_FOREACH_VIEW (sheet, sv,
		sv_flag_status_update_range (sv, range););
}

/**
 * sheet_flag_style_update_range :
 * @sheet : The sheet being changed
 * @range : the range that is changing.
 *
 * Flag format changes that will require updating the format indicators.
 **/
void
sheet_flag_style_update_range (Sheet const *sheet, GnmRange const *range)
{
	SHEET_FOREACH_VIEW (sheet, sv,
		sv_flag_style_update_range (sv, range););
}

/**
 * sheet_flag_recompute_spans:
 * @sheet :
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
cb_outline_level (GnmColRowIter const *iter, int *outline_level)
{
	if (*outline_level < iter->cri->outline_level)
		*outline_level  = iter->cri->outline_level;
	return FALSE;
}

/**
 * sheet_colrow_fit_gutter:
 * @sheet: Sheet to change for.
 * @is_cols: Column gutter or row gutter?
 *
 * Find the current max outline level.
 **/
static int
sheet_colrow_fit_gutter (Sheet const *sheet, gboolean is_cols)
{
	int outline_level = 0;
	colrow_foreach (is_cols ? &sheet->cols : &sheet->rows,
			0, colrow_max (is_cols, sheet) - 1,
		(ColRowHandler)cb_outline_level, &outline_level);
	return outline_level;
}

/**
 * sheet_update_only_grid :
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
		sheet_queue_respan (sheet, 0, gnm_sheet_get_max_rows (sheet) - 1);
	}

	if (p->reposition_objects.row < gnm_sheet_get_max_rows (sheet) ||
	    p->reposition_objects.col < gnm_sheet_get_max_cols (sheet)) {
		SHEET_FOREACH_VIEW (sheet, sv, {
			if (!p->resize && sv_is_frozen (sv)) {
				if (p->reposition_objects.col < sv->unfrozen_top_left.col ||
				    p->reposition_objects.row < sv->unfrozen_top_left.row) {
					SHEET_VIEW_FOREACH_CONTROL(sv, control,
						sc_resize (control, FALSE););
				}
			}
		});
		sheet_reposition_objects (sheet, &p->reposition_objects);
		p->reposition_objects.row = gnm_sheet_get_max_rows (sheet);
		p->reposition_objects.col = gnm_sheet_get_max_cols (sheet);
	}

	if (p->resize) {
		p->resize = FALSE;
		SHEET_FOREACH_CONTROL (sheet, sv, control, sc_resize (control, FALSE););
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
 * @sheet : #Sheet
 *
 * Should be called after a logical command has finished processing to request
 * redraws for any pending events, and to update the various status regions
 **/
void
sheet_update (Sheet const *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	sheet_update_only_grid (sheet);

	SHEET_FOREACH_VIEW (sheet, sv, sv_update (sv););
}

/**
 * sheet_cell_get:
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Return value: a (GnmCell *) containing the GnmCell, or NULL if
 * the cell does not exist
 **/
GnmCell *
sheet_cell_get (Sheet const *sheet, int col, int row)
{
	GnmCell *cell;
	GnmCellPos pos;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	pos.col = col;
	pos.row = row;
	cell = g_hash_table_lookup (sheet->cell_hash, &pos);

	return cell;
}

/**
 * sheet_cell_fetch:
 * @sheet:  The sheet where we want to locate the cell
 * @col:    the cell column
 * @row:    the cell row
 *
 * Return value: a (GnmCell *) containing the GnmCell at col, row.
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
 * @sheet : #Sheet
 * @r : A #GnmRange
 * @is_cols : boolean
 *
 * Returns TRUE if the cols/rows in @r.start -> @r.end can be grouped, return
 * FALSE otherwise. You can invert the result if you need to find out if a
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
			colrow_set_outline (cri, new_level, FALSE);
			if (new_max < new_level)
				new_max = new_level;
		}
	}

	if (!group)
		new_max = sheet_colrow_fit_gutter (sheet, is_cols);

	sheet_colrow_gutter (sheet, is_cols, new_max);
	SHEET_FOREACH_VIEW (sheet, sv,
		sv_redraw_headers (sv, is_cols, !is_cols, NULL););

	return TRUE;
}

/**
 * sheet_colrow_gutter :
 *
 * @sheet :
 * @is_cols :
 * @max_outline :
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
};

static void
cb_sheet_get_extent (gpointer ignored, gpointer value, gpointer data)
{
	GnmCell const *cell = (GnmCell const *) value;
	struct sheet_extent_data *res = data;

	if (gnm_cell_is_empty (cell))
		return;

	/* Remember the first cell is the min & max */
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
			gnm_sheet_merge_is_corner (cell->base.sheet, &cell->pos);
		res->range = range_union (&res->range, merged);
	} else {
		CellSpanInfo const *span;
		if (cell->row_info->needs_respan)
			row_calc_spans (cell->row_info, cell->pos.row, cell->base.sheet);
		span = row_span_get (cell->row_info, cell->pos.col);
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
 *
 * calculates the area occupied by cell data.
 *
 * NOTE: When spans_and_merges_extend is TRUE, this function will calculate
 * all spans.  That might be expensive.
 *
 * Return value: the range.
 **/
GnmRange
sheet_get_extent (Sheet const *sheet, gboolean spans_and_merges_extend)
{
	static GnmRange const dummy = { { 0,0 }, { 0,0 } };
	struct sheet_extent_data closure;
	GSList *ptr;

	g_return_val_if_fail (IS_SHEET (sheet), dummy);

	/* FIXME : Why -2 ??? */
	closure.range.start.col = gnm_sheet_get_max_cols (sheet) - 2;
	closure.range.start.row = gnm_sheet_get_max_rows (sheet) - 2;
	closure.range.end.col   = 0;
	closure.range.end.row   = 0;
	closure.spans_and_merges_extend = spans_and_merges_extend;

	sheet_cell_foreach (sheet, &cb_sheet_get_extent, &closure);

	for (ptr = sheet->sheet_objects; ptr; ptr = ptr->next) {
		SheetObject *so = SHEET_OBJECT (ptr->data);

		closure.range.start.col = MIN (so->anchor.cell_bound.start.col,
					       closure.range.start.col);
		closure.range.start.row = MIN (so->anchor.cell_bound.start.row,
					       closure.range.start.row);
		closure.range.end.col = MAX (so->anchor.cell_bound.end.col,
					     closure.range.end.col);
		closure.range.end.row = MAX (so->anchor.cell_bound.end.row,
					     closure.range.end.row);
	}

	if (closure.range.start.col >= gnm_sheet_get_max_cols (sheet) - 2)
		closure.range.start.col = 0;
	if (closure.range.start.row >= gnm_sheet_get_max_rows (sheet) - 2)
		closure.range.start.row = 0;
	if (closure.range.end.col < 0)
		closure.range.end.col = 0;
	if (closure.range.end.row < 0)
		closure.range.end.row = 0;

	return closure.range;
}

GnmRange
sheet_get_nominal_printarea	(Sheet const *sheet)
{
	GnmNamedExpr *nexpr;
	GnmParsePos pos;
	GnmValue *val;
	GnmRangeRef const *r_ref;
	GnmRange print_area;

	range_init_full_sheet (&print_area);

	g_return_val_if_fail (IS_SHEET (sheet), print_area);

	/* GnmParsePos should really have Sheet const * */
	parse_pos_init_sheet (&pos, (Sheet *) sheet);
	nexpr = expr_name_lookup (&pos, "Print_Area");
	if (nexpr != NULL) {
		val = gnm_expr_top_get_range (nexpr->texpr);
		if (val != NULL) {
			r_ref = value_get_rangeref (val);
			if (r_ref != NULL) {
				range_init_rangeref (&print_area,
						     r_ref);
			}
			value_release (val);
		}
	}
	

	/* We are now trying to fix any problems with the print area */
	while (print_area.start.col < 0)
		print_area.start.col += gnm_sheet_get_max_cols (sheet);
	while (print_area.start.row < 0)
		print_area.start.row += gnm_sheet_get_max_rows (sheet);
	while (print_area.end.col < 0)
		print_area.end.col += gnm_sheet_get_max_cols (sheet);
	while (print_area.end.row < 0)
		print_area.end.row += gnm_sheet_get_max_rows (sheet);

	if (print_area.start.col > print_area.end.col) {
		int col = print_area.end.col;
		print_area.end.col = print_area.start.col;
		print_area.start.col = col;
	}
	if (print_area.start.row > print_area.end.row) {
		int row = print_area.end.row;
		print_area.end.row = print_area.start.row;
		print_area.start.row = row;
	}

	range_ensure_sanity (&print_area);
	
	return print_area;
}

GnmRange
sheet_get_printarea	(Sheet const *sheet,
			 gboolean include_styles,
			 gboolean ignore_printarea)
{
	static GnmRange const dummy = { { 0,0 }, { 0,0 } };
	GnmRange r;
	GnmRange print_area;
	GnmRange intersect;

	g_return_val_if_fail (IS_SHEET (sheet), dummy);

	r = sheet_get_extent (sheet, TRUE);
	if (include_styles)
		sheet_style_get_extent (sheet, &r, NULL);

	if (ignore_printarea)
		return r;

	print_area = sheet_get_nominal_printarea (sheet);

	if (range_intersection (&intersect, &r, &print_area))
		return intersect;

	return dummy;
}

struct cb_fit {
	int max;
	gboolean ignore_strings;
};

/* find the maximum width in a range.  */
static GnmValue *
cb_max_cell_width (GnmCellIter const *iter, struct cb_fit *data)
{
	int width;
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

	/* Variable width cell must be re-rendered */
	if (cell->rendered_value == NULL ||
	    cell->rendered_value->variable_width)
		gnm_cell_render_value (cell, FALSE);

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
 * @ignore_strings: skip cells containing string values.
 *
 * This routine computes the ideal size for the column to make the contents all
 * cells in the column visible.
 *
 * Return : Maximum size in pixels INCLUDING margins and grid lines
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
	sheet_foreach_cell_in_range (sheet,
		CELL_ITER_IGNORE_NONEXISTENT | CELL_ITER_IGNORE_HIDDEN,
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
		height = gnm_style_get_pango_height (gnm_cell_get_style (cell),
						     sheet->context,
						     sheet->last_zoom_factor_used);
	} else {
		if (cell->rendered_value == NULL)
			gnm_cell_render_value (cell, TRUE);

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
 * Return : Maximum size in pixels INCLUDING margins and grid lines
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
	data.ignore_strings = FALSE;
	sheet_foreach_cell_in_range (sheet,
		CELL_ITER_IGNORE_NONEXISTENT | CELL_ITER_IGNORE_HIDDEN,
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

	colrow_foreach (&sheet->rows, 0, gnm_sheet_get_max_rows (sheet) - 1,
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

	if (NULL == (cell = iter->cell))
		cell = sheet_cell_create (iter->pp.sheet,
			iter->pp.eval.col, iter->pp.eval.row);

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
 * sheet_range_set_text :
 *
 * @pos : The position from which to parse an expression.
 * @r  :  The range to fill
 * @str : The text to be parsed and assigned.
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

	g_return_if_fail (pos != NULL);
	g_return_if_fail (r != NULL);
	g_return_if_fail (str != NULL);

	parse_text_value_or_expr (pos, str,
		&closure.val, &closure.texpr,
		NULL /* TODO : Use edit_pos format ?? */,
		workbook_date_conv (pos->sheet->workbook));

	if (closure.texpr)
		gnm_expr_top_get_boundingbox (closure.texpr,
			range_init_full_sheet (&closure.expr_bound));

	/* Store the parsed result creating any cells necessary */
	sheet_foreach_cell_in_range (pos->sheet, CELL_ITER_ALL,
		r->start.col, r->start.row, r->end.col, r->end.row,
		(CellIterFunc)&cb_set_cell_content, &closure);

	merged = gnm_sheet_merge_get_overlap (pos->sheet, r);
	for (ptr = merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange const *tmp = ptr->data;
		sheet_foreach_cell_in_range (pos->sheet, CELL_ITER_ALL,
			tmp->start.col, tmp->start.row, tmp->end.col, tmp->end.row,
			(CellIterFunc)&cb_clear_non_corner, (gpointer)tmp);
	}
	g_slist_free (merged);

	sheet_region_queue_recalc (pos->sheet, r);

	if (closure.val)
		value_release (closure.val);
	else
		gnm_expr_top_unref (closure.texpr);

	sheet_flag_status_update_range (pos->sheet, r);
}

/**
 * sheet_cell_get_value:
 * @sheet: Sheet
 * @col: Source column
 * @row: Source row
 *
 * Retrieve the value of a cell. The returned value must
 * NOT be freed or tampered with.
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
		text, &val, &texpr,
		gnm_cell_get_format (cell),
		workbook_date_conv (cell->base.sheet->workbook));

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

		gnm_cell_set_value (cell, val);
		if (markup != NULL && VALUE_IS_STRING (cell->value)) {
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
			value_set_fmt (cell->value, fmt);
			go_format_unref (fmt);
			if (quoted)
				pango_attr_list_unref (adj_markup);
		}

		/* Queue recalc before spanning, see above.  */
		cell_queue_recalc (cell);

		sheet_cell_calc_span (cell, GNM_SPANCALC_RESIZE | GNM_SPANCALC_RENDER);
	}

	sheet_flag_status_update_cell (cell);
}

/**
 * sheet_cell_set_expr:
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

/*
 * sheet_cell_set_value : Stores (WITHOUT COPYING) the supplied value.  It marks the
 *          sheet as dirty.
 *
 * The value is rendered, spans are calculated, and the rendered string
 * is stored as if that is what the user had entered.  It queues a redraw
 * and checks to see if the edit region or selection content changed.
 *
 * If an optional format is supplied it is stored for later use.
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
	GnmRange bound;

	g_return_if_fail (IS_SHEET (sheet));

	sheet_range_bounding_box (sheet,
		range_init (&bound, start_col, start_row, end_col, end_row));
	SHEET_FOREACH_CONTROL (sheet, view, control,
		sc_redraw_range (control, &bound););
}

void
sheet_redraw_range (Sheet const *sheet, GnmRange const *range)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (range != NULL);

	sheet_redraw_region (sheet,
			     range->start.col, range->start.row,
			     range->end.col, range->end.row);
}

void
sheet_redraw_cell (GnmCell const *cell)
{
	CellSpanInfo const * span;
	int start_col, end_col;
	GnmRange const *merged;

	g_return_if_fail (cell != NULL);

	merged = gnm_sheet_merge_is_corner (cell->base.sheet, &cell->pos);
	if (merged != NULL) {
		SHEET_FOREACH_CONTROL (cell->base.sheet, view, control,
			sc_redraw_range (control, merged););
		return;
	}

	start_col = end_col = cell->pos.col;
	span = row_span_get (cell->row_info, start_col);

	if (span) {
		start_col = span->left;
		end_col = span->right;
	}

	sheet_redraw_partial_row (cell->base.sheet, cell->pos.row,
				  start_col, end_col);
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


/*
 * sheet_find_boundary_horizontal
 * @sheet:  The Sheet
 * @start_col	: The column from which to begin searching.
 * @move_row	: The row in which to search for the edge of the range.
 * @base_row	: The height of the area being moved.
 * @n:      units to extend the selection vertically
 * @jump_to_boundaries : Jump to range boundaries.
 *
 * Calculate the column index for the column which is @n units
 * from @start_col doing bounds checking.  If @jump_to_boundaries is
 * TRUE @n must be 1 and the jump is to the edge of the logical range.
 *
 * NOTE : This routine implements the logic necasary for ctrl-arrow style
 * movement.  That is more compilcated than simply finding the last in a list
 * of cells with content.  If you are at the end of a range it will find the
 * start of the next.  Make sure that is the sort of behavior you want before
 * calling this.
 */
int
sheet_find_boundary_horizontal (Sheet *sheet, int start_col, int move_row,
				int base_row, int count,
				gboolean jump_to_boundaries)
{
	gboolean find_nonblank = sheet_is_cell_empty (sheet, start_col, move_row);
	gboolean keep_looking = FALSE;
	int new_col, prev_col, lagged_start_col, max_col = gnm_sheet_get_max_cols (sheet) - 1;
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
				 * non-null cell
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

/*
 * sheet_find_boundary_vertical
 * @sheet:  The Sheet *
 * @move_col	: The col in which to search for the edge of the range.
 * @start_row	: The row from which to begin searching.
 * @base_col	: The width of the area being moved.
 * @n:      units to extend the selection vertically
 * @jump_to_boundaries : Jump to range boundaries.
 *
 * Calculate the row index for the row which is @n units
 * from @start_row doing bounds checking.  If @jump_to_boundaries is
 * TRUE @n must be 1 and the jump is to the edge of the logical range.
 *
 * NOTE : This routine implements the logic necasary for ctrl-arrow style
 * movement.  That is more compilcated than simply finding the last in a list
 * of cells with content.  If you are at the end of a range it will find the
 * start of the next.  Make sure that is the sort of behavior you want before
 * calling this.
 */
int
sheet_find_boundary_vertical (Sheet *sheet, int move_col, int start_row,
			      int base_col, int count,
			      gboolean jump_to_boundaries)
{
	gboolean find_nonblank = sheet_is_cell_empty (sheet, move_col, start_row);
	gboolean keep_looking = FALSE;
	int new_row, prev_row, lagged_start_row, max_row = gnm_sheet_get_max_rows (sheet) - 1;
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
				 * non-null cell
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
cb_check_array_horizontal (GnmColRowIter const *iter, ArrayCheckData *data)
{
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
cb_check_array_vertical (GnmColRowIter const *iter, ArrayCheckData *data)
{
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
 * sheet_range_splits_array :
 * @sheet : The sheet.
 * @r     : The range to check
 * @ignore: an optionally NULL range in which it is ok to have an array.
 * @cc   : an optional place to report an error.
 * @cmd   : an optional cmd name used with @cc.
 *
 * Check the outer edges of range @sheet!@r to ensure that if an array is
 * within it then the entire array is within the range.  @ignore is useful when
 * src & dest ranges may overlap.
 *
 * returns TRUE if an array would be split.
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
	    colrow_foreach (&sheet->cols, r->start.col, r->end.col,
			    (ColRowHandler) cb_check_array_horizontal, &closure)) {
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
	    colrow_foreach (&sheet->rows, r->start.row, r->end.row,
			    (ColRowHandler) cb_check_array_vertical, &closure)) {
		if (cc)
			gnm_cmd_context_error_splits_array (cc,
				cmd, &closure.error);
		return TRUE;
	}
	return FALSE;
}

/**
 * sheet_range_splits_region :
 * @sheet: the sheet.
 * @r : The range whose boundaries are checked
 * @ignore : An optional range in which it is ok to have arrays and merges
 * @cc : The context that issued the command
 * @cmd : The translated command name.
 *
 * A utility to see whether moving the range @r will split any arrays
 * or merged regions.
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
 * @ranges : A list of ranges to check.
 * @cc : The context that issued the command
 * @cmd : The translated command name.
 *
 * A utility to see whether moving the any of the ranges @ranges will split any
 * arrays or merged regions.
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
 * sheet_range_contains_region :
 *
 * @sheet : The sheet
 * @r     : the range to check.
 * @cc   : an optional place to report errors.
 * @cmd   :
 *
 * Check to see if the target region @sheet!@r contains any merged regions or
 * arrays.  Report an error to the @cc if it is supplied.
 **/
gboolean
sheet_range_contains_region (Sheet const *sheet, GnmRange const *r,
			     GOCmdContext *cc, char const *cmd)
{
	GSList *merged;

	g_return_val_if_fail (IS_SHEET (sheet), FALSE);

	merged = gnm_sheet_merge_get_overlap (sheet, r);
	if (merged != NULL) {
		if (cc != NULL)
			go_cmd_context_error_invalid (cc, cmd,
				_("cannot operate on merged cells"));
		g_slist_free (merged);
		return TRUE;
	}

	if (sheet_foreach_cell_in_range ((Sheet *)sheet, CELL_ITER_IGNORE_NONEXISTENT,
		r->start.col, r->start.row, r->end.col, r->end.row,
		cb_cell_is_array, NULL)) {
		if (cc != NULL)
			go_cmd_context_error_invalid (cc, cmd,
				_("cannot operate on array formulae"));
		return TRUE;
	}

	return FALSE;
}

/***************************************************************************/

/**
 * sheet_colrow_get_default :
 * @sheet :
 * @is_cols :
 */
ColRowInfo const *
sheet_colrow_get_default (Sheet const *sheet, gboolean is_cols)
{
	g_return_val_if_fail (IS_SHEET (sheet), NULL);

	return is_cols ? &sheet->cols.default_style : &sheet->rows.default_style;
}

/**
 * sheet_col_get:
 *
 * Returns an allocated column:  either an existing one, or NULL
 */
ColRowInfo *
sheet_col_get (Sheet const *sheet, int pos)
{
	ColRowSegment *segment;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos < gnm_sheet_get_max_cols (sheet), NULL);
	g_return_val_if_fail (pos >= 0, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->cols), pos);
	if (segment != NULL)
		return segment->info [COLROW_SUB_INDEX (pos)];
	return NULL;
}

/**
 * sheet_row_get:
 *
 * Returns an allocated row:  either an existing one, or NULL
 */
ColRowInfo *
sheet_row_get (Sheet const *sheet, int pos)
{
	ColRowSegment *segment;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (pos < gnm_sheet_get_max_rows (sheet), NULL);
	g_return_val_if_fail (pos >= 0, NULL);

	segment = COLROW_GET_SEGMENT (&(sheet->rows), pos);
	if (segment != NULL)
		return segment->info [COLROW_SUB_INDEX (pos)];
	return NULL;
}

ColRowInfo *
sheet_colrow_get (Sheet const *sheet, int colrow, gboolean is_cols)
{
	if (is_cols)
		return sheet_col_get (sheet, colrow);
	return sheet_row_get (sheet, colrow);
}

/**
 * sheet_col_fetch:
 *
 * Returns an allocated column:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_col_fetch (Sheet *sheet, int pos)
{
	ColRowInfo *cri = sheet_col_get (sheet, pos);
	if (NULL == cri && NULL != (cri = sheet_col_new (sheet)))
		sheet_col_add (sheet, cri, pos);
	return cri;
}

/**
 * sheet_row_fetch:
 *
 * Returns an allocated row:  either an existing one, or a fresh copy
 */
ColRowInfo *
sheet_row_fetch (Sheet *sheet, int pos)
{
	ColRowInfo *cri = sheet_row_get (sheet, pos);
	if (NULL == cri && NULL != (cri = sheet_row_new (sheet)))
		sheet_row_add (sheet, cri, pos);
	return cri;
}

ColRowInfo *
sheet_colrow_fetch (Sheet *sheet, int colrow, gboolean is_cols)
{
	if (is_cols)
		return sheet_col_fetch (sheet, colrow);
	return sheet_row_fetch (sheet, colrow);
}

ColRowInfo const *
sheet_col_get_info (Sheet const *sheet, int col)
{
	ColRowInfo *ci = sheet_col_get (sheet, col);

	if (ci != NULL)
		return ci;
	return &sheet->cols.default_style;
}

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

/*****************************************************************************/

#define SWAP_INT(a,b) do { int t; t = a; a = b; b = t; } while (0)

/**
 * sheet_foreach_cell_in_range:
 *
 * For each existing cell in the range specified, invoke the
 * callback routine.  If the only_existing flag is passed, then
 * callbacks are only invoked for existing cells.
 *
 * Returns the value returned by the callback, which can be :
 *    non-NULL on error, or VALUE_TERMINATE if some invoked routine requested
 *    to stop (by returning non-NULL).
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
			     int start_col, int start_row,
			     int end_col,   int end_row,
			     CellIterFunc callback, void *closure)
{
	GnmValue *cont;
	GnmCellIter iter;
	gboolean const visiblity_matters = (flags & CELL_ITER_IGNORE_HIDDEN) != 0;
	gboolean const subtotal_magic = (flags & CELL_ITER_IGNORE_SUBTOTAL) != 0;
	gboolean const only_existing = (flags & CELL_ITER_IGNORE_NONEXISTENT) != 0;
	gboolean const ignore_empty = (flags & CELL_ITER_IGNORE_EMPTY) != 0;
	gboolean ignore;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (callback != NULL, NULL);

	iter.pp.sheet = sheet;
	iter.pp.wb = sheet->workbook;
	if (start_col > end_col)
		SWAP_INT (start_col, end_col);

	if (start_row > end_row)
		SWAP_INT (start_row, end_row);

	if (only_existing) {
		if (end_col > sheet->cols.max_used)
			end_col = sheet->cols.max_used;
		if (end_row > sheet->rows.max_used)
			end_row = sheet->rows.max_used;
	}

	for (iter.pp.eval.row = start_row; iter.pp.eval.row <= end_row; ++iter.pp.eval.row) {
		iter.ri = sheet_row_get (iter.pp.sheet, iter.pp.eval.row);

		/* no need to check visiblity, that would require a colinfo to exist */
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

		if (visiblity_matters && !iter.ri->visible)
			continue;
		if (subtotal_magic && iter.ri->in_filter && !iter.ri->visible)
			continue;

		for (iter.pp.eval.col = start_col; iter.pp.eval.col <= end_col; ++iter.pp.eval.col) {
			iter.ci = sheet_col_get (sheet, iter.pp.eval.col);
			if (iter.ci != NULL) {
				if (visiblity_matters && !iter.ci->visible)
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
 * sheet_cell_foreach :
 * @sheet : #Sheet
 * @callback :
 * @data :
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
 * sheet_cells_count :
 * @sheet : #Sheet
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
 * sheet_cells:
 *
 * @sheet     : The sheet to find cells in.
 * @comments  : If true, include cells with only comments also.
 *
 * Collects a GPtrArray of GnmEvalPos pointers for all cells in a sheet.
 * No particular order should be assumed.
 **/
GPtrArray *
sheet_cells (Sheet *sheet, gboolean comments)
{
	GPtrArray *cells = g_ptr_array_new ();

	g_return_val_if_fail (IS_SHEET (sheet), cells);

	sheet_cell_foreach (sheet, (GHFunc)cb_sheet_cells_collect, cells);

	if (comments) {
		GnmRange r;
		GSList *scomments, *ptr;

		range_init_full_sheet (&r);
		scomments = sheet_objects_get (sheet, &r, CELL_COMMENT_TYPE);
		for (ptr = scomments; ptr; ptr = ptr->next) {
			GnmComment *c = ptr->data;
			GnmRange const *loc = sheet_object_get_range (SHEET_OBJECT (c));
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
 * @start_col: starting column
 * @start_row: starting row
 * @end_col:   end column
 * @end_row:   end row
 *
 * Returns TRUE if the specified region of the @sheet does not
 * contain any cells
 */
gboolean
sheet_is_region_empty (Sheet *sheet, GnmRange const *r)
{
	g_return_val_if_fail (IS_SHEET (sheet), TRUE);

	return sheet_foreach_cell_in_range (
		sheet, CELL_ITER_IGNORE_BLANK,
		r->start.col, r->start.row, r->end.col, r->end.row,
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
	/* NOTE :
	 *   fetching the col/row here serve 3 functions
	 *   1) The obvious.  Storing the ptr in the cell.
	 *   2) Expanding col/row.max_used
	 *   3) Creating an entry in the COLROW_SEGMENT.  Lots and lots of
	 *	things use those to help limit iteration
	 *
	 * For now just call col_fetch even though it is not necessary to
	 * ensure that 2,3 still happen.  Alot will need rewriting to avoid
	 * these requirements.
	 **/
	(void) sheet_col_fetch (sheet, cell->pos.col);
	cell->row_info   = sheet_row_fetch (sheet, cell->pos.row);

	if (cell->rendered_value) {
		gnm_rendered_value_destroy (cell->rendered_value);
		cell->rendered_value = NULL;
	}

	g_hash_table_insert (sheet->cell_hash, &cell->pos, cell);

	if (gnm_sheet_merge_is_corner (sheet, &cell->pos))
		cell->base.flags |= GNM_CELL_IS_MERGED;
}

#define USE_CELL_POOL

#ifdef USE_CELL_POOL
/* The pool from which all cells are allocated.  */
static GOMemChunk *cell_pool;
#endif

static GnmCell *
cell_new (void)
{
	GnmCell *cell =
#ifdef USE_CELL_POOL
		go_mem_chunk_alloc0 (cell_pool)
#else
		g_new0 (GnmCell, 1)
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
	g_free (cell);
#endif
}

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
	g_printerr ("Leaking cell %p at %s\n", cell, cell_name (cell));
}
#endif

void
gnm_sheet_cell_shutdown (void)
{
#ifdef USE_CELL_POOL
	go_mem_chunk_foreach_leak (cell_pool, cb_cell_pool_leak, NULL);
	go_mem_chunk_destroy (cell_pool, FALSE);
	cell_pool = NULL;
#endif
}
/****************************************************************************/

/**
 * sheet_cell_create :
 * @sheet : #Sheet
 * @col   :
 * @row   :
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
 * sheet_cell_remove_from_hash :
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
	g_hash_table_remove (sheet->cell_hash, &cell->pos);
	cell->base.flags &= ~(GNM_CELL_IN_SHEET_LIST|GNM_CELL_IS_MERGED);
}

/**
 * sheet_cell_destroy : Remove the cell from the web of depenancies of a
 *        sheet.  Do NOT redraw.
 */
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
		cell_foreach_dep (cell, (DepFunc)dependent_queue_recalc, NULL);

	sheet_cell_remove_from_hash (sheet, cell);
	cell_free (cell);
}

/**
 * sheet_cell_remove : Remove the cell from the web of depenancies of a
 *        sheet.  Do NOT free the cell, optionally redraw it, optionally
 *        queue it for recalc.
 */
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
		sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_NONEXISTENT,
			col, 0, col, gnm_sheet_get_max_rows (sheet) - 1,
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
		sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_NONEXISTENT,
			0, row, gnm_sheet_get_max_cols (sheet) - 1, row,
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
cb_remove_allcells (gpointer ignore0, GnmCell *cell, gpointer ignore1)
{
	cell->base.flags &= ~GNM_CELL_IN_SHEET_LIST;
	cell_free (cell);
}

void
sheet_destroy_contents (Sheet *sheet)
{
	/* Save these because they are going to get zeroed. */
	int const max_col = sheet->cols.max_used;
	int const max_row = sheet->rows.max_used;

	int i;
	gpointer tmp;

	/* By the time we reach here dependencies should have been shut down */
	g_return_if_fail (sheet->deps == NULL);

	/* A simple test to see if this has already been run. */
	if (sheet->hash_merged == NULL)
		return;

	/* These contain SheetObjects, remove them first */
	go_slist_free_custom (sheet->filters, (GFreeFunc)gnm_filter_free);
	sheet->filters = NULL;

	if (sheet->sheet_objects) {
		/* The list is changed as we remove */
		GSList *objs = g_slist_copy (sheet->sheet_objects);
		GSList *ptr;
		for (ptr = objs; ptr != NULL ; ptr = ptr->next) {
			SheetObject *so = SHEET_OBJECT (ptr->data);
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

	go_slist_free_custom (sheet->list_merged, g_free);
	sheet->list_merged = NULL;

	/* Clear the row spans 1st */
	for (i = sheet->rows.max_used; i >= 0 ; --i)
		row_destroy_span (sheet_row_get (sheet, i));

	/* Remove all the cells */
	sheet_cell_foreach (sheet, (GHFunc) &cb_remove_allcells, NULL);
	g_hash_table_destroy (sheet->cell_hash);

	/* Delete in ascending order to avoid decrementing max_used each time */
	for (i = 0; i <= max_col; ++i)
		sheet_col_destroy (sheet, i, FALSE);

	for (i = 0; i <= max_row; ++i)
		sheet_row_destroy (sheet, i, FALSE);

	/* Free segments too */
	for (i = COLROW_SEGMENT_INDEX (max_col); i >= 0 ; --i)
		if ((tmp = g_ptr_array_index (sheet->cols.info, i)) != NULL) {
			g_free (tmp);
			g_ptr_array_index (sheet->cols.info, i) = NULL;
		}
	g_ptr_array_free (sheet->cols.info, TRUE);
	sheet->cols.info = NULL;
	for (i = COLROW_SEGMENT_INDEX (max_row); i >= 0 ; --i)
		if ((tmp = g_ptr_array_index (sheet->rows.info, i)) != NULL) {
			g_free (tmp);
			g_ptr_array_index (sheet->rows.info, i) = NULL;
		}
	g_ptr_array_free (sheet->rows.info, TRUE);
	sheet->rows.info = NULL;
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
		print_info_free (sheet->print_info);
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

	sheet_destroy (sheet);

	solver_param_destroy (sheet->solver_parameters);
	scenarios_free (sheet->scenarios);

	dependents_invalidate_sheet (sheet, TRUE);

	sheet_destroy_contents (sheet);

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

	if (sheet->context) {
		g_object_unref (G_OBJECT (sheet->context));
		sheet->context = NULL;
	}

	(void) g_idle_remove_by_data (sheet);

	g_free (sheet->name_quoted);
	g_free (sheet->name_unquoted);
	g_free (sheet->name_unquoted_collate_key);
	g_free (sheet->name_case_insensitive);
	g_free (sheet->priv);
	g_ptr_array_free (sheet->sheet_views, TRUE);

	G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/*****************************************************************************/

/*
 * cb_empty_cell: A callback for sheet_foreach_cell_in_range
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
 *
 * Clears are region of cells
 *
 * @clear_flags : If this is TRUE then styles are erased.
 *
 * We assemble a list of cells to destroy, since we will be making changes
 * to the structure being manipulated by the sheet_foreach_cell_in_range routine
 */
void
sheet_clear_region (Sheet *sheet,
		    int start_col, int start_row,
		    int end_col, int end_row,
		    int clear_flags,
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
		sheet_objects_clear (sheet, &r, CELL_COMMENT_TYPE, NULL);

	/* TODO : how to handle objects ? */
	if (clear_flags & CLEAR_VALUES) {
		/* Remove or empty the cells depending on
		 * whether or not there are comments
		 */
		sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_NONEXISTENT,
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
			gnm_sheet_merge_remove (sheet, ptr->data, cc);
		g_slist_free (merged);
	}

	if (clear_flags & CLEAR_RECALC_DEPS)
		sheet_region_queue_recalc (sheet, &r);

	/* Always redraw */
	sheet_redraw_all (sheet, FALSE);
}

/*****************************************************************************/

void
sheet_mark_dirty (Sheet *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	if (sheet->workbook)
		go_doc_set_dirty (GO_DOC (sheet->workbook), TRUE);
}

/****************************************************************************/

/*
 * Callback for sheet_foreach_cell_in_range to remove a cell from the sheet
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

/*
 * 1) collects the cells in the source rows/cols
 * 2) Moves the headers to their new location
 * 3) replaces the cells in their new location
 */
static void
colrow_move (Sheet *sheet,
	     int start_col, int start_row,
	     int end_col, int end_row,
	     ColRowCollection *info_collection,
	     int const old_pos, int const new_pos)
{
	gboolean const is_cols = (info_collection == &sheet->cols);
	ColRowSegment *segment = COLROW_GET_SEGMENT (info_collection, old_pos);
	ColRowInfo *info = (segment != NULL) ?
		segment->info [COLROW_SUB_INDEX (old_pos)] : NULL;

	GList *cells = NULL;
	GnmCell  *cell;

	g_return_if_fail (old_pos >= 0);
	g_return_if_fail (new_pos >= 0);

	if (info == NULL)
		return;

	/* Collect the cells and unlinks them if necessary */
	sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_NONEXISTENT,
		start_col, start_row, end_col, end_row,
		&cb_collect_cell, &cells);

	/* Reverse the list so that we start at the top left
	 * which makes things easier for arrays.
	 */
	cells = g_list_reverse (cells);

	/* Update the position */
	segment->info [COLROW_SUB_INDEX (old_pos)] = NULL;
	/* TODO : Figure out a way to merge these functions */
	if (is_cols)
		sheet_col_add (sheet, info, new_pos);
	else
		sheet_row_add (sheet, info, new_pos);

	/* Insert the cells back */
	for (; cells != NULL ; cells = g_list_remove (cells, cell)) {
		cell = cells->data;

		if (is_cols)
			cell->pos.col = new_pos;
		else
			cell->pos.row = new_pos;

		sheet_cell_add_to_hash (sheet, cell);
		if (gnm_cell_has_expr (cell))
			dependent_link (GNM_CELL_TO_DEP (cell));
	}
	sheet_mark_dirty (sheet);
}

static void
sheet_colrow_insdel_finish (GnmExprRelocateInfo const *rinfo, gboolean is_cols,
			    int pos, GOUndo **pundo)
{
	Sheet *sheet = rinfo->origin_sheet;

	sheet_objects_relocate (rinfo, FALSE, pundo);
	gnm_sheet_merge_relocate (rinfo);

	/* Notify sheet of pending updates */
	sheet->priv->recompute_visibility = TRUE;
	sheet_flag_recompute_spans (sheet);
	sheet_flag_status_update_range (sheet, &rinfo->origin);
	if (is_cols)
		sheet->priv->reposition_objects.col = pos;
	else
		sheet->priv->reposition_objects.row = pos;
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
sheet_colrow_insert_finish (GnmExprRelocateInfo const *rinfo, gboolean is_cols,
			    int pos, int count, GOUndo **pundo)
{
	Sheet *sheet = rinfo->origin_sheet;

	sheet_style_insert_colrow (rinfo);
	sheet_colrow_insdel_finish (rinfo, is_cols, pos, pundo);
	sheet_colrow_set_collapse (sheet, is_cols, pos);
	sheet_colrow_set_collapse (sheet, is_cols, pos + count);
	sheet_colrow_set_collapse (sheet, is_cols, colrow_max (is_cols, sheet));
	gnm_sheet_filter_insdel_colrow (sheet, is_cols, TRUE, pos, count);

	/* WARNING WARNING WARNING
	 * This is bad practice and should not really be here.
	 * However, we need to ensure that update is run before
	 * sv_panes_insdel_colrow plays with frozen panes, updating those can
	 * trigger redraws before sheet_update has been called. */
	sheet_update (sheet);

	SHEET_FOREACH_VIEW (sheet, sv,
		sv_panes_insdel_colrow (sv, is_cols, TRUE, pos, count););
}

static void
sheet_colrow_delete_finish (GnmExprRelocateInfo const *rinfo, gboolean is_cols,
			    int pos, int count, GOUndo **pundo)
{
	Sheet *sheet = rinfo->origin_sheet;
	int end = colrow_max (is_cols, sheet) - count;

	sheet_style_relocate (rinfo);
	sheet_colrow_insdel_finish (rinfo, is_cols, pos, pundo);
	sheet_colrow_set_collapse (sheet, is_cols, pos);
	sheet_colrow_set_collapse (sheet, is_cols, end);
	gnm_sheet_filter_insdel_colrow (sheet, is_cols, FALSE, pos, count);

	/* WARNING WARNING WARNING
	 * This is bad practice and should not really be here.
	 * However, we need to ensure that update is run before
	 * sv_panes_insdel_colrow plays with frozen panes, updating those can
	 * trigger redraws before sheet_update has been called. */
	sheet_update (sheet);

	SHEET_FOREACH_VIEW (sheet, sv,
		sv_panes_insdel_colrow (sv, is_cols, FALSE, pos, count););
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
	int idx;
	int count;
	ColRowStateList *states;
	int state_start;
} ColRowInsDelData;

static void
cb_undo_insdel (ColRowInsDelData *data)
{
	data->func (data->sheet, data->idx, data->count, NULL, NULL);
	colrow_set_states (data->sheet, data->is_cols,
			   data->state_start, data->states);
}

static void
cb_undo_insdel_free (ColRowInsDelData *data)
{
	colrow_state_list_destroy (data->states);
	g_free (data);
}

static void
add_undo_op (GOUndo **pundo, gboolean is_cols,
	     ColRowInsDelFunc func, Sheet *sheet, int idx, int count,
	     ColRowStateList *states, int state_start)
{
	ColRowInsDelData *data;
	GOUndo *u;

	if (!pundo)
		return;

	data = g_new (ColRowInsDelData, 1);
	data->func = func;
	data->sheet = sheet;
	data->is_cols = is_cols;
	data->idx = idx;
	data->count = count;
	data->states = states;
	data->state_start = state_start;

	u = go_undo_unary_new (data, (GOUndoUnaryFunc)cb_undo_insdel,
			       (GFreeFunc)cb_undo_insdel_free);

	combine_undo (pundo, u);
}


/**
 * sheet_insert_cols:
 * @sheet  : #Sheet
 * @col    : At which position we want to insert
 * @count  : The number of columns to be inserted
 * @pundo  : undo closure, optionally NULL; caller releases result
 * @cc     :
 **/
gboolean
sheet_insert_cols (Sheet *sheet, int col, int count,
		   GOUndo **pundo, GOCmdContext *cc)
{
	GnmExprRelocateInfo reloc_info;
	GnmRange region;
	int i;
	ColRowStateList *states = NULL;
	int first = gnm_sheet_get_max_cols (sheet) - count;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count > 0, TRUE);

	if (pundo) {
		int last = first + (count - 1);
		GnmRange r;
		range_init_cols (&r, first, last);
		*pundo = clipboard_copy_range_undo (sheet, &r);
		states = colrow_get_states (sheet, TRUE, first, last);
	}

	/* 0. Check displaced region and ensure arrays aren't divided. */
	if (count < gnm_sheet_get_max_cols (sheet)) {
		range_init (&region, col, 0, gnm_sheet_get_max_cols (sheet) - 1-count, gnm_sheet_get_max_rows (sheet) - 1);
		if (sheet_range_splits_array (sheet, &region, NULL,
					      cc, _("Insert Columns")))
			return TRUE;
	}

	/* 1. Delete all columns (and their cells) that will fall off the end */
	for (i = sheet->cols.max_used; i >= gnm_sheet_get_max_cols (sheet) - count ; --i)
		sheet_col_destroy (sheet, i, TRUE);

	/* 2. Fix references to and from the cells which are moving */
	reloc_info.reloc_type = GNM_EXPR_RELOCATE_COLS;
	reloc_info.origin.start.col = col;
	reloc_info.origin.start.row = 0;
	reloc_info.origin.end.col = gnm_sheet_get_max_cols (sheet) - 1;
	reloc_info.origin.end.row = gnm_sheet_get_max_rows (sheet) - 1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = count;
	reloc_info.row_offset = 0;
	parse_pos_init_sheet (&reloc_info.pos, sheet);

	combine_undo (pundo, dependents_relocate (&reloc_info));

	/* 3. Move the columns to their new location (from right to left) */
	for (i = sheet->cols.max_used; i >= col ; --i)
		colrow_move (sheet, i, 0, i, gnm_sheet_get_max_rows (sheet) - 1,
			     &sheet->cols, i, i + count);

	solver_insert_cols (sheet, col, count);
	scenarios_insert_cols (sheet->scenarios, col, count);
	sheet_colrow_insert_finish (&reloc_info, TRUE, col, count, pundo);

	add_undo_op (pundo, TRUE, sheet_delete_cols,
		     sheet, col, count,
		     states, first);

	return FALSE;
}

/*
 * sheet_delete_cols
 * @sheet   The sheet
 * @col     At which position we want to start deleting columns
 * @count   The number of columns to be deleted
 * @pundo  : undo closure, optionally NULL; caller releases result
 * @cc : The command context
 */
gboolean
sheet_delete_cols (Sheet *sheet, int col, int count,
		   GOUndo **pundo, GOCmdContext *cc)
{
	GnmExprRelocateInfo reloc_info;
	int i;
	ColRowStateList *states = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count > 0, TRUE);

	if (pundo) {
		int last = col + (count - 1);
		GnmRange r;
		range_init_cols (&r, col, last);
		*pundo = clipboard_copy_range_undo (sheet, &r);
		states = colrow_get_states (sheet, TRUE, col, last);
	}

	reloc_info.reloc_type = GNM_EXPR_RELOCATE_COLS;
	reloc_info.origin.start.col = col;
	reloc_info.origin.start.row = 0;
	reloc_info.origin.end.col = col + count - 1;
	reloc_info.origin.end.row = gnm_sheet_get_max_rows (sheet) - 1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = gnm_sheet_get_max_cols (sheet); /* force invalidation */
	reloc_info.row_offset = 0;
	parse_pos_init_sheet (&reloc_info.pos, sheet);

	/* 0. Walk cells in deleted cols and ensure arrays aren't divided. */
	if (sheet_range_splits_array (sheet, &reloc_info.origin, NULL,
				      cc, _("Delete Columns")))
		return TRUE;

	/* 1. Delete the columns (and their cells) */
	for (i = col + count ; --i >= col; )
		sheet_col_destroy (sheet, i, TRUE);
	sheet_objects_clear (sheet, &reloc_info.origin, G_TYPE_NONE, pundo);

	/*
	 * 1.5 sheet_colrow_delete_finish will flag the remains as changing,
	 * but we need to mark the cleared area
	 */
	sheet_flag_status_update_range (sheet, &reloc_info.origin);

	/* 2. Invalidate references to the cells in the deleted columns */
	combine_undo (pundo, dependents_relocate (&reloc_info));

	/* 3. Fix references to and from the cells which are moving */
	reloc_info.origin.start.col = col + count;
	reloc_info.origin.end.col = gnm_sheet_get_max_cols (sheet) - 1;
	reloc_info.col_offset = -count;
	reloc_info.row_offset = 0;
	combine_undo (pundo, dependents_relocate (&reloc_info));

	/* 4. Move the columns to their new location (from left to right) */
	for (i = col + count ; i <= sheet->cols.max_used; ++i)
		colrow_move (sheet, i, 0, i, gnm_sheet_get_max_rows (sheet) - 1,
			     &sheet->cols, i, i - count);

	solver_delete_cols (sheet, col, count);
	scenarios_delete_cols (sheet->scenarios, col, count);
	sheet_colrow_delete_finish (&reloc_info, TRUE, col, count, pundo);

	add_undo_op (pundo, TRUE, sheet_insert_cols,
		     sheet, col, count,
		     states, col);

	return FALSE;
}

/**
 * sheet_insert_rows:
 * @sheet  : The sheet
 * @row    : At which position we want to insert
 * @count  : The number of rows to be inserted
 * @pundo  : undo closure, optionally NULL; caller releases result
 * @cc : The command context
 */
gboolean
sheet_insert_rows (Sheet *sheet, int row, int count,
		   GOUndo **pundo, GOCmdContext *cc)
{
	GnmExprRelocateInfo reloc_info;
	GnmRange region;
	int i;
	ColRowStateList *states = NULL;
	int first = gnm_sheet_get_max_rows (sheet) - count;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count > 0, TRUE);

	if (pundo) {
		int last = first + (count - 1);
		GnmRange r;
		range_init_rows (&r, first, last);
		*pundo = clipboard_copy_range_undo (sheet, &r);
		states = colrow_get_states (sheet, FALSE, first, last);
	}

	/* 0. Check displaced region and ensure arrays aren't divided. */
	if (count < gnm_sheet_get_max_rows (sheet)) {
		range_init (&region, 0, row, gnm_sheet_get_max_cols (sheet) - 1, gnm_sheet_get_max_rows (sheet) - 1-count);
		if (sheet_range_splits_array (sheet, &region, NULL,
					      cc, _("Insert Rows")))
			return TRUE;
	}

	/* 1. Delete all rows (and their cells) that will fall off the end */
	for (i = sheet->rows.max_used; i >= gnm_sheet_get_max_rows (sheet) - count ; --i)
		sheet_row_destroy (sheet, i, TRUE);

	/* 2. Fix references to and from the cells which are moving */
	reloc_info.reloc_type = GNM_EXPR_RELOCATE_ROWS;
	reloc_info.origin.start.col = 0;
	reloc_info.origin.start.row = row;
	reloc_info.origin.end.col = gnm_sheet_get_max_cols (sheet) - 1;
	reloc_info.origin.end.row = gnm_sheet_get_max_rows (sheet) - 1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = 0;
	reloc_info.row_offset = count;
	parse_pos_init_sheet (&reloc_info.pos, sheet);

	combine_undo (pundo, dependents_relocate (&reloc_info));

	/* 3. Move the rows to their new location (from last to first) */
	for (i = sheet->rows.max_used; i >= row ; --i)
		colrow_move (sheet, 0, i, gnm_sheet_get_max_cols (sheet) - 1, i,
			     &sheet->rows, i, i + count);

	solver_insert_rows (sheet, row, count);
	scenarios_insert_rows (sheet->scenarios, row, count);
	sheet_colrow_insert_finish (&reloc_info, FALSE, row, count, pundo);

	add_undo_op (pundo, FALSE, sheet_delete_rows,
		     sheet, row, count,
		     states, first);

	return FALSE;
}

/*
 * sheet_delete_rows
 * @sheet  : The sheet
 * @row    : At which position we want to start deleting rows
 * @count  : The number of rows to be deleted
 * @pundo  : undo closure, optionally NULL; caller releases result
 * @cc : The command context
 */
gboolean
sheet_delete_rows (Sheet *sheet, int row, int count,
		   GOUndo **pundo, GOCmdContext *cc)
{
	GnmExprRelocateInfo reloc_info;
	int i;
	ColRowStateList *states = NULL;

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (count > 0, TRUE);

	if (pundo) {
		int last = row + (count - 1);
		GnmRange r;
		range_init_rows (&r, row, last);
		*pundo = clipboard_copy_range_undo (sheet, &r);
		states = colrow_get_states (sheet, FALSE, row, last);
	}

	reloc_info.reloc_type = GNM_EXPR_RELOCATE_ROWS;
	reloc_info.origin.start.col = 0;
	reloc_info.origin.start.row = row;
	reloc_info.origin.end.col = gnm_sheet_get_max_cols (sheet) - 1;
	reloc_info.origin.end.row = row + count - 1;
	reloc_info.origin_sheet = reloc_info.target_sheet = sheet;
	reloc_info.col_offset = 0;
	reloc_info.row_offset = gnm_sheet_get_max_rows (sheet); /* force invalidation */
	parse_pos_init_sheet (&reloc_info.pos, sheet);

	/* 0. Walk cells in deleted rows and ensure arrays aren't divided. */
	if (sheet_range_splits_array (sheet, &reloc_info.origin, NULL,
				      cc, _("Delete Rows")))
		return TRUE;

	/* 1. Delete the rows (and their content) */
	for (i = row + count ; --i >= row; )
		sheet_row_destroy (sheet, i, TRUE);
	sheet_objects_clear (sheet, &reloc_info.origin, G_TYPE_NONE, pundo);

	/*
	 * 1.5 sheet_colrow_delete_finish will flag the remains as changing,
	 * but we need to mark the cleared area
	 */
	sheet_flag_status_update_range (sheet, &reloc_info.origin);

	/* 2. Invalidate references to the cells in the deleted rows */
	combine_undo (pundo, dependents_relocate (&reloc_info));

	/* 3. Fix references to and from the cells which are moving */
	reloc_info.origin.start.row = row + count;
	reloc_info.origin.end.row = gnm_sheet_get_max_rows (sheet) - 1;
	reloc_info.col_offset = 0;
	reloc_info.row_offset = -count;
	combine_undo (pundo, dependents_relocate (&reloc_info));

	/* 4. Move the rows to their new location (from first to last) */
	for (i = row + count ; i <= sheet->rows.max_used; ++i)
		colrow_move (sheet, 0, i, gnm_sheet_get_max_cols (sheet) - 1, i,
			     &sheet->rows, i, i - count);

	solver_delete_rows (sheet, row, count);
	scenarios_delete_rows (sheet->scenarios, row, count);
	sheet_colrow_delete_finish (&reloc_info, FALSE, row, count, pundo);

	add_undo_op (pundo, FALSE, sheet_insert_rows,
		     sheet, row, count,
		     states, row);

	return FALSE;
}

/**
 * sheet_move_range :
 * @cc :
 * @rinfo :
 * @pundo : optionally NULL, caller releases result
 *
 * Move a range as specified in @rinfo report warnings to @cc.
 * if @pundo is non NULL, invalidate references to the
 * target region that are being cleared, and store the undo information
 * in @pundo.  If it is NULL do NOT INVALIDATE.
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
	out_of_range = range_translate (&dst,
		rinfo->col_offset, rinfo->row_offset);

	/* Redraw the src region in case anything was spanning */
	sheet_redraw_range (rinfo->origin_sheet, &rinfo->origin);

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
				invalid = g_slist_append (NULL, range_dup (&dst));

			reloc_info.origin_sheet = reloc_info.target_sheet = rinfo->target_sheet;;

			/* send to infinity to invalidate, but try to assist
			 * the relocation heuristics only move in 1
			 * dimension if possible to give us a chance to be
			 * smart about partial invalidations */
			reloc_info.col_offset = gnm_sheet_get_max_cols (rinfo->target_sheet);
			reloc_info.row_offset = gnm_sheet_get_max_rows (rinfo->target_sheet);
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
	sheet_foreach_cell_in_range (rinfo->origin_sheet, CELL_ITER_IGNORE_NONEXISTENT,
		rinfo->origin.start.col, rinfo->origin.start.row,
		rinfo->origin.end.col, rinfo->origin.end.row,
		&cb_collect_cell, &cells);

	/* Reverse list so that we start at the top left (simplifies arrays). */
	cells = g_list_reverse (cells);

	/* 4. Clear the target area & invalidate references to it */
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
	gnm_sheet_merge_relocate (rinfo);

	/* 8. Notify sheet of pending update */
	sheet_flag_recompute_spans (rinfo->origin_sheet);
	sheet_flag_status_update_range (rinfo->origin_sheet, &rinfo->origin);

	/* 9. Update the data structures of the tools */
	if (rinfo->origin_sheet == rinfo->target_sheet)
		scenarios_move_range (rinfo->origin_sheet->scenarios,
				     &rinfo->origin, rinfo->col_offset,
				     rinfo->row_offset);
}

static void
sheet_colrow_default_calc (Sheet *sheet, double units,
			   gboolean is_cols, gboolean is_pts)
{
	ColRowInfo *cri = is_cols
		? &sheet->cols.default_style
		: &sheet->rows.default_style;

	g_return_if_fail (units > 0.);

	cri->is_default	= TRUE;
	cri->hard_size	= FALSE;
	cri->visible	= TRUE;
	cri->spans	= NULL;
	if (is_pts) {
		cri->size_pts = units;
		colrow_compute_pixels_from_pts (cri, sheet, is_cols);
	} else {
		cri->size_pixels = units;
		colrow_compute_pts_from_pixels (cri, sheet, is_cols);
	}
}

/************************************************************************/
/* Col width support routines.
 */

/**
 * sheet_col_get_distance_pts:
 *
 * Return the number of points between from_col to to_col
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

	/* Do not use colrow_foreach, it ignores empties */
	dflt =  sheet->cols.default_style.size_pts;
	for (i = from ; i < to ; ++i) {
		if (NULL == (ci = sheet_col_get (sheet, i)))
			pts += dflt;
		else if (ci->visible)
			pts += ci->size_pts;
	}

	return pts * sign;
}

/**
 * sheet_col_get_distance_pixels:
 *
 * Return the number of pixels between from_col to to_col
 * measured from the upper left corner.
 */
int
sheet_col_get_distance_pixels (Sheet const *sheet, int from, int to)
{
	ColRowInfo const *ci;
	int dflt, pixels = 0, sign = 1;
	int i;

	g_return_val_if_fail (IS_SHEET (sheet), 1.);

	if (from > to) {
		int const tmp = to;
		to = from;
		from = tmp;
		sign = -1;
	}

	g_return_val_if_fail (from >= 0, 1);
	g_return_val_if_fail (to <= gnm_sheet_get_max_cols (sheet), 1);

	/* Do not use colrow_foreach, it ignores empties */
	dflt =  sheet->cols.default_style.size_pts;
	for (i = from ; i < to ; ++i) {
		if (NULL == (ci = sheet_col_get (sheet, i)))
			pixels += dflt;
		else if (ci->visible)
			pixels += ci->size_pixels;
	}

	return pixels * sign;
}

/**
 * sheet_col_set_size_pts:
 * @sheet:	 The sheet
 * @col:	 The col
 * @widtht_pts:	 The desired widtht in pts
 * @set_by_user: TRUE if this was done by a user (ie, user manually
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
	colrow_compute_pixels_from_pts (ci, sheet, TRUE);

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
	colrow_compute_pts_from_pixels (ci, sheet, TRUE);

	sheet->priv->recompute_visibility = TRUE;
	sheet_flag_recompute_spans (sheet);
	if (sheet->priv->reposition_objects.col > col)
		sheet->priv->reposition_objects.col = col;
}

/**
 * sheet_col_get_default_size_pts:
 *
 * Return the default number of pts in a column, including margins.
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
/* Row height support routines
 */

/**
 * sheet_row_get_distance_pts:
 *
 * Return the number of points between from_row to to_row
 * measured from the upper left corner.
 */
double
sheet_row_get_distance_pts (Sheet const *sheet, int from, int to)
{
	ColRowSegment const *segment;
	ColRowInfo const *ri;
	float const default_size = sheet->rows.default_style.size_pts;
	float pts = 0., sign = 1.;
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

	/* Do not use colrow_foreach, it ignores empties.
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
 * sheet_row_set_size_pts:
 * @sheet:	 The sheet
 * @row:	 The row
 * @height_pts:	 The desired height in pts
 * @set_by_user: TRUE if this was done by a user (ie, user manually
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
	colrow_compute_pixels_from_pts (ri, sheet, FALSE);

	sheet->priv->recompute_visibility = TRUE;
	if (sheet->priv->reposition_objects.row > row)
		sheet->priv->reposition_objects.row = row;
}

/**
 * sheet_row_set_size_pixels:
 * @sheet:	 The sheet
 * @row:	 The row
 * @height:	 The desired height
 * @set_by_user: TRUE if this was done by a user (ie, user manually
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
	colrow_compute_pts_from_pixels (ri, sheet, FALSE);

	sheet->priv->recompute_visibility = TRUE;
	if (sheet->priv->reposition_objects.row > row)
		sheet->priv->reposition_objects.row = row;
}

/**
 * sheet_row_get_default_size_pts:
 *
 * Return the defaul number of units in a row, including margins.
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
	sheet_colrow_default_calc (sheet, height_pts, FALSE, TRUE);
	sheet->priv->recompute_visibility = TRUE;
	sheet->priv->reposition_objects.row = 0;
}
void
sheet_row_set_default_size_pixels (Sheet *sheet, int height_pixels)
{
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
	ColRowInfo *new_colrow = sheet_colrow_fetch (closure->sheet,
		iter->pos, closure->is_column);
	colrow_copy (new_colrow, iter->cri);
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
	colrow_foreach (&src->cols, 0, max_col - 1,
			&sheet_clone_colrow_info_item, &closure);
	closure.is_column = FALSE;
	colrow_foreach (&src->rows, 0, max_row - 1,
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

	styles = sheet_style_get_list (src, range_init_full_sheet (&r));
	sheet_style_set_list (dst, &corner, FALSE, styles);
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
		char const *name = src_nexpr->name->str;
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
		char const *name = src_nexpr->name->str;
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
cb_sheet_cell_copy (gpointer unused, gpointer key, gpointer new_sheet_param)
{
	GnmCell const *cell = key;
	Sheet *dst = new_sheet_param;
	Sheet *src;
	GnmExprArrayCorner const *array;

	g_return_if_fail (dst != NULL);
	g_return_if_fail (cell != NULL);

	src = cell->base.sheet;
	array = gnm_cell_is_array_corner (cell);

	if (array) {
		unsigned int i, j;
		GnmExprTop const *texpr =
			gnm_expr_top_relocate_sheet (cell->base.texpr, src, dst);

		gnm_cell_set_array_formula (dst,
			cell->pos.col, cell->pos.row,
			cell->pos.col + array->cols-1,
			cell->pos.row + array->rows-1,
			texpr);

		for (i = 0; i < array->cols ; i++)
			for (j = 0; j < array->rows ; j++)
				if (i != 0 || j != 0) {
					GnmCell const *in = sheet_cell_fetch (src,
						cell->pos.col + i,
						cell->pos.row + j);
					GnmCell *out = sheet_cell_fetch (dst,
						cell->pos.col + i,
						cell->pos.row + j);
					gnm_cell_set_value (out, in->value);
				}
	} else {
		GnmCell *new_cell = sheet_cell_create (dst, cell->pos.col, cell->pos.row);
		GnmValue *value = value_dup (cell->value);
		if (gnm_cell_has_expr (cell)) {
			GnmExprTop const *texpr =
				gnm_expr_top_relocate_sheet (cell->base.texpr, src, dst);
			gnm_cell_set_expr_and_value (new_cell, texpr, value, TRUE);
			gnm_expr_top_unref (texpr);
		} else
			gnm_cell_set_value (new_cell, value);
	}
}

static void
sheet_dup_cells (Sheet const *src, Sheet *dst)
{
	sheet_cell_foreach (src, &cb_sheet_cell_copy, dst);
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
 * sheet_dup :
 * @src : #Sheet
 *
 * Create a new Sheet and return it.
 **/
Sheet *
sheet_dup (Sheet const *src)
{
	Workbook *wb;
	Sheet *dst;
	char *name;

	g_return_val_if_fail (IS_SHEET (src), NULL);
	g_return_val_if_fail (src->workbook !=NULL, NULL);

	wb = src->workbook;
	name = workbook_sheet_get_free_name (wb, src->name_unquoted,
					     TRUE, TRUE);
	dst = sheet_new (wb, name);
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
		"display-outlines",	   !src->display_outlines,
		"display-outlines-below",   src->outline_symbols_below,
		"display-outlines-right",   src->outline_symbols_right,
		"conventions",		    src->convs,
		"tab-foreground",	    src->tab_text_color,
		"tab-background",	    src->tab_color,
		NULL);

	print_info_free (dst->print_info);
	dst->print_info = print_info_dup (src->print_info);

	sheet_dup_styles         (src, dst);
	sheet_dup_merged_regions (src, dst);
	sheet_dup_colrows	 (src, dst);
	sheet_dup_names		 (src, dst);
	sheet_dup_cells		 (src, dst);
	sheet_objects_dup	 (src, dst, NULL);
	sheet_dup_filters	 (src, dst); /* must be after objects */

#warning selection is in view
#warning freeze/thaw is in view

	solver_param_destroy (dst->solver_parameters);
	dst->solver_parameters = solver_lp_copy (src->solver_parameters, dst);

	dst->scenarios = scenarios_dup (src->scenarios, dst);

	sheet_mark_dirty (dst);
	sheet_redraw_all (dst, TRUE);

	return dst;
}

/**
 * sheet_set_outline_direction :
 * @sheet   : the sheet
 * @is_cols : use cols or rows
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
 * sheet_get_view :
 * @sheet :
 * @wbv   :
 *
 * Find the SheetView corresponding to the supplied @wbv.
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

static gboolean
cb_queue_respan (GnmColRowIter const *iter, void *user_data)
{
	((ColRowInfo *)(iter->cri))->needs_respan = TRUE;
	return FALSE;
}

/**
 * sheet_queue_respan *
 * @sheet :
 * @start_row :
 * @end_row :
 *
 * queues a span generation for the selected rows.
 * the caller is responsible for queuing a redraw
 **/
void
sheet_queue_respan (Sheet const *sheet, int start_row, int end_row)
{
	colrow_foreach (&sheet->rows, start_row, end_row,
		cb_queue_respan, NULL);
}

/**
 * sheet_get_comment :
 * @sheet : #Sheet const *
 * @pos   : #GnmCellPos const *
 *
 * If there is a cell comment at @pos in @sheet return it.
 *
 * Caller does get a reference to the object if it exists.
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
		comments = sheet_objects_get (sheet, mr, CELL_COMMENT_TYPE);
	else {
		r.start = r.end = *pos;
		comments = sheet_objects_get (sheet, &r, CELL_COMMENT_TYPE);
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
 * Return value: TRUE if the range was totally empty.
 **/
gboolean
sheet_range_trim (Sheet const *sheet, GnmRange *r,
		  gboolean cols, gboolean rows)
{
	GnmCellPos extent = { -1, -1 };

	g_return_val_if_fail (IS_SHEET (sheet), TRUE);
	g_return_val_if_fail (r != NULL, TRUE);

	sheet_foreach_cell_in_range (
		(Sheet *)sheet, CELL_ITER_IGNORE_BLANK,
		r->start.col, r->start.row, r->end.col, r->end.row,
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
 * Returns : TRUE if @src seems to have a heading
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
		} else if (a->value->type != b->value->type)
			return TRUE;

		/* Look for style differences */
		if (!ignore_styles &&
		    !gnm_style_equal_header (gnm_cell_get_style (a),
					     gnm_cell_get_style (b), top))
			return TRUE;
	}

	return FALSE;
}

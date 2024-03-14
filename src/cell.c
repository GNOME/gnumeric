/*
 * cell.c: Cell content and simple management.
 *
 * Author:
 *    Jody Goldberg 2000-2006 (jody@gnome.org)
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 *    Copyright (C) 2000-2009 Morten Welinder (terra@gnome.org)
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <cell.h>

#include <gutils.h>
#include <workbook.h>
#include <sheet.h>
#include <expr.h>
#include <rendered-value.h>
#include <value.h>
#include <style.h>
#include <ranges.h>
#include <gnm-format.h>
#include <number-match.h>
#include <sheet-style.h>
#include <parse-util.h>
#include <style-conditions.h>

#include <goffice/goffice.h>

/**
 * gnm_cell_cleanout: (skip)
 * @cell: The #GnmCell
 *
 *      Empty a cell's
 *		- value.
 *		- rendered_value.
 *		- expression.
 *		- parse format.
 *
 *      Clears the flags to
 *		- not queued for recalc.
 *		- has no expression.
 *
 *      Does NOT change
 *		- Comments.
 *		- Spans.
 *		- unqueue a previously queued recalc.
 *		- Mark sheet as dirty.
 */
void
gnm_cell_cleanout (GnmCell *cell)
{
	g_return_if_fail (cell != NULL);

	/* A cell can have either an expression or entered text */
	if (gnm_cell_has_expr (cell)) {
		/* Clipboard cells, e.g., are not attached to a sheet.  */
		if (gnm_cell_expr_is_linked (cell))
			dependent_unlink (GNM_CELL_TO_DEP (cell));
		gnm_expr_top_unref (cell->base.texpr);
		cell->base.texpr = NULL;
	}

	value_release (cell->value);
	cell->value = NULL;

	gnm_cell_unrender (cell);

	sheet_cell_queue_respan (cell);
}

/****************************************************************************/

/**
 * gnm_cell_set_text: (skip)
 * @cell: #GnmCell
 * @text: New contents of cell
 *
 * Parses the supplied text for storage as a value or
 *		expression.  It marks the sheet as dirty.
 *
 * If the text is an expression it IS queued for recalc.
 *        the format preferred by the expression is stored for later use.
 * If the text is a value it is rendered and spans are NOT calculated.
 *        the format that matched the text is stored for later use.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *
 * NOTE : This DOES check for array partitioning.
 */
void
gnm_cell_set_text (GnmCell *cell, char const *text)
{
	GnmExprTop const *texpr;
	GnmValue      *val;
	GnmParsePos    pos;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (!gnm_cell_is_nonsingleton_array (cell));

	parse_text_value_or_expr (parse_pos_init_cell (&pos, cell),
		text, &val, &texpr);

	if (val != NULL) {	/* String was a value */
		gnm_cell_cleanout (cell);
		cell->value = val;
	} else {		/* String was an expression */
		gnm_cell_set_expr (cell, texpr);
		gnm_expr_top_unref (texpr);
	}
}

/**
 * gnm_cell_assign_value: (skip)
 * @cell: #GnmCell
 * @v: (transfer full): #GnmValue
 *
 * Stores, without copying, the supplied value.
 *    no changes are made to the expression or entered text.  This
 *    is for use by routines that wish to store values directly such
 *    as expression calculation or import for array formulas.
 *
 * WARNING : This is an internal routine that does not
 *	- queue redraws,
 *	- auto-resize
 *	- calculate spans
 *	- does not render.
 *	- mark anything as dirty.
 *
 * NOTE : This DOES NOT check for array partitioning.
 */
void
gnm_cell_assign_value (GnmCell *cell, GnmValue *v)
{
	g_return_if_fail (cell);
	g_return_if_fail (v);

	value_release (cell->value);
	cell->value = v;
}

/**
 * gnm_cell_set_value: (skip)
 * @c: #GnmCell
 * @v: (transfer full): #GnmValue
 *
 * WARNING : This is an internal routine that does not
 *	- queue redraws,
 *	- auto-resize
 *	- calculate spans
 *	- does not render.
 *
 * NOTE : This DOES check for array partitioning.
 **/
void
gnm_cell_set_value (GnmCell *cell, GnmValue *v)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (v != NULL);
	if (gnm_cell_is_nonsingleton_array (cell)) {
		value_release (v);
		g_return_if_fail (!gnm_cell_is_nonsingleton_array (cell));
	}

	gnm_cell_cleanout (cell);
	cell->value = v;
}

/**
 * gnm_cell_set_expr_and_value: (skip)
 * @cell: The #GnmCell
 * @texpr: The #GnmExprTop
 * @v: (transfer full): The #GnmValue.
 * @link_expr: If %TRUE, link the expression.
 *
 * Stores, without copying, the supplied value, and
 *        references the supplied expression and links it into the expression
 *        list.  It marks the sheet as dirty. It is intended for use by import
 *        routines or operations that do bulk assignment.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, does not calculate spans, and does
 *           not render the value.
 *
 * NOTE : This DOES check for array partitioning.
 */
void
gnm_cell_set_expr_and_value (GnmCell *cell, GnmExprTop const *texpr,
			     GnmValue *v, gboolean link_expr)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (texpr != NULL);
	if (gnm_cell_is_nonsingleton_array (cell)) {
		value_release (v);
		g_return_if_fail (!gnm_cell_is_nonsingleton_array (cell));
	}

	/* Repeat after me.  Ref before unref. */
	gnm_expr_top_ref (texpr);
	gnm_cell_cleanout (cell);

	cell->base.flags |= GNM_CELL_HAS_NEW_EXPR;
	cell->base.texpr = texpr;
	cell->value = v;
	if (link_expr)
		dependent_link (GNM_CELL_TO_DEP (cell));
}

/**
 * cell_set_expr_internal: (skip)
 * @cell: the cell to set the expr for
 * @expr: an expression
 *
 * A private internal utility to store an expression.
 * Does NOT
 *	- check for array subdivision
 *	- queue recalcs.
 *	- render value, calc dimension, compute spans
 *	- link the expression into the master list.
 */
static void
cell_set_expr_internal (GnmCell *cell, GnmExprTop const *texpr)
{
	GnmValue *save_value;

	gnm_expr_top_ref (texpr);

	/* Don't touch the value.  */
	save_value = cell->value ? cell->value : value_new_empty ();
	cell->value = NULL;
	gnm_cell_cleanout (cell);

	cell->base.flags |= GNM_CELL_HAS_NEW_EXPR;
	cell->base.texpr = texpr;
	cell->value = save_value;
}

/**
 * gnm_cell_set_expr_unsafe: (skip)
 * @cell: The #GnmCell
 * @texpr: The #GnmExprTop
 *
 * Stores and references the supplied expression.  It
 *         marks the sheet as dirty.  Intended for use by import routines that
 *         do bulk assignment.  The resulting cell is NOT linked into the
 *         dependent list.  Nor marked for recalc.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *           It also DOES NOT CHECK FOR ARRAY DIVISION.  Be very careful
 *           using this.
 */
void
gnm_cell_set_expr_unsafe (GnmCell *cell, GnmExprTop const *texpr)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (texpr != NULL);

	cell_set_expr_internal (cell, texpr);
}

/**
 * gnm_cell_set_expr: (skip)
 * @cell: The #GnmCell
 * @texpr: (transfer none): The #GnmExprTop
 *
 * Stores and references the supplied expression
 *         marks the sheet as dirty.  Intended for use by import routines that
 *         do bulk assignment.  The resulting cell _is_ linked into the
 *         dependent list, but NOT marked for recalc.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *           Be very careful using this.
 */
void
gnm_cell_set_expr (GnmCell *cell, GnmExprTop const *texpr)
{
	g_return_if_fail (!gnm_cell_is_nonsingleton_array (cell));
	g_return_if_fail (cell != NULL);
	g_return_if_fail (texpr != NULL);

	cell_set_expr_internal (cell, texpr);
	dependent_link (GNM_CELL_TO_DEP (cell));
}

/**
 * gnm_cell_set_array_formula: (skip)
 * @sheet:   The sheet to set the expr in.
 * @cola:   The left column in the destination region.
 * @rowa:   The top row in the destination region.
 * @colb:   The right column in the destination region.
 * @rowb:   The bottom row in the destination region.
 * @texpr:   an expression (the inner expression, not a corner or element)
 *
 * Uses cell_set_expr_internal to store the expr as an
 * 'array-formula'.  The supplied expression is wrapped in an array
 * operator for each cell in the range and scheduled for recalc.
 *
 * NOTE : Does not add a reference to the expression.  It takes over the
 *        caller's reference.
 *
 * Does not regenerate spans, dimensions or autosize cols/rows.
 *
 * DOES NOT CHECK for array partitioning.
 */
void
gnm_cell_set_array_formula (Sheet *sheet,
			    int col_a, int row_a, int col_b, int row_b,
			    GnmExprTop const *texpr)
{
	int const num_rows = 1 + row_b - row_a;
	int const num_cols = 1 + col_b - col_a;
	int x, y;
	GnmCell *corner;
	GnmExprTop const *wrapper;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (texpr != NULL);
	g_return_if_fail (0 <= col_a);
	g_return_if_fail (col_a <= col_b);
	g_return_if_fail (col_b < gnm_sheet_get_max_cols (sheet));
	g_return_if_fail (0 <= row_a);
	g_return_if_fail (row_a <= row_b);
	g_return_if_fail (row_b < gnm_sheet_get_max_rows (sheet));

	corner = sheet_cell_fetch (sheet, col_a, row_a);
	g_return_if_fail (corner != NULL);

	wrapper = gnm_expr_top_new_array_corner (num_cols, num_rows, gnm_expr_copy (texpr->expr));
	gnm_expr_top_unref (texpr);
	cell_set_expr_internal (corner, wrapper);
	gnm_expr_top_unref (wrapper);

	for (x = 0; x < num_cols; ++x) {
		for (y = 0; y < num_rows; ++y) {
			GnmCell *cell;
			GnmExprTop const *te;

			if (x == 0 && y == 0)
				continue;

			cell = sheet_cell_fetch (sheet, col_a + x, row_a + y);
			te = gnm_expr_top_new_array_elem (x, y);
			cell_set_expr_internal (cell, te);
			dependent_link (GNM_CELL_TO_DEP (cell));
			gnm_expr_top_unref (te);
		}
	}

	dependent_link (GNM_CELL_TO_DEP (corner));
}

static void
gnm_cell_set_array_formula_cb (GnmSheetRange const *sr, GnmExprTop const *texpr)
{
	sheet_region_queue_recalc (sr->sheet, &sr->range);
	gnm_expr_top_ref (texpr);
	gnm_cell_set_array_formula (sr->sheet,
				    sr->range.start.col, sr->range.start.row,
				    sr->range.end.col,   sr->range.end.row,
				    texpr);
	sheet_region_queue_recalc (sr->sheet, &sr->range);
	sheet_flag_status_update_range (sr->sheet, &sr->range);
	sheet_queue_respan (sr->sheet, sr->range.start.row, sr->range.end.row);
}

/**
 * gnm_cell_set_array_formula_undo:
 * @sr:
 * @texpr:
 *
 * Returns: (transfer full): the newly allocated #GOUndo.
 **/
GOUndo *
gnm_cell_set_array_formula_undo (GnmSheetRange *sr, GnmExprTop const *texpr)
{
	gnm_expr_top_ref (texpr);
	return go_undo_binary_new (sr, (gpointer)texpr,
				   (GOUndoBinaryFunc) gnm_cell_set_array_formula_cb,
				   (GFreeFunc) gnm_sheet_range_free,
				   (GFreeFunc) gnm_expr_top_unref);
}

/**
 * gnm_cell_set_array:
 * @sheet:   The sheet to set the array expression in.
 * @r:       The range to set.
 * @texpr:   an expression (the inner expression, not a corner or element)
 *
 * Set an array expression for a range.
 * Uses cell_set_expr_internal to store the expr as an
 * 'array-formula'.  The supplied expression is wrapped in an array
 * operator for each cell in the range and scheduled for recalc.
 *
 * Returns: %TRUE if the operation succeeded.
 *
 * NOTE : This adds a reference to the expression.
 *
 * Does not regenerate spans, dimensions or autosize cols/rows.
 *
 * DOES CHECK for array partitioning.
 */

gboolean
gnm_cell_set_array (Sheet *sheet,
		    const GnmRange *r,
		    GnmExprTop const *texpr)
{
	g_return_val_if_fail (sheet != NULL, FALSE);
	g_return_val_if_fail (range_is_sane (r), FALSE);
	g_return_val_if_fail (r->end.row < gnm_sheet_get_max_rows (sheet), FALSE);
	g_return_val_if_fail (r->end.col < gnm_sheet_get_max_cols (sheet), FALSE);
	g_return_val_if_fail (texpr != NULL, FALSE);

	if (sheet_range_splits_array (sheet, r, NULL, NULL, NULL))
		return FALSE;

	gnm_expr_top_ref (texpr);
	gnm_cell_set_array_formula (sheet,
				    r->start.col, r->start.row,
				    r->end.col, r->end.row,
				    texpr);
	return TRUE;
}

/***************************************************************************/

/**
 * gnm_cell_is_empty:
 * @cell: (nullable): #GnmCell
 *
 * Returns: %TRUE, If the cell has not been created, or has VALUE_EMPTY.
 **/
gboolean
gnm_cell_is_empty (GnmCell const *cell)
{
	return cell == NULL || VALUE_IS_EMPTY (cell->value);
}

/**
 * gnm_cell_is_blank:
 * @cell: (nullable): #GnmCell
 *
 * Returns: %TRUE, if the cell has not been created, has VALUE_EMPTY,
 * or has an empty VALUE_STRING.
 **/
gboolean
gnm_cell_is_blank (GnmCell const * cell)
{
	return gnm_cell_is_empty (cell) ||
		(VALUE_IS_STRING (cell->value) &&
		 *value_peek_string (cell->value) == '\0');
}

/**
 * gnm_cell_is_error:
 * @cell: #GnmCell
 *
 * Returns: (nullable) (transfer none): @cell's value if it is an error,
 * or %NULL.
 **/
GnmValue *
gnm_cell_is_error (GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->value != NULL, NULL);

	if (VALUE_IS_ERROR (cell->value))
		return cell->value;
	return NULL;
}

/**
 * gnm_cell_is_number:
 * @cell: #GnmCell
 *
 * Returns: %TRUE, if the cell contains a number.
 **/
gboolean
gnm_cell_is_number (GnmCell const *cell)
{
	/* FIXME : This does not handle arrays or ranges */
	return (cell->value && VALUE_IS_NUMBER (cell->value));
}

/**
 * gnm_cell_is_zero:
 * @cell: #GnmCell
 *
 * Returns: %TRUE, if the cell contains zero.
 **/
gboolean
gnm_cell_is_zero (GnmCell const *cell)
{
	GnmValue const * const v = cell->value;
	return v && VALUE_IS_NUMBER (v) && gnm_abs (value_get_as_float (v)) < 64 * GNM_EPSILON;
}

/**
 * gnm_cell_get_value:
 * @cell: #GnmCell
 *
 * Returns: (transfer none): @cell's value
 **/
GnmValue *
gnm_cell_get_value (GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	return cell->value;
}

/**
 * gnm_cell_array_bound:
 * @cell: (nullable): #GnmCell
 * @res: (out): The range containing an array cell
 *
 * Returns: %TRUE, if the cell is an array cell
 **/
gboolean
gnm_cell_array_bound (GnmCell const *cell, GnmRange *res)
{
	GnmExprTop const *texpr;
	int x, y;
	int cols, rows;

	range_init (res, 0, 0, 0, 0);

	if (NULL == cell || !gnm_cell_has_expr (cell))
		return FALSE;

	g_return_val_if_fail (res != NULL, FALSE);

	texpr = cell->base.texpr;
	if (gnm_expr_top_is_array_elem (texpr, &x, &y)) {
		cell = sheet_cell_get (cell->base.sheet, cell->pos.col - x, cell->pos.row - y);

		g_return_val_if_fail (cell != NULL, FALSE);
		g_return_val_if_fail (gnm_cell_has_expr (cell), FALSE);

		texpr = cell->base.texpr;
	}

	if (!gnm_expr_top_is_array_corner (texpr))
		return FALSE;

	gnm_expr_top_get_array_size (texpr, &cols, &rows);

	range_init (res, cell->pos.col, cell->pos.row,
		cell->pos.col + cols - 1,
		cell->pos.row + rows - 1);
	return TRUE;
}

/**
 * gnm_cell_is_array:
 * @cell: #GnmCell
 *
 * Returns %TRUE is @cell is part of an array
 **/
gboolean
gnm_cell_is_array (GnmCell const *cell)
{
	return cell != NULL && gnm_cell_has_expr (cell) &&
		(gnm_expr_top_is_array_corner (cell->base.texpr) ||
		 gnm_expr_top_is_array_elem (cell->base.texpr, NULL, NULL));
}

/**
 * gnm_cell_is_nonsingleton_array:
 * @cell: #GnmCell
 *
 * Returns: %TRUE is @cell is part of an array larger than 1x1
 **/
gboolean
gnm_cell_is_nonsingleton_array (GnmCell const *cell)
{
	int cols, rows;

	if ((cell == NULL) || !gnm_cell_has_expr (cell))
		return FALSE;
	if (gnm_expr_top_is_array_elem (cell->base.texpr, NULL, NULL))
		return TRUE;

	if (!gnm_expr_top_is_array_corner (cell->base.texpr))
		return FALSE;

	gnm_expr_top_get_array_size (cell->base.texpr, &cols, &rows);
	return cols > 1 || rows > 1;
}

/***************************************************************************/

/**
 * gnm_cell_get_rendered_value: (skip)
 * @cell: #GnmCell
 *
 * Returns: (transfer none): The #GnmRenderedValue for the cell.
 **/
GnmRenderedValue *
gnm_cell_get_rendered_value (GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);

	return gnm_rvc_query (cell->base.sheet->rendered_values, cell);
}

/**
 * gnm_cell_fetch_rendered_value: (skip)
 * @cell: #GnmCell
 * @allow_variable_width: Allow format to depend on column width.
 *
 * Returns: (transfer none): A rendered value for the cell.
 **/
GnmRenderedValue *
gnm_cell_fetch_rendered_value (GnmCell const *cell,
			       gboolean allow_variable_width)
{
	GnmRenderedValue *rv;

	g_return_val_if_fail (cell != NULL, NULL);

	rv = gnm_cell_get_rendered_value (cell);
	if (rv)
		return rv;

	return gnm_cell_render_value (cell, allow_variable_width);
}

void
gnm_cell_unrender (GnmCell const *cell)
{
	gnm_rvc_remove (cell->base.sheet->rendered_values, cell);
}

/**
 * gnm_cell_render_value: (skip)
 * @cell: The cell whose value needs to be rendered
 * @allow_variable_width: Allow format to depend on column width.
 *
 * Returns: (transfer none): The newly #GnmRenderedValue.
 */
GnmRenderedValue *
gnm_cell_render_value (GnmCell const *cell, gboolean allow_variable_width)
{
	GnmRenderedValue *rv;
	Sheet *sheet;

	g_return_val_if_fail (cell != NULL, NULL);

	sheet = cell->base.sheet;
	rv = gnm_rendered_value_new (cell,
				     sheet->rendered_values->context,
				     allow_variable_width,
				     sheet->last_zoom_factor_used);

	gnm_rvc_store (sheet->rendered_values, cell, rv);

	return rv;
}

/*
 * gnm_cell_get_rendered_text:
 *
 * Warning: use this only when you really want what is displayed on the
 * screen.  If the user has decided to display formulas instead of values
 * then that is what you get.
 */
char *
gnm_cell_get_rendered_text (GnmCell *cell)
{
	GnmRenderedValue *rv;

	g_return_val_if_fail (cell != NULL, g_strdup ("ERROR"));

	rv = gnm_cell_fetch_rendered_value (cell, TRUE);

	return g_strdup (gnm_rendered_value_get_text (rv));
}

/**
 * gnm_cell_get_render_color:
 * @cell: the cell from which we want to pull the color from
 *
 * Returns: A #GOColor used for foreground in @cell.
 */
GOColor
gnm_cell_get_render_color (GnmCell const *cell)
{
	GnmRenderedValue *rv;

	g_return_val_if_fail (cell != NULL, GO_COLOR_BLACK);

	rv = gnm_cell_fetch_rendered_value (cell, TRUE);

	return gnm_rendered_value_get_color (rv);
}

/**
 * gnm_cell_get_entered_text:
 * @cell: the cell from which we want to pull the content from
 *
 * Returns: (transfer full): a text expression if the cell contains a
 * formula, or a string representation of the value.
 */
char *
gnm_cell_get_entered_text (GnmCell const *cell)
{
	GnmValue const *v;
	Sheet *sheet;

	g_return_val_if_fail (cell != NULL, NULL);

	sheet = cell->base.sheet;

	if (gnm_cell_has_expr (cell)) {
		GnmParsePos pp;
		GnmConventionsOut out;

		out.accum = g_string_new ("=");
		out.pp = parse_pos_init_cell (&pp, cell);
		out.convs = sheet->convs;

		gnm_expr_top_as_gstring (cell->base.texpr, &out);
		return g_string_free (out.accum, FALSE);
	}

	v = cell->value;
	if (v != NULL) {
		GODateConventions const *date_conv =
			sheet_date_conv (sheet);

		if (VALUE_IS_STRING (v)) {
			/* Try to be reasonably smart about adding a leading quote */
			char const *tmp = value_peek_string (v);

			if (tmp[0] != '\'' &&
			    tmp[0] != 0 &&
			    !gnm_expr_char_start_p (tmp)) {
				GnmValue *val = format_match_number
					(tmp,
					 gnm_cell_get_format (cell),
					 date_conv);
				if (val == NULL)
					return g_strdup (tmp);
				value_release (val);
			}
			return g_strconcat ("\'", tmp, NULL);
		} else {
			GOFormat const *fmt = gnm_cell_get_format (cell);
			return format_value (fmt, v, -1, date_conv);
		}
	}

	g_warning ("A cell with no expression, and no value ??");
	return g_strdup ("<ERROR>");
}

static gboolean
close_to_int (gnm_float x, gnm_float eps)
{
	return gnm_abs (x - gnm_fake_round (x)) < eps;
}

static GOFormat *
guess_time_format (const char *prefix, gnm_float f)
{
	int decs = 0;
	gnm_float eps = 1e-6;
	static int maxdecs = 6;
	GString *str = g_string_new (prefix);
	GOFormat *fmt;

	if (f >= 0 && f < 1)
		g_string_append (str, "hh:mm");
	else
		g_string_append (str, "[h]:mm");
	f *= 24 * 60;
	if (!close_to_int (f, eps / 60)) {
		g_string_append (str, ":ss");
		f *= 60;
		if (!close_to_int (f, eps)) {
			g_string_append_c (str, '.');
			while (decs < maxdecs) {
				decs++;
				g_string_append_c (str, '0');
				f *= 10;
				if (close_to_int (f, eps))
					break;
			}
		}
	}

	while (go_format_is_invalid ((fmt = go_format_new_from_XL (str->str))) && decs > 0) {
		/* We don't know how many decimals GOFormat allows.  */
		go_format_unref (fmt);
		maxdecs = --decs;
		g_string_truncate (str, str->len - 1);
	}

	g_string_free (str, TRUE);
	return fmt;
}

static void
render_percentage (GString *str, gnm_float f)
{
	gnm_float f100 = 100 * f;
	gboolean qneg = (f < 0);
	gnm_float f2;

	// Render slightly narrow
	gnm_render_general (NULL, str, go_format_measure_strlen,
			    go_font_metrics_unit, f100,
			    12 + qneg, FALSE, 0, 0);
	// Explicit cast to drop excess precision
	f2 = (gnm_float)(gnm_strto (str->str, NULL) / 100);
	if (f2 == f)
		return;

	// No good -- rerender unconstrained.
	gnm_render_general (NULL, str, go_format_measure_zero,
			    go_font_metrics_unit, f100,
			    -1, FALSE, 0, 0);
}

/**
 * gnm_cell_get_text_for_editing:
 * @cell: the cell from which we want to pull the content from
 * @quoted: (out) (optional): Whether a single quote was used to force
 * string interpretation
 * @cursor_pos: (out) (optional): Desired initial cursor position
 *
 * Returns: (transfer full): A string suitable for editing
 *
 * Primary user of this function is the formula entry.
 * This function should return the value most appropriate for
 * editing
 */
char *
gnm_cell_get_text_for_editing (GnmCell const * cell,
			       gboolean *quoted, int *cursor_pos)
{
	GODateConventions const *date_conv;
	gchar *text = NULL;

	g_return_val_if_fail (cell != NULL, NULL);

	if (quoted)
		*quoted = FALSE;

	date_conv = sheet_date_conv (cell->base.sheet);

	if (!gnm_cell_is_array (cell) &&
	    !gnm_cell_has_expr (cell) && VALUE_IS_FLOAT (cell->value)) {
		GOFormat const *fmt = gnm_cell_get_format (cell);
		gnm_float f = value_get_as_float (cell->value);

		switch (go_format_get_family (fmt)) {
		case GO_FORMAT_FRACTION:
			text = gnm_cell_get_entered_text (cell);
			g_strchug (text);
			g_strchomp (text);
			break;

		case GO_FORMAT_PERCENTAGE: {
			GString *new_str = g_string_new (NULL);
			render_percentage (new_str, f);
			if (cursor_pos)
				*cursor_pos = g_utf8_strlen (new_str->str, -1);
			g_string_append_c (new_str, '%');
			text = g_string_free (new_str, FALSE);
			break;
		}

		default:
		case GO_FORMAT_NUMBER:
		case GO_FORMAT_SCIENTIFIC:
		case GO_FORMAT_CURRENCY:
		case GO_FORMAT_ACCOUNTING:
		case GO_FORMAT_GENERAL: {
			GString *new_str = g_string_new (NULL);
			gnm_render_general (NULL, new_str, go_format_measure_zero,
					    go_font_metrics_unit, f,
					    -1, FALSE, 0, 0);
			text = g_string_free (new_str, FALSE);
			break;
		}

		case GO_FORMAT_DATE: {
			GOFormat *new_fmt;

			new_fmt = gnm_format_for_date_editing (cell);

			if (!close_to_int (f, 1e-6 / (24 * 60 * 60))) {
				GString *fstr = g_string_new (go_format_as_XL (new_fmt));
				go_format_unref (new_fmt);

				g_string_append_c (fstr, ' ');
				new_fmt = guess_time_format
					(fstr->str,
					 f - gnm_floor (f));
				g_string_free (fstr, TRUE);
			}

			text = format_value (new_fmt, cell->value,
					     -1, date_conv);
			if (!text || text[0] == 0) {
				g_free (text);
				text = format_value (go_format_general (),
						     cell->value,
						     -1,
						     date_conv);
			}
			go_format_unref (new_fmt);
			break;
		}

		case GO_FORMAT_TIME: {
			GOFormat *new_fmt = guess_time_format (NULL, f);

			text = format_value (new_fmt, cell->value, -1,
					     date_conv);
			go_format_unref (new_fmt);
			break;
		}
		}
	}

	if (!text) {
		text = gnm_cell_get_entered_text (cell);
		if (quoted)
			*quoted = (text[0] == '\'');
	}

	return text;
}



/*
 * Return the height of the rendered layout after rotation.
 */
int
gnm_cell_rendered_height (GnmCell const *cell)
{
	const GnmRenderedValue *rv;

	g_return_val_if_fail (cell != NULL, 0);

	rv = gnm_cell_get_rendered_value (cell);
	return rv
		? PANGO_PIXELS (rv->layout_natural_height)
		: 0;
}

/*
 * Return the width of the rendered layout after rotation.
 */
int
gnm_cell_rendered_width (GnmCell const *cell)
{
	const GnmRenderedValue *rv;

	g_return_val_if_fail (cell != NULL, 0);

	rv = gnm_cell_get_rendered_value (cell);
	return rv
		? PANGO_PIXELS (rv->layout_natural_width)
		: 0;
}

int
gnm_cell_rendered_offset (GnmCell const * cell)
{
	const GnmRenderedValue *rv;

	g_return_val_if_fail (cell != NULL, 0);

	rv = gnm_cell_get_rendered_value (cell);
	return rv
		? rv->indent_left + rv->indent_right
		: 0;
}

/**
 * gnm_cell_get_style:
 * @cell: #GnmCell to query
 *
 * Returns: (transfer none): the fully qualified style for @cell.
 */
GnmStyle const *
gnm_cell_get_style (GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	return sheet_style_get (cell->base.sheet,
				cell->pos.col,
				cell->pos.row);
}

/**
 * gnm_cell_get_effective_style:
 * @cell: #GnmCell to query
 *
 * Returns: (transfer none): the fully qualified style for @cell, taking any
 * conditional formats into account.
 */
GnmStyle const *
gnm_cell_get_effective_style (GnmCell const *cell)
{
	GnmStyleConditions *conds;
	GnmStyle const *mstyle;

	g_return_val_if_fail (cell != NULL, NULL);

	mstyle = gnm_cell_get_style (cell);
	conds = gnm_style_get_conditions (mstyle);
	if (conds) {
		GnmEvalPos ep;
		int res;
		eval_pos_init_cell (&ep, cell);

		res = gnm_style_conditions_eval (conds, &ep);
		if (res >= 0)
			mstyle = gnm_style_get_cond_style (mstyle, res);
	}
	return mstyle;
}


/**
 * gnm_cell_get_format_given_style: (skip)
 * @cell: #GnmCell to query
 * @style: (nullable): #GnmStyle for @cell.
 *
 * Returns: (transfer none): the effective format for the cell, i.e., @style's
 * format unless that is General and the cell value has a format.
 **/
GOFormat const *
gnm_cell_get_format_given_style (GnmCell const *cell, GnmStyle const *style)
{
	GOFormat const *fmt;

	g_return_val_if_fail (cell != NULL, go_format_general ());

	if (style == NULL)
		style = gnm_cell_get_effective_style (cell);

	fmt = gnm_style_get_format (style);

	g_return_val_if_fail (fmt != NULL, go_format_general ());

	if (go_format_is_general (fmt) &&
	    cell->value != NULL && VALUE_FMT (cell->value))
		fmt = VALUE_FMT (cell->value);

	return fmt;
}

/**
 * gnm_cell_get_format:
 * @cell: #GnmCell to query
 *
 * Returns: (transfer none): the effective format for the cell, i.e., the
 * cell style's format unless that is General and the cell value has a format.
 **/
GOFormat const *
gnm_cell_get_format (GnmCell const *cell)
{
	GnmStyle const *mstyle = gnm_cell_get_effective_style (cell);
	return gnm_cell_get_format_given_style (cell, mstyle);
}

static GnmValue *
cb_set_array_value (GnmCellIter const *iter, gpointer user)
{
	GnmValue const *value = user;
	GnmCell *cell = iter->cell;
	int x, y;

	/* Clipboard cells, e.g., are not attached to a sheet.  */
	if (gnm_cell_expr_is_linked (cell))
		dependent_unlink (GNM_CELL_TO_DEP (cell));

	if (!gnm_expr_top_is_array_elem (cell->base.texpr, &x, &y))
		return NULL;

	gnm_expr_top_unref (cell->base.texpr);
	cell->base.texpr = NULL;
	value_release (cell->value);
	cell->value = value_dup (value_area_get_x_y (value, x, y, NULL));

	return NULL;
}

/**
 * gnm_cell_convert_expr_to_value:
 * @cell: #GnmCell
 * drops the expression keeps its value.  Then uses the formatted
 *      result as if that had been entered.
 *
 * NOTE : the cell's expression cannot be linked into the expression * list.
 *
 * The cell is rendered but spans are not calculated,  the cell is NOT marked for
 * recalc.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 */
void
gnm_cell_convert_expr_to_value (GnmCell *cell)
{
	GnmExprTop const *texpr;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (gnm_cell_has_expr (cell));

	/* Clipboard cells, e.g., are not attached to a sheet.  */
	if (gnm_cell_expr_is_linked (cell))
		dependent_unlink (GNM_CELL_TO_DEP (cell));

	texpr = cell->base.texpr;
	if (gnm_expr_top_is_array_corner (texpr)) {
		int rows, cols;

		gnm_expr_top_get_array_size (texpr, &cols, &rows);

		sheet_foreach_cell_in_region (cell->base.sheet, CELL_ITER_ALL,
					     cell->pos.col, cell->pos.row,
					     cell->pos.col + cols - 1,
					     cell->pos.row + rows - 1,
					     cb_set_array_value,
					     gnm_expr_top_get_array_value (texpr));
	} else {
		g_return_if_fail (!gnm_cell_is_array (cell));
	}

	gnm_expr_top_unref (texpr);
	cell->base.texpr = NULL;
}

static gpointer cell_boxed_copy (gpointer c) { return c; }
static void cell_boxed_free (gpointer c) { }

GType
gnm_cell_get_type (void)
{
    static GType type_cell = 0;

    if (!type_cell)
	type_cell = g_boxed_type_register_static
	    ("GnmCell",
	     cell_boxed_copy,
	     cell_boxed_free);

    return type_cell;
}

// Provide the external version of inline functions, used mainly for
// introspection

/**
 * gnm_cell_has_expr:
 * @cell: #GnmCell
 *
 * Returns: %TRUE if @cell has an expression or %FALSE if it is empty
 * or contains a value.
 */
extern inline gboolean gnm_cell_has_expr (GnmCell const *cell);

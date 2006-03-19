/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * cell.c: Cell content and simple management.
 *
 * Author:
 *    Jody Goldberg 2000-2005 (jody@gnome.org)
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "cell.h"

#include "gutils.h"
#include "workbook.h"
#include "sheet.h"
#include "expr.h"
#include "expr-impl.h"
#include "rendered-value.h"
#include "value.h"
#include "str.h"
#include "style.h"
#include "ranges.h"
#include "gnm-format.h"
#include "sheet-object-cell-comment.h"
#include "sheet-style.h"
#include "parse-util.h"
#include <goffice/utils/go-glib-extras.h>

#define USE_CELL_POOL 1

/**
 * cell_cleanout :
 *      Empty a cell's
 *      	- value.
 *      	- rendered_value.
 *      	- expression.
 *      	- parse format.
 *
 *      Clears the flags to
 *      	- not queued for recalc.
 *      	- has no expression.
 *
 *      Does NOT change
 *      	- Comments.
 *      	- Spans.
 *      	- unqueue a previously queued recalc.
 *      	- Mark sheet as dirty.
 */
static void
cell_cleanout (GnmCell *cell)
{
	/* A cell can have either an expression or entered text */
	if (cell_has_expr (cell)) {
		/* Clipboard cells, e.g., are not attached to a sheet.  */
		if (cell_expr_is_linked (cell))
			dependent_unlink (CELL_TO_DEP (cell));
		gnm_expr_top_unref (cell->base.texpr);
		cell->base.texpr = NULL;
	}

	if (cell->value != NULL) {
		value_release (cell->value);
		cell->value = NULL;
	}
	if (cell->rendered_value != NULL) {
		rendered_value_destroy (cell->rendered_value);
		cell->rendered_value = NULL;
	}
	if (cell->row_info != NULL)
		cell->row_info->needs_respan = TRUE;
}

/* The pool from which all cells are allocated.  */
static GOMemChunk *cell_pool;

/**
 * cell_new:
 * @sheet: sheet in which to allocate cell.
 *
 * Creates a new cell.
 */
GnmCell *
cell_new (void)
{
	GnmCell *cell = USE_CELL_POOL ? go_mem_chunk_alloc0 (cell_pool) : g_new0 (GnmCell, 1);
	cell->base.flags = DEPENDENT_CELL;
	return cell;
}


/**
 * cell_copy:
 * @cell: existing cell to duplicate
 *
 * Makes a copy of a GnmCell.
 *
 * Returns a copy of the cell.
 */
GnmCell *
cell_copy (GnmCell const *cell)
{
	GnmCell *new_cell;

	g_return_val_if_fail (cell != NULL, NULL);

	new_cell = cell_new ();

	/* bitmap copy first */
	*new_cell = *cell;

	/* The new cell is not linked into any of the major management structures */
	new_cell->base.sheet = NULL;
	new_cell->base.flags &= ~(DEPENDENT_NEEDS_RECALC |
				  (int)CELL_IN_SHEET_LIST |
				  DEPENDENT_IS_LINKED);

	/* now copy properly the rest */
	if (cell_has_expr (new_cell))
		gnm_expr_top_ref (new_cell->base.texpr);

	new_cell->rendered_value = NULL;

	new_cell->value = (new_cell->value)
		? value_dup (new_cell->value)
		: value_new_empty ();

	return new_cell;
}

/**
 * cell_destroy: Frees all resources allocated to the cell's content and marks the
 *     GnmCell's container as dirty.
 *
 * @cell : The cell to destroy
 */
void
cell_destroy (GnmCell *cell)
{
	g_return_if_fail (cell != NULL);

	cell_cleanout (cell);
	if (USE_CELL_POOL) go_mem_chunk_free (cell_pool, cell); else g_free (cell);
}

/*
 * cell_relocate:
 * @cell   : The cell that is changing position
 * @rwinfo : An OPTIONAL pointer to allow for bounds checking and relocation
 *
 * This routine is used to move a cell to a different location:
 */
void
cell_relocate (GnmCell *cell, GnmExprRewriteInfo const *rwinfo)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (rwinfo != NULL);

	if (cell_has_expr (cell)) {
		GnmExprTop const *texpr =
			gnm_expr_top_rewrite (cell->base.texpr, rwinfo);

#warning "make this a precondition"
		if (cell_expr_is_linked (cell))
			dependent_unlink (CELL_TO_DEP (cell));

		/* bounds check, and adjust local references from the cell */
		if (texpr != NULL) {
			gnm_expr_top_unref (cell->base.texpr);
			cell->base.texpr = texpr;
		}

		dependent_link (CELL_TO_DEP (cell));
	}
}

/****************************************************************************/

/*
 * cell_set_text : Parses the supplied text for storage as a value or
 * 		expression.  It marks the sheet as dirty.
 *
 * If the text is an expression it IS queued for recalc.
 *        the format prefered by the expression is stored for later use.
 * If the text is a value it is rendered and spans are NOT calculated.
 *        the format that matched the text is stored for later use.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *
 * NOTE : This DOES check for array partitioning.
 */
void
cell_set_text (GnmCell *cell, char const *text)
{
	GnmExprTop const *texpr;
	GnmValue      *val;
	GnmParsePos    pos;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (!cell_is_nonsingleton_array (cell));

	parse_text_value_or_expr (parse_pos_init_cell (&pos, cell),
		text, &val, &texpr, gnm_style_get_format (cell_get_mstyle (cell)),
		workbook_date_conv (cell->base.sheet->workbook));

	if (val != NULL) {	/* String was a value */
		cell_cleanout (cell);
		cell->value = val;
	} else {		/* String was an expression */
		cell_set_expr (cell, texpr);
		gnm_expr_top_unref (texpr);
	}
}

/*
 * cell_assign_value : Stores (WITHOUT COPYING) the supplied value.
 *    no changes are made to the expression or entered text.  This
 *    is for use by routines that wish to store values directly such
 *    as expression calculation or import for array formulas.
 *
 * WARNING : This is an internal routine that does not
 * 	- queue redraws,
 *	- auto-resize
 *	- calculate spans
 *	- does not render.
 *	- mark anything as dirty.
 *
 * NOTE : This DOES NOT check for array partitioning.
 */
void
cell_assign_value (GnmCell *cell, GnmValue *v)
{
	g_return_if_fail (cell);
	g_return_if_fail (v);

	if (cell->value != NULL)
		value_release (cell->value);
	cell->value = v;
}

/**
 * cell_set_value : Stores (WITHOUT COPYING) the supplied value.  It marks the
 *          sheet as dirty.
 *
 * WARNING : This is an internal routine that does not
 * 	- queue redraws,
 *	- auto-resize
 *	- calculate spans
 *	- does not render.
 *
 * NOTE : This DOES check for array partitioning.
 **/
void
cell_set_value (GnmCell *cell, GnmValue *v)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (v != NULL);
	g_return_if_fail (!cell_is_nonsingleton_array (cell));

	cell_cleanout (cell);
	cell->value = v;
}

/*
 * cell_set_expr_and_value : Stores (WITHOUT COPYING) the supplied value, and
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
cell_set_expr_and_value (GnmCell *cell, GnmExprTop const *texpr, GnmValue *v,
			 gboolean link_expr)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (texpr != NULL);
	g_return_if_fail (!cell_is_nonsingleton_array (cell));

	/* Repeat after me.  Ref before unref. */
	gnm_expr_top_ref (texpr);
	cell_cleanout (cell);

	cell->base.texpr = texpr;
	cell->value = v;
	if (link_expr)
		dependent_link (CELL_TO_DEP (cell));
}

/**
 * cell_set_expr_internal:
 * @cell: the cell to set the expr for
 * @expr: an expression
 *
 * A private internal utility to store an expression.
 * Does NOT
 * 	- check for array subdivision
 * 	- queue recalcs.
 * 	- render value, calc dimension, compute spans
 * 	- link the expression into the master list.
 */
static void
cell_set_expr_internal (GnmCell *cell, GnmExprTop const *texpr)
{
	gnm_expr_top_ref (texpr);

	cell_cleanout (cell);

	cell->base.flags |= CELL_HAS_NEW_EXPR;
	cell->base.texpr = texpr;

	/* Until the value is recomputed, we put in this value.
	 *
	 * We should consider using 0 instead and take out the
	 * cell_needs_recalc call in sheet_foreach_cell_in_range.
	 */
	cell->value = value_new_empty ();
}

/*
 * cell_set_expr_unsafe : Stores and references the supplied expression.  It
 *         marks the sheet as dirty.  Intented for use by import routines that
 *         do bulk assignment.  The resulting cell is NOT linked into the
 *         dependent list.  Nor marked for recalc.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *           It also DOES NOT CHECK FOR ARRAY DIVISION.  Be very careful
 *           using this.
 */
void
cell_set_expr_unsafe (GnmCell *cell, GnmExprTop const *texpr)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (texpr != NULL);

	cell_set_expr_internal (cell, texpr);
}

/**
 * cell_set_expr :  Stores and references the supplied expression
 *         marks the sheet as dirty.  Intented for use by import routines that
 *         do bulk assignment.  The resulting cell _is_ linked into the
 *         dependent list, but NOT marked for recalc.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *           Be very careful using this.
 */
void
cell_set_expr (GnmCell *cell, GnmExprTop const *texpr)
{
	g_return_if_fail (!cell_is_nonsingleton_array (cell));
	g_return_if_fail (cell != NULL);
	g_return_if_fail (texpr != NULL);

	cell_set_expr_internal (cell, texpr);
	dependent_link (CELL_TO_DEP (cell));
}

/**
 * cell_set_array_formula:
 * @sheet:   The sheet to set the expr in.
 * @row_a:   The top row in the destination region.
 * @col_a:   The left column in the destination region.
 * @row_b:   The bottom row in the destination region.
 * @col_b:   The right column in the destination region.
 * @expr:    an expression
 *
 * Uses cell_set_expr_internal to store the expr as an
 * 'array-formula'.  The supplied expression is wrapped in an array
 * operator for each cell in the range and scheduled for recalc.  The
 * upper left corner is handled as a special case and care is taken to
 * put it at the head of the recalc queue if recalcs are requested.
 *
 * NOTE : Does not add a reference to the expression.  It takes over the
 *        caller's reference.
 *
 * Does not regenerate spans, dimensions or autosize cols/rows.
 *
 * DOES NOT CHECK for array partitioning.
 */
void
cell_set_array_formula (Sheet *sheet,
			int col_a, int row_a, int col_b, int row_b,
			GnmExprTop const *texpr)
{
	int const num_rows = 1 + row_b - row_a;
	int const num_cols = 1 + col_b - col_a;
	int x, y;
	GnmCell *corner;
	GnmExprTop const *wrapper;

	g_return_if_fail (num_cols > 0);
	g_return_if_fail (num_rows > 0);
	g_return_if_fail (texpr != NULL);
	g_return_if_fail (col_a <= col_b);
	g_return_if_fail (row_a <= row_b);

	corner = sheet_cell_fetch (sheet, col_a, row_a);
	g_return_if_fail (corner != NULL);

	wrapper = gnm_expr_top_new (gnm_expr_new_array_corner (num_cols, num_rows, gnm_expr_copy (texpr->expr)));
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
			te = gnm_expr_top_new (gnm_expr_new_array_elem (x, y));
			cell_set_expr_internal (cell, te);
			dependent_link (CELL_TO_DEP (cell));
			gnm_expr_top_unref (te);
		}
	}

	dependent_link (CELL_TO_DEP (corner));
}

/***************************************************************************/

/**
 * cell_is_empty :
 * @cell : #GnmCell
 *
 * If the cell has not been created, or has VALUE_EMPTY.
 **/
gboolean
cell_is_empty (GnmCell const * cell)
{
	return  cell == NULL ||
		cell->value == NULL ||
		cell->value->type == VALUE_EMPTY;
}

/**
 * cell_is_blank :
 * @cell : #GnmCell
 *
 * If the cell has not been created, has VALUE_EMPTY, or has a VALUE_STRING == ""
 **/
gboolean
cell_is_blank (GnmCell const * cell)
{
	return  cell == NULL ||
		cell->value == NULL ||
		cell->value->type == VALUE_EMPTY ||
		(VALUE_IS_STRING (cell->value) && *(cell->value->v_str.val->str) == '\0');
}

GnmValue *
cell_is_error (GnmCell const * cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->value != NULL, NULL);

	if (cell->value->type == VALUE_ERROR)
		return cell->value;
	return NULL;
}

gboolean
cell_is_number (GnmCell const *cell)
{
	/* FIXME : This does not handle arrays or ranges */
	return (cell->value && VALUE_IS_NUMBER (cell->value));
}

gboolean
cell_is_zero (GnmCell const *cell)
{
	GnmValue const * const v = cell->value;
	if (v == NULL)
		return FALSE;
	switch (v->type) {
	case VALUE_BOOLEAN : return !v->v_bool.val;
	case VALUE_INTEGER : return v->v_int.val == 0;
	case VALUE_FLOAT :
	{
		gnm_float res = v->v_float.val;
		return (-1e-10 < res && res < 1e-10);
	}

	default :
		return FALSE;
	}
}

gboolean
cell_array_bound (GnmCell const *cell, GnmRange *res)
{
	GnmExpr const *expr;

	if (NULL == cell || !cell_has_expr (cell))
		return FALSE;

	g_return_val_if_fail (res != NULL, FALSE);

	expr = cell->base.texpr->expr;
	if (GNM_EXPR_GET_OPER (expr) == GNM_EXPR_OP_ARRAY_ELEM) {
		cell = sheet_cell_get (cell->base.sheet,
			cell->pos.col - expr->array_elem.x,
			cell->pos.row - expr->array_elem.y);

		g_return_val_if_fail (cell != NULL, FALSE);
		g_return_val_if_fail (cell_has_expr (cell), FALSE);

		expr = cell->base.texpr->expr;
	}

	if (GNM_EXPR_GET_OPER (expr) != GNM_EXPR_OP_ARRAY_CORNER)
		return FALSE;

	range_init (res, cell->pos.col, cell->pos.row,
		cell->pos.col + expr->array_corner.cols - 1,
		cell->pos.row + expr->array_corner.rows - 1);
	return TRUE;
}

GnmExprArrayCorner const *
cell_is_array_corner (GnmCell const *cell)
{
	if (cell != NULL && cell_has_expr (cell) &&
	    GNM_EXPR_GET_OPER (cell->base.texpr->expr) == GNM_EXPR_OP_ARRAY_CORNER)
		return &cell->base.texpr->expr->array_corner;
	return NULL;
}

/**
 * cell_is_array :
 * @cell : #GnmCell const *
 *
 * Return TRUE is @cell is part of an array
 **/
gboolean
cell_is_array (GnmCell const *cell)
{
	if (cell != NULL && cell_has_expr (cell))
		switch (GNM_EXPR_GET_OPER (cell->base.texpr->expr)) {
		case GNM_EXPR_OP_ARRAY_CORNER :
		case GNM_EXPR_OP_ARRAY_ELEM :
			return TRUE;
		default :
			break;
		}
	return FALSE;
}

/**
 * cell_is_nonsingleton_array :
 * @cell : #GnmCell const *
 *
 * Return TRUE is @cell is part of an array larger than 1x1
 **/
gboolean
cell_is_nonsingleton_array (GnmCell const *cell)
{
	GnmExprArrayCorner const *corner = cell_is_array_corner (cell);

	return corner && (corner->cols > 1 || corner->rows > 1);
}

/***************************************************************************/

/**
 * cell_render_value :
 * @cell: The cell whose value needs to be rendered
 * @allow_variable_width : Allow format to depend on column width.
 *
 * TODO :
 * The reason the rendered values are stored separately from the GnmCell is
 * that in the future only visible cells will be rendered.  The render
 * will be SheetControl specific to allow for multiple zooms and different
 * display resolutions.
 */
void
cell_render_value (GnmCell *cell, gboolean allow_variable_width)
{
	RenderedValue *rv;
	Sheet *sheet;

	g_return_if_fail (cell != NULL);

	sheet = cell->base.sheet;
	rv = rendered_value_new (cell, cell_get_mstyle (cell),
				 allow_variable_width,
				 sheet->context,
				 sheet->last_zoom_factor_used);
	if (cell->rendered_value)
		rendered_value_destroy (cell->rendered_value);
	cell->rendered_value = rv;
}

/*
 * cell_get_rendered_text:
 *
 * Warning: use this only when you really want what is displayed on the
 * screen.  If the user has decided to display formulas instead of values
 * then that is what you get.
 */
char *
cell_get_rendered_text  (GnmCell *cell)
{
	g_return_val_if_fail (cell != NULL, g_strdup ("ERROR"));

	/* A precursor to just in time rendering Ick! */
	if (cell->rendered_value == NULL)
		cell_render_value (cell, TRUE);

	return g_strdup (rendered_value_get_text (cell->rendered_value));
}


GnmStyle *
cell_get_mstyle (GnmCell const *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	return sheet_style_get (cell->base.sheet,
				cell->pos.col,
				cell->pos.row);
}

/**
 * cell_get_format :
 * @cell :
 *
 * Get the display format.  If the assigned format is General,
 * the format of the value will be used.
 */
GOFormat *
cell_get_format (GnmCell const *cell)
{
	GOFormat *fmt;

	g_return_val_if_fail (cell != NULL, go_format_general ());

	fmt = gnm_style_get_format (cell_get_mstyle (cell));

	g_return_val_if_fail (fmt != NULL, go_format_general ());

	if (go_format_is_general (fmt) &&
	    cell->value != NULL && VALUE_FMT (cell->value))
		fmt = VALUE_FMT (cell->value);

	return fmt;
}

/*
 * cell_set_format:
 *
 * Changes the format for CELL to be FORMAT.  FORMAT should be
 * a number display format as specified on the manual
 *
 * Does not render, redraw, or respan.
 */
void
cell_set_format (GnmCell *cell, char const *format)
{
	GnmRange r;
	GnmStyle *mstyle = gnm_style_new ();

	g_return_if_fail (mstyle != NULL);

	gnm_style_set_format_text (mstyle, format);

	r.start = r.end = cell->pos;
	sheet_style_apply_range (cell->base.sheet, &r, mstyle);
}

/**
 * cell_convert_expr_to_value : drops the expression keeps its value.  Then uses the formatted
 *      result as if that had been entered.
 *
 * NOTE : the cell's expression cannot be linked into the expression * list.
 *
 * The cell is rendered but spans are not calculated,  the cell is NOT marked for
 * recalc.
 *
 * WARNING : This is an internal routine that does not queue redraws,
 *           does not auto-resize, and does not calculate spans.
 *
 * NOTE : This DOES NOT check for array partitioning.
 */
void
cell_convert_expr_to_value (GnmCell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell_has_expr (cell));

	/* Clipboard cells, e.g., are not attached to a sheet.  */
	if (cell_expr_is_linked (cell))
		dependent_unlink (CELL_TO_DEP (cell));

	gnm_expr_top_unref (cell->base.texpr);
	cell->base.texpr = NULL;
}

/****************************************************************************/

void
cell_init (void)
{
#if USE_CELL_POOL
	cell_pool = go_mem_chunk_new ("cell pool",
				       sizeof (GnmCell),
				       128 * 1024 - 128);
#endif
}

#if USE_CELL_POOL
static void
cb_cell_pool_leak (gpointer data, G_GNUC_UNUSED gpointer user)
{
	GnmCell const *cell = data;
	fprintf (stderr, "Leaking cell %p at %s\n", cell, cellpos_as_string (&cell->pos));
}
#endif

void
cell_shutdown (void)
{
#if USE_CELL_POOL
	go_mem_chunk_foreach_leak (cell_pool, cb_cell_pool_leak, NULL);
	go_mem_chunk_destroy (cell_pool, FALSE);
	cell_pool = NULL;
#endif
}

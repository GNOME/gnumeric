/* vim: set sw=8: */
/*
 * cell.c: Cell content and simple management.
 *
 * Author:
 *    Jody Goldberg 2000, 2001 (jody@gnome.org)
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
#include "format.h"
#include "sheet-object-cell-comment.h"
#include "sheet-style.h"
#include "parse-util.h"

#define USE_CELL_POOL 1

/**
 * cell_dirty : Mark the sheet containing the cell as being dirty.
 * @cell : the dirty cell.
 *
 * INTERNAL.
 */
#warning this is just plain silly handle this at a higher level
static inline void
cell_dirty (GnmCell *cell)
{
	Sheet *sheet = cell->base.sheet;

	/* Cells from the clipboard do not have a sheet attached */
	if (sheet)
		sheet_set_dirty (sheet, TRUE);
}

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
			dependent_unlink (CELL_TO_DEP (cell), &cell->pos);
		gnm_expr_unref (cell->base.expression);
		cell->base.expression = NULL;
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
static GnmMemChunk *cell_pool;

/**
 * cell_new:
 * @sheet: sheet in which to allocate cell.
 *
 * Creates a new cell.
 */
GnmCell *
cell_new (void)
{
	GnmCell *cell = USE_CELL_POOL ? gnm_mem_chunk_alloc0 (cell_pool) : g_new0 (GnmCell, 1);
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
	new_cell->base.flags &= ~(DEPENDENT_NEEDS_RECALC|CELL_IN_SHEET_LIST|DEPENDENT_IS_LINKED);

	/* now copy properly the rest */
	if (cell_has_expr (new_cell))
		gnm_expr_ref (new_cell->base.expression);

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
	if (USE_CELL_POOL) gnm_mem_chunk_free (cell_pool, cell); else g_free (cell);
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

	/* 1. Tag the cell as dirty */
	cell_dirty (cell);

	/* 2. If the cell contains a expr, relocate it */
	if (cell_has_expr (cell)) {
		GnmExpr const *expr = gnm_expr_rewrite (cell->base.expression, rwinfo);

#warning make this a precondition
		if (cell_expr_is_linked (cell))
			dependent_unlink (CELL_TO_DEP (cell), &cell->pos);

		/* bounds check, and adjust local references from the cell */
		if (expr != NULL) {
			gnm_expr_unref (cell->base.expression);
			cell->base.expression = expr;
		}

		dependent_link (CELL_TO_DEP (cell), &cell->pos);
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
	GnmExpr const *expr;
	GnmValue      *val;
	GnmParsePos       pos;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (!cell_is_partial_array (cell));

	parse_text_value_or_expr (parse_pos_init_cell (&pos, cell),
		text, &val, &expr, mstyle_get_format (cell_get_mstyle (cell)),
		workbook_date_conv (cell->base.sheet->workbook));

	if (val != NULL) {	/* String was a value */
		cell_cleanout (cell);
		cell->value = val;
		cell_dirty (cell);
	} else {		/* String was an expression */
		cell_set_expr (cell, expr);
		gnm_expr_unref (expr);
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
	g_return_if_fail (!cell_is_partial_array (cell));

	cell_cleanout (cell);
	cell->value = v;
	cell_dirty (cell);
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
cell_set_expr_and_value (GnmCell *cell, GnmExpr const *expr, GnmValue *v,
			 gboolean link_expr)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (expr != NULL);
	g_return_if_fail (!cell_is_partial_array (cell));

	/* Repeat after me.  Ref before unref. */
	gnm_expr_ref (expr);
	cell_cleanout (cell);
	cell_dirty (cell);

	cell->base.expression = expr;
	cell->value = v;
	if (link_expr)
		dependent_link (CELL_TO_DEP (cell), &cell->pos);
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
cell_set_expr_internal (GnmCell *cell, GnmExpr const *expr)
{
	gnm_expr_ref (expr);

	cell_cleanout (cell);

	cell->base.flags |= CELL_HAS_NEW_EXPR;
	cell->base.expression = expr;

	/* Until the value is recomputed, we put in this value.
	 *
	 * We should consider using 0 instead and take out the
	 * cell_needs_recalc call in sheet_foreach_cell_in_range.
	 */
	cell->value = value_new_empty ();
	cell_dirty (cell);
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
cell_set_expr_unsafe (GnmCell *cell, GnmExpr const *expr)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (expr != NULL);

	cell_set_expr_internal (cell, expr);
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
cell_set_expr (GnmCell *cell, GnmExpr const *expr)
{
	g_return_if_fail (!cell_is_partial_array (cell));
	g_return_if_fail (cell != NULL);
	g_return_if_fail (expr != NULL);

	cell_set_expr_internal (cell, expr);
	dependent_link (CELL_TO_DEP (cell), &cell->pos);
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
 * NOTE : Does not add a reference to the expression.  It takes over the callers
 *        reference.
 *
 * Does not regenerate spans, dimensions or autosize cols/rows.
 *
 * DOES NOT CHECK for array partitioning.
 */
void
cell_set_array_formula (Sheet *sheet,
			int col_a, int row_a, int col_b, int row_b,
			GnmExpr const *expr)
{
	int const num_rows = 1 + row_b - row_a;
	int const num_cols = 1 + col_b - col_a;
	int x, y;
	GnmCell * const corner = sheet_cell_fetch (sheet, col_a, row_a);
	GnmExpr const *wrapper;

	g_return_if_fail (num_cols > 0);
	g_return_if_fail (num_rows > 0);
	g_return_if_fail (expr != NULL);
	g_return_if_fail (corner != NULL);
	g_return_if_fail (col_a <= col_b);
	g_return_if_fail (row_a <= row_b);

	wrapper = gnm_expr_new_array (0, 0, num_cols, num_rows, expr);
	cell_set_expr_internal (corner, wrapper);
	gnm_expr_unref (wrapper);

	for (x = 0; x < num_cols; ++x)
		for (y = 0; y < num_rows; ++y) {
			GnmCell *cell;

			if (x == 0 && y == 0)
				continue;

			cell = sheet_cell_fetch (sheet, col_a + x, row_a + y);
			wrapper = gnm_expr_new_array (x, y, num_cols, num_rows, NULL);
			cell_set_expr_internal (cell, wrapper);
			dependent_link (CELL_TO_DEP (cell), &cell->pos);
			gnm_expr_unref (wrapper);
		}

	dependent_link (CELL_TO_DEP (corner), &corner->pos);
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

GnmExprArray const *
cell_is_array (GnmCell const *cell)
{
	if (cell != NULL && cell_has_expr (cell) &&
	    cell->base.expression->any.oper == GNM_EXPR_OP_ARRAY)
		return &cell->base.expression->array;
	return NULL;
}

gboolean
cell_is_partial_array (GnmCell const *cell)
{
	GnmExprArray const *ref = cell_is_array (cell);
	return ref != NULL && (ref->cols > 1 || ref->rows > 1);
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
	GnmStyle *mstyle;
	PangoContext *context;

	g_return_if_fail (cell != NULL);

	mstyle = cell_get_mstyle (cell);
	context = cell->base.sheet->context;

	rv = rendered_value_new (cell, mstyle, allow_variable_width, context);
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
GnmFormat *
cell_get_format (GnmCell const *cell)
{
	GnmFormat *fmt;

	g_return_val_if_fail (cell != NULL, style_format_general ());

	fmt = mstyle_get_format (cell_get_mstyle (cell));

	g_return_val_if_fail (fmt != NULL, style_format_general ());

	if (style_format_is_general (fmt) &&
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
	GnmStyle *mstyle = mstyle_new ();

	g_return_if_fail (mstyle != NULL);

	cell_dirty (cell);
	mstyle_set_format_text (mstyle, format);

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
	g_return_if_fail (cell_has_expr(cell));

	/* Clipboard cells, e.g., are not attached to a sheet.  */
	if (cell_expr_is_linked (cell))
		dependent_unlink (CELL_TO_DEP (cell), &cell->pos);

	gnm_expr_unref (cell->base.expression);
	cell->base.expression = NULL;

	cell_dirty (cell);
}

GnmComment *
cell_has_comment_pos (Sheet const *sheet, GnmCellPos const *pos)
{
	GnmRange r;
	GSList *comments;
	GnmComment *res;

	r.start = r.end = *pos;
	comments = sheet_objects_get (sheet, &r, CELL_COMMENT_TYPE);
	if (!comments)
		return NULL;

	/* This assumes just one comment per cell.  */
	res = comments->data;
	g_slist_free (comments);
	return res;
}


GnmComment *
cell_has_comment (GnmCell const *cell)
{
	return cell_has_comment_pos (cell->base.sheet, &cell->pos);
}

/****************************************************************************/

void
cell_init (void)
{
#if USE_CELL_POOL
	cell_pool = gnm_mem_chunk_new ("cell pool",
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
	gnm_mem_chunk_foreach_leak (cell_pool, cb_cell_pool_leak, NULL);
	gnm_mem_chunk_destroy (cell_pool, FALSE);
	cell_pool = NULL;
#endif
}

/*
 * cell.c: Cell management of the Gnumeric spreadsheet.
 *
 * Author:
 *    Miguel de Icaza 1998, 1999 (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include <ctype.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "eval.h"
#include "value.h"
#include "format.h"
#include "color.h"
#include "border.h"
#include "cursors.h"
#include "gutils.h"
#include "cell.h"
#include "cellspan.h"
#include "gnumeric-util.h"

/* FIXME : Move this into workbook */
static int         redraws_frozen           = 0;
static int         redraws_deep_frozen      = 0;
static GHashTable *cell_hash_queue;

static void
cell_formula_changed (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	sheet_cell_formula_link (cell);

	cell_queue_recalc (cell);
}

static inline void
cell_modified (Cell *cell)
{
	Sheet *sheet = cell->sheet;

	/* Cells from the clipboard do not have a sheet attached */
	if (sheet)
		sheet->modified = TRUE;
}


/* Empty a cell's value, entered_text, and parsed_node.  */
static void
cell_cleanout (Cell *cell)
{
	if (cell->parsed_node){
		/* Clipboard cells, e.g., are not attached to a sheet.  */
		if (cell->sheet)
			sheet_cell_formula_unlink (cell);
		expr_tree_unref (cell->parsed_node);
		cell->parsed_node = NULL;
	}

	if (cell->value) {
		value_release (cell->value);
		cell->value = NULL;
	}

	if (cell->entered_text) {
		string_unref (cell->entered_text);
		cell->entered_text = NULL;
	}
}


void
cell_set_formula (Cell *cell, const char *text)
{
	ExprTree *new_expr;
	char *error_msg = _("ERROR");
	char *desired_format = NULL;
	ParsePosition pp;
	gboolean may_set_format;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);
	g_return_if_fail (gnumeric_char_start_expr_p (*text));

	may_set_format = !cell_has_assigned_format (cell);

	cell_modified (cell);
	new_expr = expr_parse_string (text+1, /* Ignore leading char (=,@,+) */
				      parse_pos_cell (&pp, cell),
				      may_set_format ? &desired_format : NULL,
				      &error_msg);
	cell_cleanout (cell);

	if (new_expr == NULL) {
		cell_set_rendered_text (cell, error_msg);
		cell->entered_text = string_get (text);
		/* FIXME: Supply a proper position?  */
		cell->value = value_new_error (NULL, error_msg);
		return;
	}

	/* Until the value is recomputed, we put in this value.  */
	cell->value = value_new_error (NULL, _("Pending recomputation"));

	if (desired_format) {
		cell_set_format (cell, desired_format);
		g_free (desired_format);
	}

	if (new_expr->oper == OPER_ARRAY) {
		/* The corner sets up the entire array block */
		if (new_expr->u.array.x != 0 || new_expr->u.array.y != 0) {
			/* Throw away the expression, including the inner
			   one.  */
			expr_tree_unref (new_expr->u.array.corner.func.expr);
			expr_tree_unref (new_expr);
			return;
		}

		/*
		 * NOTE : The wrapper supplied by the parser will be released
		 *        and recreated.  new_expr will NOT be valid on exit
		 *        from cell_set_array_formula.
		 */
		cell->parsed_node = new_expr;
		cell_set_array_formula (cell->sheet,
					cell->row->pos, cell->col->pos,
					cell->row->pos +
					    new_expr->u.array.rows -1,
					cell->col->pos +
					    new_expr->u.array.cols -1,
					new_expr->u.array.corner.func.expr);
	} else {
		cell->parsed_node = new_expr;
		cell_formula_changed (cell);
	}
}

void
cell_comment_destroy (Cell *cell)
{
	CellComment *comment;
	GList *l;

	g_return_if_fail (cell != NULL);

	comment = cell->comment;
	if (!comment)
		return;
	cell->comment = NULL;

	/* Free resources */
	string_unref (comment->comment);

	if (comment->timer_tag != -1)
		gtk_timeout_remove (comment->timer_tag);

	if (comment->window)
		gtk_object_destroy (GTK_OBJECT (comment->window));

	for (l = comment->realized_list; l; l = l->next)
		gtk_object_destroy (l->data);
	g_list_free (comment->realized_list);

	g_free (comment);
}

static void
cell_comment_cancel_timer (Cell *cell)
{
	if (cell->comment->timer_tag != -1){
		gtk_timeout_remove (cell->comment->timer_tag);
		cell->comment->timer_tag = -1;
	}

}

static void
cell_display_comment (Cell *cell)
{
	GtkWidget *window, *label;
	int x, y;

	g_return_if_fail (cell != NULL);

	cell_comment_cancel_timer (cell);

	window = gtk_window_new (GTK_WINDOW_POPUP);
	label = gtk_label_new (cell->comment->comment->str);
	gtk_container_add (GTK_CONTAINER (window), label);

	gdk_window_get_pointer (NULL, &x, &y, NULL);
	gtk_widget_set_uposition (window, x+10, y+10);

	gtk_widget_show_all (window);

	cell->comment->window = window;
}

static gint
cell_popup_comment (gpointer data)
{
	Cell *cell = data;

	cell->comment->timer_tag = -1;

	cell_display_comment (cell);
	return FALSE;
}

static int
cell_comment_clicked (GnomeCanvasItem *item, GdkEvent *event, Cell *cell)
{
	GnomeCanvas *canvas = item->canvas;

	switch (event->type){
	case GDK_BUTTON_RELEASE:
		if (event->button.button != 1)
			return FALSE;
		if (cell->comment->window)
			return FALSE;
		cell_display_comment (cell);
		break;

	case GDK_BUTTON_PRESS:
		if (event->button.button != 1)
			return FALSE;
		break;

	case GDK_ENTER_NOTIFY:
		cell->comment->timer_tag = gtk_timeout_add (1000, cell_popup_comment, cell);
		cursor_set_widget (canvas, GNUMERIC_CURSOR_ARROW);
		break;

	case GDK_LEAVE_NOTIFY:
		cell_comment_cancel_timer (cell);
		if (cell->comment->window){
			gtk_object_destroy (GTK_OBJECT (cell->comment->window));
			cell->comment->window = NULL;
		}
		break;

	default:
		return FALSE;
	}
	return TRUE;
}

static void
cell_comment_realize (Cell *cell)
{
	GList *l;

	g_return_if_fail (cell->comment != NULL);

	sheet_cell_comment_link (cell);
	for (l = cell->sheet->sheet_views; l; l = l->next){
		SheetView *sheet_view = SHEET_VIEW (l->data);
		GnomeCanvasItem *o;

		o = sheet_view_comment_create_marker (
			sheet_view,
			cell->col->pos, cell->row->pos);
		gtk_object_ref (GTK_OBJECT (o));

		cell->comment->realized_list = g_list_prepend (
			cell->comment->realized_list, o);

		gtk_signal_connect (GTK_OBJECT (o), "event",
				    GTK_SIGNAL_FUNC (cell_comment_clicked), cell);
	}
}

static void
cell_comment_unrealize (Cell *cell)
{
	GList *l;

	g_return_if_fail (cell->comment != NULL);

	sheet_cell_comment_unlink (cell);
	for (l = cell->comment->realized_list; l; l = l->next){
		GnomeCanvasItem *o = l->data;

		gtk_object_unref (GTK_OBJECT (o));
	}
	g_list_free (cell->comment->realized_list);
	cell->comment->realized_list = NULL;
}

void
cell_realize (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	if (cell->comment)
		cell_comment_realize (cell);
}

void
cell_unrealize (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	if (cell->comment)
		cell_comment_unrealize (cell);
}

void
cell_set_comment (Cell *cell, const char *str)
{
	int had_comments = FALSE;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (str != NULL);

	cell_modified (cell);

	cell_comment_destroy (cell);

	cell->comment = g_new (CellComment, 1);
	cell->comment->realized_list = NULL;
	cell->comment->timer_tag = -1;
	cell->comment->window = NULL;

	cell->comment->comment = string_get (str);

	if (had_comments)
		cell_queue_redraw (cell);

	if (cell->sheet)
		cell_comment_realize (cell);
}

/*
 * cell_set_rendered_text
 * @cell:          the cell we will modify
 * @rendered_text: the text we will display
 *
 * This routine sets the rendered text field of the cell
 * it recomputes the bounding box for the cell as well
 */
static void
cell_set_rendered_text (Cell *cell, const char *rendered_text)
{
	String *oldtext;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (rendered_text != NULL);

	cell_modified (cell);

	oldtext = cell->text;
	cell->text = string_get (rendered_text);
	if (oldtext)
		string_unref (oldtext);

	cell_calc_dimensions (cell, TRUE);
}

char *
cell_get_formatted_val (Cell *cell, StyleColor **colour)
{
	MStyle *mstyle;
	char *txt;

	mstyle = sheet_style_compute (cell->sheet, cell->col->pos,
				      cell->row->pos);
	if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT)) {
		String *tmp = cell->entered_text;
		txt = format_value (mstyle_get_format (mstyle),
				    cell->value, colour,
				    (tmp!=NULL) ? tmp->str : NULL);
	} else {
		g_warning ("No format: serious error");
		txt = g_strdup ("Error");
	}
	mstyle_unref (mstyle);
	return txt;
}

/*
 * cell_render_value
 * @cell: The cell whose value needs to be rendered
 *
 * The value of the cell is formated according to the format style, but if
 * formulas are being displayed then use the text of the formula instead of 
 * its value.
 */
void
cell_render_value (Cell *cell)
{
	StyleColor *color;
	char *str;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->value != NULL);

	if (cell->render_color == (void *)0xdeadbeef) {
		g_error ("This cell is dead!");
	}
	if (cell->render_color) {
		style_color_unref (cell->render_color);
		cell->render_color = NULL;
	}

	if (cell->sheet->display_formulas && cell->parsed_node){
		ParsePosition pp;
		char *tmpstr = expr_decode_tree (cell->parsed_node,
						 parse_pos_cell (&pp, cell));
		str = g_strconcat ("=", tmpstr, NULL);
		g_free (tmpstr);
	} else {
		str = cell_get_formatted_val (cell, &color);		
		cell->render_color = color;
	}

	cell_set_rendered_text (cell, str);
	g_free (str);
}

/*
 * Sets the value for a cell:
 *
 * This is kind of an internal function and should be only called by
 * routines that know what they are doing.  These are the important
 * differences from cell_set_value:
 *
 *    - It does not queue redraws (so you have to queue the redraw yourself
 *      or queue a full redraw).
 *
 *    - It does not queue any recomputations.  You have to queue the recompute
 *      yourself.
 */
void
cell_set_value_simple (Cell *cell, Value *v)
{
	g_return_if_fail (cell);
	g_return_if_fail (v);

	cell_modified (cell);
	cell_cleanout (cell);

	cell->value = v;
	cell_render_value (cell);
}

/*
 * cell_set_value
 *
 * Changes the value of a cell
 */
void
cell_set_value (Cell *cell, Value *v)
{
	g_return_if_fail (cell);
	g_return_if_fail (v);

	cell_queue_redraw (cell);

	cell_set_value_simple (cell, v);
	cell_content_changed (cell);

	cell_queue_redraw (cell);
}

/*
 * Sets the text for a cell:
 *
 * This is kind of an internal function and should be only called by
 * routines that know what they are doing.  These are the important
 * differences from cell_set_text:
 *
 *    - It does not queue redraws (so you have to queue the redraw yourself
 *      or queue a full redraw).
 *
 *    - It does not queue any recomputations.  You have to queue the recompute
 *      yourself.
 *
 *    - It stores the rendered value of the text as if that is what was entered.
 */
void
cell_set_text_simple (Cell *cell, const char *text)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	cell_modified (cell);
	cell_cleanout (cell);

	if (gnumeric_char_start_expr_p (*text) && text[1] != '\0')
		cell_set_formula (cell, text);
	else {
		char *end;
		long l = strtol (text, &end, 10);
		if (l != LONG_MAX && l != LONG_MIN &&
		    text != end && (l == (int)l)) {
			/* Allow and ignore spaces at the end of integers. */
			/* JEG : 2000/3/23 Why for ints and not doubles ? */
			while (*end == ' ')
				end++;
			if (*end == FALSE)
				cell->value = value_new_int (l);
		}

		if (cell->value == NULL) {
			double d;
			d = strtod (text, &end);

			/* It is a floating point number.  */
			if (text != end && *end == 0)
				cell->value = value_new_float ((float_t)d);
		}

#if 0
		if (cell->value == NULL) {
			/* Check to see if it matches via current format */
			if (format_match (text, float_t *v, char **format))
			{
			}
		}
#endif

		if (cell->value == NULL) {
			/* It is text. Ignore leading single quotes */
			cell->value = value_new_string (text[0] == '\'' ? text+1 : text);
			cell->entered_text = string_get (text);
		} else {
			/* NOTE : do not set the entered text, we can not always parse
			 * the result when we reimport later
			 */
			cell->entered_text = NULL;
		}

		/* FIXME : This calls calc_dimension much too early in the
		 * import process.  There is no point calculating spans or
		 * dimensions before the cells neighbours or format has been
		 * applied.
		 */
		cell_render_value (cell);
	}
}

/*
 * cell_content_changed:
 *
 * Call this routine if you modify the contents of a cell
 * to trigger a recompute on any dependencies
 */
void
cell_content_changed (Cell *cell)
{
	GList   *deps;

	g_return_if_fail (cell != NULL);

	/* Queue all of the dependencies for this cell */
	deps = cell_get_dependencies (cell);
	if (deps)
		cell_queue_recalc_list (deps, TRUE);

	sheet_cell_changed (cell);
}


/*
 * cell_set_text
 *
 * Changes the content of a cell and stores
 * the actual entered text.
 */
void
cell_set_text (Cell *cell, const char *text)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	if (cell->parsed_node != NULL && cell->parsed_node->oper == OPER_ARRAY) {
		gnumeric_no_modify_array_notice (cell->sheet->workbook);
		return;
	}

	if (*text == 0)
		g_warning ("Cell value being set to empty string");

	cell_queue_redraw (cell);

	cell_set_text_simple (cell, text);

	/* Store the real entered text */
	if (cell->entered_text != NULL) {
		String *tmp = string_get (text);
		string_unref (cell->entered_text);
		cell->entered_text = tmp;
	}
	cell_content_changed (cell);

	cell_queue_redraw (cell);
}

/**
 * cell_set_formula_tree_simple:
 * @cell:    the cell to set the formula to
 * @formula: an expression tree with the formula
 *
 * This is an internal function.  It should be only called by routines that
 * know what they are doing.  These are the important differences from
 * cell_set_formula:
 *
 *   - It does not queue redraws (so you have to queue the redraw yourself
 *     or queue a full redraw).
 */
void
cell_set_formula_tree_simple (Cell *cell, ExprTree *formula)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (formula != NULL);

	/* Ref before unref.  Repeat after me.  */
	expr_tree_ref (formula);

	cell_modified (cell);
	cell_cleanout (cell);

	cell->parsed_node = formula;
	/* Until the value is recomputed, we put in this value.  */
	cell->value = value_new_error (NULL, _("Circular reference"));
	cell_formula_changed (cell);
}

/**
 * cell_set_array_formula:
 * @sheet:   The sheet to set the formula to.
 * @row_a:   The top row in the destination region.
 * @col_a:   The left column in the destination region.
 * @row_b:   The bottom row in the destination region.
 * @col_b:   The right column in the destination region.
 * @formula: an expression tree with the formula
 *
 * Uses cell_set_formula_tree_simple to store the formula as an
 * 'array-formula'.  The supplied expression is wrapped in an array
 * operator for each cell in the range and scheduled for recalc.  The
 * upper left corner is handled as a special case and care is taken to
 * out it at the head of the recalc queue.
 */
void
cell_set_array_formula (Sheet *sheet,
			int row_a, int col_a, int row_b, int col_b,
			ExprTree *formula)
{
	int const num_rows = 1 + row_b - row_a;
	int const num_cols = 1 + col_b - col_a;
	int x, y;
	Cell * const corner = sheet_cell_fetch (sheet, col_a, row_a);
	Cell * cell = NULL;
	ExprTree *wrapper;

	g_return_if_fail (num_cols > 0);
	g_return_if_fail (num_rows > 0);
	g_return_if_fail (formula != NULL);
	g_return_if_fail (corner != NULL);

	wrapper = expr_tree_array_formula (0, 0, num_rows, num_cols);
	wrapper->u.array.corner.func.value = NULL;
	wrapper->u.array.corner.func.expr = formula;
	expr_tree_ref (formula);
	cell_set_formula_tree_simple (corner, wrapper);
	expr_tree_unref (wrapper);

	/* The corner must be 1st on the recalc list, queue it later */
	cell_unqueue_from_recalc (corner);

	for (x = 0; x < num_cols; ++x)
		for (y = 0; y < num_rows; ++y) {
			if (x == 0 && y == 0)
				continue;
			cell = sheet_cell_fetch (sheet, col_a + x, row_a + y);
			wrapper = expr_tree_array_formula (x, y,
							   num_rows, num_cols);
			wrapper->u.array.corner.cell = corner;
			cell_set_formula_tree_simple (cell, wrapper);
			expr_tree_unref (wrapper);
		}

	/* Put the corner at the head of the recalc list */
	cell_queue_recalc (corner);
}

void
cell_set_formula_tree (Cell *cell, ExprTree *formula)
{
	g_return_if_fail (cell != NULL);

	cell_queue_redraw (cell);

	cell_set_formula_tree_simple (cell, formula);
	cell_content_changed (cell);

	cell_queue_redraw (cell);
}

/**
 * cell_copy:
 * @cell: existing cell to duplicate
 *
 * Makes a copy of a Cell.
 *
 * Returns a copy of the cell.
 */
Cell *
cell_copy (const Cell *cell)
{
	Cell *new_cell;

	g_return_val_if_fail (cell != NULL, NULL);

	new_cell = g_new (Cell, 1);

	/* bitmap copy first */
	*new_cell = *cell;

	new_cell->flags &= ~CELL_QUEUED_FOR_RECALC;

	/* now copy properly the rest */
	if (new_cell->parsed_node)
		expr_tree_ref (new_cell->parsed_node);

	if (new_cell->text)
		string_ref (new_cell->text);

	if (new_cell->entered_text)
		string_ref (new_cell->entered_text);

/*	if (new_cell->style)
	new_cell->style = style_duplicate (new_cell->style);*/

	if (new_cell->render_color)
		style_color_ref (new_cell->render_color);

	if (new_cell->value)
		new_cell->value = value_duplicate (new_cell->value);

	if (cell->comment) {
		new_cell->comment = NULL;
		cell_set_comment (new_cell, cell->comment->comment->str);
	}

	return new_cell;
}

void
cell_destroy (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	if (cell_hash_queue && g_hash_table_lookup (cell_hash_queue, cell)) {
		g_warning ("FIXME: Deleting cell %s which was queued for redraw",
			   cell_name (cell->col->pos, cell->row->pos));
		g_hash_table_remove (cell_hash_queue, cell);
	}

	cell_modified (cell);
	cell_cleanout (cell);

	if (cell->render_color)
		style_color_unref (cell->render_color);
	cell->render_color = (void *)0xdeadbeef;

	cell_comment_destroy (cell);

	if (cell->text)
		string_unref (cell->text);
	cell->text = (void *)0xdeadbeef;

/*	cell->style = (void *)0xdeadbeef;*/

	g_free (cell);
}

void
cell_freeze_redraws (void)
{
	redraws_frozen++;
	if (redraws_frozen == 1)
		cell_hash_queue = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
call_cell_queue_redraw (gpointer key, gpointer value, gpointer user_data)
{
	cell_queue_redraw (value);
}

void
cell_thaw_redraws (void)
{
	redraws_frozen--;
	if (redraws_frozen < 0) {
		g_warning ("unbalanced freeze/thaw\n");
		return;
	}
	if (redraws_frozen == 0) {
		g_hash_table_foreach (cell_hash_queue, call_cell_queue_redraw, NULL);
		g_hash_table_destroy (cell_hash_queue);
		cell_hash_queue = NULL;
	}
}
void
cell_deep_freeze_redraws (void)
{
	redraws_deep_frozen++;
}

void
cell_deep_thaw_redraws (void)
{
	redraws_deep_frozen--;
	if (redraws_frozen < 0)
		g_warning ("unbalanced deep freeze/thaw\n");
}

static void
queue_cell (Cell *cell)
{
	if (g_hash_table_lookup (cell_hash_queue, cell))
		return;
	g_hash_table_insert (cell_hash_queue, cell, cell);
}

void
cell_queue_redraw (Cell *cell)
{
	/* You wake up dead after a deep freeze */
	if (redraws_deep_frozen > 0)
		return;

	g_return_if_fail (cell != NULL);

	if (redraws_frozen) {
		queue_cell (cell);
		return;
	}

	sheet_redraw_cell_region (cell->sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);
}

/*
 * cell_set_format_simple:
 *
 * This routine is similar to cell_set_format, but it does not queue
 * any redraws, nor expects the cell to have a value.
 *
 * Make sure you queue a draw in the future for this cell.
 */
void
cell_set_format_simple (Cell *cell, const char *format)
{
	MStyle *mstyle = mstyle_new ();

	mstyle_set_format (mstyle, format);

	cell_set_mstyle (cell, mstyle);
}

/*
 * cell_set_format:
 *
 * Changes the format for CELL to be FORMAT.  FORMAT should be
 * a number display format as specified on the manual
 */
void
cell_set_format (Cell *cell, const char *format)
{
	g_return_if_fail (cell != NULL);

	cell_set_format_simple (cell, format);

	/* re-render the cell text */
	cell_render_value (cell);
	cell_queue_redraw (cell);
}

void
cell_comment_reposition (Cell *cell)
{
	GList *l;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->comment != NULL);

	/* FIXME : This should use the sheet_view list */
	for (l = cell->comment->realized_list; l; l = l->next){
		GnomeCanvasItem *o = l->data;
		SheetView *sheet_view = GNUMERIC_SHEET (o->canvas)->sheet_view;

		sheet_view_comment_relocate (sheet_view, cell->col->pos, cell->row->pos, o);
	}
}

/*
 * cell_relocate:
 * @cell:           The cell that is changing position
 * @check_bonunds : Should expressions be bounds checked.
 *
 * This routine is used to move a cell to a different location:
 *
 * Auxiliary items canvas items attached to the cell are moved.
 */
void
cell_relocate (Cell *cell, gboolean const check_bounds)
{
	g_return_if_fail (cell != NULL);

	/* 1. Tag the cell as modified */
	cell_modified (cell);

	/* 2. If the cell contains a formula, relocate the formula */
	if (cell->parsed_node) {
		sheet_cell_formula_unlink (cell);

		/*
		 * WARNING WARNING WARNING
		 *
		 * This will only work if the new array cell has already
		 * been inserted.
		 *
		 * WARNING WARNING WARNING
		 */
		/* If cell was part of an array, reset the corner pointer */
		if (cell->parsed_node->oper == OPER_ARRAY) {
			int const x = cell->parsed_node->u.array.x;
			int const y = cell->parsed_node->u.array.y;
			if (x != 0 || y != 0)
				cell->parsed_node->u.array.corner.cell =
					sheet_cell_get (cell->sheet,
							cell->col->pos - x,
							cell->row->pos - y);
		}

		/* We do not actually need to change any references
		 * the move is from its current location to its current
		 * location.  All the move is doing is a bounds check.
		 */
		if (check_bounds) {
			ExprRelocateInfo	rinfo;
			EvalPosition 		pos;
			ExprTree    	*expr = cell->parsed_node;

			rinfo.origin.start.col =
				rinfo.origin.end.col = cell->col->pos;
			rinfo.origin.start.row =
				rinfo.origin.end.row = cell->row->pos;
			rinfo.origin_sheet = rinfo.target_sheet = cell->sheet;
			rinfo.col_offset = 0;
			rinfo.row_offset = 0;
			expr = expr_relocate (expr, eval_pos_cell (&pos, cell), &rinfo);

			if (expr != NULL) {
				expr_tree_unref (cell->parsed_node);
				cell->parsed_node = expr;
			}
		}

		/* The following call also relinks the cell.  */
		cell_formula_changed (cell);
	}

	/* 3. Move any auxiliary canvas items */
	if (cell->comment)
		cell_comment_reposition (cell);

	/* 4. Tag the contents as having changed */
	cell_content_changed (cell);
}

/*
 * This routine drops the formula and just keeps the value
 */
void
cell_make_value (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->parsed_node != NULL);

	/* FIXME: does this work at all?  -- MW */
	cell_modified (cell);
}

int
cell_get_horizontal_align (const Cell *cell, int align)
{
	g_return_val_if_fail (cell != NULL, HALIGN_RIGHT);

	if (align == HALIGN_GENERAL) {
		if (cell->value) {
			if (cell->value->type == VALUE_FLOAT ||
			    cell->value->type == VALUE_INTEGER)
				align = HALIGN_RIGHT;
			else
				align = HALIGN_LEFT;
		} else
			align = HALIGN_RIGHT;
	}

	return align;
}

gboolean inline
cell_is_number (const Cell *cell)
{
	return cell->value && VALUE_IS_NUMBER (cell->value);
}
gboolean
cell_is_zero (const Cell *cell)
{
	Value const * const v = cell->value;
	if (v == NULL)
		return FALSE;
	switch (v->type) {
	case VALUE_BOOLEAN : return !v->v.v_bool;
	case VALUE_INTEGER : return v->v.v_int == 0;
	case VALUE_FLOAT :
	{
		double const res = v->v.v_float;
		return (-1e-10 < res && res < 1e-10);
	}

	default :
		return FALSE;
	}
}

static inline int
cell_contents_fit_inside_column (const Cell *cell)
{
	if (cell->width_pixel <= COL_INTERNAL_WIDTH (cell->col))
		return TRUE;
	else
		return FALSE;
}

/*
 * cell_calculate_span:
 * @cell:   The cell we will examine
 * @col1:   return value: the first column used by this cell
 * @col2:   return value: the last column used by this cell
 *
 * This routine returns the column interval used by a Cell.
 */
void
cell_calculate_span (Cell const * const cell,
		     int * const col1, int * const col2)
{
	Sheet *sheet;
	int align, left;
	int row, pos, margin;
	MStyle *mstyle;

	g_return_if_fail (cell != NULL);

        /*
	 * If the cell is a number, or the text fits inside the column, or the
	 * alignment modes are set to "justify", then we report only one
	 * column is used.
	 */

	if (cell_is_number (cell)) {
		*col1 = *col2 = cell->col->pos;
		return;
	}

	sheet = cell->sheet;
	mstyle = sheet_style_compute (cell->sheet, cell->col->pos,
				      cell->row->pos);
	align = cell_get_horizontal_align (cell,
					   mstyle_get_align_h (mstyle));
	row   = cell->row->pos;

	if ((cell_contents_fit_inside_column (cell) &&
	     align != HALIGN_CENTER_ACROSS_SELECTION) ||
	    align == HALIGN_JUSTIFY ||
	    align == HALIGN_FILL ||
	    mstyle_get_fit_in_cell (mstyle) ||
	    mstyle_get_align_v (mstyle) == VALIGN_JUSTIFY) {
		*col1 = *col2 = cell->col->pos;
		mstyle_unref (mstyle);
		return;
	}
	mstyle_unref (mstyle);

	switch (align) {
	case HALIGN_LEFT:
		*col1 = *col2 = cell->col->pos;
		pos = cell->col->pos + 1;
		left = cell->width_pixel - COL_INTERNAL_WIDTH (cell->col);
		margin = cell->col->margin_b;

		for (; left > 0 && pos < SHEET_MAX_COLS-1; pos++){
			ColRowInfo *ci;
			Cell *sibling;

			sibling = sheet_cell_get (sheet, pos, row);

			if (!cell_is_blank (sibling))
				return;

			ci = sheet_col_get_info (sheet, pos);

			/* The space consumed is:
			 *    - The margin_b from the last column
			 *    - The width of the cell
			 */
			left -= COL_INTERNAL_WIDTH (ci) +
				margin + ci->margin_a;
			margin = ci->margin_b;
			(*col2)++;
		}
		return;

	case HALIGN_RIGHT:
		*col1 = *col2 = cell->col->pos;
		pos = cell->col->pos - 1;
		left = cell->width_pixel - COL_INTERNAL_WIDTH (cell->col);
		margin = cell->col->margin_a;

		for (; left > 0 && pos >= 0; pos--){
			ColRowInfo *ci;
			Cell *sibling;

			sibling = sheet_cell_get (sheet, pos, row);

			if (!cell_is_blank (sibling))
				return;

			ci = sheet_col_get_info (sheet, pos);

			/* The space consumed is:
			 *   - The margin_a from the last column
			 *   - The width of this cell
			 */
			left -= COL_INTERNAL_WIDTH (ci) +
				margin + ci->margin_b;
			margin = ci->margin_a;
			(*col1)--;
		}
		return;

	case HALIGN_CENTER: {
		int left_left, left_right;
		int margin_a, margin_b;

		*col1 = *col2 = cell->col->pos;
		left = cell->width_pixel -  COL_INTERNAL_WIDTH (cell->col);

		left_left  = left / 2 + (left % 2);
		left_right = left / 2;
		margin_a = cell->col->margin_a;
		margin_b = cell->col->margin_b;

		for (; left_left > 0 || left_right > 0;){
			ColRowInfo *ci;
			Cell *left_sibling, *right_sibling;

			if (*col1 - 1 >= 0){
				left_sibling = sheet_cell_get (sheet, *col1 - 1, row);

				if (!cell_is_blank (left_sibling))
					left_left = 0;
				else {
					ci = sheet_col_get_info (sheet, *col1 - 1);

					left_left -= COL_INTERNAL_WIDTH (ci) +
						margin_a + ci->margin_b;
					margin_a = ci->margin_a;
					(*col1)--;
				}
			} else
				left_left = 0;

			if (*col2 + 1 < SHEET_MAX_COLS-1){
				right_sibling = sheet_cell_get (sheet, *col2 + 1, row);

				if (!cell_is_blank (right_sibling))
					left_right = 0;
				else {
					ci = sheet_col_get_info (sheet, *col2 + 1);

					left_right -= COL_INTERNAL_WIDTH (ci) +
						margin_b + ci->margin_a;
					margin_b = ci->margin_b;
					(*col2)++;
				}
			} else
				left_right = 0;

		} /* for */
		break;
	} /* case HALIGN_CENTER */

	case HALIGN_CENTER_ACROSS_SELECTION:
	{
		int tmp;
		int const row = cell->row->pos;
		int left = cell->col->pos, right = left;

		left_loop :
			tmp = left - 1;
			if (tmp >= 0 &&
			    cell_is_blank (sheet_cell_get (sheet, tmp, row))) {
				MStyle * const mstyle =
				    sheet_style_compute (cell->sheet, tmp, row);
				gboolean const res =
				    (mstyle_get_align_h (mstyle) == HALIGN_CENTER_ACROSS_SELECTION);
				mstyle_unref (mstyle);

				if (res) {
					left = tmp;
					goto left_loop;
				}
			}
		right_loop :
			tmp = right + 1;
			if (tmp < SHEET_MAX_COLS &&
			    cell_is_blank (sheet_cell_get (sheet, tmp, row))) {
				MStyle * const mstyle =
				    sheet_style_compute (cell->sheet, tmp, row);
				gboolean const res =
				    (mstyle_get_align_h (mstyle) == HALIGN_CENTER_ACROSS_SELECTION);
				mstyle_unref (mstyle);

				if (res) {
					right = tmp;
					goto right_loop;
				}
			}

		*col1 = left;
		*col2 = right;
		break;
	}

	default:
		g_warning ("Unknown horizontal alignment type %d\n", align);
		*col1 = *col2 = cell->col->pos;
	} /* switch */
}

/*
 * calc_text_dimensions
 * @cell:      The cell we are working on.
 * @style:     the style formatting constraints (font, alignments)
 * @text:      the string contents.
 *
 * Computes the width and height used by the cell based on alignments
 * constraints in the style using the font specified on the style.
 *
 * NOTE :
 * The line splitting code is VERY similar to cell-draw.c:cell_split_text
 * please keep it that way.
 */
static void
calc_text_dimensions (Cell *cell,
		      MStyle const * const mstyle,
		      char const * const text)
{
	StyleFont * const style_font =
	    sheet_view_get_style_font (cell->sheet, mstyle);
	GdkFont * const gdk_font = style_font_gdk_font (style_font);
	int const font_height    = style_font_get_height (style_font);
	int const text_width     = gdk_string_measure (gdk_font, text);
	int const cell_w = COL_INTERNAL_WIDTH (cell->col);

	if (text_width < cell_w || cell_is_number (cell)) {
		cell->width_pixel  = text_width;
		cell->height_pixel = font_height;
	} else if (mstyle_get_align_h (mstyle) == HALIGN_JUSTIFY ||
		   mstyle_get_align_v (mstyle) == VALIGN_JUSTIFY ||
		   mstyle_get_fit_in_cell (mstyle)) {
		char const *p, *line_begin;
		char const *first_whitespace = NULL;
		char const *last_whitespace = NULL;
		gboolean prev_was_space = FALSE;
		int used = 0, used_last_space = 0;
		int h = 0;

		cell->width_pixel  = cell_w;

		for (line_begin = p = text; *p; p++){
			int const len_current = gdk_text_width (gdk_font, p, 1);

			/* Wrap if there is an embeded newline, or we have overflowed */
			if (*p == '\n' || used + len_current > cell_w){
				char const *begin = line_begin;
				int len;

				if (*p == '\n'){
					/* start after newline, preserve whitespace */
					line_begin = p+1;
					len = p - begin;
					used = 0;
				} else if (last_whitespace != NULL){
					/* Split at the run of whitespace */
					line_begin = last_whitespace + 1;
					len = first_whitespace - begin;
					used = len_current + used - used_last_space;
				} else {
					/* Split before the current character */
					line_begin = p; /* next line starts here */
					len = p - begin;
					used = len_current;
				}

				h += font_height;
				first_whitespace = last_whitespace = NULL;
				prev_was_space = FALSE;
				continue;
			}

			used += len_current;
			if (*p == ' '){
				used_last_space = used;
				last_whitespace = p;
				if (!prev_was_space)
					first_whitespace = p;
				prev_was_space = TRUE;
			} else
				prev_was_space = FALSE;
		}

		/* Catch the final bit that did not wrap */
		if (*line_begin)
			h += font_height;

		cell->height_pixel = h;
	} else {
		cell->width_pixel  = text_width;
		cell->height_pixel = font_height;
	}
	style_font_unref (style_font);
}

/*
 * cell_calc_dimensions
 * @cell:  The cell
 * @auto_resize_height : If true the reow height should be resized if needed.
 *
 * This routine updates the dimensions of the the rendered text of a cell
 */
void
cell_calc_dimensions (Cell *cell, gboolean const auto_resize_height)
{
	CellSpanInfo const * otherspan;
	int  left, right;

	g_return_if_fail (cell != NULL);

	cell_unregister_span (cell);

	/* In case there was a different cell that used to span into this cell */
	if (NULL != (otherspan = row_span_get (cell->row, cell->col->pos))) {
		Cell *other = otherspan->cell;
		cell_queue_redraw (other);
		cell_calc_dimensions (other, FALSE);
		cell_queue_redraw (other);
	}

	if (cell->text) {
		char *rendered_text = cell->text->str;
		MStyle *mstyle = sheet_style_compute (cell->sheet,
						      cell->col->pos,
						      cell->row->pos);

		calc_text_dimensions (cell, mstyle, rendered_text);

		if (auto_resize_height && !cell->row->hard_size) {
			/* Text measurements do not include margins or grid line */
			/* FIXME : This never shrinks things.  Switch to an async approach
			 * that will flag the need to update here, and will autofit later
			 */
			int const height_pixels = cell->height_pixel +
			    cell->row->margin_a + cell->row->margin_b + 1;
			if (height_pixels > cell->row->size_pixels)
				sheet_row_set_size_pixels (cell->sheet, cell->row->pos,
							   height_pixels, FALSE);
		}

		mstyle_unref (mstyle);
	} else
		cell->width_pixel = 0;

	/* Register the span */
	cell_calculate_span (cell, &left, &right);
	if (left != right)
		cell_register_span (cell, left, right);
}

/**
 * cell_get_text:
 * @cell: the cell to fetch the text from.
 *
 * Returns a g_malloced () version of the contents of the cell.  It will
 * return a formula if it is a formula, or a string value rendered with the
 * current format.
 *
 * This should not be used by routines that need the actual content
 * of the cell value in a reliable way
 */
char *
cell_get_text (Cell *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);

	if (cell->parsed_node && cell->sheet){
		char *func, *ret;
		ParsePosition pp;

		func = expr_decode_tree (cell->parsed_node,
					 parse_pos_cell (&pp, cell));
		ret = g_strconcat ("=", func, NULL);
		g_free (func);

		return ret;
	}

	if (cell->entered_text)
		return g_strdup (cell->entered_text->str);
	else
		return cell_get_formatted_val (cell, NULL);
}

/**
 * cell_get_content:
 * @cell: the cell from which we want to pull the content from
 *
 * This returns a g_malloc()ed region of memory with a text representation
 * of the cell contents.
 *
 * This will return a text expression if the cell contains a formula, or
 * a string representation of the value.
 */
char *
cell_get_content (Cell *cell)
{
	g_return_val_if_fail (cell != NULL, NULL);

	if (cell->parsed_node){
		char *func, *ret;
		ParsePosition pp;

		func = expr_decode_tree (cell->parsed_node,
					 parse_pos_cell (&pp, cell));
		ret = g_strconcat ("=", func, NULL);
		g_free (func);

		return ret;
	}

	/*
	 * Return the value without parsing.
	 */
	if (cell->entered_text)
		return g_strdup (cell->entered_text->str);
	else
		return value_get_as_string (cell->value);
}

char *
cell_get_comment (Cell *cell)
{
	char *str;

	g_return_val_if_fail (cell != NULL, NULL);

	if (cell->comment)
		str = g_strdup (cell->comment->comment->str);
	else
		str = NULL;

	return str;
}

gboolean
cell_is_blank (Cell const * const cell)
{
	return (cell == NULL || cell->value == NULL ||
		cell->value->type == VALUE_EMPTY);
}

Value *
cell_is_error (Cell const * const cell)
{
	g_return_val_if_fail (cell != NULL, NULL);
	g_return_val_if_fail (cell->value != NULL, NULL);

	if (cell->value->type == VALUE_ERROR)
		return cell->value;
	return NULL;
}

MStyle *
cell_get_mstyle (const Cell *cell)
{
	MStyle *mstyle;

	mstyle = sheet_style_compute (cell->sheet,
				      cell->col->pos,
				      cell->row->pos);
	return mstyle;
}

/*
Style *
cell_get_style (const Cell *cell)
{
	MStyle *mstyle;
	Style  *ans;

	mstyle = sheet_style_compute (cell->sheet,
				      cell->col->pos,
				      cell->row->pos);
	ans = style_new_mstyle (mstyle, MSTYLE_ELEMENT_MAX,
				cell->sheet->last_zoom_factor_used);
	mstyle_unref (mstyle);

	return ans;
}
*/

void
cell_set_mstyle (const Cell *cell, MStyle *mstyle)
{
	Range         range;

	range.start.col = cell->col->pos;
	range.start.row = cell->row->pos;
	range.end       = range.start;

	sheet_style_attach (cell->sheet, range, mstyle);
}

gboolean
cell_has_assigned_format (const Cell *cell)
{
	MStyle *mstyle;
	gboolean result;

	mstyle = sheet_style_compute (cell->sheet, cell->col->pos,
				      cell->row->pos);
	if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT)) {
		const char *format;
		format = mstyle_get_format (mstyle)->format;
		/* FIXME: we really should distinguish between "not assigned"
		   and "assigned General".  */
		result = (format && strcmp (format, "General") != 0);
	} else
		result = FALSE;

	mstyle_unref (mstyle);
	return result;
}


char *
cell_get_format (const Cell *cell)
{
	MStyle *mstyle;
	char *result;

	mstyle = sheet_style_compute (cell->sheet, cell->col->pos,
				      cell->row->pos);
	if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT)) {
		const char *format;
		format = mstyle_get_format (mstyle)->format;
		/* FIXME: we really should distinguish between "not assigned"
		   and "assigned General".  */
		if (format && strcmp (format, "General") != 0)
			result = g_strdup (format);
		else
			result = NULL;
	} else
		result = NULL;

	mstyle_unref (mstyle);
	return result;
}

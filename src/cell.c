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
#include "gnumeric-sheet.h"
#include "gnumeric-util.h"
#include "eval.h"
#include "format.h"
#include "color.h"
#include "border.h"
#include "cursors.h"
#include "utils.h"
#include "gnumeric-util.h"

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
	const char *desired_format = NULL;
	ParsePosition pp;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	cell_modified (cell);
	new_expr = expr_parse_string (&text [1],
				      parse_pos_cell (&pp, cell),
				      &desired_format,
				      &error_msg);
	cell_cleanout (cell);

	if (new_expr == NULL){
		cell_set_rendered_text (cell, error_msg);
		cell->entered_text = string_get (text);
		/* FIXME: Supply a proper position?  */
		cell->value = value_new_error (NULL, error_msg);
		return;
	}

	if (desired_format)
		cell_set_format (cell, desired_format);

	if (new_expr->oper == OPER_ARRAY){
		/* The corner sets up the entire array block */
		if (new_expr->u.array.x != 0 || new_expr->u.array.y != 0) {
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
		/* Until the value is recomputed, we put in this value.  */
		cell->value = value_new_error (NULL, _("Pending recomputation"));
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
void
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

	cell_calc_dimensions (cell);
}

static char *
cell_get_formatted_val (Cell *cell, StyleColor **col)
{
	MStyle *mstyle;
	char *txt;
	
	mstyle = sheet_style_compute (cell->sheet, cell->col->pos,
				      cell->row->pos);
	if (mstyle_is_element_set (mstyle, MSTYLE_FORMAT))
		txt = format_value (mstyle_get_format (mstyle),
				    cell->value, col);
	else {
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
 * The value of the cell is formated according to the format style
 */
void
cell_render_value (Cell *cell)
{
	StyleColor *color;
	char *str;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->value != NULL);

	if (cell->render_color) {
		style_color_unref (cell->render_color);
		cell->render_color = NULL;
	}

	str = cell_get_formatted_val (cell, &color);
	cell->render_color = color;

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
 */
void
cell_set_text_simple (Cell *cell, const char *text)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	cell_modified (cell);
	cell_cleanout (cell);

 	if (text [0] == '=' && text [1] != 0){
		cell_set_formula (cell, text);
	} else {
		char *end;
		long l;
		int  set=0;

		l = strtol (text, &end, 10);
		if (l != LONG_MAX && l != LONG_MIN &&
		    text != end && (l == (int)l)) {
			/* Allow and ignore spaces at the end of integers.  */
			while (*end == ' ')
				end++;
			if (*end == 0) {
				cell->value = value_new_int (l);
				set = 1;
			}
		}
		
		if (!set) {
			double d;
			d = strtod (text, &end);
			if (text != end && *end == 0) {
				/* It is a floating point number.  */
				cell->value = value_new_float ((float_t)d);
			} else {
				/* It is text.  */
				cell->value = value_new_string (text);
			}
		}

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
}


/*
 * cell_set_text
 *
 * Changes the content of a cell
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

	cell_queue_redraw (cell);

	cell_set_text_simple (cell, text);
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
 *
 *   - It does not queue any recomputations.  You have to queue the
 *     recompute yourself.
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
			cell = sheet_cell_fetch (sheet, col_a+x,row_a+y);
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
	if (redraws_frozen < 0){
		g_warning ("unbalanced freeze/thaw\n");
		return;
	}
	if (redraws_frozen == 0){
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

	if (redraws_frozen){
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

	for (l = cell->comment->realized_list; l; l = l->next){
		GnomeCanvasItem *o = l->data;
		SheetView *sheet_view = GNUMERIC_SHEET (o->canvas)->sheet_view;

		sheet_view_comment_relocate (sheet_view, cell->col->pos, cell->row->pos, o);
	}
}

/*
 * cell_relocate:
 * @cell:     The cell that is changing position
 *
 * This routine is used to move a cell to a different location:
 *
 * Auxiliary items canvas items attached to the cell are moved.
 */
void
cell_relocate (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	/* 1. Tag the cell as modified */
	cell_modified (cell);

	/* 2. If the cell contains a formula, relocate the formula */
	if (cell->parsed_node){
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

		/* The following call also relinks the cell.  */
		cell_formula_changed (cell);
	}

	/* 3. Move any auxiliary canvas items */
	if (cell->comment)
		cell_comment_reposition (cell);
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

int inline
cell_is_number (const Cell *cell)
{
	return cell->value && VALUE_IS_NUMBER (cell->value);
}

static inline int
cell_contents_fit_inside_column (const Cell *cell)
{
	if (cell->width < COL_INTERNAL_WIDTH (cell->col))
		return TRUE;
	else
		return FALSE;
}

/*
 * cell_get_span:
 * @cell:   The cell we will examine
 * @col1:   return value: the first column used by this cell
 * @col2:   return value: the last column used by this cell
 *
 * This routine returns the column interval used by a Cell.
 */
void
cell_get_span (Cell *cell, int *col1, int *col2)
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

	if (cell_is_number (cell) ||
	    cell_contents_fit_inside_column (cell)) {
		*col1 = *col2 = cell->col->pos;
		return;
	}

	sheet = cell->sheet;
	mstyle = sheet_style_compute (cell->sheet, cell->col->pos,
				      cell->row->pos);
	align = cell_get_horizontal_align (cell,
					   mstyle_get_align_h (mstyle));
	row   = cell->row->pos;

	if (align == HALIGN_JUSTIFY ||
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
		left = cell->width - COL_INTERNAL_WIDTH (cell->col);
		margin = cell->col->margin_b;

		for (; left > 0 && pos < SHEET_MAX_COLS-1; pos++){
			ColRowInfo *ci;
			Cell *sibling;

			sibling = sheet_cell_get (sheet, pos, row);

			if (!cell_is_blank(sibling))
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
		left = cell->width - COL_INTERNAL_WIDTH (cell->col);
		margin = cell->col->margin_a;

		for (; left > 0 && pos >= 0; pos--){
			ColRowInfo *ci;
			Cell *sibling;

			sibling = sheet_cell_get (sheet, pos, row);

			if (!cell_is_blank(sibling))
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
		left = cell->width -  COL_INTERNAL_WIDTH (cell->col);

		left_left  = left / 2 + (left % 2);
		left_right = left / 2;
		margin_a = cell->col->margin_a;
		margin_b = cell->col->margin_b;

		for (; left_left > 0 || left_right > 0;){
			ColRowInfo *ci;
			Cell *left_sibling, *right_sibling;

			if (*col1 - 1 >= 0){
				left_sibling = sheet_cell_get (sheet, *col1 - 1, row);

				if (!cell_is_blank(left_sibling))
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

				if (!cell_is_blank(right_sibling))
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

	default:
		g_warning ("Unknown horizontal alignment type %d\n", align);
		*col1 = *col2 = cell->col->pos;
	} /* case HALIGN_CENTER */

	} /* switch */
}

/*
 * calc_text_dimensions
 * @is_number: whether we are computing the size for a number.
 * @style:     the style formatting constraints (font, alignments)
 * @text:      the string contents.
 * @cell_w:    the cell width
 * @cell_h:    the cell height
 * @h:         return value: the height used
 * @w:         return value: the width used.
 *
 * Computes the width and height used by the cell based on alignments
 * constraints in the style using the font specified on the style.
 */
static void
calc_text_dimensions (int is_number, MStyle *mstyle,
		      const char *text, int cell_w, int cell_h, int *h, int *w,
		      double zoom)
{
	StyleFont *style_font = mstyle_get_font (mstyle, zoom);
	GdkFont *gdk_font = style_font->dfont->gdk_font;
	int text_width, font_height;
	
	text_width = gdk_string_measure (gdk_font, text);
	font_height = style_font_get_height (style_font);
	
	if (text_width < cell_w || is_number){
		*w = text_width;
		*h = font_height;
	} else if (mstyle_get_align_h (mstyle) == HALIGN_JUSTIFY ||
	    mstyle_get_align_v (mstyle) == VALIGN_JUSTIFY ||
	    mstyle_get_fit_in_cell (mstyle)) {
		const char *ideal_cut_spot = NULL;
		int  used, last_was_cut_point;
		const char *p = text;
		*w = cell_w;
		*h = font_height;

		used = 0;
		last_was_cut_point = FALSE;

		for (; *p; p++) {
			int len;

			if (last_was_cut_point && *p != ' ')
				ideal_cut_spot = p;
			
			len = gdk_text_measure (gdk_font, p, 1);

			/* If we have overflowed the cell, wrap */
			if (used + len > cell_w){
				if (ideal_cut_spot){
					int n = p - ideal_cut_spot;
					used = gdk_text_measure (
						gdk_font, ideal_cut_spot, n);
				} else {
					used = len;
				}
				*h += font_height;
				ideal_cut_spot = NULL;
			} else
				used += len;

			if (*p == ' ')
				last_was_cut_point = TRUE;
			else
				last_was_cut_point = FALSE;
		}
	} else {
		*w = text_width;
		*h = font_height;
	}
	style_font_unref (style_font);
}

/*
 * cell_calc_dimensions
 * @cell:  The cell
 *
 * This routine updates the dimensions of the the rendered text of a cell
 */
void
cell_calc_dimensions (Cell *cell)
{
	char *rendered_text;
	int  left, right;

	g_return_if_fail (cell != NULL);

	cell_unregister_span (cell);

	if (cell->text) {
		MStyle *mstyle = sheet_style_compute (cell->sheet,
						      cell->col->pos,
						      cell->row->pos);
		int h, w;

		rendered_text = cell->text->str;
		calc_text_dimensions (cell_is_number (cell),
				      mstyle, rendered_text,
				      COL_INTERNAL_WIDTH (cell->col),
				      ROW_INTERNAL_HEIGHT (cell->row),
				      &h, &w,
				      cell->sheet->last_zoom_factor_used);

		cell->width  = cell->col->margin_a + cell->col->margin_b + w;
		cell->height = cell->row->margin_a + cell->row->margin_b + h;

		if (cell->height > cell->row->pixels && !cell->row->hard_size)
			sheet_row_set_internal_height (cell->sheet, cell->row, h);

		mstyle_unref (mstyle);
	} else
		cell->width = cell->col->margin_a + cell->col->margin_b;

	/* Register the span */
	cell_get_span (cell, &left, &right);
	if (left != right)
		cell_register_span (cell, left, right);
}

/**
 * cell_get_text:
 * @cell: the cell to fetch the text from.
 *
 * Returns a g_malloced() version of the contents of the cell.  It will
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
cell_is_blank(Cell *cell)
{
	if (cell == NULL || cell->value == NULL ||
	    cell->value->type == VALUE_EMPTY)
		return TRUE;

	/* FIXME FIXME : this won't be necessary when we have a VALUE_EMPTY */
	return (cell->value->type == VALUE_STRING &&
		*(cell->value->v.str->str) == '\0');
}

Value *
cell_is_error (Cell const *cell)
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


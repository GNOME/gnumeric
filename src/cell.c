/*
 * cell.c: Cell management of the Gnumeric spreadsheet.
 *
 * Author:
 *    Miguel de Icaza 1998 (miguel@kernel.org)
 */
#include <config.h>
#include <gnome.h>
#include <locale.h>
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "eval.h"
#include "format.h"
#include "color.h"
#include "cursors.h"

static int         redraws_frozen = 0;
static GHashTable *cell_hash_queue;

void
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

void
cell_set_formula (Cell *cell, char *text)
{
	char *error_msg = NULL;
	char *desired_format = NULL;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	cell_modified (cell);
	cell->parsed_node = expr_parse_string (&text [1],
					       cell->col->pos,
					       cell->row->pos,
					       &desired_format,
					       &error_msg);
	if (cell->parsed_node == NULL){
		if (cell->text)
			string_unref_ptr (&cell->text);
		cell->text = string_get (error_msg);
		return;
	}
	if (desired_format && strcmp (cell->style->format->format, "General") == 0){
		style_format_unref (cell->style->format);
		cell->style->format = style_format_new (desired_format);
	}

	cell_formula_changed (cell);
}

/*
 * cell_set_alignment:
 *
 * @cell: the cell to change the alignment of
 * @halign: the horizontal alignemnt
 * @valign: the vertical alignemnt
 * @orient: the text orientation
 *
 * This routine changes the alignment of a cell to those specified.
 */
void
cell_set_alignment (Cell *cell, int halign, int valign, int orient, int auto_return)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->style != NULL);

	if ((cell->style->halign      == halign) &&
	    (cell->style->valign      == valign) &&
	    (cell->style->fit_in_cell == auto_return) &&
	    (cell->style->orientation == orient))
		return;

	cell_modified (cell);

	cell_queue_redraw (cell);
	
	cell->style->halign = halign;
	cell->style->valign = valign;
	cell->style->orientation = orient;
	cell->style->fit_in_cell = auto_return;
	
	cell_calc_dimensions (cell);

	cell_queue_redraw (cell);
}

void
cell_set_halign (Cell *cell, StyleHAlignFlags halign)
{
	g_return_if_fail (cell != NULL);

	if (((unsigned int)cell->style->halign) == ((unsigned int) halign))
		return;

	cell_modified (cell);

	cell_queue_redraw (cell);
	cell->style->halign = halign;

	cell_calc_dimensions (cell);
	cell_queue_redraw (cell);
}

void
cell_set_font_from_style (Cell *cell, StyleFont *style_font)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (style_font != NULL);

	cell_modified (cell);

	cell_queue_redraw (cell);
	
	style_font_unref (cell->style->font);
	style_font_ref (style_font);
	
	cell->style->font = style_font;

	cell_calc_dimensions (cell);
	
	cell_queue_redraw (cell);
}

void
cell_set_font (Cell *cell, char *font_name)
{
	StyleFont *style_font;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (font_name != NULL);

	style_font = style_font_new (font_name, 1);

	if (style_font)
		cell_set_font_from_style (cell, style_font);
}

void
cell_set_style (Cell *cell, Style *reference_style)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (reference_style != NULL);

	cell_modified (cell);

	cell_queue_redraw (cell);
	style_destroy (cell->style);
	cell->style = style_duplicate (reference_style);
	if (cell->value)
		cell_render_value (cell);
	cell_calc_dimensions (cell);
	cell_queue_redraw (cell);
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
	for (l = ((Sheet *)cell->sheet)->sheet_views; l; l = l->next){
		SheetView *sheet_view = SHEET_VIEW (l->data);
		GnomeCanvasItem *o;
		
		o = sheet_view_comment_create_marker (
			sheet_view, 
			cell->col->pos, cell->row->pos);

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

		printf ("Destruyendo: %p\n", o);
		gtk_object_destroy (GTK_OBJECT (o));
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
cell_set_comment (Cell *cell, char *str)
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

void
cell_set_foreground (Cell *cell, gushort red, gushort green, gushort blue)
{
	g_return_if_fail (cell != NULL);

	cell_modified (cell);
	
	if (cell->style->valid_flags & STYLE_FORE_COLOR)
		style_color_unref (cell->style->fore_color);

	cell->style->valid_flags |= STYLE_FORE_COLOR;
	cell->style->fore_color = style_color_new (red, green, blue);

	cell_queue_redraw (cell);
}

void
cell_set_background (Cell *cell, gushort red, gushort green, gushort blue)
{
	g_return_if_fail (cell != NULL);
	
	cell_modified (cell);

	if (cell->style->valid_flags & STYLE_BACK_COLOR)
		style_color_unref (cell->style->back_color);

	cell->style->valid_flags |= STYLE_BACK_COLOR;
	cell->style->back_color = style_color_new (red, green, blue);

	cell_queue_redraw (cell);
}


void
cell_set_pattern (Cell *cell, int pattern)
{
	g_return_if_fail (cell != NULL);

	cell_modified (cell);

	cell->style->valid_flags |= STYLE_PATTERN;
	cell->style->pattern = pattern;

	cell_queue_redraw (cell);
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
cell_set_rendered_text (Cell *cell, char *rendered_text)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (rendered_text != NULL);
	
	cell_modified (cell);

	if (cell->text)
		string_unref (cell->text);

	cell->text = string_get (rendered_text);
	cell_calc_dimensions (cell);
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

	if (cell->render_color){
		style_color_unref (cell->render_color);
		cell->render_color = NULL;
	}

	str = format_value (cell->style->format, cell->value, &color);
	cell->render_color = color;
	
	cell_set_rendered_text (cell, str);
	g_free (str);
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
cell_set_text_simple (Cell *cell, char *text)
{
	struct lconv *lconv;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	cell_modified (cell);

	if (cell->value){
		value_release (cell->value);
		cell->value = NULL;
	}
	
	if (cell->parsed_node){
		sheet_cell_formula_unlink (cell);

		expr_tree_unref (cell->parsed_node);
		cell->parsed_node = NULL;
	}

 	if (text [0] == '='){
		cell_set_formula (cell, text); 
	} else {
		Value *v = g_new (Value, 1);
		int is_text, is_float, maybe_float, has_digits;
		int seen_exp;
		char *p;

		lconv = localeconv ();
		
		is_text = is_float = maybe_float = has_digits = FALSE;
		seen_exp = FALSE;
		for (p = text; *p && !is_text; p++){
			switch (*p){
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				has_digits = TRUE;
				break;

			case '-':
				if (p == text)
					break;
				if (seen_exp)
					is_text = TRUE;
				/* falldown */
				
			case 'E': case 'e': case '+': case ':': case '.': case ',':
				if (*p == 'e' || *p == 'E')
					seen_exp = TRUE;
				
				if (*p == ',' || *p == '.')
					if (*lconv->decimal_point != *p){
						is_text = TRUE;
						break;
					}
				maybe_float = TRUE;
				break;

			default:
				is_text = TRUE;
			}
		}
		if (has_digits && maybe_float)
			is_float = TRUE;
		
		if (has_digits && !is_text){
			if (is_float){
				v->type = VALUE_FLOAT;
				float_get_from_range (text, text+strlen(text),
						      &v->v.v_float);
			} else {
				v->type = VALUE_INTEGER;
				int_get_from_range (text, text+strlen (text),
						    &v->v.v_int);
			}
		} else {
			v->type = VALUE_STRING;
			v->v.str = string_get (text);
		}
		cell->value = v;
		
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
	deps = cell_get_dependencies (cell->sheet,
				      cell->col->pos,
				      cell->row->pos);
	if (deps)
		cell_queue_recalc_list (deps);
}

/*
 * cell_set_text
 *
 * Changes the content of a cell
 */
void
cell_set_text (Cell *cell, char *text)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);
	
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
 *   - It does not queue any recomputations.  YOu have to queue the
 *     recompute yourself. 
 */
void
cell_set_formula_tree_simple (Cell *cell, ExprTree *formula)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (formula != NULL);

	cell_modified (cell);

	if (cell->parsed_node)
		expr_tree_unref (cell->parsed_node);

	cell->parsed_node = formula;
	expr_tree_ref (formula);
	cell_formula_changed (cell);
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
 * Returns a copy of the cell.  Note that the col, row and sheet
 * fields are set to NULL.
 */
Cell *
cell_copy (Cell *cell)
{
	Cell *new_cell;

	g_return_val_if_fail (cell != NULL, NULL);

	new_cell = g_new (Cell, 1);

	/* bitmap copy first */
	*new_cell = *cell;

	new_cell->col   = NULL;
	new_cell->row   = NULL;
	new_cell->sheet = NULL;
	
	/* now copy propertly the rest */
	if (new_cell->parsed_node)
		expr_tree_ref   (new_cell->parsed_node);
	string_ref      (new_cell->text);
	
	new_cell->style = style_duplicate (new_cell->style);
	new_cell->value = value_duplicate (new_cell->value);

	new_cell->comment = NULL;
	if (cell->comment)
		cell_set_comment (new_cell, cell->comment->comment->str);

	return new_cell;
}

void
cell_destroy (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	cell_modified (cell);

	if (cell->parsed_node){
		expr_tree_unref (cell->parsed_node);
	}

	if (cell->render_color)
		style_color_unref (cell->render_color);

	cell_comment_destroy (cell);

	if (cell->text)
		string_unref  (cell->text);

	style_destroy (cell->style);
	value_release (cell->value);

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
cell_set_format_simple (Cell *cell, char *format)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (format != NULL);
	
	if (strcmp (format, cell->style->format->format) == 0)
		return;

	/* Change the format */
	cell_modified (cell);
	style_format_unref (cell->style->format);
	cell->style->format = style_format_new (format);
	cell->flags |= CELL_FORMAT_SET;
}

/*
 * cell_set_format:
 *
 * Changes the format for CELL to be FORMAT.  FORMAT should be
 * a number display format as specified on the manual
 */
void
cell_set_format (Cell *cell, char *format)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (format != NULL);
	g_return_if_fail (cell->value);
	
	if (strcmp (format, cell->style->format->format) == 0)
		return;

	cell_modified (cell);
	cell_queue_redraw (cell);
	
	/* Change the format */
	style_format_unref (cell->style->format);
	cell->style->format = style_format_new (format);
	cell->flags |= CELL_FORMAT_SET;
	
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
 * @cell:       The cell that is changing position
 * @target_col: The new column
 * @target_row: The new row.
 *
 * This routine is used to move a cell to a different location:
 *
 * Auxiliary items canvas items attached to the cell are moved.
 */
void
cell_relocate (Cell *cell, int target_col, int target_row)
{
	g_return_if_fail (cell != NULL);

	/* 1. Tag the cell as modified */
	cell_modified (cell);

	/* 2. If the cell contains a formula, relocate the formula */
	if (cell->parsed_node){
		char *text, *formula;

		expr_tree_ref (cell->parsed_node);
		cell_formula_changed (cell);
	}

	/* 3. Move any auxiliary canvas items */
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

	cell_modified (cell);
}

int
cell_get_horizontal_align (Cell *cell)
{
	g_return_val_if_fail (cell != NULL, HALIGN_LEFT);

	if (cell->style->halign == HALIGN_GENERAL)
		if (cell->value){
			if (cell->value->type== VALUE_FLOAT ||
			    cell->value->type == VALUE_INTEGER)
				return HALIGN_RIGHT;
			else
				return HALIGN_LEFT;
		} else
			return HALIGN_RIGHT;
	else
		return cell->style->halign;
}

static inline int
cell_is_number (Cell *cell)
{
	if (cell->value)
		if (cell->value->type == VALUE_FLOAT || cell->value->type == VALUE_INTEGER)
			return TRUE;

	return FALSE;
}

static inline int
cell_contents_fit_inside_column (Cell *cell)
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
	
	g_return_if_fail (cell != NULL);

        /*
	 * If the cell is a number, or the text fits inside the column, or the
	 * alignment modes are set to "justify", then we report only one
	 * column is used.
	 */
	if (cell_is_number (cell) ||
	    cell->style->fit_in_cell ||
	    cell->style->valign == VALIGN_JUSTIFY ||
	    cell->style->halign == HALIGN_JUSTIFY ||
	    cell->style->halign == HALIGN_FILL    ||
	    cell_contents_fit_inside_column (cell)){
		*col1 = *col2 = cell->col->pos;
		return;
	}

	sheet = cell->sheet;
	align = cell_get_horizontal_align (cell);
	row   = cell->row->pos;

	switch (align){
	case HALIGN_LEFT:
		*col1 = *col2 = cell->col->pos;
		pos = cell->col->pos + 1;
		left = cell->width - COL_INTERNAL_WIDTH (cell->col);
		margin = cell->col->margin_b;
		
		for (; left > 0 && pos < SHEET_MAX_COLS-1; pos++){
			ColRowInfo *ci;
			Cell *sibling;

			sibling = sheet_cell_get (sheet, pos, row);

			if (sibling)
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

			if (sibling)
				return;

			ci = sheet_col_get_info (sheet, pos);

			/* The space consumed is:
			 *   - The margin_a from the last column
			 *   - The widht of this cell
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

				if (left_sibling)
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

				if (right_sibling)
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
		g_warning ("Unknown horizontal alignment type\n");
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
void
calc_text_dimensions (int is_number, Style *style, char *text, int cell_w, int cell_h, int *h, int *w)
{
	GdkFont *font = style->font->font;
	int text_width, font_height;
	
	text_width = gdk_string_width (font, text);
	font_height = font->ascent + font->descent;
	
	if (text_width < cell_w || is_number){
		*w = text_width;
		*h = font_height;
		return;
	} 

	if (style->halign == HALIGN_JUSTIFY ||
	    style->valign == VALIGN_JUSTIFY ||
	    style->fit_in_cell){
		char *ideal_cut_spot = NULL;
		int  used, last_was_cut_point;
		char *p = text;
		*w = cell_w;
		*h = font_height;
		
		used = 0;
		last_was_cut_point = FALSE;

		for (; *p; p++){
			int len;

			if (last_was_cut_point && *p != ' ')
				ideal_cut_spot = p;
			
			len = gdk_text_width (font, p, 1);

			/* If we have overflowed the cell, wrap */
			if (used + len > cell_w){
				if (ideal_cut_spot){
					int n = p - ideal_cut_spot;
					
					used = gdk_text_width (font, ideal_cut_spot, n);
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
		return;
	}
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
	
	if (cell->text){
		Style *style = cell->style;
		int h, w;
		
		rendered_text = cell->text->str;
		calc_text_dimensions (cell_is_number (cell),
				      style, rendered_text,
				      COL_INTERNAL_WIDTH (cell->col),
				      ROW_INTERNAL_HEIGHT (cell->row),
				      &h, &w);
		
		cell->width = cell->col->margin_a + cell->col->margin_b + w;
		cell->height = cell->row->margin_a + cell->row->margin_b + h;

		if (cell->height > cell->row->pixels && !cell->row->hard_size)
			sheet_row_set_internal_height (cell->sheet, cell->row, h);
	} else
		cell->width = cell->col->margin_a + cell->col->margin_b;

	/* Register the span */
	cell_get_span (cell, &left, &right);
	if (left != right)
		cell_register_span (cell, left, right);
}

static void
draw_overflow (GdkDrawable *drawable, GdkGC *gc, GdkFont *font, int x1, int y1, int text_base, int width, int height)
{
	GdkRectangle rect;
	int len = gdk_string_width (font, "#");
	int total, offset;
	
	rect.x = x1;
	rect.y = y1;
	rect.width = width;
	rect.height = height;
	gdk_gc_set_clip_rectangle (gc, &rect);
	
	offset = x1 + width - len;
	for (total = len;  offset > len; total += len){
		gdk_draw_text (drawable, font, gc, x1 + offset, text_base, "#", 1);
		offset -= len;
	} 
}

static GList *
cell_split_text (GdkFont *font, char *text, int width)
{
	GList *list;
	char *p, *line, *line_begin, *ideal_cut_spot = NULL;
	int  line_len, used, last_was_cut_point;

	list = NULL;
	used = 0;
	last_was_cut_point = FALSE;
	for (line_begin = p = text; *p; p++){
		int len;

		if (last_was_cut_point && *p != ' ')
			ideal_cut_spot = p;

		len = gdk_text_width (font, p, 1);

		/* If we have overflowed, do the wrap */
		if (used + len > width){
			char *begin = line_begin;
			
			if (ideal_cut_spot){
				int n = p - ideal_cut_spot + 1;

				line_len = ideal_cut_spot - line_begin;
				used = gdk_text_width (font, ideal_cut_spot, n);
				line_begin = ideal_cut_spot;
			} else {
				used = len;
				line_len = p - line_begin;
				line_begin = p;
			}
			
			line = g_malloc (line_len + 1);
			memcpy (line, begin, line_len);
			line [line_len] = 0;
			list = g_list_append (list, line);

			ideal_cut_spot = NULL;
		} else
			used += len;

		if (*p == ' ')
			last_was_cut_point = TRUE;
		else
			last_was_cut_point = FALSE;
	}
	if (*line_begin){
		line_len = p - line_begin;
		line = g_malloc (line_len+1);
		memcpy (line, line_begin, line_len);
		line [line_len] = 0;
		list = g_list_append (list, line);
	}

	return list;
}

/*
 * str_trim_spaces:
 * s: the string to modify
 *
 * This routine trims the leading and trailing spaces of the
 * string.  The string is possibly modified and the returned
 * value lies inside the original string.
 *
 * No duplication takes place
 */
static char *
str_trim_spaces (char *s)
{
	char *p;
	
	while (*s && *s == ' ')
		s++;

	p = s + strlen (s);
	while (p >= s){
		if (*p == ' ')
			*p = 0;
		else
			break;
		p--;
	}
	return s;
}

/*
 * Returns the number of columns used for the draw
 */
int 
cell_draw (Cell *cell, void *sv, GdkGC *gc, GdkDrawable *drawable, int x1, int y1)
{
	Style        *style = cell->style;
	GdkFont      *font = style->font->font;
	SheetView    *sheet_view = sv;
	GdkGC        *white_gc = GTK_WIDGET (sheet_view->sheet_view)->style->white_gc;
	GdkRectangle rect;
	
	int start_col, end_col;
	int width, height;
	int text_base = y1 + cell->row->pixels - cell->row->margin_b - font->descent + 1;
	int font_height;
	int halign;
	int do_multi_line;
	char *text;

	cell_get_span (cell, &start_col, &end_col);

	width  = COL_INTERNAL_WIDTH (cell->col);
	height = ROW_INTERNAL_HEIGHT (cell->row);

	font_height = font->ascent + font->descent;
	
	halign = cell_get_horizontal_align (cell);

	/* if a number overflows, do special drawing */
	if (width < cell->width && cell_is_number (cell)){
		draw_overflow (drawable, gc, font, x1 + cell->col->margin_a, y1, text_base,
			       width, height);
		return 1;
	}

	if (halign == HALIGN_JUSTIFY || style->valign == VALIGN_JUSTIFY || style->fit_in_cell)
		do_multi_line = TRUE;
	else
		do_multi_line = FALSE;

#if 0
	if (cell->text)
		text = cell->text->str;
	else
		text = "";
#endif
	
	if (do_multi_line){
		GList *lines, *l;
		int cell_pixel_height = ROW_INTERNAL_HEIGHT (cell->row);
		int line_count, x_offset, y_offset, inter_space;
		
		lines = cell_split_text (font, text, width);
		line_count = g_list_length (lines);

		rect.x = x1;
		rect.y = y1;
		rect.height = cell->row->pixels;
		rect.width = cell->col->pixels;
		gdk_gc_set_clip_rectangle (gc, &rect);
		
		switch (style->valign){
		case VALIGN_TOP:
			y_offset = 0;
			inter_space = font_height;
			break;
			
		case VALIGN_CENTER:
			y_offset = (cell_pixel_height - (line_count * font_height))/2;
			inter_space = font_height;
			break;
			
		case VALIGN_JUSTIFY:
			if (line_count > 1){
				y_offset = 0;
				inter_space = font_height + 
					(cell_pixel_height - (line_count * font_height))
					/ (line_count-1);
				break;
			} 
			/* Else, we become a VALIGN_BOTTOM line */
			
		case VALIGN_BOTTOM:
			y_offset = cell_pixel_height - (line_count * font_height);
			inter_space = font_height;
			break;
			
		default:
			g_warning ("Unhandled cell vertical alignment\n");
			y_offset = 0;
			inter_space = font_height;
		}

		y_offset += font_height - 1;
		for (l = lines; l; l = l->next){
			char *str = l->data;

			str = str_trim_spaces (str);
			
			switch (halign){
			case HALIGN_LEFT:
			case HALIGN_JUSTIFY:
				x_offset = cell->col->margin_a;
				break;
				
			case HALIGN_RIGHT:
				x_offset = cell->col->pixels - cell->col->margin_b -
					gdk_string_width (font, str);
				break;

			case HALIGN_CENTER:
				x_offset = (cell->col->pixels - gdk_string_width (font, str)) / 2;
				break;
			default:
				g_warning ("Multi-line justification style not supported\n");
				x_offset = cell->col->margin_a;
			}
			/* Advance one pixel for the border */
			x_offset++;
			gc = GTK_WIDGET (sheet_view->sheet_view)->style->black_gc;
			gdk_draw_text (drawable, font, gc, x1 + x_offset, y1 + y_offset, str, strlen (str));
			y_offset += inter_space;
			
			g_free (l->data);
		}
		g_list_free (lines);
		
	} else {
		int x, diff, total, len;

		/*
		 * x1, y1 are relative to this cell origin, but the cell might be using
		 * columns to the left (if it is set to right justify or center justify)
		 * compute the pixel difference 
		 */
		if (start_col != cell->col->pos)
			diff = -sheet_col_get_distance (cell->sheet, start_col, cell->col->pos);
		else
			diff = 0;

		/* This rectangle has the whole area used by this cell */
		rect.x = x1 + 1 + diff;
		rect.y = y1 + 1;
		rect.width  = sheet_col_get_distance (cell->sheet, start_col, end_col+1) - 1;
		rect.height = cell->row->pixels - 2;
		
		/* Set the clip rectangle */
		gdk_gc_set_clip_rectangle (gc, &rect);
		gdk_draw_rectangle (drawable, white_gc, TRUE,
				    rect.x, rect.y, rect.width, rect.height);

		len = 0;
		switch (halign){
		case HALIGN_FILL:
			printf ("FILL!\n");
			len = gdk_string_width (font, text);
			/* fall down */
			
		case HALIGN_LEFT:
			x = cell->col->margin_a;
			break;

		case HALIGN_RIGHT:
			x = cell->col->pixels - cell->col->margin_b - cell->width;
			break;

		case HALIGN_CENTER:
			x = (cell->col->pixels - cell->width)/2;
			break;
			
		default:
			g_warning ("Single-line justification style not supported\n");
			x = cell->col->margin_a;
			break;
		}

		total = 0;
		do {
			gdk_draw_text (drawable, font, gc, 1 + x1 + x, text_base, text, strlen (text));
			x1 += len;
			total += len;
		} while (halign == HALIGN_FILL && total < rect.width);
	}

	return end_col - start_col + 1;
}

char *
cell_get_text (Cell *cell)
{
	char *str;
	
	g_return_val_if_fail (cell != NULL, NULL);

	if (cell->parsed_node){
		char *func, *ret;
		
		func = expr_decode_tree (cell->parsed_node, cell->col->pos, cell->row->pos);
		ret = g_copy_strings ("=", func, NULL);
		g_free (func);

		return ret;
	}

	str = format_value (cell->style->format, cell->value, NULL);
	return str;
}


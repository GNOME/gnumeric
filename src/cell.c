/*
 * cell.c: Cell management of the Gnumeric spreadsheet.
 *
 * Author:
 *    Miguel de Icaza (miguel@kernel.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "eval.h"
#include "format.h"

void
cell_formula_changed (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	
	sheet_cell_formula_link (cell);
	cell_queue_recalc (cell);
}

void
cell_set_formula (Cell *cell, char *text)
{
	char *error_msg = NULL;

	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	cell->parsed_node = expr_parse_string (&text [1],
					       cell->col->pos,
					       cell->row->pos,
					       &error_msg);
	if (cell->parsed_node == NULL){
		if (cell->text)
			string_unref_ptr (&cell->text);
		cell->text = string_get (error_msg);
		return;
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
cell_set_alignment (Cell *cell, int halign, int valign, int orient)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->style != NULL);

	if ((cell->style->halign == halign) &&
	    (cell->style->halign == valign) &&
	    (cell->style->orientation == orient))
		return;

	cell->style->halign = halign;
	cell->style->valign = valign;
	cell->style->orientation = orient;

	cell_queue_redraw (cell);
}

void
cell_set_font_from_style (Cell *cell, StyleFont *style_font)
{
	GdkFont *font;
	int height;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (style_font != NULL);

	style_font_unref (cell->style->font);
	style_font_ref (style_font);
	
	cell->style->font = style_font;

	font = style_font->font;
	
	height = font->ascent + font->descent;
	
	if (!cell->row->hard_size)
		sheet_row_set_internal_height (cell->sheet, cell->row, height);
	
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
	char *str;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->value != NULL);

	str = format_value (cell->style->format, cell->value, NULL);
	cell_set_rendered_text (cell, str);
	g_free (str);
}

void
cell_set_text (Cell *cell, char *text)
{
	GList   *deps;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	/* The value entered */
	if (cell->entered_text)
		string_unref (cell->entered_text);

	cell->entered_text = string_get (text);
	
	if (cell->parsed_node){
		sheet_cell_formula_unlink (cell);

		expr_tree_unref (cell->parsed_node);
		cell->parsed_node = NULL;
	}
	
 	if (text [0] == '='){
		cell_set_formula (cell, text); 
	} else {
		Value *v = g_new (Value, 1);
		int is_text = 0, is_float = 0;
		char *p;
		
		for (p = text; *p && !is_text; p++){
			switch (*p){
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				break;

			case '-':
				if (p == text)
					break;
				/* falldown */
				
			case 'E': case 'e': case '+': case ':': case '.':
				is_float = 1;
				break;
			default:
				is_text = 1;
			}
		}
		if (is_text){
			v->type = VALUE_STRING;
			v->v.str = string_get (text);
		} else {
			if (is_float){
				v->type = VALUE_FLOAT;
				float_get_from_range (text, text+strlen(text),
						      &v->v.v_float);
			} else {
				v->type = VALUE_INTEGER;
				int_get_from_range (text, text+strlen (text),
						    &v->v.v_int);
			}
		}
		cell->value = v;
		
		cell_render_value (cell);
	}

	/* Queue all of the dependencies for this cell */
	deps = cell_get_dependencies (cell->sheet,
				      cell->col->pos,
				      cell->row->pos);
	if (deps)
		cell_queue_recalc_list (deps);

	cell_queue_redraw (cell);
}

/*
 * Makes a copy of a Cell
 */
Cell *
cell_copy (Cell *cell)
{
	Cell *new_cell;

	g_return_val_if_fail (cell != NULL, NULL);

	new_cell = g_new (Cell, 1);

	/* bitmap copy first */
	*new_cell = *cell;

	/* now copy propertly the rest */
	string_ref      (new_cell->entered_text);
	if (new_cell->parsed_node)
		expr_tree_ref   (new_cell->parsed_node);
	string_ref      (new_cell->text);
	
	new_cell->style = style_duplicate (new_cell->style);
	new_cell->value = value_duplicate (new_cell->value);

	return new_cell;
}

void
cell_destroy (Cell *cell)
{
	g_return_if_fail (cell != NULL);

	if (cell->parsed_node){
		expr_tree_unref (cell->parsed_node);
	}

	string_unref    (cell->entered_text);
	string_unref    (cell->text);
	style_destroy   (cell->style);
	value_release   (cell->value);
}

void
cell_queue_redraw (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	
	sheet_redraw_cell_region (cell->sheet,
				  cell->col->pos, cell->row->pos,
				  cell->col->pos, cell->row->pos);
}

void
cell_set_format (Cell *cell, char *format)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (format != NULL);
	
	if (strcmp (format, cell->style->format->format) == 0)
		return;

	/* Change the format */
	style_format_unref (cell->style->format);
	cell->style->format = style_format_new (format);

	/* re-render the cell text */
	cell_render_value (cell);
	cell_queue_redraw (cell);
}

void
cell_formula_relocate (Cell *cell, int target_col, int target_row)
{
	char *text, *formula;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->entered_text);
	
	string_unref (cell->entered_text);
	
	text = expr_decode_tree (cell->parsed_node, target_col, target_row);
	
	formula = g_copy_strings ("=", text, NULL);
	cell->entered_text = string_get (formula);

	cell_set_formula (cell, formula);
	
	g_free (formula);
	g_free (text);
	
	cell_formula_changed (cell);
}

/*
 * This routine drops the formula and just keeps the value
 */
void
cell_make_value (Cell *cell)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->parsed_node != NULL);

	expr_tree_unref (cell->parsed_node);
	cell->parsed_node = NULL;
	string_unref (cell->entered_text);
	cell->entered_text = string_ref (cell->text);
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
	    cell_contents_fit_inside_column (cell)){
		*col1 = *col2 = cell->col->pos;
		return;
	}

	sheet = cell->sheet;
	align = cell_get_horizontal_align (cell);
	row   = cell->row->pos;

	switch (cell->style->halign){
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

			if (*col1 - 1 > 0){
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
					used = 0;
				}
				*h += CELL_TEXT_INTER_SPACE + font_height;
				ideal_cut_spot = NULL;
			} 
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
	char    *rendered_text;

	g_return_if_fail (cell != NULL);

	if (cell->text){
		Style *style = cell->style;
		int h, w;
		
		rendered_text = CELL_TEXT_GET (cell);

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
}

#if 0
void
cell_draw (GdkDrawable *drawable, GdkFont *font, GdkGc *gc,
	   Style *style, int   x_offset, int y_offset, int width, char  *text)
{
	
}
#endif

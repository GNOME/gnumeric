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
#include "gnumeric-sheet.h"
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
cell_set_alignment (Cell *cell, int halign, int valign, int orient, int auto_return)
{
	g_return_if_fail (cell != NULL);
	g_return_if_fail (cell->style != NULL);

	if ((cell->style->halign      == halign) &&
	    (cell->style->valign      == valign) &&
	    (cell->style->fit_in_cell == auto_return) &&
	    (cell->style->orientation == orient))
		return;

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

	if (cell->style->halign == halign)
		return;

	cell_queue_redraw (cell);
	cell->style->halign = halign;

	cell_calc_dimensions (cell);
	cell_queue_redraw (cell);
}

void
cell_set_font_from_style (Cell *cell, StyleFont *style_font)
{
	GdkFont *font;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (style_font != NULL);

	cell_queue_redraw (cell);
	
	style_font_unref (cell->style->font);
	style_font_ref (style_font);
	
	cell->style->font = style_font;

	font = style_font->font;
	
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
	char *color_name;
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

	cell_queue_redraw (cell);

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

	cell_queue_redraw (cell);
	
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
				printf ("Anadiendo %d pixeles\n", font_height);
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
	char    *rendered_text;
	int     left, right;

	g_return_if_fail (cell != NULL);

	cell_unregister_span (cell);
	
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
		line = g_malloc (line_len);
		memcpy (line, line_begin, line_len);
		line [line_len] = 0;
		list = g_list_append (list, line);
	}

	return list;
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
	text = CELL_TEXT_GET (cell);

	if ((cell->style->valid_flags & STYLE_BACK_COLOR) && cell->style->back_color){
		gdk_gc_set_background (gc, &cell->style->back_color->color);
	}
	
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

	if (do_multi_line){
		GList *lines, *l;
		int cell_pixel_height = ROW_INTERNAL_HEIGHT (cell->row);
		int line_count, x_offset, y_offset, inter_space;
		
		lines = cell_split_text (font, text, width);
		line_count = g_list_length (lines);

		rect.x = x1;
		rect.y = y1;
		rect.height = cell->height;
		rect.width = cell->width;
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

			switch (halign){
			case HALIGN_LEFT:
			case HALIGN_JUSTIFY:
				x_offset = cell->col->margin_a;
				break;
				
			case HALIGN_RIGHT:
				x_offset = cell->col->pixels - cell->col->margin_b - gdk_string_width (font, str);
				break;

			case HALIGN_CENTER:
				x_offset = (cell->col->pixels - cell->width) / 2;
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
			
			g_free (str);
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
		rect.width  = sheet_col_get_distance (cell->sheet, start_col, end_col+1) - 2;
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


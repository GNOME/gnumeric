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
	GdkFont *font;

	g_return_if_fail (cell != NULL);

	if (cell->text){
		rendered_text = CELL_TEXT_GET (cell);

		font = cell->style->font->font;
	
		cell->width = cell->col->margin_a + cell->col->margin_b + 
			gdk_text_width (font, rendered_text, strlen (rendered_text));
		cell->height = font->ascent + font->descent;
	} else
		cell->width = cell->col->margin_a + cell->col->margin_b;
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

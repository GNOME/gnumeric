#include <config.h>
#include <gnome.h>
#include "gnumeric.h"
#include "eval.h"

static void
cell_formula_link (Cell *cell)
{
	Sheet *sheet = cell->sheet;

	sheet->formula_cell_list = g_list_prepend (sheet->formula_cell_list, cell);
}

static void
cell_formula_unlink (Cell *cell)
{
	Sheet *sheet = cell->sheet;
	
	sheet->formula_cell_list = g_list_remove (sheet->formula_cell_list, cell);
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

	cell_formula_link (cell);
	cell_add_dependencies (cell);
	cell_queue_recalc (cell);
}

void
cell_set_text (Cell *cell, char *text)
{
	GdkFont *font;
	char    *rendered_text;
	GList   *deps;
	
	g_return_if_fail (cell != NULL);
	g_return_if_fail (text != NULL);

	/* The value entered */
	if (cell->entered_text)
		string_unref_ptr (&cell->entered_text);
	cell->entered_text = string_get (text);
	
	if (cell->parsed_node){
		cell_drop_dependencies (cell);
		expr_tree_unref (cell->parsed_node);
	}
	
	if (text [0] == '='){
		cell_set_formula (cell, text); 
	} else {
		Value *v = g_new (Value, 1);
		int is_text = 0, is_float = 0;
		char *p;
		
		if (cell->text)
			string_unref_ptr (&cell->text);

		cell->text = string_get (text);

		for (p = text; *p && !is_text; p++){
			switch (*p){
			case '0': case '1': case '2': case '3': case '4':
			case '5': case '6': case '7': case '8': case '9':
				break;
				
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
			/* FIXME: */
			/* In this case we need to format the text */
		}
		cell->value = v;
	}

	/* Queue all of the dependencies for this cell */
	deps = cell_get_dependencies (cell->sheet,
				      cell->col->pos,
				      cell->row->pos);
	if (deps)
		cell_queue_recalc_list (deps);

	
	/* Finish setting the values for this cell */
	cell->flags = 0;

	rendered_text = CELL_TEXT_GET (cell);
	
	font = cell->style->font->font;
	
	cell->width = cell->col->margin_a + cell->col->margin_b + 
		gdk_text_width (font, rendered_text, strlen (rendered_text));
	cell->height = font->ascent + font->descent;
}

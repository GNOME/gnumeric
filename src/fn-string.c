/*
 * fn-math.c:  Built in string functions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static Value *
string_and_optional_int (Sheet *sheet, GList *l, int eval_col, int eval_row, char **error, int *count)
{
	int argc = g_list_length (l);
	Value *v, *vs;
	
	if (argc < 1 || argc > 2){
		*error = "Invalid number of arguments";
		return NULL;
	}

	vs = eval_expr (sheet, l->data, eval_col, eval_row, error);
	if (vs == NULL)
		return NULL;

	if (vs->type != VALUE_STRING){
		value_release (vs);
		*error = "Type mismatch";
		return NULL;
	}
	
	if (argc == 1){
		*count = 1;
	} else {
		v = eval_expr (sheet, l->next->data,
			       eval_col, eval_row, error);
		if (v == NULL)
			return NULL;
		*count = value_get_as_int (v);
		value_release (v);
	}

	return vs;
}

static char *help_left = {
	N_("@FUNCTION=LEFT\n"
	   "@SYNTAX=LEFT(text[,num_chars])\n"

	   "@DESCRIPTION="
	   "Returns the leftmost num_chars characters or the left"
	   " character if num_chars is not specified"
	   "\n"
	   "@SEEALSO=MID, RIGHT")
};

static Value *
gnumeric_left (void *sheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *vs, *v;
	int count;
	char *s;

	vs = string_and_optional_int (sheet, expr_node_list, eval_col, eval_row,
				      error_string, &count);

	if (vs == NULL)
		return NULL;
			
	s = g_malloc (count + 1);
	strncpy (s, vs->v.str->str, count);
	s [count] = 0;

	value_release (vs);
	
	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get (s);
	g_free (s);
	
	return v;
}

static char *help_right = {
	N_("@FUNCTION=RIGHT\n"
	   "@SYNTAX=RIGHT(text[,num_chars])\n"

	   "@DESCRIPTION="
	   "Returns the rightmost num_chars characters or the right"
	   " character if num_chars is not specified"
	   "\n"
	   "@SEEALSO=MID, LEFT")
};

static Value *
gnumeric_right (void *sheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *vs, *v;
	int count, len;
	char *s;

	vs = string_and_optional_int (sheet, expr_node_list, eval_col, eval_row,
				      error_string, &count);

	if (vs == NULL)
		return NULL;

	len = strlen (vs->v.str->str);
	if (count > len)
		count = len;
	
	s = g_malloc (count + 1);
	strncpy (s, vs->v.str->str+len-count, count);
	s [count] = 0;

	value_release (vs);
	
	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get (s);
	g_free (s);
	
	return v;
}


FunctionDefinition string_functions [] = {
	{ "left",     0,    "text,num_chars",    &help_left, gnumeric_left, NULL },
	{ "right",     0,   "text,num_chars",    &help_right, gnumeric_right, NULL },
	{ NULL, NULL },
};

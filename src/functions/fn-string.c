/*
 * fn-math.c:  Built in string functions.
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <gnome.h>
#include <ctype.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

static char *help_char = {
	N_("@FUNCTION=CHAR\n"
	   "@SYNTAX=CHAR(x)\n"

	   "@DESCRIPTION="
	   "Returns the ascii character represented by the number x."
	   "\n"
	   
	   "@SEEALSO=CODE")
};

static Value *
gnumeric_char (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	char result [2];

	result [0] = value_get_as_int (argv [0]);
	result [1] = 0;

	return value_str (result);
}

static char *help_code = {
	N_("@FUNCTION=CODE\n"
	   "@SYNTAX=CODE(char)\n"

	   "@DESCRIPTION="
	   "Returns the ASCII number for the character char."
	   "\n"
	   
	   "@SEEALSO=CHAR")
};

static Value *
gnumeric_code (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	if (argv [0]->type != VALUE_STRING){
		*error_string = "Type mismatch";
		return NULL;
	}

	return value_int (argv [0]->v.str->str [0]);
}

static char *help_exact = {
	N_("@FUNCTION=EXACT\n"
	   "@SYNTAX=EXACT(string1, string2)\n"

	   "@DESCRIPTION="
	   "Returns true if string1 is exactly equal to string2 (this routine is case sensitive)."
	   "\n"
	   
	   "@SEEALSO=")
};

static Value *
gnumeric_exact (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	if (argv [0]->type != VALUE_STRING || argv [1]->type != VALUE_STRING){
		*error_string = "Type mismatch";
		return NULL;
	}

	return value_int (strcmp (argv [0]->v.str->str, argv [1]->v.str->str) == 0);
}

static char *help_len = {
	N_("@FUNCTION=LEN\n"
	   "@SYNTAX=LEN(string)\n"

	   "@DESCRIPTION="
	   "Returns the length in characters of the string @string."
	   "\n"
	   
	   "@SEEALSO=CHAR, CODE")
};

static Value *
gnumeric_len (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	if (argv [0]->type != VALUE_STRING){
		*error_string = "Type mismatch";
		return NULL;
	}

	return value_int (strlen (argv [0]->v.str->str));
}

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

	v = value_str (s);
	g_free (s);
	
	return v;
}

static char *help_lower = {
	N_("@FUNCTION=LOWER\n"
	   "@SYNTAX=LOWER(text)\n"

	   "@DESCRIPTION="
	   "Returns a lower-case version of the string in @text"
	   "\n"
	   "@SEEALSO=UPPER")
};

static Value *
gnumeric_lower (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	char *s, *p;
	
	if (argv [0]->type != VALUE_STRING){
		*error_string = "Type mismatch";
		return NULL;
	}

	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	p = s = strdup (argv [0]->v.str->str);
	for (; *p; p++){
		*p = tolower (*p);
	}
	v->v.str = string_get (s);
	g_free (s);

	return v;
}

static char *help_mid = {
	N_("@FUNCTION=MID\n"
	   "@SYNTAX=MID(string, position, length)\n"

	   "@DESCRIPTION="
	   "Returns a substring from @string starting at @position for @lenght characters."
	   "\n"
	   
	   "@SEEALSO=LEFT, RIGHT")
};

static Value *
gnumeric_mid (struct FunctionDefinition *i, Value *argv [], char **error)
{
	Value *v;
	int pos, len;
	char *s, *source;

	if (argv [0]->type != VALUE_STRING ||
	    argv [1]->type != VALUE_INTEGER ||
	    argv [2]->type != VALUE_INTEGER){
		*error = "Type mismatch";
		return NULL;
	}

	len = value_get_as_int (argv [2]);
	pos = value_get_as_int (argv [1]);

	if (len < 0 || pos < 0){
		*error = "Invalid arguments";
		return NULL;
	}

	source = argv [0]->v.str->str;

	if (pos > strlen (source))
		return value_str ("");
	
	s = g_malloc0 (len);
	
	strncpy (s, source + pos, len);

	v = value_str (s);
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

	v = value_str (s);
	g_free (s);
	
	return v;
}

static char *help_upper = {
	N_("@FUNCTION=UPPER\n"
	   "@SYNTAX=UPPER(text)\n"

	   "@DESCRIPTION="
	   "Returns a upper-case version of the string in @text"
	   "\n"
	   "@SEEALSO=LOWER")
};

static Value *
gnumeric_upper (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	char *s, *p;
	
	if (argv [0]->type != VALUE_STRING){
		*error_string = "Type mismatch";
		return NULL;
	}

	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	p = s = strdup (argv [0]->v.str->str);

	for (;*p; p++){
		*p = toupper (*p);
	}
	v->v.str = string_get (s);
	g_free (s);

	return v;
}

FunctionDefinition string_functions [] = {
	{ "char",     "f",  "number",            &help_char,  NULL, gnumeric_char },
	{ "code",     "s",  "text",              &help_code,  NULL, gnumeric_code },
	{ "exact",    "ss", "text1,text2",       &help_exact, NULL, gnumeric_exact },
	{ "left",     0,    "text,num_chars",    &help_left,  gnumeric_left, NULL },
	{ "len",      "s",  "text",              &help_len,   NULL, gnumeric_len },
	{ "lower",    "s",  "text",              &help_lower, NULL, gnumeric_lower },
        { "mid",      "sff","text,pos,num",      &help_mid,   NULL, gnumeric_mid },
	{ "right",    0,    "text,num_chars",    &help_right, gnumeric_right, NULL },
	{ "upper",    "s",  "text",              &help_upper, NULL, gnumeric_upper },
	{ NULL, NULL },
};

/*
 * Missing:
 *
 * DOLLAR(number [,decimals] formats number as currency.
 *
 * FIXED (number, decimals, no_comma)
 * formats number as text with a fixed number of decimals
 *
 * FIND (find_text, within_text [,start_at_num])
 *
 * PROPER(text) capitalizes the first letter in each word of a text value
 *
 * REPLACE(old_text, start_num, num_chars, new_text)
 *
 * REPT (text, number_of_times)
 *
 * SUBSTITUTE(text,old_text, new_text [,intenace_num])
 *
 * T(value) -> converts its argumnt to text
 *
 * TEXT (value, format_text)  converts value to text with format_text
 *
 * TRIM(text) removes spaces form text
 *
 * VALUE(text) converts a text argument to a number
 */

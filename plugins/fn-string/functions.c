/*
 * fn-string.c:  Built in string functions.
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Sean Atkinson (sca20@cam.ac.uk)
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
		*error_string = _("Type mismatch");
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
	   
	   "@SEEALSO=LEN")  /* FIXME: DELTA, LEN, SEARCH */
};

static Value *
gnumeric_exact (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	if (argv [0]->type != VALUE_STRING || argv [1]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
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
		*error_string = _("Type mismatch");
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
		*error = _("Invalid number of arguments");
		return NULL;
	}

	vs = eval_expr (sheet, l->data, eval_col, eval_row, error);
	if (vs == NULL)
		return NULL;

	if (vs->type != VALUE_STRING){
		value_release (vs);
		*error = _("Type mismatch");
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
		*error_string = _("Type mismatch");
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
	   "Returns a substring from @string starting at @position for @length characters."
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
		*error = _("Type mismatch");
		return NULL;
	}

	len = value_get_as_int (argv [2]);
	pos = value_get_as_int (argv [1]);

	if (len < 0 || pos <= 0){
		*error = _("Invalid arguments");
		return NULL;
	}

	source = argv [0]->v.str->str;
	if (pos > strlen (source))
		return value_str ("");
	pos-- ;
	s = g_new (gchar, len+1) ;
	strncpy (s, &source[pos], len);
	s[len] = '\0' ;
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
		*error_string = _("Type mismatch");
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

static char *help_concatenate = {
	N_("@FUNCTION=CONCATENATE\n"
	   "@SYNTAX=CONCATENATE(string1[,string2...])\n"
	   "@DESCRIPTION=Returns up appended strings\n"
	   "@SEEALSO=LEFT, MID, RIGHT")
};

static Value *
gnumeric_concatenate (void *sheet, GList *l, int eval_col, int eval_row, char **error_string)
{
	Value *v;
	char *s, *p, *tmp;
	
	if (l==NULL) {
		*error_string = _("Invalid number of arguments") ;
		return NULL ;
	}
	s = g_new(gchar, 1) ;
	*s = '\0' ;
	while ( l != NULL && 
		(v=eval_expr(sheet, l->data, eval_col, eval_row, error_string)) != NULL) {
/*
		if (v->type != VALUE_STRING) {
			*error_string = _("Invalid argument") ;
			value_release (v) ;
			return NULL ;
		}
*/
		tmp = value_string (v) ;
		/* FIXME: this could be massively sped-up with strlen's etc... */
		p = g_strconcat (s, tmp, NULL) ;
		g_free (tmp) ;
		value_release (v) ;
		g_free (s) ;
		s = p ;
		l = g_list_next (l) ;
	}
	
	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get (s);
	g_free (s);

	return v;
}

static char *help_rept = {
	N_("@FUNCTION=REPT\n"
	   "@SYNTAX=REPT(string,num)\n"
	   "@DESCRIPTION=Returns @num repetitions of @string\n"
	   "@SEEALSO=CONCATENATE")
};

static Value *
gnumeric_rept (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	gchar *s, *p;
	gint num;
	guint len;
	
	if (argv [0]->type != VALUE_STRING) {
		*error_string = _("Type mismatch");
		return NULL;
	} else if ( (num=value_get_as_int(argv[1])) < 0) {
		*error_string = _("Invalid argument");
		return NULL;
	}

	len = strlen (argv[0]->v.str->str);
	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	p = s = g_new (gchar, 1 + len * num);
	while (num--) {
		strncpy (p, argv[0]->v.str->str, len) ;
		p += len;
	}
	*p = '\0' ;
	v->v.str = string_get (s);
	g_free (s);

	return v;
}
/*
static char *help_clean = {
	N_("@FUNCTION=CLEAN\n"
	   "@SYNTAX=CLEAN(string)\n"

	   "@DESCRIPTION=Returns @string without non-printable characters\n"
	   "@SEEALSO=CHAR, TRIM")
};

static Value *
gnumeric_clean (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	gchar *s, *p;

	if (argv [0]->type != VALUE_STRING) {
		*error_string = _("Type mismatch");
		return NULL;
	}

	s = p = argv[0]->v.str->str;
	while (*p) {
		if (isprint(*p)) *s++ = *p;
		p++;
	}
	*s = '\0';

	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get (argv[0]->v.str->str);
	return v;
}
*/
static char *help_find = {
	N_("@FUNCTION=FIND\n"
	   "@SYNTAX=FIND(string1,string2[,start])\n"
	   "@DESCRIPTION=Returns position of @string1 in @string2 (case-sesitive), "
	   "searching only from character @start onwards (assumed 1 if omitted)\n"
	   "@SEEALSO=EXACT, LEN, MID, SEARCH")
};

static Value *
gnumeric_find (void *sheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *v1, *v2, *ret;
	int count;
	char *s, *p;

	if ( expr_node_list == NULL) {
		*error_string = _("Invalid number of arguments") ;
		return NULL ;
	}
	if ( (v1 = eval_expr (sheet, expr_node_list->data, eval_col, eval_row, error_string))
	     == NULL) return NULL ;
	if ( (v2 = string_and_optional_int (sheet, g_list_next(expr_node_list), 
					    eval_col, eval_row, error_string, &count) )
	     ==NULL ) return NULL;
	if ( count > strlen(v2->v.str->str) ||
	     count == 0) { /* start position too high or low */
		*error_string = _("Invalid argument") ;
		return NULL ;
	}

	g_assert (count >= 1) ;
	s = v2->v.str->str + count - 1;
	if ( (p = strstr(s, v1->v.str->str)) == NULL ) {
		*error_string = _("Invalid argument") ;
		return NULL ;
	}
	value_release (v1);
	value_release (v2);

	ret = g_new(Value, 1);
	ret->type = VALUE_INTEGER;
	ret->v.v_int = count + p - s ;
	return ret ;
}

static char *help_fixed = {
	N_("@FUNCTION=FIXED\n"
	   "@SYNTAX=FIXED(num, decimals, no_commas)\n"

	   "@DESCRIPTION=Returns @num as a formatted string with @decimals numbers "
	   "after the decimal point, omitting commas if requested by @no_commas\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_fixed (void *sheet, GList *l, int eval_col, int eval_row, char **error_string)
{
	Value *v;
	gchar *s, *p, *f;
	gint dec = 2, commas = TRUE, tmp;
	float_t num;

	if (l == NULL || 
	    ( v = eval_expr (sheet, l->data, eval_col, eval_row, error_string) )
	    == NULL) {
		*error_string = _("Invalid number of arguments");
		return NULL;
	}
	num = value_get_as_double (v);
	value_release (v);
	if ( (l = g_list_next(l)) != NULL && l->data != NULL &&
	     (v = eval_expr (sheet, l->data, eval_col, eval_row, error_string))
	     != NULL) {
		if (v->type != VALUE_INTEGER &&
		    v->type != VALUE_FLOAT ) {
			value_release (v);
			*error_string = _("Type mismatch");
			return NULL;
		}
		dec = value_get_as_int (v);
		value_release (v);
	}
	if ( (l = g_list_next(l)) != NULL && l->data != NULL &&
	     (v = eval_expr (sheet, l->data, eval_col, eval_row, error_string))
	     != NULL) {
		commas = !value_get_bool (v, &tmp);
		if (tmp) {
			*error_string = _("Type mismatch");
			return NULL;
		}
		value_release (v);
	}
	if (dec >= 1000) { /* else buffer under-run */
		*error_string = _("Invlaid argument") ;
		return NULL ;
		/*
	} else if (lc->thousands_sep[1] != '\0') {
		fprintf (stderr, "thousands_sep:\"%s\"\n", lc->thousands_sep) ;
		*error_string = _("Invlaid thousands separator") ;
		return NULL ;
		*/
	} else if (dec <= 0) { /* no decimal point : just round and pad 0's */
		dec *= -1;
		num /= pow(10, dec);
		if (num < 1 && num > -1) {
			s = g_strdup("0") ;
			commas = 0 ;
		} else {
			f = g_strdup("%00?s%.0f%.00?u") ; /* commas, no point, 0's */
			tmp = dec ;
			dec += log10(fabs(num)) ;
			if (commas)
				commas = dec / 3 ;
			p = &f[13] ; /* last 0 in trailing 0's count */
			do
				*p-- = '0' + (tmp % 10) ;
			while (tmp /= 10) ;
			tmp = commas ;
			p = &f[3] ; /* last 0 in leading blank spaces for commas */
			do
				*p-- = '0' + (tmp % 10) ;
			while (tmp /= 10) ;
			s = g_strdup_printf (f, "", num, 0) ;
			g_free (f) ;
		}
	} else { /* decimal point format */
		f = g_strdup("%00?s%.00?f") ;
		tmp = dec ;
		dec = log10(fabs(num)) ;
		if (commas)
			commas = dec / 3 ;
		p = &f[9] ;
		do
			*p-- = '0' + (tmp % 10) ;
		while (tmp /= 10) ;
		tmp = commas ;
		p = &f[3] ;
		do
			*p-- = '0' + (tmp % 10) ;
		while (tmp /= 10) ;
		s = g_strdup_printf (f, "", num) ;
		g_free (f) ;
	}
	if (commas) {
		p = s ;
		f = &s[commas] ;
		if (*f == '-')
			*p++ = *f++ ;
		dec -= 2 ;
		while (dec-- > 0) {
			*p++ = *f++ ;
			if (dec%3 == 0)
				/* FIXME: should use lc->thousands_sep[0] */
				*p++ = ',' ;
		}
	}

	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get (s);
	g_free (s);
	return v;
}

/* proper could be a LOT nicer
 * (e.g. "US Of A" -> "US of A", "Cent'S Worth" -> "Cent's Worth")
 * but this is how Excel does it */
static char *help_proper = {
	N_("@FUNCTION=PROPER\n"
	   "@SYNTAX=PROPER(string)\n"

	   "@DESCRIPTION=Returns @string with initial of each word capitalised\n"
	   "@SEEALSO=LOWER, UPPER")
};

static Value *
gnumeric_proper (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	gchar *s, *p;
	gboolean inword = FALSE;

	if (argv [0]->type != VALUE_STRING) {
		*error_string = _("Type mismatch");
		return NULL;
	}

	s = p = argv[0]->v.str->str;
	while (*s) {
		if (isalpha(*s)) {
			if (inword) {
				*s = tolower(*s);
			} else {
				*s = toupper(*s);
				inword = TRUE;
			}
		} else
			inword = FALSE;
		s++;
	}

	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get (p);
	return v;
}
static char *help_replace = {
	N_("@FUNCTION=REPLACE\n"
	   "@SYNTAX=REPLACE(old,start,num,new)\n"
	   "@DESCRIPTION=Returns @old with @new replacing @num characters from @start\n"
	   "@SEEALSO=MID, SEARCH, SUBSTITUTE, TRIM")
};

static Value *
gnumeric_replace (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	gchar *s, *p;
	gint start, num, oldlen, newlen;

	if (argv[0]->type != VALUE_STRING ||
	    argv[1]->type != VALUE_INTEGER ||
	    argv[2]->type != VALUE_INTEGER ||
	    argv[3]->type != VALUE_STRING ) {
		*error_string = _("Type mismatch");
		return NULL;
	}
	start = value_get_as_int (argv[1]) ;
	num = value_get_as_int (argv[2]) ;
	oldlen = strlen(argv[0]->v.str->str) ;	
	if (start <= 0 || num <= 0 || --start + num > oldlen ) {
		*error_string = _("Invalid arguments");
		return NULL;
	}
	newlen = strlen(argv[3]->v.str->str) ;
	s = g_new (gchar, 1 + newlen + oldlen - num) ;
	strncpy (s, argv[0]->v.str->str, start);
	strncpy (&s[start], argv[3]->v.str->str, newlen);
	strncpy (&s[start+newlen], &argv[0]->v.str->str[start+num], oldlen - num - start ) ;
	s[newlen+oldlen-num] = '\0' ;
	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get (s);
	g_free(s);
	return v;
}

static char *help_t = {
	N_("@FUNCTION=T\n"
	   "@SYNTAX=T(value)\n"
	   "@DESCRIPTION=Returns @value if and only if it is text, otherwise a blank string\n"
	   "@SEEALSO=CELL, N, VALUE")
};

static Value *
gnumeric_t (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get ( (argv[0]->type==VALUE_STRING) ? argv[0]->v.str->str : "" ) ;
	return v;
}

static char *help_trim = {
	N_("@FUNCTION=TRIM\n"
	   "@SYNTAX=TRIM(text)\n"
	   "@DESCRIPTION=Returns @text with only single spaces between words\n"
	   "@SEEALSO=CLEAN, MID, REPLACE, SUBSTITUTE")
};

static Value *
gnumeric_trim (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	Value *v;
	gchar *new, *dest, *src;
	gboolean space = TRUE;

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("Type mismatch");
		return NULL;
	}
	dest = new = g_new (gchar, strlen(argv[0]->v.str->str) + 1) ;
	src = argv[0]->v.str->str ;
	while (*src) {
		if (*src == ' ') {
			if (!space) {
				*dest++ = *src;
				space = TRUE ;
			}
		} else {
			space = FALSE ;
			*dest++ = *src ;
		}
		src++ ;
	}
	if (space && dest > new) dest--;
	*dest = '\0';
	v = g_new (Value, 1);
	v->type = VALUE_STRING;
	v->v.str = string_get (new);
	g_free(new);
	return v;
}

static char *help_value = {
	N_("@FUNCTION=VALUE\n"
	   "@SYNTAX=VALUE(text)\n"
	   "@DESCRIPTION=Returns numeric value of @text\n"
	   "@SEEALSO=DOLLAR, FIXED, TEXT")
};

static Value *
gnumeric_value (struct FunctionDefinition *i, Value *argv [], char **error_string)
/* FIXME: in Excel, VALUE("$1, 000") = 1000, and dates etc. supported */
{
	Value *v;
	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("Type mismatch");
		return NULL;
	}
	v = g_new (Value, 1);
	v->type = VALUE_FLOAT;
	v->v.v_float = value_get_as_double(argv[0]);
	return v;
}

struct subs_string {
	gchar *str ;
	guint len ;
	guint mem ;
} ;

static struct subs_string *
subs_string_new (guint len)
{
	struct subs_string *s = g_new (struct subs_string, 1) ;
	s->len = 0 ;
	s->mem = len ;
	s->str = g_new (gchar, len) ;
	*s->str = '\0' ;
	return s ;
}

static void
subs_string_append_n (struct subs_string *s, gchar *src, guint n)
{
	const guint chunk = 1024 ;
	while (s->len + n >= s->mem)
		s->str = g_realloc (s->str, s->mem += chunk) ;
	strncpy (&s->str[s->len], src, n) ;
	s->str[s->len += n] = '\0' ;
}

static void
subs_string_free (struct subs_string *s)
{
	g_free (s->str) ;
	g_free (s) ;
}

static char *help_substitute = {
	N_("@FUNCTION=SUBSTITUTE\n"
	   "@SYNTAX=SUBSTITUTE(text, old, new [,num]\n"
	   "@DESCRIPTION=Replaces @old with @new in @text.  Substitutions are only applied to "
	   "instance @num of @old in @text, otherwise every one is changed.\n"
	   "@SEEALSO=REPLACE, TRIM")
};

static Value *
gnumeric_substitute (void *sheet, GList *l, int eval_col, int eval_row, char **error_string)
{
	Value *v ;
	gchar *text, *old, *new, *p ,*f ;
	gint num ;
	guint oldlen, newlen, len, inst ;
	struct subs_string *s ;

	if ( l == NULL) {
		*error_string = _("Invalid number of arguments") ;
		return NULL ;
	} else if ((v =eval_expr(sheet,l->data,eval_col,eval_row,error_string))==NULL) {
		return NULL ;
	} else if (v->type != VALUE_STRING) {
		*error_string = _("Invalid argument") ;
		return NULL ;
	}
	text = value_string (v) ;
	value_release (v) ;

	if ( (l = g_list_next(l)) == NULL) {
		*error_string = _("Invalid number of arguments") ;
		g_free (text) ;
		return NULL ;
	} else if ((v =eval_expr(sheet,l->data,eval_col,eval_row,error_string))==NULL) {
		g_free (text) ;
		return NULL ;
	} else if (v->type != VALUE_STRING) {
		*error_string = _("Invalid argument") ;
		g_free (text) ;
		return NULL ;
	}
	old = value_string (v) ;
	value_release (v) ;

	if ( (l = g_list_next(l)) == NULL) {
		*error_string = _("Invalid number of arguments") ;
		g_free (text) ;
		g_free (old) ;
		return NULL ;
	} else if ((v=eval_expr(sheet,l->data,eval_col,eval_row,error_string))==NULL) {
		g_free (text) ;
		g_free (old) ;
		return NULL;
	} else if (v->type != VALUE_STRING) {
		*error_string = _("Invalid argument") ;
		g_free (text) ;
		g_free (old) ;
		return NULL ;
	}
	new = value_string (v) ;
	value_release (v) ;

	if ( (l = g_list_next(l)) == NULL ||
	     (v=eval_expr(sheet,l->data,eval_col,eval_row,error_string))==NULL ) {
		num = 0 ;
	} else {
		num = value_get_as_int (v) ;
		value_release (v) ;
		if (num < 0) {
			*error_string = _("Invalid argument") ;
			g_free (text) ;
			g_free (old) ;
			g_free (new) ;
			return NULL ;
		}
	}

	oldlen = strlen (old) ;
	newlen = strlen (new) ;
	len = strlen (text) ;
	if (newlen != oldlen) {
		s = subs_string_new (len) ;
	} else 
		s = NULL ;

	p = text ;
	inst = 0 ;
	while (p-text < len) {
		if ( (f=strstr(p, old)) == NULL )
			break ;
		if (num == 0 || num == ++inst) {
			if (s == NULL) {
				strncpy (f, new, newlen) ;
			} else {
				subs_string_append_n (s, p, f-p) ;
				subs_string_append_n (s, new, newlen) ;
			}
			if (num != 0 && num == inst)
				break ;
		}
		p = f + oldlen ;
	}
	if (newlen != oldlen) { /* FIXME: (p-text) might be bad ? */
		subs_string_append_n (s, p, len - (p-text) ) ;
		p = s->str ;
	} else
		p = text ;

	v = g_new (Value, 1) ;
	v->type = VALUE_STRING ;
	v->v.str = string_get (p) ;

	g_free (new) ;
	g_free (old) ;
	g_free (text) ;
	if (s != NULL)
		subs_string_free (s) ;

	return v ;
}

static char *help_dollar = {
	N_("@FUNCTION=DOLLAR\n"
	   "@SYNTAX=DOLLAR(num,decimals)\n"
	   "@DESCRIPTION=Returns @num formatted as currency \n"
	   "@SEEALSO=FIXED, TEXT, VALUE")
};

static Value *
gnumeric_dollar (void *sheet, GList *l, int eval_col, int eval_row, char **error_string)
/* FIXME: should use lc->[pn]_sign_posn, mon_thousands_sep, negative_sign */
{
	Value *v ;
	guint len, neg ;
	gchar *s ;

	v = gnumeric_fixed (sheet, l, eval_col, eval_row, error_string) ;
	if (v == NULL)
		return NULL ;
	g_assert (v->type == VALUE_STRING) ;
	len = strlen (v->v.str->str) ;
	neg = (v->v.str->str[0] == '-') ? 1 : 0 ;
	s = g_new (gchar, len + 2 + neg ) ;
	strncpy (&s[1], v->v.str->str, len) ;
	string_unref (v->v.str) ;
	if (neg) {
		s[0] = '(' ;
		s[len+1] = ')' ;
	}
	/* FIXME: should use *lc->currency_symbol */
	s[neg] = '$' ;
	s[len + 1 + neg] = '\0' ;
	v->v.str = string_get (s) ;
	g_free (s) ;
	return v ;
}

FunctionDefinition string_functions [] = {
	{ "char",       "f",   "number",            &help_char,       NULL, gnumeric_char },
/*	{ "clean",      "s",   "text",              &help_clean,      NULL, gnumeric_clean }, // already is in fn-misc.c !?! */
	{ "code",       "s",   "text",              &help_code,       NULL, gnumeric_code },
	{ "concatenate",0,     "text1,text2",       &help_concatenate,gnumeric_concatenate, NULL },
	{ "dollar",     0,     "num,decimals",      &help_dollar,     gnumeric_dollar, NULL },
	{ "exact",      "ss",  "text1,text2",       &help_exact,      NULL, gnumeric_exact },
	{ "find",       "ssf", "text1,text2,num",   &help_find,       gnumeric_find, NULL },
	{ "fixed",      0,     "num,decs,no_commas",&help_fixed,      gnumeric_fixed, NULL },
	{ "left",       0,     "text,num_chars",    &help_left,       gnumeric_left, NULL },
	{ "len",        "s",   "text",              &help_len,        NULL, gnumeric_len },
	{ "lower",      "s",   "text",              &help_lower,      NULL, gnumeric_lower },
	{ "proper",     "s",   "text",              &help_proper,     NULL, gnumeric_proper },
        { "mid",        "sff", "text,pos,num",      &help_mid,        NULL, gnumeric_mid },
	{ "replace",    "sffs","old,start,num,new", &help_replace,    NULL,gnumeric_replace },
	{ "rept",       "sf",  "text,num",          &help_rept,       NULL, gnumeric_rept },
	{ "right",      0,     "text,num_chars",    &help_right,      gnumeric_right, NULL },
	{ "substitute", 0,     "text,old,new,num",  &help_substitute, gnumeric_substitute, NULL },
	{ "t",          "?",   "value",             &help_t,          NULL, gnumeric_t },
	{ "trim",       "s",   "text",              &help_trim,       NULL, gnumeric_trim },
	{ "upper",      "s",   "text",              &help_upper,      NULL, gnumeric_upper },
	{ "value",      "s",   "text",              &help_value,      NULL, gnumeric_value },
	{ NULL, NULL },
};

/* Missing:
 * SEARCH(find, within, start) searches using wildcards
 * TEXT(number,format) formats number
 */

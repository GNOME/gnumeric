/*
 * fn-string.c:  Built in string functions.
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Sean Atkinson (sca20@cam.ac.uk)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include <ctype.h>
#include <math.h>
#include "gnumeric.h"
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
gnumeric_char (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	char result [2];

	result [0] = value_get_as_int (argv [0]);
	result [1] = 0;

	return value_new_string (result);
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
gnumeric_code (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	unsigned char c;

	if (argv [0]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}

	c = argv [0]->v.str->str [0];
	return value_new_int (c);
}

static char *help_exact = {
	N_("@FUNCTION=EXACT\n"
	   "@SYNTAX=EXACT(string1, string2)\n"

	   "@DESCRIPTION="
	   "Returns true if string1 is exactly equal to string2 "
	   "(this routine is case sensitive)."
	   "\n"

	   "@SEEALSO=LEN")  /* FIXME: DELTA, LEN, SEARCH */
};

static Value *
gnumeric_exact (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	if (argv [0]->type != VALUE_STRING || argv [1]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}

	return value_new_int (strcmp (argv [0]->v.str->str,
				      argv [1]->v.str->str) == 0);
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
gnumeric_len (struct FunctionDefinition *i,
	      Value *argv [], char **error_string)
{
	if (argv [0]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}

	return value_new_int (strlen (argv [0]->v.str->str));
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
gnumeric_left (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	Value *v;
	int count;
	char *s;

	if (argv[1])
		count = value_get_as_int(argv[1]);
	else
		count = 1;

	s = g_malloc (count + 1);
	strncpy (s, argv[0]->v.str->str, count);
	s [count] = 0;

	v = value_new_string (s);
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
gnumeric_lower (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	Value *v;
	unsigned char *s, *p;

	if (argv [0]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}

	p = s = g_strdup (argv [0]->v.str->str);
	for (; *p; p++){
		*p = tolower (*p);
	}
	v = value_new_string (s);
	g_free (s);

	return v;
}

static char *help_mid = {
	N_("@FUNCTION=MID\n"
	   "@SYNTAX=MID(string, position, length)\n"

	   "@DESCRIPTION="
	   "Returns a substring from @string starting at @position for "
	   "@length characters."
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
		return value_new_string ("");
	pos--;
	s = g_new (gchar, len+1);
	strncpy (s, &source[pos], len);
	s[len] = '\0';
	v = value_new_string (s);
	g_free (s);

	return v;
}

static char *help_right = {
	N_("@FUNCTION=RIGHT\n"
	   "@SYNTAX=RIGHT(text[,num_chars])\n"

	   "@DESCRIPTION="
	   "Returns the rightmost num_chars characters or the right "
	   "character if num_chars is not specified"
	   "\n"
	   "@SEEALSO=MID, LEFT")
};

static Value *
gnumeric_right (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	Value *v;
	int count, len;
	char *s;

	if (argv[1])
		count = value_get_as_int(argv[1]);
	else
		count = 1;

	len = strlen (argv[0]->v.str->str);
	if (count > len)
		count = len;

	s = g_malloc (count + 1);
	strncpy (s, argv[0]->v.str->str+len-count, count);
	s [count] = 0;

	v = value_new_string (s);
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
gnumeric_upper (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	Value *v;
	unsigned char *s, *p;

	if (argv [0]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}

	p = s = g_strdup (argv [0]->v.str->str);

	for (;*p; p++){
		*p = toupper (*p);
	}
	v = value_new_string (s);
	g_free (s);

	return v;
}

static char *help_concatenate = {
	N_("@FUNCTION=CONCATENATE\n"
	   "@SYNTAX=CONCATENATE(string1[,string2...])\n"
	   "@DESCRIPTION="
	   "Returns up appended strings."
	   "\n"
	   "@SEEALSO=LEFT, MID, RIGHT")
};

static Value *
gnumeric_concatenate (Sheet *sheet, GList *l,
		      int eval_col, int eval_row, char **error_string)
{
	Value *v;
	char *s, *p, *tmp;

	if (l==NULL) {
		*error_string = _("Invalid number of arguments");
		return NULL;
	}
	s = g_new(gchar, 1);
	*s = '\0';
	while ( l != NULL &&
		(v=eval_expr(sheet, l->data, eval_col, eval_row, error_string)) != NULL) {
/*
		if (v->type != VALUE_STRING) {
			*error_string = _("Invalid argument");
			value_release (v);
			return NULL;
		}
*/
		tmp = value_get_as_string (v);
		/* FIXME: this could be massively sped-up with strlen's etc... */
		p = g_strconcat (s, tmp, NULL);
		g_free (tmp);
		value_release (v);
		g_free (s);
		s = p;
		l = g_list_next (l);
	}

	v = value_new_string (s);
	g_free (s);

	return v;
}

static char *help_rept = {
	N_("@FUNCTION=REPT\n"
	   "@SYNTAX=REPT(string,num)\n"
	   "@DESCRIPTION="
	   "Returns @num repetitions of @string."
	   "\n"
	   "@SEEALSO=CONCATENATE")
};

static Value *
gnumeric_rept (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
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
	p = s = g_new (gchar, 1 + len * num);
	while (num--) {
		strncpy (p, argv[0]->v.str->str, len);
		p += len;
	}
	*p = '\0';
	v = value_new_string (s);
	g_free (s);

	return v;
}

static char *help_clean = {
	N_("@FUNCTION=CLEAN\n"
	   "@SYNTAX=CLEAN(string)\n"

	   "@DESCRIPTION="
	   "Cleans the string from any non-printable characters."
	   "\n"

	   "@SEEALSO=")
};

static Value *
gnumeric_clean (FunctionDefinition *fn, Value *argv [], char **error_string)
{
	Value *res;
	unsigned char *copy, *p, *q;

	if (argv [0]->type != VALUE_STRING){
		*error_string = _("Type mismatch");
		return NULL;
	}
	p = argv [0]->v.str->str;
	copy = q = g_malloc (strlen (p) + 1);

	while (*p){
		if (isprint (*p))
			*q++ = *p;
		p++;
	}
	*q = 0;

	res = value_new_string (copy);
	g_free (copy);

	return res;
}

static char *help_find = {
	N_("@FUNCTION=FIND\n"
	   "@SYNTAX=FIND(string1,string2[,start])\n"
	   "@DESCRIPTION="
	   "Returns position of @string1 in @string2 (case-sesitive), "
	   "searching only from character @start onwards (assumed 1 if "
	   "omitted)."
	   "\n"
	   "@SEEALSO=EXACT, LEN, MID, SEARCH")
};

static Value *
gnumeric_find (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	int count;
	char *s, *p;

	if (argv[2])
		count = value_get_as_int(argv[2]);
	else
		count = 1;

	if ( count > strlen(argv[1]->v.str->str) ||
	     count == 0) { /* start position too high or low */
		*error_string = _("Invalid argument");
		return NULL;
	}

	g_assert (count >= 1);
	s = argv[1]->v.str->str + count - 1;
	if ( (p = strstr(s, argv[0]->v.str->str)) == NULL ) {
		*error_string = _("Invalid argument");
		return NULL;
	}

	return value_new_int (count + p - s);
}

static char *help_fixed = {
	N_("@FUNCTION=FIXED\n"
	   "@SYNTAX=FIXED(num, [decimals, no_commas])\n"

	   "@DESCRIPTION="
	   "Returns @num as a formatted string with @decimals numbers "
	   "after the decimal point, omitting commas if requested by "
	   "@no_commas."
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_fixed (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
{
	Value *v;
	gchar *s, *p, *f;
	gint dec, commas, tmp;
	float_t num;

	num = value_get_as_float (argv[0]);
	if (argv[1])
		dec = value_get_as_int (argv[1]);
	else
		dec = 2;

	if (argv[2])
		commas = !value_get_as_bool (argv[2], &tmp);
	else
		commas = TRUE;

	if (dec >= 1000) { /* else buffer under-run */
		*error_string = _("Invalid argument");
		return NULL;
		/*
	} else if (lc->thousands_sep[1] != '\0') {
		fprintf (stderr, "thousands_sep:\"%s\"\n", lc->thousands_sep);
		*error_string = _("Invalid thousands separator");
		return NULL;
		*/
	} else if (dec <= 0) { /* no decimal point: just round and pad 0's */
		dec *= -1;
		num /= pow(10, dec);
		if (num < 1 && num > -1) {
			s = g_strdup("0");
			commas = 0;
		} else {
			f = g_strdup("%00?s%.0f%.00?u"); /* commas, no point, 0's */
			tmp = dec;
			dec += log10(fabs(num));
			if (commas)
				commas = dec / 3;
			p = &f[13]; /* last 0 in trailing 0's count */
			do
				*p-- = '0' + (tmp % 10);
			while (tmp /= 10);
			tmp = commas;
			p = &f[3]; /* last 0 in leading blank spaces for commas */
			do
				*p-- = '0' + (tmp % 10);
			while (tmp /= 10);
			s = g_strdup_printf (f, "", num, 0);
			g_free (f);
		}
	} else { /* decimal point format */
		f = g_strdup("%00?s%.00?f");
		tmp = dec;
		dec = log10(fabs(num));
		if (commas)
			commas = dec / 3;
		p = &f[9];
		do
			*p-- = '0' + (tmp % 10);
		while (tmp /= 10);
		tmp = commas;
		p = &f[3];
		do
			*p-- = '0' + (tmp % 10);
		while (tmp /= 10);
		s = g_strdup_printf (f, "", num);
		g_free (f);
	}
	if (commas) {
		p = s;
		f = &s[commas];
		if (*f == '-')
			*p++ = *f++;
		dec -= 2;
		while (dec-- > 0) {
			*p++ = *f++;
			if (dec%3 == 0)
				/* FIXME: should use lc->thousands_sep[0] */
				*p++ = ',';
		}
	}

	v = value_new_string (s);
	g_free (s);
	return v;
}

/*
 * proper could be a LOT nicer
 * (e.g. "US Of A" -> "US of A", "Cent'S Worth" -> "Cent's Worth")
 * but this is how Excel does it
 */
static char *help_proper = {
	N_("@FUNCTION=PROPER\n"
	   "@SYNTAX=PROPER(string)\n"

	   "@DESCRIPTION="
	   "Returns @string with initial of each word capitalised."
	   "\n"
	   "@SEEALSO=LOWER, UPPER")
};

static Value *
gnumeric_proper (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
	Value *v;
	unsigned char *s, *p;
	gboolean inword = FALSE;

	if (argv [0]->type != VALUE_STRING) {
		*error_string = _("Type mismatch");
		return NULL;
	}

	s = p = g_strdup (argv[0]->v.str->str);
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

	v = value_new_string (p);
	g_free (p);
	return v;
}

static char *help_replace = {
	N_("@FUNCTION=REPLACE\n"
	   "@SYNTAX=REPLACE(old,start,num,new)\n"
	   "@DESCRIPTION="
	   "Returns @old with @new replacing @num characters from @start."
	   "\n"
	   "@SEEALSO=MID, SEARCH, SUBSTITUTE, TRIM")
};

static Value *
gnumeric_replace (struct FunctionDefinition *i,
		  Value *argv [], char **error_string)
{
	Value *v;
	gchar *s;
	gint start, num, oldlen, newlen;

	if (argv[0]->type != VALUE_STRING ||
	    argv[1]->type != VALUE_INTEGER ||
	    argv[2]->type != VALUE_INTEGER ||
	    argv[3]->type != VALUE_STRING ) {
		*error_string = _("Type mismatch");
		return NULL;
	}

	start = value_get_as_int (argv[1]);
	num = value_get_as_int (argv[2]);
	oldlen = strlen(argv[0]->v.str->str);

	if (start <= 0 || num <= 0 || --start + num > oldlen ) {
		*error_string = _("Invalid arguments");
		return NULL;
	}

	newlen = strlen (argv [3]->v.str->str);

	s = g_new (gchar, 1 + newlen + oldlen - num);
	strncpy (s, argv[0]->v.str->str, start);
	strncpy (&s[start], argv[3]->v.str->str, newlen);
	strncpy (&s[start+newlen], &argv[0]->v.str->str[start+num],
		 oldlen - num - start );

	s [newlen+oldlen-num] = '\0';

	v = value_new_string (s);

	g_free(s);

	return v;
}

static char *help_t = {
	N_("@FUNCTION=T\n"
	   "@SYNTAX=T(value)\n"
	   "@DESCRIPTION="
	   "Returns @value if and only if it is text, otherwise a blank "
	   "string.\n"
	   "@SEEALSO=CELL, N, VALUE")
};

static Value *
gnumeric_t (struct FunctionDefinition *i,
	    Value *argv [], char **error_string)
{
	if (argv[0]->type == VALUE_STRING)
		return value_duplicate (argv[0]);
	else
		return value_new_string ("");
}

static char *help_trim = {
	N_("@FUNCTION=TRIM\n"
	   "@SYNTAX=TRIM(text)\n"
	   "@DESCRIPTION="
	   "Returns @text with only single spaces between words."
	   "\n"
	   "@SEEALSO=CLEAN, MID, REPLACE, SUBSTITUTE")
};

static Value *
gnumeric_trim (struct FunctionDefinition *i,
	       Value *argv [], char **error_string)
{
	Value *v;
	gchar *new, *dest, *src;
	gboolean space = TRUE;

	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("Type mismatch");
		return NULL;
	}

	dest = new = g_new (gchar, strlen(argv[0]->v.str->str) + 1);
	src = argv [0]->v.str->str;

	while (*src){
		if (*src == ' '){
			if (!space) {
				*dest++ = *src;
				space = TRUE;
			}
		} else {
			space = FALSE;
			*dest++ = *src;
		}
		src++;
	}
	if (space && dest > new)
		dest--;

	*dest = '\0';

	v = value_new_string (new);
	g_free(new);

	return v;
}

static char *help_value = {
	N_("@FUNCTION=VALUE\n"
	   "@SYNTAX=VALUE(text)\n"
	   "@DESCRIPTION="
	   "Returns numeric value of @text."
	   "\n"
	   "@SEEALSO=DOLLAR, FIXED, TEXT")
};

static Value *
gnumeric_value (struct FunctionDefinition *i,
		Value *argv [], char **error_string)
/* FIXME: in Excel, VALUE("$1, 000") = 1000, and dates etc. supported */
{
	if (argv[0]->type != VALUE_STRING) {
		*error_string = _("Type mismatch");
		return NULL;
	}
	return value_new_float (value_get_as_float (argv[0]));
}

struct subs_string {
	gchar *str;
	guint len;
	guint mem;
};

static struct subs_string *
subs_string_new (guint len)
{
	struct subs_string *s = g_new (struct subs_string, 1);

	s->len = 0;
	s->mem = len;
	s->str = g_new (gchar, len);
	*s->str = '\0';
	return s;
}

static void
subs_string_append_n (struct subs_string *s, gchar *src, guint n)
{
	const guint chunk = 1024;

	while (s->len + n >= s->mem)
		s->str = g_realloc (s->str, s->mem += chunk);

	strncpy (&s->str [s->len], src, n);

	s->len += n;
	s->str [s->len] = '\0';
}

static void
subs_string_free (struct subs_string *s)
{
	g_free (s->str);
	g_free (s);
}

static char *help_substitute = {
	N_("@FUNCTION=SUBSTITUTE\n"
	   "@SYNTAX=SUBSTITUTE(text, old, new [,num])\n"
	   "@DESCRIPTION="
	   "Replaces @old with @new in @text.  Substitutions are only "
	   "applied to instance @num of @old in @text, otherwise every "
	   "one is changed."
	   "\n"
	   "@SEEALSO=REPLACE, TRIM")
};

static Value *
gnumeric_substitute (struct FunctionDefinition *i,
		     Value *argv [], char **error_string)
{
	Value *v;
	gchar *text, *old, *new, *p ,*f;
	gint num;
	guint oldlen, newlen, len, inst;
	struct subs_string *s;

	text = value_get_as_string (argv[0]);
	old  = value_get_as_string (argv[1]);
	new  = value_get_as_string (argv[2]);

	if (argv[3])
		num = value_get_as_int (argv[3]);
	else
		num = 0;

	oldlen = strlen (old);
	newlen = strlen (new);
	len = strlen (text);
	if (newlen != oldlen) {
		s = subs_string_new (len);
	} else
		s = NULL;

	p = text;
	inst = 0;
	while (p-text < len) {
		if ( (f=strstr(p, old)) == NULL )
			break;
		if (num == 0 || num == ++inst) {
			if (s == NULL) {
				strncpy (f, new, newlen);
			} else {
				subs_string_append_n (s, p, f-p);
				subs_string_append_n (s, new, newlen);
			}
			if (num != 0 && num == inst)
				break;
		}
		p = f + oldlen;
	}
	if (newlen != oldlen) { /* FIXME: (p-text) might be bad ? */
		subs_string_append_n (s, p, len - (p-text) );
		p = s->str;
	} else
		p = text;

	v = value_new_string (p);

	g_free (new);
	g_free (old);
	g_free (text);
	if (s != NULL)
		subs_string_free (s);

	return v;
}

static char *help_dollar = {
	N_("@FUNCTION=DOLLAR\n"
	   "@SYNTAX=DOLLAR(num,[decimals])\n"
	   "@DESCRIPTION="
	   "Returns @num formatted as currency."
	   "\n"
	   "@SEEALSO=FIXED, TEXT, VALUE")
};

/* FIXME: should use lc->[pn]_sign_posn, mon_thousands_sep, negative_sign */
static Value *
gnumeric_dollar (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
	Value *v, *ag [3];
	guint len, neg;
	gchar *s;

	g_warning ("GNUMERIC_DOLLAR is broken, it should use the "
		   "format_value routine");
	ag[0] = argv [0];
	ag[1] = argv [1];
	ag[2] = NULL;

	v = gnumeric_fixed (i, ag, error_string);
	if (v == NULL)
		return NULL;

	g_assert (v->type == VALUE_STRING);

	len = strlen (v->v.str->str);
	neg = (v->v.str->str [0] == '-') ? 1 : 0;

	s = g_new (gchar, len + 2 + neg);
	strncpy (&s [1], v->v.str->str, len);

	string_unref (v->v.str);
	if (neg){
		s [0] = '(';
		s [len+1] = ')';
	}
	/* FIXME: should use *lc->currency_symbol */
	s[neg] = '$';
	s[len + 1 + neg] = '\0';
	v->v.str = string_get (s);
	g_free (s);
	return v;
}

static char *help_search = {
	N_("@FUNCTION=SEARCH\n"
	   "@SYNTAX=SEARCH(text,within[,start_num])\n"
	   "@DESCRIPTION="
	   "Returns the location of a character or text string within "
	   "another string.  @text is the string or character to be searched. "
	   "@within is the string in which you want to search.  @start_num "
	   "is the start position of the search in @within.  If @start_num "
	   "is omitted, it is assumed to be one.  The search is not case "
	   "sensitive. "
	   "\n"
	   "@text can contain wildcard characters (*) and question marks (?) "
	   "to control the search.  A question mark matches with any "
	   "character and wildcard matches with any string including empty "
	   "string.  If you want the actual wildcard or question mark to "
	   "be searched, use tilde (~) before the character. "
	   "\n"
	   "If @text is not found, SEARCH returns #VALUE! error. "
	   "If @start_num is less than one or it is greater than the length "
	   "of @within, SEARCH returns #VALUE! error. "
	   "\n"
	   "@SEEALSO=FIND")
};

typedef struct {
        gchar    *str;
        int      min_skip;
        gboolean wildcard_prefix;
} string_search_t;


static int
wildcards_and_question_marks(gchar *find_str, int *qmarks, int *wildcard)
{
        int pos, skip=0;

	*wildcard = 0;
	for (pos=0; find_str[pos]; pos++)
	        if (find_str[pos] == '?')
		        ++skip;
	        else if (find_str[pos] == '*')
		        *wildcard = 1;
	        else
		        break;
	*qmarks = skip;

	return pos;
}


/* Breaks the regular expression into a list of string and skip pairs.
 */
static GSList *
parse_search_string(gchar *find_str)
{
        string_search_t *search_cond;
        GSList          *conditions = NULL;
	int             i, pos, qmarks;
	gboolean        wildcard;
        gchar           *buf, *p;

	buf = g_new(gchar, strlen(find_str) + 1);
	p = buf;
	i = 0;
	
	pos = wildcards_and_question_marks(find_str, &qmarks, &wildcard);
	wildcard = 1;
	find_str += pos;
	
	while (*find_str) {
	        if (*find_str == '~') {
		        buf[i++] = *(++find_str);
			find_str++;
		} else if (*find_str == '?' || *find_str == '*') {
		        buf[i] = '\0';
			search_cond = g_new(string_search_t, 1);
			search_cond->str = g_new(gchar, strlen(buf)+1);
			strcpy(search_cond->str, buf);
			search_cond->min_skip = qmarks;
			search_cond->wildcard_prefix = wildcard;
			conditions = g_slist_append(conditions, search_cond);
			i = 0;
			pos = wildcards_and_question_marks(find_str, &qmarks,
							   &wildcard);
			find_str += pos;
		} else
		        buf[i++] = *find_str++;
	}
	buf[i] = '\0';
	search_cond = g_new(string_search_t, 1);
	search_cond->str = g_new(gchar, strlen(buf)+1);
	strcpy(search_cond->str, buf);
	search_cond->min_skip = qmarks;
	search_cond->wildcard_prefix = wildcard;
	conditions = g_slist_append(conditions, search_cond);
	
	g_free(buf);

	return conditions;
}

/* Returns 1 if a given condition ('cond') matches with 'str'.  'match_start'
 * points to the beginning of the token found and 'match_end' points to the
 * end of the token.
 */
static int
match_string(gchar *str, string_search_t *cond, gchar **match_start, 
	     gchar **match_end)
{
        gchar *p;

        if (cond->min_skip > strlen(str))
	        return 0;

	if (*cond->str == '\0') {
	         *match_start = str;
		 *match_end = str+1;
		 return 1;
	}
        p = strstr(str+cond->min_skip, cond->str);

	/* Check no match case */
	if (p == NULL)
	        return 0;

	/* Check if match in a wrong place and no wildcard */
	if (! cond->wildcard_prefix && p > str+cond->min_skip)
	        return 0;

	/* Matches correctly */
	*match_start = p-cond->min_skip;
	*match_end = p+strlen(cond->str);

	return 1;
}

static void
free_all_after_search(GSList *conditions, gchar *text, gchar *within)
{
        GSList          *current;
	string_search_t *current_cond;

	current = conditions;
	while (current != NULL) {
	        current_cond = current->data;
	        g_free(current_cond->str);
		g_free(current_cond);
		current = current->next;
	}
	g_slist_free(conditions);
	g_free(text);
	g_free(within);
}

static Value *
gnumeric_search (struct FunctionDefinition *i,
		 Value *argv [], char **error_string)
{
        GSList          *conditions, *current;
	string_search_t *current_cond;
        int             ret, start_num, within_len;
	gchar           *text, *within, *match_str, *match_str_next;
	gchar           *p_start, *p_end;

	if (argv[2] == NULL)
	        start_num = 0;
	else
	        start_num = value_get_as_int(argv[2]) - 1;

	text = g_new(gchar, strlen(argv[0]->v.str->str) + 1);
	within = g_new(gchar, strlen(argv[1]->v.str->str) + 1);
	strcpy(text, argv[0]->v.str->str);
	strcpy(within, argv[1]->v.str->str);
	g_strdown(text);
	g_strdown(within);
	
	within_len = strlen(within);

	if (within_len <= start_num) {
	        g_free(text);
		g_free(within);
		*error_string = gnumeric_err_VALUE;
		return NULL;
	}

	conditions = parse_search_string(text);
	if (conditions == NULL) {
	        g_free(text);
		g_free(within);
		*error_string = gnumeric_err_VALUE;
		return NULL;
	}

	match_str = within;

match_again:
	current = conditions;
	current_cond = current->data;
	ret = match_string(match_str, current_cond, &p_start, &p_end);
	if (ret) {
	        current = current->next;
		if (current == NULL) {
			free_all_after_search(conditions, text, within);
			return value_new_int (p_start - within + 1);
		}
		current_cond = current->data;
		match_str = p_start;
		match_str_next = p_end;
		while (match_string(p_end, current_cond, &p_start, &p_end)) {
		        current = current->next;
			if (current == NULL) {
				free_all_after_search(conditions,
						      text, within);
				return value_new_int (match_str - within + 1);
			}
			current_cond = current->data;
		}
		match_str = match_str_next;
		goto match_again;
	}

	free_all_after_search(conditions, text, within);
	*error_string = gnumeric_err_VALUE;
	return NULL;
}


FunctionDefinition string_functions [] = {
	{ "char",       "f",    "number",
	  &help_char,       NULL, gnumeric_char },
	{ "clean",      "s",    "text",
	  &help_clean,      NULL, gnumeric_clean },
	{ "code",       "s",    "text",
	  &help_code,       NULL, gnumeric_code },
	{ "concatenate",0,      "text1,text2", 
	  &help_concatenate,gnumeric_concatenate, NULL },
	{ "dollar",     "f|f",  "num,decimals",
	  &help_dollar,     NULL, gnumeric_dollar },
	{ "exact",      "ss",   "text1,text2",
	  &help_exact,      NULL, gnumeric_exact },
	{ "find",       "ss|f", "text1,text2,num",
	  &help_find,       NULL, gnumeric_find },
	{ "fixed",      "f|fb", "num,decs,no_commas",
	  &help_fixed,      NULL, gnumeric_fixed },
	{ "left",       "s|f",  "text,num_chars",
	  &help_left,       NULL, gnumeric_left },
	{ "len",        "s",    "text",
	  &help_len,        NULL, gnumeric_len },
	{ "lower",      "s",    "text",
	  &help_lower,      NULL, gnumeric_lower },
	{ "proper",     "s",    "text",
	  &help_proper,     NULL, gnumeric_proper },
        { "mid",        "sff",  "text,pos,num",
	  &help_mid,        NULL, gnumeric_mid },
	{ "replace",    "sffs", "old,start,num,new",
	  &help_replace,    NULL, gnumeric_replace },
	{ "rept",       "sf",   "text,num",
          &help_rept,       NULL, gnumeric_rept },
	{ "right",      "s|f",  "text,num_chars",
	  &help_right,      NULL, gnumeric_right },
	{ "search",     "ss|f",  "find,within[,start_num]",
	  &help_search,     NULL, gnumeric_search },
	{ "substitute", "sss|f","text,old,new,num",
	  &help_substitute, NULL, gnumeric_substitute },
	{ "t",          "?",    "value",
	  &help_t,          NULL, gnumeric_t },
	{ "trim",       "s",    "text",
	  &help_trim,       NULL, gnumeric_trim },
	{ "upper",      "s",    "text",
	  &help_upper,      NULL, gnumeric_upper },
	{ "value",      "s",    "text",
	  &help_value,      NULL, gnumeric_value },
	{ NULL, NULL },
};

/* Missing:
 * TEXT(number,format) formats number
 */

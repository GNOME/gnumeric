/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-string.c:  Built in string functions.
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Sean Atkinson (sca20@cam.ac.uk)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 */
#include <config.h>
#include "func.h"
#include "parse-util.h"
#include "cell.h"
#include "format.h"
#include "str.h"
#include "sheet.h"
#include "number-match.h"

#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <string.h>

/***************************************************************************/

static char *help_char = {
	N_("@FUNCTION=CHAR\n"
	   "@SYNTAX=CHAR(x)\n"

	   "@DESCRIPTION="
	   "CHAR returns the ASCII character represented by the number @x."
	   "\n"
	   "@EXAMPLES=\n"
	   "CHAR(65) equals A.\n"
	   "\n"
	   "@SEEALSO=CODE")
};

static Value *
gnumeric_char (FunctionEvalInfo *ei, Value **argv)
{
	char result[2];

	result[0] = value_get_as_int (argv[0]);
	result[1] = 0;

	return value_new_string (result);
}

/***************************************************************************/

static char *help_code = {
	N_("@FUNCTION=CODE\n"
	   "@SYNTAX=CODE(char)\n"

	   "@DESCRIPTION="
	   "CODE returns the ASCII number for the character @char.\n"
           "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "CODE(\"A\") equals 65.\n"
	   "\n"
	   "@SEEALSO=CHAR")
};

static Value *
gnumeric_code (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_int (*(unsigned char *)value_peek_string (argv[0]));
}

/***************************************************************************/

static char *help_exact = {
	N_("@FUNCTION=EXACT\n"
	   "@SYNTAX=EXACT(string1, string2)\n"

	   "@DESCRIPTION="
	   "EXACT returns true if @string1 is exactly equal to @string2 "
	   "(this routine is case sensitive).\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "EXACT(\"key\",\"key\") equals TRUE.\n"
	   "EXACT(\"key\",\"Key\") equals FALSE.\n"
	   "\n"
	   "@SEEALSO=LEN, SEARCH, DELTA")
};

static Value *
gnumeric_exact (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_bool (strcmp (value_peek_string (argv[0]),
				       value_peek_string (argv[1])) == 0);
}

/***************************************************************************/

static char *help_len = {
	N_("@FUNCTION=LEN\n"
	   "@SYNTAX=LEN(string)\n"

	   "@DESCRIPTION="
	   "LEN returns the length in characters of the string @string.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "len(\"Helsinki\") equals 8.\n"
	   "\n"
	   "@SEEALSO=CHAR, CODE")
};

static Value *
gnumeric_len (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_int (strlen (value_peek_string (argv[0])));
}

/***************************************************************************/

static char *help_left = {
	N_("@FUNCTION=LEFT\n"
	   "@SYNTAX=LEFT(text[,num_chars])\n"

	   "@DESCRIPTION="
	   "LEFT returns the leftmost @num_chars characters or the left "
	   "character if @num_chars is not specified.\n"
           "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "LEFT(\"Directory\",3) equals \"Dir\".\n"
	   "\n"
	   "@SEEALSO=MID, RIGHT")
};

static Value *
gnumeric_left (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	char *s;
	int count, slen;

	count = argv[1] ? value_get_as_int (argv[1]) : 1;
	s = value_get_as_string (argv[0]);

	slen = strlen (s);
	if (count < slen)
		s[count] = 0;
	v = value_new_string (s);
	g_free (s);

	return v;
}

/***************************************************************************/

static char *help_lower = {
	N_("@FUNCTION=LOWER\n"
	   "@SYNTAX=LOWER(text)\n"

	   "@DESCRIPTION="
	   "LOWER returns a lower-case version of the string in @text.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "LOWER(\"J. F. Kennedy\") equals \"j. f. kennedy\".\n"
	   "\n"
	   "@SEEALSO=UPPER")
};

static Value *
gnumeric_lower (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	char *s;

	s = value_get_as_string (argv[0]);
	g_strdown (s);
	v = value_new_string (s);
	g_free (s);

	return v;
}

/***************************************************************************/

static char *help_mid = {
	N_("@FUNCTION=MID\n"
	   "@SYNTAX=MID(string, position, length)\n"

	   "@DESCRIPTION="
	   "MID returns a substring from @string starting at @position for "
	   "@length characters.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "MID(\"testing\",2,3) equals \"est\".\n"
	   "\n"
	   "@SEEALSO=LEFT, RIGHT")
};

static Value *
gnumeric_mid (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	int pos, len;
	char *s;
	char const *source;

	pos = value_get_as_int (argv[1]);
	len = value_get_as_int (argv[2]);

	if (len < 0 || pos <= 0)
		return value_new_error (ei->pos, _("Invalid arguments"));

	pos--;  /* Make pos zero-based.  */

	source = value_peek_string (argv[0]);
	len = MIN (len, (int)strlen (source) - pos);

	s = g_new (gchar, len + 1);
	memcpy (s, source + pos, len);
	s[len] = '\0';
	v = value_new_string (s);
	g_free (s);

	return v;
}

/***************************************************************************/

static char *help_right = {
	N_("@FUNCTION=RIGHT\n"
	   "@SYNTAX=RIGHT(text[,num_chars])\n"

	   "@DESCRIPTION="
	   "RIGHT returns the rightmost @num_chars characters or the right "
	   "character if @num_chars is not specified.\n"
           "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "RIGHT(\"end\") equals \"d\".\n"
	   "RIGHT(\"end\",2) equals \"nd\".\n"
	   "\n"
	   "@SEEALSO=MID, LEFT")
};

static Value *
gnumeric_right (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	int count, slen;
	char *s;

	count = argv[1] ? value_get_as_int (argv[1]) : 1;
	s = value_get_as_string (argv[0]);

	slen = strlen (s);
	if (count < slen) {
		memmove (s, s + (slen - count), count);
		s[count] = 0;
	}
	v = value_new_string (s);
	g_free (s);

	return v;
}

/***************************************************************************/

static char *help_upper = {
	N_("@FUNCTION=UPPER\n"
	   "@SYNTAX=UPPER(text)\n"

	   "@DESCRIPTION="
	   "UPPER returns a upper-case version of the string in @text.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "UPPER(\"canceled\") equals \"CANCELED\".\n"
	   "\n"
	   "@SEEALSO=LOWER")
};

static Value *
gnumeric_upper (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	char *s;

	s = value_get_as_string (argv[0]);
	g_strup (s);
	v = value_new_string (s);
	g_free (s);

	return v;
}

/***************************************************************************/

static char *help_concatenate = {
	N_("@FUNCTION=CONCATENATE\n"
	   "@SYNTAX=CONCATENATE(string1[,string2...])\n"
	   "@DESCRIPTION="
	   "CONCATENATE returns up appended strings.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "CONCATENATE(\"aa\",\"bb\") equals \"aabb\".\n"
	   "\n"
	   "@SEEALSO=LEFT, MID, RIGHT")
};

static Value *
gnumeric_concatenate (FunctionEvalInfo *ei, GList *l)
{
	Value *v;
	GString *s;

	if (l == NULL)
		return value_new_error (ei->pos,
					_("Invalid number of arguments"));

	s = g_string_new ("");
	while (l != NULL 
	       && (v = expr_eval (l->data, ei->pos, EVAL_STRICT)) != NULL) {
		if (VALUE_IS_EMPTY_OR_ERROR (v))
			goto error;
		g_string_append (s, value_peek_string (v));
		l = g_list_next (l);
		value_release (v);
	}

	v = value_new_string (s->str);

 error:
	g_string_free (s, TRUE);
	return v;
}

/***************************************************************************/

static char *help_rept = {
	N_("@FUNCTION=REPT\n"
	   "@SYNTAX=REPT(string,num)\n"
	   "@DESCRIPTION="
	   "REPT returns @num repetitions of @string.\n"
           "This function is Excel compatible. "
	   "\n"
	   "@EXAMPLES=\n"
	   "REPT(\".\",3) equals \"...\".\n"
	   "\n"
	   "@SEEALSO=CONCATENATE")
};

static Value *
gnumeric_rept (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	char *s, *p;
	const char *source;
	int num;
	int len;

	num = value_get_as_int (argv[1]);
	if (num < 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	source = value_peek_string (argv[0]);
	len = strlen (source);

	/* Fast special case.  =REPT ("",2^30) should not take long.  */
	if (len == 0 || num == 0)
		return value_new_string ("");

	/* Check if the length would overflow.  */
	if (num >= INT_MAX / len)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	p = s = g_new (gchar, 1 + len * num);
	if (!p)
		/* FIXME: this and above case should probably have the
		   same error message.  */
		return value_new_error (ei->pos, _("Out of memory"));

	while (num--) {
		memcpy (p, source, len);
		p += len;
	}
	*p = '\0';
	v = value_new_string (s);
	g_free (s);

	return v;
}

/***************************************************************************/

static char *help_clean = {
	N_("@FUNCTION=CLEAN\n"
	   "@SYNTAX=CLEAN(string)\n"
	   "@DESCRIPTION="
	   "CLEAN removes any non-printable characters from @string.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "CLEAN(\"one\"\\&char(7)) equals \"one\".\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_clean (FunctionEvalInfo *ei, Value **argv)
{
	Value *res;
	unsigned char *s, *p, *q;

	s = value_get_as_string (argv[0]);
	for (p = q = s; *p; p++)
		if (isprint (*p))
			*q++ = *p;
	*q = 0;
	res = value_new_string (s);
	g_free (s);

	return res;
}

/***************************************************************************/

static char *help_find = {
	N_("@FUNCTION=FIND\n"
	   "@SYNTAX=FIND(string1,string2[,start])\n"
	   "@DESCRIPTION="
	   "FIND returns position of @string1 in @string2 (case-sensitive), "
	   "searching only from character @start onwards (assuming 1 if "
	   "omitted).\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "FIND(\"ac\",\"Jack\") equals 2.\n"
	   "\n"
	   "@SEEALSO=EXACT, LEN, MID, SEARCH")
};

static Value *
gnumeric_find (FunctionEvalInfo *ei, Value **argv)
{
	int count, haystacksize;
	const char *haystack, *needle;

	needle = value_peek_string (argv[0]);
	haystack = value_peek_string (argv[1]);
	count = argv[2] ? value_get_as_int (argv[2]) : 1;

	haystacksize = strlen (haystack);

	if (count <= 0 || count > haystacksize) {
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else {
		const char *haystart = haystack + (count - 1);
		const char *p = strstr (haystart, needle);
		if (p)
			return value_new_int (count + (p - haystart));
		else
			/* Really?  */
			return value_new_error (ei->pos, gnumeric_err_VALUE);
	}
}

/***************************************************************************/

static char *help_fixed = {
	N_("@FUNCTION=FIXED\n"
	   "@SYNTAX=FIXED(num,[decimals, no_commas])\n"
	   "@DESCRIPTION="
	   "FIXED returns @num as a formatted string with @decimals numbers "
	   "after the decimal point, omitting commas if requested by "
	   "@no_commas.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "FIXED(1234.567,2) equals \"1,234.57\".\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_fixed (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	gchar *s, *p, *f;
	gint dec, commas, tmp;
	gnum_float num;

	num = value_get_as_float (argv[0]);
	dec = argv[1] ? value_get_as_int (argv[1]) : 2;

	if (argv[2]) {
		gboolean err;
		commas = !value_get_as_bool (argv[2], &err);
		if (err)
			return value_new_error (ei->pos, _("Type Mismatch"));
	} else
		commas = TRUE;

	if (dec >= 1000) { /* else buffer under-run */
		return value_new_error (ei->pos, gnumeric_err_VALUE);
		/*
	} else if (lc->thousands_sep[1] != '\0') {
		fprintf (stderr, "thousands_sep:\"%s\"\n", lc->thousands_sep);
		return value_new_error (ei->pos,
		_("Invalid thousands separator"));
		*/
	} else if (dec <= 0) { /* no decimal point : just round and pad 0's */
		dec *= -1;
		num /= pow (10, dec);
		if (num < 1 && num > -1) {
			s = g_strdup ("0");
			commas = 0;
		} else {
			f = g_strdup ("%00?s%.0f%.00?u"); /* commas, no point, 0's */
			tmp = dec;
			dec += log10 (fabs (num));
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
		f = g_strdup ("%00?s%.00?f");
		tmp = dec;
		dec = log10 (fabs (num));
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

/***************************************************************************/

static char *help_proper = {
	N_("@FUNCTION=PROPER\n"
	   "@SYNTAX=PROPER(string)\n"

	   "@DESCRIPTION="
	   "PROPER returns @string with initial of each word capitalised.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "PROPER(\"j. f. kennedy\") equals \"J. F. Kennedy\".\n"
	   "\n"
	   "@SEEALSO=LOWER, UPPER")
};

/*
 * proper could be a LOT nicer
 * (e.g. "US Of A" -> "US of A", "Cent'S Worth" -> "Cent's Worth")
 * but this is how Excel does it
 */
static Value *
gnumeric_proper (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	unsigned char *s, *p;
	gboolean inword = FALSE;

	s = value_get_as_string (argv[0]);
	for (p = s; *p; p++) {
		if (isalpha (*p)) {
			if (inword) {
				*p = tolower (*p);
			} else {
				*p = toupper (*p);
				inword = TRUE;
			}
		} else
			inword = FALSE;
	}

	v = value_new_string (s);
	g_free (s);
	return v;
}

/***************************************************************************/

static char *help_replace = {
	N_("@FUNCTION=REPLACE\n"
	   "@SYNTAX=REPLACE(old,start,num,new)\n"
	   "@DESCRIPTION="
	   "REPLACE returns @old with @new replacing @num characters from "
	   "@start.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "REPLACE(\"testing\",2,3,\"*****\") equals \"t*****ing\".\n"
	   "\n"
	   "@SEEALSO=MID, SEARCH, SUBSTITUTE, TRIM")
};

static Value *
gnumeric_replace (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	GString *s;
	gint start, num, oldlen, newlen;
	char const *old;
	char const *new;

	start = value_get_as_int (argv[1]);
	num = value_get_as_int (argv[2]);
	old = value_peek_string (argv[0]);
	oldlen = strlen (old);

	if (start <= 0 || num <= 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	start--;  /* Make this zero-based.  */

	if (start + num > oldlen)
		num = oldlen - start;

	new = value_peek_string (argv[3]);
	newlen = strlen (new);

	s = g_string_new (old);
	g_string_erase (s, start, num);
	g_string_insert (s, start, new);

	v = value_new_string (s->str);
	g_string_free (s, TRUE);

	return v;
}

/***************************************************************************/

static char *help_t = {
	N_("@FUNCTION=T\n"
	   "@SYNTAX=T(value)\n"
	   "@DESCRIPTION="
	   "T returns @value if and only if it is text, otherwise a blank "
	   "string.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "T(\"text\") equals \"text\".\n"
	   "T(64) returns an empty cell.\n"
	   "\n"
	   "@SEEALSO=CELL, N, VALUE")
};

static Value *
gnumeric_t (FunctionEvalInfo *ei, Value **argv)
{
	if (argv[0]->type == VALUE_STRING)
		return value_duplicate (argv[0]);
	else
		return value_new_string ("");
}

/***************************************************************************/

static char *help_text = {
	N_("@FUNCTION=TEXT\n"
	   "@SYNTAX=TEXT(value,format_text)\n"
	   "@DESCRIPTION="
	   "TEXT returns @value as a string with the specified format.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "TEXT(3.223,\"$0.00\") equals \"$3.22\".\n"
	   "TEXT(date(1999,4,15),\"mmmm, dd, yy\") equals \"April, 15, 99\".\n"
	   "\n"
	   "@SEEALSO=DOLLAR")
};

static Value *
gnumeric_text (FunctionEvalInfo *ei, Value **args)
{
	StyleFormat *format = style_format_new_XL (value_peek_string (args[1]), TRUE);
	Value *res, *tmp = NULL;
	Value const *arg  = args[0];
	gboolean ok = FALSE;

	if (arg->type == VALUE_STRING) {
		Value *match = format_match (value_peek_string (arg), NULL, NULL);
		ok = (match != NULL);
		if (ok)
			tmp = match;
	} else
		ok = VALUE_IS_NUMBER (arg);

	if (ok) {
		char *str = format_value (format,
					  (tmp != NULL) ? tmp : arg,
					  NULL, -1);
		res = value_new_string (str);
		g_free (str);
	} else
		res = value_new_error (ei->pos, _("Type mismatch"));

	if (tmp != NULL)
		value_release (tmp);
	style_format_unref (format);
	return res;
}


/***************************************************************************/

static char *help_trim = {
	N_("@FUNCTION=TRIM\n"
	   "@SYNTAX=TRIM(text)\n"
	   "@DESCRIPTION="
	   "TRIM returns @text with only single spaces between words.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "TRIM(\"  a bbb  cc\") equals \"a bbb cc\".\n"
	   "\n"
	   "@SEEALSO=CLEAN, MID, REPLACE, SUBSTITUTE")
};

static Value *
gnumeric_trim (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	gchar *new, *dest, *src;
	gboolean space = TRUE;

	src = dest = new = value_get_as_string (argv[0]);

	while (*src) {
		if (*src == ' ') {
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
	g_free (new);

	return v;
}

/***************************************************************************/

static char *help_value = {
	N_("@FUNCTION=VALUE\n"
	   "@SYNTAX=VALUE(text)\n"
	   "@DESCRIPTION="
	   "VALUE returns numeric value of @text.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "VALUE(\"$1,000\") equals 1000.\n"
	   "\n"
	   "@SEEALSO=DOLLAR, FIXED, TEXT")
};

static Value *
gnumeric_value (FunctionEvalInfo *ei, Value **argv)
{
	switch (argv[0]->type) {
	case VALUE_EMPTY:
	case VALUE_INTEGER:
	case VALUE_FLOAT:
	case VALUE_BOOLEAN:
		return value_duplicate (argv[0]);

	default: {
		Value *v;
		unsigned char *p, *arg = value_get_as_string (argv[0]);

		/* Skip leading spaces */
		for (p = arg ; *p && isspace (*p) ; ++p)
			;

		v = format_match_number (p, NULL, NULL);
		g_free (arg);

		if (v != NULL)
			return v;
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}
	}
}

/***************************************************************************/

static char *help_substitute = {
	N_("@FUNCTION=SUBSTITUTE\n"
	   "@SYNTAX=SUBSTITUTE(text, old, new [,num])\n"
	   "@DESCRIPTION="
	   "SUBSTITUTE replaces @old with @new in @text.  Substitutions "
	   "are only applied to instance @num of @old in @text, otherwise "
	   "every one is changed.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "SUBSTITUTE(\"testing\",\"test\",\"wait\") equals \"waiting\".\n"
	   "\n"
	   "@SEEALSO=REPLACE, TRIM")
};

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

	strncpy (&s->str[s->len], src, n);

	s->len += n;
	s->str[s->len] = '\0';
}

static void
subs_string_free (struct subs_string *s)
{
	g_free (s->str);
	g_free (s);
}

static Value *
gnumeric_substitute (FunctionEvalInfo *ei, Value **argv)
{
	Value *v;
	gchar *text, *old, *new, *p ,*f;
	int num;
	int oldlen, newlen, len, inst;
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
	while (p - text < len) {
		if ( (f = strstr (p, old)) == NULL )
			break;
		if (num == 0 || num == ++inst) {
			if (s == NULL) {
				strncpy (f, new, newlen);
			} else {
				subs_string_append_n (s, p, f - p);
				subs_string_append_n (s, new, newlen);
			}
			if (num != 0 && num == inst)
				break;
		}
		p = f + oldlen;
	}
	if (newlen != oldlen) { /* FIXME: (p-text) might be bad ? */
		subs_string_append_n (s, p, len - (p - text) );
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

/***************************************************************************/

static char *help_dollar = {
	N_("@FUNCTION=DOLLAR\n"
	   "@SYNTAX=DOLLAR(num[,decimals])\n"
	   "@DESCRIPTION="
	   "DOLLAR returns @num formatted as currency.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "DOLLAR(12345) equals \"$12,345.00\".\n"
	   "\n"
	   "@SEEALSO=FIXED, TEXT, VALUE")
};

static Value *
gnumeric_dollar (FunctionEvalInfo *ei, Value **argv)
{
	Value *v, *ag[3];
	guint len, neg;
	gchar *s;
	static int barfed = 0;

	gnum_float x;
	int     i, n;

	if (!barfed) {
		g_warning ("GNUMERIC_DOLLAR is broken, it should use the "
			   "format_value routine");
		barfed = 1;
	}
	if (argv[1] != NULL) {
		x = 0.5;
		n = value_get_as_int (argv[1]);
		for (i = 0; i < n; i++)
			x /= 10;
		ag[0] = value_new_float (value_get_as_float (argv[0]) + x);
	} else
		ag[0] = value_duplicate (argv[0]);

	ag[1] = argv[1];
	ag[2] = NULL;

	v = gnumeric_fixed (ei, ag);
	if (v == NULL)
		return NULL;

	g_assert (v->type == VALUE_STRING);

	len = strlen (v->v_str.val->str);
	neg = (v->v_str.val->str[0] == '-') ? 1 : 0;

	s = g_new (gchar, len + 2 + neg);
	strncpy (&s[1], v->v_str.val->str, len);

	string_unref (v->v_str.val);
	if (neg) {
		s[0] = '(';
		s[len + 1] = ')';
	}
	/* FIXME: should use *lc->currency_symbol */
	s[neg] = '$';
	s[len + 1 + neg] = '\0';
	v->v_str.val = string_get_nocopy (s);
	value_release (ag[0]);

	return v;
}

/***************************************************************************/

static char *help_search = {
	N_("@FUNCTION=SEARCH\n"
	   "@SYNTAX=SEARCH(text,within[,start_num])\n"
	   "@DESCRIPTION="
	   "SEARCH returns the location of a character or text string within "
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
	   "of @within, SEARCH returns #VALUE! error.\n"
           "This function is Excel compatible."
	   "\n"
	   "@EXAMPLES=\n"
	   "SEARCH(\"c\",\"Cancel\") equals 1.\n"
	   "SEARCH(\"c\",\"Cancel\",2) equals 4.\n"
	   "\n"
	   "@SEEALSO=FIND")
};

typedef struct {
        gchar    *str;
        int      min_skip;
        gboolean wildcard_prefix;
} string_search_t;

static int
wildcards_and_question_marks (const gchar *find_str, int *qmarks, int *wildcard)
{
        int pos, skip = 0;

	*wildcard = 0;
	for (pos = 0; find_str[pos]; pos++)
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
parse_search_string (const gchar *find_str)
{
        string_search_t *search_cond;
        GSList          *conditions = NULL;
	int             i, pos, qmarks;
	gboolean        wildcard;
        gchar           *buf, *p;

	buf = g_new (gchar, strlen (find_str) + 1);
	p = buf;
	i = 0;

	pos = wildcards_and_question_marks (find_str, &qmarks, &wildcard);
	wildcard = 1;
	find_str += pos;

	while (*find_str) {
		if (*find_str == '~') {
			buf[i++] = *(++find_str);
			find_str++;
		} else if (*find_str == '?' || *find_str == '*') {
			buf[i] = '\0';
			search_cond = g_new (string_search_t, 1);
			search_cond->str = g_strdup (buf);
			search_cond->min_skip = qmarks;
			search_cond->wildcard_prefix = wildcard;
			conditions = g_slist_append (conditions, search_cond);
			i = 0;
			pos = wildcards_and_question_marks (find_str, &qmarks,
							    &wildcard);
			find_str += pos;
		} else
			buf[i++] = *find_str++;
	}
	buf[i] = '\0';
	search_cond = g_new (string_search_t, 1);
	search_cond->str = g_strdup (buf);
	search_cond->min_skip = qmarks;
	search_cond->wildcard_prefix = wildcard;
	conditions = g_slist_append (conditions, search_cond);

	g_free (buf);

	return conditions;
}

/* Returns 1 if a given condition ('cond') matches with 'str'.  'match_start'
 * points to the beginning of the token found and 'match_end' points to the
 * end of the token.
 */
static int
match_string (const gchar *str, string_search_t *cond, gchar **match_start,
	      gchar **match_end)
{
        gchar *p;

        if (cond->min_skip > (int)strlen (str))
		return 0;

	if (*cond->str == '\0') {
		 *match_start = (char *)str;
		 *match_end = (char *)str + 1;
		 return 1;
	}
        p = strstr (str + cond->min_skip, cond->str);

	/* Check no match case */
	if (p == NULL)
		return 0;

	/* Check if match in a wrong place and no wildcard */
	if (!cond->wildcard_prefix && p > str + cond->min_skip)
		return 0;

	/* Matches correctly */
	*match_start = p - cond->min_skip;
	*match_end = p + strlen (cond->str);

	return 1;
}

static void
free_all_after_search (GSList *conditions, gchar *text, gchar *within)
{
        GSList          *current;
	string_search_t *current_cond;

	current = conditions;
	while (current != NULL) {
		current_cond = current->data;
		g_free (current_cond->str);
		g_free (current_cond);
		current = current->next;
	}
	g_slist_free (conditions);
	g_free (text);
	g_free (within);
}

static Value *
gnumeric_search (FunctionEvalInfo *ei, Value **argv)
{
        GSList          *conditions, *current;
	string_search_t *current_cond;
        int             ret, start_num, within_len;
	gchar           *text, *within, *match_str, *match_str_next;
	gchar           *p_start, *p_end;

	if (argv[2] == NULL)
		start_num = 0;
	else
		start_num = value_get_as_int (argv[2]) - 1;

	text = value_get_as_string (argv[0]);
	within = value_get_as_string (argv[1]);
	g_strdown (text);
	g_strdown (within);

	within_len = strlen (within);

	if (within_len <= start_num) {
		g_free (text);
		g_free (within);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	conditions = parse_search_string (text);
	if (conditions == NULL) {
		g_free (text);
		g_free (within);
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	match_str = within + start_num;

match_again:
	current = conditions;
	current_cond = current->data;
	ret = match_string (match_str, current_cond, &p_start, &p_end);
	if (ret) {
		current = current->next;
		if (current == NULL) {
			free_all_after_search (conditions, text, within);
			return value_new_int (p_start - within + 1);
		}
		current_cond = current->data;
		match_str = p_start;
		match_str_next = p_end;
		while (match_string (p_end, current_cond, &p_start, &p_end)) {
			current = current->next;
			if (current == NULL) {
				free_all_after_search (conditions,
						       text, within);
				return value_new_int (match_str - within + 1);
			}
			current_cond = current->data;
		}
		match_str = match_str_next;
		goto match_again;
	}

	free_all_after_search (conditions, text, within);

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

void string_functions_init (void);

void
string_functions_init (void)
{
	FunctionCategory *cat = function_get_category_with_translation ("String", _("String"));

	function_add_args  (cat, "char",       "f",    "number",
			    &help_char,       gnumeric_char);
	function_add_args  (cat, "clean",      "S",    "text",
			    &help_clean,      gnumeric_clean);
	function_add_args  (cat, "code",       "S",    "text",
			    &help_code,       gnumeric_code);
	function_add_nodes (cat, "concatenate",0,      "text1,text2",
			    &help_concatenate,gnumeric_concatenate);
	function_add_args  (cat, "dollar",     "f|f",  "num,decimals",
			    &help_dollar,     gnumeric_dollar);
	function_add_args  (cat, "exact",      "SS",   "text1,text2",
			    &help_exact,      gnumeric_exact);
	function_add_args  (cat, "find",       "SS|f", "text1,text2,num",
			    &help_find,       gnumeric_find);
	function_add_args  (cat, "fixed",      "f|fb", "num,decs,no_commas",
			    &help_fixed,      gnumeric_fixed);
	function_add_args  (cat, "left",       "S|f",  "text,num_chars",
			    &help_left,       gnumeric_left);
	function_add_args  (cat, "len",        "S",    "text",
			    &help_len,        gnumeric_len);
	function_add_args  (cat, "lower",      "S",    "text",
			    &help_lower,      gnumeric_lower);
	function_add_args  (cat, "proper",     "S",    "text",
			    &help_proper,     gnumeric_proper);
	function_add_args  (cat, "mid",        "Sff",  "text,pos,num",
			    &help_mid,        gnumeric_mid);
	function_add_args  (cat, "replace",    "SffS", "old,start,num,new",
			    &help_replace,    gnumeric_replace);
	function_add_args  (cat, "rept",       "Sf",   "text,num",
			    &help_rept,       gnumeric_rept);
	function_add_args  (cat, "right",      "S|f",  "text,num_chars",
			    &help_right,      gnumeric_right);
	function_add_args  (cat, "search",     "SS|f", "find,within[,start_num]",
			    &help_search,     gnumeric_search);
	function_add_args  (cat, "substitute", "SSS|f","text,old,new,num",
			    &help_substitute, gnumeric_substitute);
	function_add_args  (cat, "t",          "?",    "value",
			    &help_t,          gnumeric_t);
	function_add_args  (cat, "text",       "Ss",   "value,format_text",
			    &help_text,       gnumeric_text);
	function_add_args  (cat, "trim",       "S",    "text",
			    &help_trim,       gnumeric_trim);
	function_add_args  (cat, "upper",      "S",    "text",
			    &help_upper,      gnumeric_upper);
	function_add_args  (cat, "value",      "?",    "text",
			    &help_value,      gnumeric_value);
}

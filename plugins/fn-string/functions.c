/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * fn-string.c:  Built in string functions.
 *
 * Authors:
 *  Miguel de Icaza (miguel@gnu.org)
 *  Sean Atkinson (sca20@cam.ac.uk)
 *  Jukka-Pekka Iivonen (iivonen@iki.fi)
 *  Almer S. Tigelaar (almer@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <func.h>

#include <parse-util.h>
#include <cell.h>
#include <format.h>
#include <str.h>
#include <sheet.h>
#include <value.h>
#include <expr.h>
#include <number-match.h>
#include <mathfunc.h>
#include <rangefunc-strings.h>
#include <collect.h>
#include <regutf8.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>

#include <limits.h>
#include <string.h>

#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"

GNUMERIC_MODULE_PLUGIN_INFO_DECL;

/***************************************************************************/

static GIConv CHAR_iconv;

static const char *help_char = {
	N_("@FUNCTION=CHAR\n"
	   "@SYNTAX=CHAR(x)\n"

	   "@DESCRIPTION="
	   "CHAR returns the ASCII character represented by the number @x.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CHAR(65) equals A.\n"
	   "\n"
	   "@SEEALSO=CODE")
};

static Value *
gnumeric_char (FunctionEvalInfo *ei, Value **argv)
{
	int c = value_get_as_int (argv[0]);

	if (c > 0 && c <= 127) {
		char result[2];
		result[0] = c;
		result[1] = 0;
		return value_new_string (result);
	} else if (c >= 128 && c <= 255) {
		char c2 = c;
		char *str = g_convert_with_iconv (&c2, 1, CHAR_iconv,
						  NULL, NULL, NULL);
		if (str) {
			int len = g_utf8_strlen (str, -1);
			if (len == 1)
				return value_new_string_nocopy (str);
			g_warning ("iconv for CHAR(%d) produced a string of length %d",
				   c, len);
		} else
			g_warning ("iconv failed for CHAR(%d)", c);
	}

	return value_new_error (ei->pos, gnumeric_err_VALUE);
}

/***************************************************************************/

static GIConv CODE_iconv;

static const char *help_code = {
	N_("@FUNCTION=CODE\n"
	   "@SYNTAX=CODE(char)\n"

	   "@DESCRIPTION="
	   "CODE returns the ASCII number for the character @char.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CODE(\"A\") equals 65.\n"
	   "\n"
	   "@SEEALSO=CHAR")
};

static Value *
gnumeric_code (FunctionEvalInfo *ei, Value **argv)
{
	const char *s = value_peek_string (argv[0]);
	const unsigned char *us = (const unsigned char *)s;
	gsize written, clen;
	char *str;
	Value *res;

	if (*us == 0)
		value_new_error (ei->pos, gnumeric_err_VALUE);

	if (*us <= 127)
		return value_new_int (*us);

	clen = g_utf8_next_char (s) - s;
	str = g_convert_with_iconv (s, clen, CODE_iconv,
				    NULL, &written, NULL);
	if (written)
		res = value_new_int ((unsigned char)*str);
	else {
		g_warning ("iconv failed for CODE(U%x)", g_utf8_get_char (s));
		res = value_new_error (ei->pos, gnumeric_err_VALUE);
	}
	g_free (str);

	return res;
}

/***************************************************************************/

static const char *help_exact = {
	N_("@FUNCTION=EXACT\n"
	   "@SYNTAX=EXACT(string1, string2)\n"

	   "@DESCRIPTION="
	   "EXACT returns true if @string1 is exactly equal to @string2 "
	   "(this routine is case sensitive).\n\n"
           "* This function is Excel compatible.\n"
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
	return value_new_bool (g_utf8_collate (value_peek_string (argv[0]),
					       value_peek_string (argv[1])) == 0);
}

/***************************************************************************/

static const char *help_len = {
	N_("@FUNCTION=LEN\n"
	   "@SYNTAX=LEN(string)\n"

	   "@DESCRIPTION="
	   "LEN returns the length in characters of the string @string.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "len(\"Helsinki\") equals 8.\n"
	   "\n"
	   "@SEEALSO=CHAR, CODE")
};

static Value *
gnumeric_len (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_int (g_utf8_strlen (value_peek_string (argv[0]), -1));
}

/***************************************************************************/

static const char *help_left = {
	N_("@FUNCTION=LEFT\n"
	   "@SYNTAX=LEFT(text[,num_chars])\n"

	   "@DESCRIPTION="
	   "LEFT returns the leftmost @num_chars characters or the left "
	   "character if @num_chars is not specified.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LEFT(\"Directory\",3) equals \"Dir\".\n"
	   "\n"
	   "@SEEALSO=MID, RIGHT")
};

static Value *
gnumeric_left (FunctionEvalInfo *ei, Value **argv)
{
	char const *peek;
	int         count, newlen;

	count = argv[1] ? value_get_as_int (argv[1]) : 1;

	if (count < 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	peek = value_peek_string (argv[0]);
	if (count >= g_utf8_strlen (peek, -1))
		return value_new_string (peek);

	newlen = g_utf8_offset_to_pointer (peek, count) - peek;
	return value_new_string_nocopy (g_strndup (peek, newlen));
}

/***************************************************************************/

static const char *help_lower = {
	N_("@FUNCTION=LOWER\n"
	   "@SYNTAX=LOWER(text)\n"

	   "@DESCRIPTION="
	   "LOWER returns a lower-case version of the string in @text.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LOWER(\"J. F. Kennedy\") equals \"j. f. kennedy\".\n"
	   "\n"
	   "@SEEALSO=UPPER")
};

static Value *
gnumeric_lower (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_string_nocopy (g_utf8_strdown (value_peek_string (argv[0]), -1));
}

/***************************************************************************/

static const char *help_mid = {
	N_("@FUNCTION=MID\n"
	   "@SYNTAX=MID(string, position, length)\n"

	   "@DESCRIPTION="
	   "MID returns a substring from @string starting at @position for "
	   "@length characters.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "MID(\"testing\",2,3) equals \"est\".\n"
	   "\n"
	   "@SEEALSO=LEFT, RIGHT")
};

static Value *
gnumeric_mid (FunctionEvalInfo *ei, Value **argv)
{
	char       *upos;
	char const *source;
	int         pos, len, ulen, slen;

	pos = value_get_as_int (argv[1]);
	len = value_get_as_int (argv[2]);

	if (len < 0 || pos <= 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	source = value_peek_string (argv[0]);
	slen   = g_utf8_strlen (source, -1);

	if (pos > slen)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	pos--;  /* Make pos zero-based.  */

	len  = MIN (len, slen - pos);
	upos = g_utf8_offset_to_pointer (source, pos);
	ulen = g_utf8_offset_to_pointer (upos, len) - upos;

	return value_new_string_nocopy (g_strndup (upos, ulen));
}

/***************************************************************************/

static const char *help_right = {
	N_("@FUNCTION=RIGHT\n"
	   "@SYNTAX=RIGHT(text[,num_chars])\n"

	   "@DESCRIPTION="
	   "RIGHT returns the rightmost @num_chars characters or the right "
	   "character if @num_chars is not specified.\n\n"
           "* This function is Excel compatible.\n"
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
	int count, slen;
	char const *os;
	char *s;

	count = argv[1] ? value_get_as_int (argv[1]) : 1;

	if (count < 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	os   = value_peek_string (argv[0]);
	slen = g_utf8_strlen (os, -1);

	if (count < slen)
		s = g_strdup (g_utf8_offset_to_pointer (os, slen - count));
	else {
		/* We could just duplicate the arg, but that would not ensure
		   that the result was a string.  */
		s = g_strdup (os);
	}

	return value_new_string_nocopy (s);
}

/***************************************************************************/

static const char *help_upper = {
	N_("@FUNCTION=UPPER\n"
	   "@SYNTAX=UPPER(text)\n"

	   "@DESCRIPTION="
	   "UPPER returns a upper-case version of the string in @text.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "UPPER(\"canceled\") equals \"CANCELED\".\n"
	   "\n"
	   "@SEEALSO=LOWER")
};

static Value *
gnumeric_upper (FunctionEvalInfo *ei, Value **argv)
{
	return value_new_string_nocopy (g_utf8_strup (value_peek_string (argv[0]), -1));
}

/***************************************************************************/

static const char *help_concatenate = {
	N_("@FUNCTION=CONCATENATE\n"
	   "@SYNTAX=CONCATENATE(string1[,string2...])\n"
	   "@DESCRIPTION="
	   "CONCATENATE returns up appended strings.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CONCATENATE(\"aa\",\"bb\") equals \"aabb\".\n"
	   "\n"
	   "@SEEALSO=LEFT, MID, RIGHT")
};

static Value *
gnumeric_concatenate (FunctionEvalInfo *ei, GnmExprList *nodes)
{
	return string_range_function (nodes, ei,
				      range_concatenate,
				      COLLECT_IGNORE_BLANKS,
				      gnumeric_err_VALUE);
}

/***************************************************************************/

static const char *help_rept = {
	N_("@FUNCTION=REPT\n"
	   "@SYNTAX=REPT(string,num)\n"
	   "@DESCRIPTION="
	   "REPT returns @num repetitions of @string.\n\n"
           "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "REPT(\".\",3) equals \"...\".\n"
	   "\n"
	   "@SEEALSO=CONCATENATE")
};

static Value *
gnumeric_rept (FunctionEvalInfo *ei, Value **argv)
{
	GString    *res;
	const char *source;
	int         num;
	int         len;
	int         i;

	num = value_get_as_int (argv[1]);
	if (num < 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	source = value_peek_string (argv[0]);
	len    = strlen (source);

	/* Fast special case.  =REPT ("",2^30) should not take long.  */
	if (len == 0 || num == 0)
		return value_new_string ("");

	/* Check if the length would overflow.  */
	if (num >= INT_MAX / len)
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	res = g_string_sized_new (len * num);
	for (i = 0; i < num; i++)
		g_string_append (res, source);

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/

static const char *help_clean = {
	N_("@FUNCTION=CLEAN\n"
	   "@SYNTAX=CLEAN(string)\n"
	   "@DESCRIPTION="
	   "CLEAN removes any non-printable characters from @string.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CLEAN(\"one\"\\&char(7)) equals \"one\".\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_clean (FunctionEvalInfo *ei, Value **argv)
{
	const char *s = value_peek_string (argv[0]);
	GString *res = g_string_sized_new (strlen (s));

	while (*s) {
		gunichar uc = g_utf8_get_char (s);

		if (g_unichar_isprint (uc))
			g_string_append_unichar (res, uc);

		s = g_utf8_next_char (s);
	}

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/

static const char *help_find = {
	N_("@FUNCTION=FIND\n"
	   "@SYNTAX=FIND(string1,string2[,start])\n"
	   "@DESCRIPTION="
	   "FIND returns position of @string1 in @string2 (case-sensitive), "
	   "searching only from character @start onwards (assuming 1 if "
	   "omitted).\n\n"
           "* This function is Excel compatible.\n"
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

	/*
	 * FIXME: My gut feeling is that we should return arguments
	 * invalid when g_utf8_strlen (needle, -1) is 0 (i.e. needle is "")
	 * Currently we return "1" which seems nonsensical.
	 */
	needle   = value_peek_string (argv[0]);
	haystack = value_peek_string (argv[1]);
	count    = argv[2] ? value_get_as_int (argv[2]) : 1;

	haystacksize = g_utf8_strlen (haystack, -1);

	/*
	 * NOTE: It seems that the implementation of g_strstr will
	 * even work for UTF-8 string, even though there is no special
	 * UTF-8 version for it, this is why we use "strlen (haystart)"
	 * and not g_utf8_strlen below
	 */
	if (count <= 0 || count > haystacksize) {
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	} else {
		const char *haystart = g_utf8_offset_to_pointer (haystack,
								 count - 1);
		const char *p        = g_strstr_len (haystart,
						     strlen (haystart), needle);

		if (p)
			/* One-based */
			return value_new_int
				(g_utf8_pointer_to_offset (haystack, p) + 1);
		else
			/* Really?  */
			return value_new_error (ei->pos, gnumeric_err_VALUE);
	}
}

/***************************************************************************/

static const char *help_fixed = {
	N_("@FUNCTION=FIXED\n"
	   "@SYNTAX=FIXED(num,[decimals, no_commas])\n"
	   "@DESCRIPTION="
	   "FIXED returns @num as a formatted string with @decimals numbers "
	   "after the decimal point, omitting commas if requested by "
	   "@no_commas.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "FIXED(1234.567,2) equals \"1,234.57\".\n"
	   "\n"
	   "@SEEALSO=")
};

static Value *
gnumeric_fixed (FunctionEvalInfo *ei, Value **argv)
{
	int decimals;
	gnm_float num;
	gboolean commas = TRUE;
	format_info_t fmt;
	GString *str;

	num = value_get_as_float (argv[0]);
	decimals = argv[1] ? value_get_as_int (argv[1]) : 2;
	if (argv[2] != NULL) {
		gboolean err;
		commas = !value_get_as_bool (argv[2], &err);
		if (err)
			return value_new_error (ei->pos, gnumeric_err_VALUE);
	}

	if (decimals >= 127) /* else buffer overflow */
		return value_new_error (ei->pos, gnumeric_err_VALUE);

	if (decimals <= 0) {
		/* no decimal point : just round and pad 0's */
		gnm_float mult = gpow10 (decimals);
		num = (gnumeric_fake_round (num * mult) / mult);
		fmt.right_req = fmt.right_allowed = 0;
	} else /* decimal point format */
		fmt.right_req = fmt.right_allowed = decimals;

	fmt.right_optional	   = 0;
	fmt.right_spaces	   = 0;
	fmt.left_spaces		   = 0;
	fmt.left_req		   = 0;
	fmt.decimal_separator_seen = (decimals > 0);
	fmt.supress_minus	   = FALSE;
	fmt.group_thousands	   = commas;
	fmt.has_fraction	   = FALSE;
	fmt.negative		   = num < 0.;
	if (fmt.negative)
		num = -num;

	str = g_string_new ("");
	render_number (str, num, &fmt);
	if (str->len == 0)
		g_string_append_c (str, '0');
	return value_new_string_nocopy (g_string_free (str, FALSE));
}

/***************************************************************************/

static const char *help_proper = {
	N_("@FUNCTION=PROPER\n"
	   "@SYNTAX=PROPER(string)\n"

	   "@DESCRIPTION="
	   "PROPER returns @string with initial of each word capitalised.\n\n"
           "* This function is Excel compatible.\n"
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
	char const *p;
	GString    *res    = g_string_new ("");
	gboolean   inword = FALSE;

	p = value_peek_string (argv[0]);
	while (*p) {
		gunichar uc = g_utf8_get_char (p);

		if (g_unichar_isalpha (uc)) {
			if (inword) {
				g_string_append_unichar
					(res, g_unichar_tolower (uc));
			} else {
				g_string_append_unichar
					(res, g_unichar_toupper (uc));
				inword = TRUE;
			}
		} else {
			g_string_append_unichar (res, uc);
			inword = FALSE;
		}

		p = g_utf8_next_char (p);
	}

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/

static const char *help_replace = {
	N_("@FUNCTION=REPLACE\n"
	   "@SYNTAX=REPLACE(old,start,num,new)\n"
	   "@DESCRIPTION="
	   "REPLACE returns @old with @new replacing @num characters from "
	   "@start.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "REPLACE(\"testing\",2,3,\"*****\") equals \"t*****ing\".\n"
	   "\n"
	   "@SEEALSO=MID, SEARCH, SUBSTITUTE, TRIM")
};

static Value *
gnumeric_replace (FunctionEvalInfo *ei, Value **argv)
{
	GString *res;
	gint start, num, oldlen;
	char const *old;
	char const *new;

	start  = value_get_as_int (argv[1]);
	num    = value_get_as_int (argv[2]);
	old    = value_peek_string (argv[0]);
	oldlen = g_utf8_strlen (old, -1);

	if (start <= 0 || num <= 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	if (start > oldlen)
		return value_new_error (ei->pos, _ ("Arguments out of range"));

	start--;  /* Make this zero-based.  */

	if (start + num > oldlen)
		num = oldlen - start;

	new = value_peek_string (argv[3]);

	res = g_string_new (old);
	g_string_erase (res, start, num);
	g_string_insert (res, start, new);

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/
/* Note: help_t is a reserved symbol.  */

static const char *help_t_ = {
	N_("@FUNCTION=T\n"
	   "@SYNTAX=T(value)\n"
	   "@DESCRIPTION="
	   "T returns @value if and only if it is text, otherwise a blank "
	   "string.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "T(\"text\") equals \"text\".\n"
	   "T(64) returns an empty cell.\n"
	   "\n"
	   "@SEEALSO=CELL, N, VALUE")
};

/* Note: gnumeric_t is a reserved symbol.  */

static Value *
gnumeric_t_ (FunctionEvalInfo *ei, Value **argv)
{
	if (argv[0]->type == VALUE_STRING)
		return value_duplicate (argv[0]);
	else
		return value_new_empty ();
}

/***************************************************************************/

static const char *help_text = {
	N_("@FUNCTION=TEXT\n"
	   "@SYNTAX=TEXT(value,format_text)\n"
	   "@DESCRIPTION="
	   "TEXT returns @value as a string with the specified format.\n\n"
           "* This function is Excel compatible.\n"
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
	StyleFormat *format = style_format_new_XL (value_peek_string (args[1]),
						   TRUE);
	Value       *res, *tmp = NULL;
	Value const *arg  = args[0];
	gboolean    ok = FALSE;

	if (arg->type == VALUE_STRING) {
		Value *match = format_match (value_peek_string (arg), NULL);
		ok = (match != NULL);
		if (ok)
			tmp = match;
	} else
		ok = VALUE_IS_NUMBER (arg);

	if (ok) {
		char *str = format_value (format,
					  (tmp != NULL) ? tmp : arg,
					  NULL, -1);
		res = value_new_string_nocopy (str);
	} else
		res = value_new_error (ei->pos, _("Type mismatch"));

	if (tmp != NULL)
		value_release (tmp);
	style_format_unref (format);
	return res;
}


/***************************************************************************/

static const char *help_trim = {
	N_("@FUNCTION=TRIM\n"
	   "@SYNTAX=TRIM(text)\n"
	   "@DESCRIPTION="
	   "TRIM returns @text with only single spaces between words.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TRIM(\"  a bbb  cc\") equals \"a bbb cc\".\n"
	   "\n"
	   "@SEEALSO=CLEAN, MID, REPLACE, SUBSTITUTE")
};

static Value *
gnumeric_trim (FunctionEvalInfo *ei, Value **argv)
{
	const char *s;
	GString  *res   = g_string_new ("");
	gboolean  space = TRUE;
	int       len;

	s = value_peek_string (argv[0]);
	while (*s) {
		gunichar uc = g_utf8_get_char (s);

		/*
		 * FIXME: This takes care of tabs and the likes too
		 * is that the desired behaviour?
		 */
		if (g_unichar_isspace (uc)) {
			if (!space) {
				g_string_append_unichar (res, uc);
				space = TRUE;
			}
		} else {
			g_string_append_unichar (res, uc);
			space = FALSE;
		}

		s = g_utf8_next_char (s);
	}

	g_warning ("FIXME: this looks bogus.");
	len = g_utf8_strlen (res->str, -1);
	if (space && len > 0)
		g_string_truncate (res, len - 1);

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/

static const char *help_value = {
	N_("@FUNCTION=VALUE\n"
	   "@SYNTAX=VALUE(text)\n"
	   "@DESCRIPTION="
	   "VALUE returns numeric value of @text.\n\n"
           "* This function is Excel compatible.\n"
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
		const char *p, *arg = value_peek_string (argv[0]);

		/* Skip leading spaces */
		for (p = arg; *p && g_unichar_isspace (g_utf8_get_char (p));
		     p = g_utf8_next_char (p))
			;
		v = format_match_number (p, NULL);

		if (v != NULL)
			return v;
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	}
	}
}

/***************************************************************************/

static const char *help_substitute = {
	N_("@FUNCTION=SUBSTITUTE\n"
	   "@SYNTAX=SUBSTITUTE(text, old, new [,num])\n"
	   "@DESCRIPTION="
	   "SUBSTITUTE replaces @old with @new in @text.  Substitutions "
	   "are only applied to instance @num of @old in @text, otherwise "
	   "every one is changed.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "SUBSTITUTE(\"testing\",\"test\",\"wait\") equals \"waiting\".\n"
	   "\n"
	   "@SEEALSO=REPLACE, TRIM")
};

static Value *
gnumeric_substitute (FunctionEvalInfo *ei, Value **argv)
{
	const char *p;
	int oldlen, newlen, len, inst;
	GString *s;

	const char *text = value_peek_string (argv[0]);
	const char *old  = value_peek_string (argv[1]);
	const char *new  = value_peek_string (argv[2]);
	int num = argv[3] ? value_get_as_int (argv[3]) : 0;

	oldlen = strlen (old);
	newlen = strlen (new);
	len = strlen (text);
	s = g_string_sized_new (len);

	p = text;
	inst = 0;
	while (p - text < len) {
		const char *f = strstr (p, old);
		if (!f)
			break;

		g_string_append_len (s, p, f - p);
		p = f + oldlen;

		inst++;
		if (num == 0 || num == inst) {
			g_string_append_len (s, new, newlen);
			if (num == inst)
				break;
		} else
			g_string_append_len (s, old, oldlen);
	}
	g_string_append (s, p);

	return value_new_string_nocopy (g_string_free (s, FALSE));
}

/***************************************************************************/

static const char *help_dollar = {
	N_("@FUNCTION=DOLLAR\n"
	   "@SYNTAX=DOLLAR(num[,decimals])\n"
	   "@DESCRIPTION="
	   "DOLLAR returns @num formatted as currency.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DOLLAR(12345) equals \"$12,345.00\".\n"
	   "\n"
	   "@SEEALSO=FIXED, TEXT, VALUE")
};

static Value *
gnumeric_dollar (FunctionEvalInfo *ei, Value **argv)
{
	gboolean precedes, space_sep;
	char const *curr = format_get_currency (&precedes, &space_sep);
	char *format, *s;
	StyleFormat *sf;
	const char *base_format =
		"%s#,##0%s%s;(%s#,##0%s)%s;_(%s\"-\"??%s_);_(@_)";
        gnm_float number = value_get_as_float (argv[0]);
        int decimals = argv[1] ? value_get_as_int (argv[1]) : 2;
	gnm_float p10;
	Value *v;
	char dotdecimals[1000];

	if (decimals > 0) {
		/* ".0000" */
		decimals = MIN (decimals, (int)sizeof (dotdecimals) - 2); /* FIXME? */
		dotdecimals[0] = '.';
		memset (&dotdecimals[1], '0', decimals);
		dotdecimals[decimals + 1] = 0;
	} else {
		dotdecimals[0] = 0;
	}

	if (precedes) {
		char *pre = g_strconcat ("\"", curr, "\"",
					 (space_sep ? " " : ""),
					 NULL);

		format = g_strdup_printf (base_format,
					  pre, dotdecimals, "",
					  pre, dotdecimals, "",
					  pre, "");
		g_free (pre);
	} else {
		char *post = g_strconcat ((space_sep ? " " : ""),
					  "\"", curr, "\"",
					  NULL);

		format = g_strdup_printf (base_format,
					  "", dotdecimals, post,
					  "", dotdecimals, post,
					  "", post);
		g_free (post);
	}
	sf = style_format_new_XL (format, FALSE);
	g_free (format);
	g_return_val_if_fail (sf != NULL,
			      value_new_error (ei->pos, gnumeric_err_NA));

	/* Since decimals can be negative, round the number.  */
	p10 = gpow10 (decimals);
	number = gnumeric_fake_round (number * p10) / p10;
	v = value_new_float (number);
	s = format_value (sf, v, NULL, -1);
	value_release (v);

	style_format_unref (sf);

	return value_new_string_nocopy (s);
}

/***************************************************************************/

static const char *help_search = {
	N_("@FUNCTION=SEARCH\n"
	   "@SYNTAX=SEARCH(text,within[,start_num])\n"
	   "@DESCRIPTION="
	   "SEARCH returns the location of a character or text string within "
	   "another string.  @text is the string or character to be searched. "
	   "@within is the string in which you want to search.  @start_num "
	   "is the start position of the search in @within.  If @start_num "
	   "is omitted, it is assumed to be one.  The search is not case "
	   "sensitive.\n"
	   "\n"
	   "@text can contain wildcard characters (*) and question marks (?) "
	   "to control the search.  A question mark matches with any "
	   "character and wildcard matches with any string including empty "
	   "string.  If you want the actual wildcard or question mark to "
	   "be searched, use tilde (~) before the character.\n"
	   "\n"
	   "* If @text is not found, SEARCH returns #VALUE! error.\n"
	   "* If @start_num is less than one or it is greater than the length "
	   "of @within, SEARCH returns #VALUE! error.\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "SEARCH(\"c\",\"Cancel\") equals 1.\n"
	   "SEARCH(\"c\",\"Cancel\",2) equals 4.\n"
	   "\n"
	   "@SEEALSO=FIND")
};

static Value *
gnumeric_search (FunctionEvalInfo *ei, Value **argv)
{
	const char *needle = value_peek_string (argv[0]);
	const char *haystack = value_peek_string (argv[1]);
	int start = argv[2] ? value_get_as_int (argv[2]) : 1;
	const char *hay2;
	gnumeric_regex_t r;
	regmatch_t rm;
	Value *res = NULL;
	int i;

	start--;
	if (start < 0)
		return value_new_error (ei->pos, gnumeric_err_VALUE);
	for (i = start, hay2 = haystack; i > 0; i--) {
		if (*hay2 == 0)
			return value_new_error (ei->pos, gnumeric_err_VALUE);
		hay2 = g_utf8_next_char (hay2);
	}

	if (gnumeric_regcomp_XL (&r, needle, REG_ICASE) == REG_OK) {
		switch (gnumeric_regexec (&r, hay2, 1, &rm, 0)) {
		case REG_NOMATCH: break;
		case REG_OK:
			res = value_new_int (1 + start + rm.rm_so);
			break;
		default:
			g_warning ("Unexpected regexec result");
		}
		gnumeric_regfree (&r);
	} else {
		g_warning ("Unexpected regcomp result");
	}

	if (res == NULL)
		res = value_new_error (ei->pos, gnumeric_err_VALUE);
	return res;
}

/***************************************************************************/

const GnmFuncDescriptor string_functions[] = {
        { "char",       "f",     N_("number"),                  &help_char,
	  gnumeric_char, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "clean",      "S",     N_("text"),                    &help_clean,
          gnumeric_clean, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "code",       "S",     N_("text"),                    &help_code,
	  gnumeric_code, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "concatenate", 0,      N_("text,text,"),            &help_concatenate,
	  NULL, gnumeric_concatenate, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dollar",     "f|f",   N_("num,decimals"),            &help_dollar,
	  gnumeric_dollar, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "exact",      "SS",    N_("text1,text2"),             &help_exact,
	  gnumeric_exact, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "find",       "SS|f",  N_("text1,text2,num"),         &help_find,
	  gnumeric_find, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "fixed",      "f|fb",  N_("num,decs,no_commas"),      &help_fixed,
	  gnumeric_fixed, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "left",       "S|f",   N_("text,num_chars"),          &help_left,
	  gnumeric_left, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "len",        "S",     N_("text"),                    &help_len,
	  gnumeric_len, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "lower",      "S",     N_("text"),                    &help_lower,
	  gnumeric_lower, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "mid",        "Sff",   N_("text,pos,num"),            &help_mid,
	  gnumeric_mid, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "proper",     "S",     N_("text"),                    &help_proper,
	  gnumeric_proper, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "replace",    "SffS",  N_("old,start,num,new"),       &help_replace,
	  gnumeric_replace, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "rept",       "Sf",    N_("text,num"),                &help_rept,
	  gnumeric_rept, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "right",      "S|f",   N_("text,num_chars"),          &help_right,
	  gnumeric_right, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "search",     "SS|f",  N_("find,within,start_num"),   &help_search,
	  gnumeric_search, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "substitute", "SSS|f", N_("text,old,new,num"),       &help_substitute,
	  gnumeric_substitute, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "t",          "S",     N_("value"),                   &help_t_,
          gnumeric_t_, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "text",       "Ss",    N_("value,format_text"),       &help_text,
	  gnumeric_text, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "trim",       "S",     N_("text"),                    &help_trim,
	  gnumeric_trim, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "upper",      "S",     N_("text"),                    &help_upper,
	  gnumeric_upper, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "value",      "S",     N_("text"),                    &help_value,
	  gnumeric_value, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};


void
plugin_init (void)
{
	int codepage = gsf_msole_iconv_win_codepage ();
	CHAR_iconv = gsf_msole_iconv_open_for_import (codepage);
	CODE_iconv = gsf_msole_iconv_open_for_export ();
}

void
plugin_cleanup (void)
{
	gsf_iconv_close (CHAR_iconv);
	gsf_iconv_close (CODE_iconv);
}

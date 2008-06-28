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
#include <gnumeric.h>
#include <func.h>
#include <parse-util.h>
#include <cell.h>
#include <gnm-format.h>
#include <str.h>
#include <gutils.h>
#include <sheet.h>
#include <workbook.h>
#include <value.h>
#include <expr.h>
#include <number-match.h>
#include <mathfunc.h>
#include <rangefunc-strings.h>
#include <collect.h>
#include <goffice/utils/regutf8.h>
#include <goffice/utils/go-locale.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>
#include <gnm-i18n.h>
#include <goffice/app/go-plugin.h>
#include <goffice/utils/go-glib-extras.h>
#include <gnm-plugin.h>

#include <limits.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

static GIConv CHAR_iconv;

static GnmFuncHelp const help_char[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CHAR\n"
	   "@SYNTAX=CHAR(x)\n"

	   "@DESCRIPTION="
	   "CHAR returns the ASCII character represented by the number @x.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CHAR(65) equals A.\n"
	   "\n"
	   "@SEEALSO=CODE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_char (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float c = value_get_as_float (argv[0]);

	if (c >= 1 && c < 128) {
		char result[2];
		result[0] = (char)c;
		result[1] = 0;
		return value_new_string (result);
	} else if (c >= 128 && c < 256) {
		char c2 = (char)c;
		char *str = g_convert_with_iconv (&c2, 1, CHAR_iconv,
						  NULL, NULL, NULL);
		if (str) {
			int len = g_utf8_strlen (str, -1);
			if (len == 1)
				return value_new_string_nocopy (str);
			g_warning ("iconv for CHAR(%d) produced a string of length %d",
				   c2, len);
			g_free (str);
		} else
			g_warning ("iconv failed for CHAR(%d)", c2);
	}

	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_unichar[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=UNICHAR\n"
	   "@SYNTAX=UNICHAR(x)\n"

	   "@DESCRIPTION="
	   "UNICHAR returns the Unicode character represented by the number @x.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "UNICHAR(65) equals A.\n"
	   "UNICHAR(960) equals a small Greek pi.\n"
	   "\n"
	   "@SEEALSO=CHAR,UNICODE,CODE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_unichar (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float c = value_get_as_float (argv[0]);

	if (c >= 0 && c <= INT_MAX && g_unichar_validate ((gunichar)c)) {
		char utf8[8];
		int len = g_unichar_to_utf8 ((gunichar)c, utf8);
		utf8[len] = 0;
		return value_new_string (utf8);
	} else
		return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GIConv CODE_iconv;

static GnmFuncHelp const help_code[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CODE\n"
	   "@SYNTAX=CODE(char)\n"

	   "@DESCRIPTION="
	   "CODE returns the ASCII number for the character @char.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CODE(\"A\") equals 65.\n"
	   "\n"
	   "@SEEALSO=CHAR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_code (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *s = value_peek_string (argv[0]);
	const unsigned char *us = (const unsigned char *)s;
	gsize written, clen;
	char *str;
	GnmValue *res;

	if (*us == 0)
		return value_new_error_VALUE (ei->pos);

	if (*us <= 127)
		return value_new_int (*us);

	clen = g_utf8_next_char (s) - s;
	str = g_convert_with_iconv (s, clen, CODE_iconv,
				    NULL, &written, NULL);
	if (written)
		res = value_new_int ((unsigned char)*str);
	else {
		g_warning ("iconv failed for CODE(U%x)", g_utf8_get_char (s));
		res = value_new_error_VALUE (ei->pos);
	}
	g_free (str);

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_unicode[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=UNICODE\n"
	   "@SYNTAX=UNICODE(char)\n"

	   "@DESCRIPTION="
	   "UNICODE returns the Unicode number for the character @char.\n\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "UNICODE(\"A\") equals 65.\n"
	   "\n"
	   "@SEEALSO=UNICHAR,CODE,CHAR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_unicode (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *s = value_peek_string (argv[0]);

	if (*s == 0)
		return value_new_error_VALUE (ei->pos);
	else
		return value_new_int (g_utf8_get_char (s));
}

/***************************************************************************/

static GnmFuncHelp const help_exact[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=EXACT\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_exact (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (g_utf8_collate (value_peek_string (argv[0]),
					       value_peek_string (argv[1])) == 0);
}

/***************************************************************************/

static GnmFuncHelp const help_len[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LEN\n"
	   "@SYNTAX=LEN(string)\n"

	   "@DESCRIPTION="
	   "LEN returns the length in characters of the string @string.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LEN(\"Helsinki\") equals 8.\n"
	   "\n"
	   "@SEEALSO=CHAR, CODE, LENB")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_len (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_int (g_utf8_strlen (value_peek_string (argv[0]), -1));
}

/***************************************************************************/
static GnmFuncHelp const help_lenb[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LENB\n"
	   "@SYNTAX=LENB(string)\n"

	   "@DESCRIPTION="
	   "LENB returns the length in bytes of the string @string.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LENB(\"Helsinki\") equals 8.\n"
	   "\n"
	   "@SEEALSO=CHAR, CODE, LEN")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_lenb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_int (strlen (value_peek_string (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_left[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LEFT\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_left (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	const guchar *peek = (const guchar *)value_peek_string (argv[0]);
	gnm_float count = argv[1] ? value_get_as_float (argv[1]) : 1.0;
	int icount, newlen;

	if (count < 0)
		return value_new_error_VALUE (ei->pos);
	icount = (int)MIN ((gnm_float)INT_MAX, count);

	for (newlen = 0; peek[newlen] != 0 && icount > 0; icount--)
		newlen += g_utf8_skip[peek[newlen]];

	return value_new_string_nocopy (g_strndup (peek, newlen));
}

/***************************************************************************/

static GnmFuncHelp const help_lower[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=LOWER\n"
	   "@SYNTAX=LOWER(text)\n"

	   "@DESCRIPTION="
	   "LOWER returns a lower-case version of the string in @text.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "LOWER(\"J. F. Kennedy\") equals \"j. f. kennedy\".\n"
	   "\n"
	   "@SEEALSO=UPPER")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_lower (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_string_nocopy (g_utf8_strdown (value_peek_string (argv[0]), -1));
}

/***************************************************************************/

static GnmFuncHelp const help_mid[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=MID\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_mid (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *source = value_peek_string (argv[0]);
	gnm_float pos = value_get_as_float (argv[1]);
	gnm_float len = value_get_as_float (argv[2]);
	size_t slen = g_utf8_strlen (source, -1);
	char const *upos;
	size_t ilen, ipos, ulen;

	if (len < 0 || pos < 1)
		return value_new_error_VALUE (ei->pos);
	if (pos >= slen + 1)
		return value_new_string ("");

	/* Make ipos zero-based.  */
	ipos = (size_t)(pos - 1);
	ilen  = (size_t)MIN (len, (gnm_float)(slen - ipos));

	upos = g_utf8_offset_to_pointer (source, ipos);
	ulen = g_utf8_offset_to_pointer (upos, ilen) - upos;

	return value_new_string_nocopy (g_strndup (upos, ulen));
}

/***************************************************************************/

static GnmFuncHelp const help_right[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=RIGHT\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_right (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *os = value_peek_string (argv[0]);
	gnm_float count = argv[1] ? value_get_as_float (argv[1]) : 1.0;
	int icount, slen;

	if (count < 0)
		return value_new_error_VALUE (ei->pos);
	icount = (int)MIN ((gnm_float)INT_MAX, count);

	slen = g_utf8_strlen (os, -1);

	if (icount < slen)
		return value_new_string (g_utf8_offset_to_pointer (os, slen - icount));
	else
		/* We could just duplicate the arg, but that would not ensure
		   that the result was a string.  */
		return value_new_string (os);
}

/***************************************************************************/

static GnmFuncHelp const help_upper[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=UPPER\n"
	   "@SYNTAX=UPPER(text)\n"

	   "@DESCRIPTION="
	   "UPPER returns a upper-case version of the string in @text.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "UPPER(\"cancelled\") equals \"CANCELLED\".\n"
	   "\n"
	   "@SEEALSO=LOWER")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_upper (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_string_nocopy (g_utf8_strup (value_peek_string (argv[0]), -1));
}

/***************************************************************************/

static GnmFuncHelp const help_concatenate[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CONCATENATE\n"
	   "@SYNTAX=CONCATENATE(string1[,string2...])\n"
	   "@DESCRIPTION="
	   "CONCATENATE returns the string obtained by concatenation of "
	   "the given strings.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CONCATENATE(\"aa\",\"bb\") equals \"aabb\".\n"
	   "\n"
	   "@SEEALSO=LEFT, MID, RIGHT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_concatenate (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return string_range_function (argc, argv, ei,
				      range_concatenate,
				      COLLECT_IGNORE_BLANKS,
				      GNM_ERROR_VALUE);
}

/***************************************************************************/

static GnmFuncHelp const help_rept[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=REPT\n"
	   "@SYNTAX=REPT(string,num)\n"
	   "@DESCRIPTION="
	   "REPT returns @num repetitions of @string.\n\n"
           "* This function is Excel compatible.\n "
	   "\n"
	   "@EXAMPLES=\n"
	   "REPT(\".\",3) equals \"...\".\n"
	   "\n"
	   "@SEEALSO=CONCATENATE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_rept (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *source = value_peek_string (argv[0]);
	gnm_float num = value_get_as_float (argv[1]);
	size_t len = strlen (source);
	char *res;
	size_t i, inum;

	if (num < 0)
		return value_new_error_VALUE (ei->pos);

	/* Fast special case.  =REPT ("",2^30) should not take long.  */
	if (len == 0 || num < 1)
		return value_new_string ("");

	/* Check if the length would overflow.  */
	if (num >= INT_MAX / len)
		return value_new_error_VALUE (ei->pos);

	inum = (size_t)num;
	res = g_try_malloc (len * inum + 1);
	if (!res)
		return value_new_error_VALUE (ei->pos);

	for (i = 0; inum-- > 0; i += len)
		memcpy (res + i, source, len);
	res[i] = 0;

	return value_new_string_nocopy (res);
}

/***************************************************************************/

static GnmFuncHelp const help_clean[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=CLEAN\n"
	   "@SYNTAX=CLEAN(string)\n"
	   "@DESCRIPTION="
	   "CLEAN removes any non-printable characters from @string.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CLEAN(\"one\"\\&char(7)) equals \"one\".\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_clean (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *s = value_peek_string (argv[0]);
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

static GnmFuncHelp const help_find[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=FIND\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_find (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *needle   = value_peek_string (argv[0]);
	char const *haystack = value_peek_string (argv[1]);
	gnm_float count      = argv[2] ? value_get_as_float (argv[2]) : 1.0;
	size_t haystacksize = g_utf8_strlen (haystack, -1);
	size_t icount;
	char const *p;

	if (count < 1 || count >= haystacksize + 1)
		return value_new_error_VALUE (ei->pos);
	icount = (size_t)count;

	haystack = g_utf8_offset_to_pointer (haystack, icount - 1);

	p = g_strstr_len (haystack, strlen (haystack), needle);
	if (p)
		return value_new_int
			(g_utf8_pointer_to_offset (haystack, p) + icount);
	else
		return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_fixed[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=FIXED\n"
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
	   "@SEEALSO=TEXT, VALUE, DOLLAR")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_fixed (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float num = value_get_as_float (argv[0]);
	gnm_float decimals = argv[1] ? value_get_as_float (argv[1]) : 2.0;
	gboolean commas = argv[2] ? value_get_as_checked_bool (argv[2]) : TRUE;
	GString *format;
	GOFormat *fmt;
	GnmValue *v;
	char *res;

	decimals = gnm_fake_trunc (decimals);
	if (decimals >= 128)
		return value_new_error_VALUE (ei->pos);

	if (decimals < 0) {
		/* no decimal point : just round and pad 0's */
		gnm_float mult = gnm_pow10 (decimals);
		if (mult == 0)
			num = 0;  /* Underflow */
		else
			num = gnm_fake_round (num * mult) / mult;
	}
	v = value_new_float (num);

	format = g_string_sized_new (200);
	if (commas)
		g_string_append (format, "#,##0");
	else
		g_string_append_c (format, '0');
	if (decimals > 0) {
		g_string_append_c (format, '.');
		go_string_append_c_n (format, '0', decimals);
	}

	fmt = go_format_new_from_XL (format->str);
	g_string_free (format, TRUE);

	res = format_value (fmt, v, NULL, -1, workbook_date_conv (ei->pos->sheet->workbook));

	go_format_unref (fmt);
	value_release (v);

	return value_new_string_nocopy (res);
}

/***************************************************************************/

static GnmFuncHelp const help_proper[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=PROPER\n"
	   "@SYNTAX=PROPER(string)\n"

	   "@DESCRIPTION="
	   "PROPER returns @string with initial of each word capitalised.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "PROPER(\"j. f. kennedy\") equals \"J. F. Kennedy\".\n"
	   "\n"
	   "@SEEALSO=LOWER, UPPER")
	},
	{ GNM_FUNC_HELP_END }
};

/*
 * proper could be a LOT nicer
 * (e.g. "US Of A" -> "US of A", "Cent'S Worth" -> "Cent's Worth")
 * but this is how Excel does it
 */
static GnmValue *
gnumeric_proper (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *p;
	GString    *res    = g_string_new (NULL);
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

static GnmFuncHelp const help_replace[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=REPLACE\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_replace (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *old = value_peek_string (argv[0]);
	gnm_float start = value_get_as_float (argv[1]);
	gnm_float num = value_get_as_float (argv[2]);
	char const *new = value_peek_string (argv[3]);
	size_t istart, inum, oldlen, precutlen, postcutlen, newlen;
	char const *p, *q;
	char *res;

	if (start < 1 || num < 0)
		return value_new_error_VALUE (ei->pos);

	oldlen = g_utf8_strlen (old, -1);
	/* Make istart zero-based.  */
	istart = (int)MIN ((gnm_float)oldlen, start - 1);
	inum = (int)MIN((gnm_float)(oldlen - istart), num);

	/* |<----precut----><cut><---postcut--->| */
	/*  ^old            ^p   ^q               */

	p = g_utf8_offset_to_pointer (old, istart);
	q = g_utf8_offset_to_pointer (p, inum);

	precutlen = p - old;
	postcutlen = strlen (q);
	newlen = strlen (new);

	res = g_malloc (precutlen + newlen + postcutlen + 1);
	memcpy (res, old, precutlen);
	memcpy (res + precutlen, new, newlen);
	memcpy (res + precutlen + newlen, q, postcutlen + 1);
	return value_new_string_nocopy (res);
}

/***************************************************************************/
/* Note: help_t is a reserved symbol.  */

static GnmFuncHelp const help_t_[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=T\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

/* Note: gnumeric_t is a reserved symbol.  */

static GnmValue *
gnumeric_t_ (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	if (VALUE_IS_STRING (argv[0]))
		return value_dup (argv[0]);
	else
		return value_new_empty ();
}

/***************************************************************************/

static GnmFuncHelp const help_text[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TEXT\n"
	   "@SYNTAX=TEXT(value,format_text)\n"
	   "@DESCRIPTION="
	   "TEXT returns @value as a string with the specified format.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TEXT(3.223,\"$0.00\") equals \"$3.22\".\n"
	   "TEXT(date(1999,4,15),\"mmmm, dd, yy\") equals \"April, 15, 99\".\n"
	   "\n"
	   "@SEEALSO=DOLLAR, FIXED, VALUE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_text (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue *res, *match = NULL;
	GnmValue const *v  = argv[0];
	GOFormat *fmt;
	GODateConventions const *conv =
		workbook_date_conv (ei->pos->sheet->workbook);
	GString *str;
	GOFormatNumberError err;
	char *lfmt;

	/* Why do we have to do these here?  */
	if (VALUE_IS_STRING (v)) {
		match = format_match (value_peek_string (v), NULL, conv);
		if (match != NULL)
			v = match;
	} else if (VALUE_IS_EMPTY (v))
		v = value_zero;

	lfmt = go_format_str_delocalize (value_peek_string (argv[1]));
	fmt = go_format_new_from_XL (lfmt);
	g_free (lfmt);
	str = g_string_sized_new (80);
	err = format_value_gstring (str, fmt, v, NULL, -1, conv);
	if (err) {
		g_string_free (str, TRUE);
		res = value_new_error_VALUE (ei->pos);
	} else {
		res = value_new_string_nocopy (g_string_free (str, FALSE));
	}
	go_format_unref (fmt);

	if (match != NULL)
		value_release (match);

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_trim[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=TRIM\n"
	   "@SYNTAX=TRIM(text)\n"
	   "@DESCRIPTION="
	   "TRIM returns @text with only single spaces between words.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "TRIM(\"  a bbb  cc\") equals \"a bbb cc\".\n"
	   "\n"
	   "@SEEALSO=CLEAN, MID, REPLACE, SUBSTITUTE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_trim (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *s;
	GString  *res   = g_string_new (NULL);
	gboolean  space = TRUE;
	size_t    last_len = 0;

	s = value_peek_string (argv[0]);
	while (*s) {
		gunichar uc = g_utf8_get_char (s);

		/*
		 * FIXME: This takes care of tabs and the likes too
		 * is that the desired behaviour?
		 */
		if (g_unichar_isspace (uc)) {
			if (!space) {
				last_len = res->len;
				g_string_append_unichar (res, uc);
				space = TRUE;
			}
		} else {
			g_string_append_unichar (res, uc);
			space = FALSE;
		}

		s = g_utf8_next_char (s);
	}

	if (space)
		g_string_truncate (res, last_len);

	return value_new_string_nocopy (g_string_free (res, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_value[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=VALUE\n"
	   "@SYNTAX=VALUE(text)\n"
	   "@DESCRIPTION="
	   "VALUE returns numeric value of @text.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "VALUE(\"$1,000\") equals 1000.\n"
	   "\n"
	   "@SEEALSO=DOLLAR, FIXED, TEXT")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_value (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	if (VALUE_IS_EMPTY (argv[0]) || VALUE_IS_NUMBER (argv[0]))
		return value_dup (argv[0]);
	else {
		GnmValue *v;
		char const *p = value_peek_string (argv[0]);

		/* Skip leading spaces */
		while (*p && g_unichar_isspace (g_utf8_get_char (p)))
		       p = g_utf8_next_char (p);

		v = format_match_number (p, NULL,
			workbook_date_conv (ei->pos->sheet->workbook));

		if (v != NULL)
			return v;
		return value_new_error_VALUE (ei->pos);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_substitute[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SUBSTITUTE\n"
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
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_substitute (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	/*
	 * Careful: value_peek_string handle only two live
	 * pointers at a time.
	 */
	char *textcopy = VALUE_IS_STRING (argv[0]) ? NULL : value_get_as_string (argv[0]);
	char const *text = textcopy ? textcopy : value_peek_string (argv[0]);
	char const *old  = value_peek_string (argv[1]);
	char const *new  = value_peek_string (argv[2]);
	char const *p;
	int oldlen, newlen, len, inst;
	GString *s;
	int num = 0;

	if (argv[3]) {
		gnm_float fnum = value_get_as_float (argv[3]);
		if (fnum <= 0) {
			g_free (textcopy);
			return value_new_error_VALUE (ei->pos);
		}
		num = (int)MIN((gnm_float)INT_MAX, fnum);
	}

	oldlen = strlen (old);
	if (oldlen == 0)
		return textcopy
			? value_new_string_nocopy (textcopy)
			: value_dup (argv[0]);

	newlen = strlen (new);
	len = strlen (text);
	s = g_string_sized_new (len);

	p = text;
	inst = 0;
	while (p - text < len) {
		char const *f = strstr (p, old);
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

static GnmFuncHelp const help_dollar[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=DOLLAR\n"
	   "@SYNTAX=DOLLAR(num[,decimals])\n"
	   "@DESCRIPTION="
	   "DOLLAR returns @num formatted as currency.\n\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "DOLLAR(12345) equals \"$12,345.00\".\n"
	   "\n"
	   "@SEEALSO=FIXED, TEXT, VALUE")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_dollar (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GOFormat *sf;
	gnm_float p10;
	GnmValue *v;
	char *s;
        gnm_float number = value_get_as_float (argv[0]);
        gnm_float decimals = argv[1] ? value_get_as_float (argv[1]) : 2.0;
	gboolean precedes, space_sep;
	const GString *curr = go_locale_get_currency (&precedes, &space_sep);
	GString *fmt_str;

	/* This is what Excel appears to do.  */
	if (decimals >= 128)
		return value_new_error_VALUE (ei->pos);
	decimals = gnm_fake_trunc (decimals);

	/* Since decimals can be negative, round the number.  */
	p10 = gnm_pow10 (decimals);
	if (p10 == 0)
		number = 0; /* Underflow.  */
	else
		number = gnm_fake_round (number * p10) / p10;

	fmt_str = g_string_sized_new (150);
	if (precedes) {
		g_string_append_c (fmt_str, '"');
		go_string_append_gstring (fmt_str, curr);
		g_string_append (fmt_str, space_sep ? "\" " : "\"");
	}
	g_string_append (fmt_str, "#,##0");
	if (decimals > 0) {
		g_string_append_c (fmt_str, '.');
		go_string_append_c_n (fmt_str, '0', (int)decimals);
	}
	if (!precedes) {
		g_string_append (fmt_str, space_sep ? " \"" : "\"");
		go_string_append_gstring (fmt_str, curr);
		g_string_append_c (fmt_str, '"');
	}

	/* No color and no space-for-parenthesis.  */
	g_string_append (fmt_str, ";(");
	g_string_append_len (fmt_str, fmt_str->str, fmt_str->len - 2);
	g_string_append_c (fmt_str, ')');

	sf = go_format_new_from_XL (fmt_str->str);

	v = value_new_float (number);
	s = format_value (sf, v, NULL, -1,
		workbook_date_conv (ei->pos->sheet->workbook));
	value_release (v);
	go_format_unref (sf);

	g_string_free (fmt_str, TRUE);

	return value_new_string_nocopy (s);
}

/***************************************************************************/

static GnmFuncHelp const help_search[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=SEARCH\n"
	   "@SYNTAX=SEARCH(search_string,text[,start_num])\n"
	   "@DESCRIPTION="
	   "SEARCH returns the location of the @search_ string within "
	   "@text. The search starts  with the @start_num character of text "
	   "@text.  If @start_num "
	   "is omitted, it is assumed to be one.  The search is not case "
	   "sensitive.\n"
	   "\n"
	   "@search_string can contain wildcard characters (*) and "
	   "question marks (?). "
	   "A question mark matches any "
	   "character and a wildcard matches any string including the empty "
	   "string.  If you want the actual wildcard or question mark to "
	   "be found, use tilde (~) before the character.\n"
	   "\n"
	   "* If @search_string is not found, SEARCH returns #VALUE! error.\n"
	   "* If @start_num is less than one or it is greater than the length "
	   "of @text, SEARCH returns #VALUE! error.\n"
           "* This function is Excel compatible.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "SEARCH(\"c\",\"Cancel\") equals 1.\n"
	   "SEARCH(\"c\",\"Cancel\",2) equals 4.\n"
	   "\n"
	   "@SEEALSO=FIND")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_search (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *needle = value_peek_string (argv[0]);
	char const *haystack = value_peek_string (argv[1]);
	gnm_float start = argv[2] ? value_get_as_float (argv[2]) : 1.0;
	size_t i, istart;
	char const *hay2;
	GORegexp r;

	if (start < 1 || start >= INT_MAX)
		return value_new_error_VALUE (ei->pos);
	// Make istart zero-based.  */
	istart = (int)(start - 1);

	for (i = istart, hay2 = haystack; i > 0; i--) {
		if (*hay2 == 0)
			return value_new_error_VALUE (ei->pos);
		hay2 = g_utf8_next_char (hay2);
	}

	if (gnm_regcomp_XL (&r, needle, REG_ICASE) == REG_OK) {
		GORegmatch rm;

		switch (go_regexec (&r, hay2, 1, &rm, 0)) {
		case REG_NOMATCH:
			break;
		case REG_OK:
			go_regfree (&r);
			return value_new_int
				(1 + istart +
				 g_utf8_pointer_to_offset (hay2, hay2 + rm.rm_so));
		default:
			g_warning ("Unexpected go_regexec result");
		}
		go_regfree (&r);
	} else {
		g_warning ("Unexpected regcomp result");
	}

	return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_asc[] = {
	{ GNM_FUNC_HELP_OLD,
	F_("@FUNCTION=ASC\n"
	   "@SYNTAX=ASC(string)\n"

	   "@DESCRIPTION="
	   "ASC a compatibility function that is meaningless in Gnumeric.  "
	   "In MS Excel (tm) it converts 2 byte @string into single byte text.\n"
	   "\n"
	   "@EXAMPLES=\n"
	   "CHAR(\"Foo\") equals \"Foo\".\n"
	   "\n"
	   "@SEEALSO=")
	},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_asc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_string (value_peek_string (argv[0]));
}

/***************************************************************************/
GnmFuncDescriptor const string_functions[] = {
        { "asc",       "s",     N_("string"),                  help_asc,
	  gnumeric_asc, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "char",       "f",     N_("number"),                  help_char,
	  gnumeric_char, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "unichar",    "f",     N_("number"),                  help_unichar,
	  gnumeric_unichar, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
        { "clean",      "S",     N_("text"),                    help_clean,
          gnumeric_clean, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "code",       "S",     N_("text"),                    help_code,
	  gnumeric_code, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "unicode",    "S",     N_("text"),                    help_unicode,
	  gnumeric_unicode, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
        { "concatenate", NULL,   N_("text,text,"),            help_concatenate,
	  NULL, gnumeric_concatenate, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dollar",     "f|f",   N_("num,decimals"),            help_dollar,
	  gnumeric_dollar, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "exact",      "SS",    N_("text1,text2"),             help_exact,
	  gnumeric_exact, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "find",       "SS|f",  N_("text1,text2,num"),         help_find,
	  gnumeric_find, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "fixed",      "f|fb",  N_("num,decs,no_commas"),      help_fixed,
	  gnumeric_fixed, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "left",       "S|f",   N_("text,num_chars"),          help_left,
	  gnumeric_left, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "len",        "S",     N_("text"),                    help_len,
	  gnumeric_len, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "lenb",       "S",     N_("text"),                    help_lenb,
	  gnumeric_lenb, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "lower",      "S",     N_("text"),                    help_lower,
	  gnumeric_lower, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "mid",        "Sff",   N_("text,pos,num"),            help_mid,
	  gnumeric_mid, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "proper",     "S",     N_("text"),                    help_proper,
	  gnumeric_proper, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "replace",    "SffS",  N_("old,start,num,new"),       help_replace,
	  gnumeric_replace, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "rept",       "Sf",    N_("text,num"),                help_rept,
	  gnumeric_rept, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "right",      "S|f",   N_("text,num_chars"),          help_right,
	  gnumeric_right, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "search",     "SS|f",  N_("search_string,text,start_num"),   help_search,
	  gnumeric_search, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "substitute", "SSS|f", N_("text,old,new,num"),       help_substitute,
	  gnumeric_substitute, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "t",          "S",     N_("value"),                   help_t_,
          gnumeric_t_, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "text",       "Ss",    N_("value,format_text"),       help_text,
	  gnumeric_text, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "trim",       "S",     N_("text"),                    help_trim,
	  gnumeric_trim, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "upper",      "S",     N_("text"),                    help_upper,
	  gnumeric_upper, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "value",      "S",     N_("text"),                    help_value,
	  gnumeric_value, NULL, NULL, NULL, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },

        {NULL}
};


G_MODULE_EXPORT void
go_plugin_init (GOPlugin *plugin, GOCmdContext *cc)
{
	int codepage = gsf_msole_iconv_win_codepage ();
	CHAR_iconv = gsf_msole_iconv_open_for_import (codepage);
	CODE_iconv = gsf_msole_iconv_open_for_export ();
}

G_MODULE_EXPORT void
go_plugin_shutdown (GOPlugin *plugin, GOCmdContext *cc)
{
	gsf_iconv_close (CHAR_iconv);
	gsf_iconv_close (CODE_iconv);
}

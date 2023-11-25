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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <func.h>
#include <parse-util.h>
#include <cell.h>
#include <gnm-format.h>
#include <gutils.h>
#include <sheet.h>
#include <workbook.h>
#include <value.h>
#include <expr.h>
#include <number-match.h>
#include <mathfunc.h>
#include <rangefunc-strings.h>
#include <collect.h>
#include <goffice/goffice.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-msole-utils.h>
#include <gnm-i18n.h>
#include <gnm-plugin.h>

#include <limits.h>
#include <string.h>

GNM_PLUGIN_MODULE_HEADER;

/***************************************************************************/

static GIConv CHAR_iconv;

static GnmFuncHelp const help_char[] = {
	{ GNM_FUNC_HELP_NAME, F_("CHAR:the CP1252 (Windows-1252) character for the code point @{x}")},
	{ GNM_FUNC_HELP_ARG, F_("x:code point")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CHAR(@{x}) returns the CP1252 (Windows-1252) character with code @{x}.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("@{x} must be in the range 1 to 255.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CP1252 (Windows-1252) is also known as the \"ANSI code page\", "
					"but it is not an ANSI standard.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CP1252 (Windows-1252) is based on an early draft of ISO-8859-1, "
					"and contains all of its printable characters. It also contains all "
					"of ISO-8859-15's printable characters (but partially at different "
					"positions.)")},
	{ GNM_FUNC_HELP_NOTE, F_("In CP1252 (Windows-1252), 129, 141, 143, 144, and 157 do not have matching characters.") },
	{ GNM_FUNC_HELP_NOTE, F_("For @{x} from 1 to 255 except 129, 141, 143, 144, and 157 we have CODE(CHAR(@{x}))=@{x}.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CHAR(65)" },
	{ GNM_FUNC_HELP_SEEALSO, "CODE"},
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
        { GNM_FUNC_HELP_NAME, F_("UNICHAR:the Unicode character represented by the Unicode code point @{x}")},
        { GNM_FUNC_HELP_ARG, F_("x:Unicode code point")},
        { GNM_FUNC_HELP_EXAMPLES, "=UNICHAR(65)"},
        { GNM_FUNC_HELP_EXAMPLES, "=UNICHAR(960)"},
        { GNM_FUNC_HELP_EXAMPLES, "=UNICHAR(20000)"},
        { GNM_FUNC_HELP_SEEALSO, "CHAR,UNICODE,CODE"},
        { GNM_FUNC_HELP_END}
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
	{ GNM_FUNC_HELP_NAME, F_("CODE:the CP1252 (Windows-1252) code point for the character @{c}")},
	{ GNM_FUNC_HELP_ARG, F_("c:character")},
{ GNM_FUNC_HELP_DESCRIPTION, F_("@{c} must be a valid CP1252 (Windows-1252) character.")},
{ GNM_FUNC_HELP_DESCRIPTION, F_("CP1252 (Windows-1252) is also known as the \"ANSI code page\", but it is not an ANSI standard.")},
{ GNM_FUNC_HELP_DESCRIPTION, F_("CP1252 (Windows-1252) is based on an early draft of ISO-8859-1, and contains all of its printable characters (but partially at different positions.)")},
	{ GNM_FUNC_HELP_NOTE, F_("In CP1252 (Windows-1252), 129, 141, 143, 144, and 157 do not have matching characters.") },
	{ GNM_FUNC_HELP_NOTE, F_("For @{x} from 1 to 255 except 129, 141, 143, 144, and 157 we have CODE(CHAR(@{x}))=@{x}.") },
	{ GNM_FUNC_HELP_DESCRIPTION, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CODE(\"A\")" },
	{ GNM_FUNC_HELP_SEEALSO, "CHAR"},
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
        { GNM_FUNC_HELP_NAME, F_("UNICODE:the Unicode code point for the character @{c}")},
        { GNM_FUNC_HELP_ARG, F_("c:character")},
        { GNM_FUNC_HELP_EXAMPLES, "=UNICODE(\"A\")" },
        { GNM_FUNC_HELP_SEEALSO, "UNICHAR,CODE,CHAR"},
        { GNM_FUNC_HELP_END}
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

static gboolean
gnm_compare_strings (const char *cstr1, const char *cstr2)
{
	const char *a = cstr1, *b = cstr2;
	char *str1, *str2;
	gboolean eq;

	/* Skip leading ASCII prefixes that match.  */
	while (*a == *b && *a != 0 && *b != 0)
		a++, b++;
	/*
	 * If we've hit the end of one string, we ought to have hit the
	 * end of the other.  Otherwise the strings are different.
	 */
	if (*a == 0 || *b == 0)
		return *a == *b;

	/*
	 * If they differ in two ASCII characters (including terminating
	 * NULs), the strings must be distinct.
	 */
	if ((guchar)*a < 128 && (guchar)*b < 128)
		return FALSE;

	/*
	 * We are using NFD normalization, ie. Characters are decomposed by
	 * canonical equivalence, and multiple combining characters are
	 * arranged in a specific order. Note that ligatures remain ligatures,
	 * formatting such as subscript-3 versus 3 are retained.
	 *
	 * Note that for example, the distinct Unicode strings "U+212B"
	 * (the angstrom sign "Å") and "U+00C5" (the Swedish letter "Å")
	 * are both expanded by NFD (or NFKD) into the sequence
	 * "U+0041 U+030A" (Latin letter "A" and combining ring above "°")
	 * Of course "U+0041 U+030A" is retained in form, so we need to work
	 * with at least the last ASCII character. Performance should nearly
	 * be identical to using all
	 */
	str1 = g_utf8_normalize (cstr1, -1, G_NORMALIZE_DEFAULT);
	str2 = g_utf8_normalize (cstr2, -1, G_NORMALIZE_DEFAULT);

	eq = (g_strcmp0 (str1, str2) == 0);

	g_free (str1);
	g_free (str2);

	return eq;
}

static GnmFuncHelp const help_exact[] = {
        { GNM_FUNC_HELP_NAME, F_("EXACT:TRUE if @{string1} is exactly equal to @{string2}")},
        { GNM_FUNC_HELP_ARG, F_("string1:first string")},
        { GNM_FUNC_HELP_ARG, F_("string2:second string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=EXACT(\"Gnumeric\",\"Gnumeric\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=EXACT(\"gnumeric\",\"Gnumeric\")" },
        { GNM_FUNC_HELP_SEEALSO, "LEN,SEARCH,DELTA"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_exact (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_bool (gnm_compare_strings (value_peek_string (argv[0]),
						    value_peek_string (argv[1])));
}

/***************************************************************************/

static GnmFuncHelp const help_encodeurl[] = {
	{ GNM_FUNC_HELP_NAME, F_("ENCODEURL:encode a string for use in a URL")},
	{ GNM_FUNC_HELP_ARG, F_("string:string to encode")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=ENCODEURL(\"http://www.gnumeric.org/\")" },
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_encodeurl (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	const char *str = value_peek_string (argv[0]);
	return value_new_string_nocopy (go_url_encode (str, 1));
}

/***************************************************************************/

static GnmFuncHelp const help_len[] = {
        { GNM_FUNC_HELP_NAME, F_("LEN:the number of characters of the string @{s}")},
        { GNM_FUNC_HELP_ARG, F_("s:the string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=LEN(\"Helsinki\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=LEN(\"L\xc3\xa9vy\")" },
	{ GNM_FUNC_HELP_SEEALSO, "CHAR,CODE,LENB"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_len (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_int (g_utf8_strlen (value_peek_string (argv[0]), -1));
}

/***************************************************************************/
static GnmFuncHelp const help_lenb[] = {
        { GNM_FUNC_HELP_NAME, F_("LENB:the number of bytes in the string @{s}")},
        { GNM_FUNC_HELP_ARG, F_("s:the string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=LENB(\"Helsinki\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=LENB(\"L\xc3\xa9vy\")" },
        { GNM_FUNC_HELP_SEEALSO, "CHAR,CODE,LEN"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_lenb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_int (strlen (value_peek_string (argv[0])));
}

/***************************************************************************/

static GnmFuncHelp const help_left[] = {
	{ GNM_FUNC_HELP_NAME, F_("LEFT:the first @{num_chars} characters of the string @{s}")},
	{ GNM_FUNC_HELP_ARG, F_("s:the string")},
	{ GNM_FUNC_HELP_ARG, F_("num_chars:the number of characters to return (defaults to 1)")},
	{ GNM_FUNC_HELP_NOTE, F_("If the string @{s} is in a right-to-left script, the returned first characters are from the right of the string.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=LEFT(\"L\xc3\xa9vy\",3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=LEFT(\"L\xc3\xa9vy\",2)" },
	{ GNM_FUNC_HELP_SEEALSO, "MID,RIGHT,LEN,MIDB,RIGHTB,LENB"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_left (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	const guchar *peek = (const guchar *)value_peek_string (argv[0]);
	gnm_float count = argv[1] ? value_get_as_float (argv[1]) : 1;
	int icount, newlen;

	if (count < 0)
		return value_new_error_VALUE (ei->pos);
	icount = (int)MIN ((gnm_float)INT_MAX, count);

	for (newlen = 0; peek[newlen] != 0 && icount > 0; icount--)
		newlen += g_utf8_skip[peek[newlen]];

	return value_new_string_nocopy (g_strndup (peek, newlen));
}

/***************************************************************************/

static GnmFuncHelp const help_leftb[] = {
	{ GNM_FUNC_HELP_NAME, F_("LEFTB:the first characters of the string @{s} comprising at most @{num_bytes} bytes")},
	{ GNM_FUNC_HELP_ARG, F_("s:the string")},
	{ GNM_FUNC_HELP_ARG, F_("num_bytes:the maximum number of bytes to return (defaults to 1)")},
	{ GNM_FUNC_HELP_NOTE, F_("The semantics of this function is subject to change as various applications implement it.")},
	{ GNM_FUNC_HELP_NOTE, F_("If the string is in a right-to-left script, the returned first characters are from the right of the string.")},
	{ GNM_FUNC_HELP_EXCEL, F_("While this function is syntactically Excel compatible, the differences in the underlying text encoding will usually yield different results.")},
	{ GNM_FUNC_HELP_ODF, F_("While this function is OpenFormula compatible, most of its behavior is, at this time, implementation specific.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=LEFTB(\"L\xc3\xa9vy\",3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=LEFTB(\"L\xc3\xa9vy\",2)" },
	{ GNM_FUNC_HELP_SEEALSO, "MIDB,RIGHTB,LENB,LEFT,MID,RIGHT,LEN" },
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_leftb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	const guchar *peek = (const guchar *)value_peek_string (argv[0]);
	gnm_float count = argv[1] ? value_get_as_float (argv[1]) : 1;
	int len = strlen (peek);
	int icount, newlen;

	if (count < 0)
		return value_new_error_VALUE (ei->pos);
	icount = (int)MIN ((gnm_float)INT_MAX, count);
	if (icount >= len)
		return value_new_string (peek);

	newlen = ((const guchar *)g_utf8_find_prev_char (peek, peek + icount + 1)) - peek;

	return value_new_string_nocopy (g_strndup (peek, newlen));
}
/***************************************************************************/

static GnmFuncHelp const help_lower[] = {
        { GNM_FUNC_HELP_NAME, F_("LOWER:a lower-case version of the string @{text}")},
	{ GNM_FUNC_HELP_ARG, F_("text:string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
        { GNM_FUNC_HELP_EXAMPLES, "=LOWER(\"J. F. Kennedy\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=LOWER(\"L\xc3\x89VY\")" },
        { GNM_FUNC_HELP_SEEALSO, "UPPER"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_lower (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_string_nocopy (g_utf8_strdown (value_peek_string (argv[0]), -1));
}

/***************************************************************************/

static GnmFuncHelp const help_mid[] = {
	{ GNM_FUNC_HELP_NAME, F_("MID:the substring of the string @{s} starting at position @{position} consisting of @{length} characters")},
	{ GNM_FUNC_HELP_ARG, F_("s:the string")},
	{ GNM_FUNC_HELP_ARG, F_("position:the starting position")},
	{ GNM_FUNC_HELP_ARG, F_("length:the number of characters to return")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=MID(\"L\xc3\xa9vy\",2,1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=MID(\"L\xc3\xa9vy\",3,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "LEFT,RIGHT,LEN,LEFTB,MIDB,RIGHTB,LENB"},
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

static GnmFuncHelp const help_midb[] = {
	{ GNM_FUNC_HELP_NAME, F_("MIDB:the characters following the first @{start_pos} bytes comprising at most @{num_bytes} bytes")},
	{ GNM_FUNC_HELP_ARG, F_("s:the string")},
	{ GNM_FUNC_HELP_ARG, F_("start_pos:the number of the byte with which to start (defaults to 1)")},
	{ GNM_FUNC_HELP_ARG, F_("num_bytes:the maximum number of bytes to return (defaults to 1)")},
	{ GNM_FUNC_HELP_NOTE, F_("The semantics of this function is subject to change as various applications implement it.")},
	{ GNM_FUNC_HELP_EXCEL, F_("While this function is syntactically Excel compatible, "
				  "the differences in the underlying text encoding will usually yield different results.")},
	{ GNM_FUNC_HELP_ODF, F_("While this function is OpenFormula compatible, most of its behavior is, at this time, implementation specific.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=MIDB(\"L\xc3\xa9vy\",2,1)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=MIDB(\"L\xc3\xa9vy\",2,2)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=MIDB(\"L\xc3\xa9vy\",3,2)" },
	{ GNM_FUNC_HELP_SEEALSO, "LEFTB,RIGHTB,LENB,LEFT,MID,RIGHT,LEN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_midb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	const guchar *peek = (const guchar *)value_peek_string (argv[0]);
	gnm_float pos = value_get_as_float (argv[1]);
	gnm_float len = value_get_as_float (argv[2]);
 	int slen = strlen (peek);
	int ipos, ilen, newlen;

	if ((len < 0) || (pos < 1))
		return value_new_error_VALUE (ei->pos);
	ipos = (int)MIN ((gnm_float)INT_MAX / 2, pos) - 1;
	ilen = (int)MIN ((gnm_float)INT_MAX / 2, len);
	if ((ipos >= slen) ||
	    ((gunichar)-1 == g_utf8_get_char_validated (peek + ipos, -1)))
		return value_new_error_VALUE (ei->pos);

	if ((ipos + ilen) > slen)
		return value_new_string (peek + ipos);

	newlen = ((const guchar *)g_utf8_find_prev_char (peek + ipos, peek + ipos + ilen + 1))
		- (peek + ipos);

	return value_new_string_nocopy (g_strndup (peek + ipos, newlen));
}

/***************************************************************************/

static GnmFuncHelp const help_findb[] = {
        { GNM_FUNC_HELP_NAME, F_("FINDB:first byte position of @{string1} in @{string2} following byte position @{start}")},
        { GNM_FUNC_HELP_ARG, F_("string1:search string")},
        { GNM_FUNC_HELP_ARG, F_("string2:search field")},
        { GNM_FUNC_HELP_ARG, F_("start:starting byte position, defaults to 1")},
        { GNM_FUNC_HELP_NOTE, F_("This search is case-sensitive.")},
	{ GNM_FUNC_HELP_EXCEL, F_("While this function is syntactically Excel compatible, "
				  "the differences in the underlying text encoding will usually yield different results.")},
	{ GNM_FUNC_HELP_ODF, F_("While this function is OpenFormula compatible, most of its behavior is, at this time, implementation specific.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=FINDB(\"v\",\"L\xc3\xa9vy\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=FINDB(\"v\",\"L\xc3\xa9vy\",3)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=FINDB(\"v\",\"L\xc3\xa9vy\",5)" },
	{ GNM_FUNC_HELP_SEEALSO, "FIND,LEFTB,RIGHTB,LENB,LEFT,MID,RIGHT,LEN"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_findb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *needle   = value_peek_string (argv[0]);
	char const *haystack = value_peek_string (argv[1]);
	gnm_float count      = argv[2] ? value_get_as_float (argv[2]) : 1;
	size_t haystacksize = strlen (haystack);
	size_t icount;
	char const *p;


	if (count < 1 || count >= haystacksize + 1)
		return value_new_error_VALUE (ei->pos);

	icount = (size_t) count;
	p = (icount == 1) ? haystack : g_utf8_find_next_char (haystack + (icount - 2) , NULL);

	p = g_strstr_len (p, strlen (p), needle);
	if (p)
		return value_new_int
			((p - haystack) + 1);
	else
		return value_new_error_VALUE (ei->pos);
}

/***************************************************************************/

static GnmFuncHelp const help_right[] = {
	{ GNM_FUNC_HELP_NAME, F_("RIGHT:the last @{num_chars} characters of the string @{s}")},
	{ GNM_FUNC_HELP_ARG, F_("s:the string")},
	{ GNM_FUNC_HELP_ARG, F_("num_chars:the number of characters to return (defaults to 1)")},
	{ GNM_FUNC_HELP_NOTE, F_("If the string @{s} is in a right-to-left script, the returned last characters are from the left of the string.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.")},
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=RIGHT(\"L\xc3\xa9vy\",2)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=RIGHT(\"L\xc3\xa9vy\",3)" },
	{ GNM_FUNC_HELP_SEEALSO, "LEFT,MID,LEN,LEFTB,MIDB,RIGHTB,LENB"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_right (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *os = value_peek_string (argv[0]);
	gnm_float count = argv[1] ? value_get_as_float (argv[1]) : 1;
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

static GnmFuncHelp const help_rightb[] = {
	{ GNM_FUNC_HELP_NAME, F_("RIGHTB:the last characters of the string @{s} comprising at most @{num_bytes} bytes")},
	{ GNM_FUNC_HELP_ARG, F_("s:the string")},
	{ GNM_FUNC_HELP_ARG, F_("num_bytes:the maximum number of bytes to return (defaults to 1)")},
	{ GNM_FUNC_HELP_NOTE, F_("The semantics of this function is subject to change as various applications implement it.")},
	{ GNM_FUNC_HELP_NOTE, F_("If the string @{s} is in a right-to-left script, the returned last characters are from the left of the string.")},
	{ GNM_FUNC_HELP_EXCEL, F_("While this function is syntactically Excel compatible, the differences in the underlying text encoding will usually yield different results.")},
	{ GNM_FUNC_HELP_ODF, F_("While this function is OpenFormula compatible, most of its behavior is, at this time, implementation specific.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=RIGHTB(\"L\xc3\xa9vy\",2)" },
	{ GNM_FUNC_HELP_EXAMPLES, "=RIGHTB(\"L\xc3\xa9vy\",3)" },
	{ GNM_FUNC_HELP_SEEALSO, "LEFTB,MIDB,LENB,LEFT,MID,RIGHT,LEN"},
	{ GNM_FUNC_HELP_END }
};

static GnmValue *
gnumeric_rightb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	const guchar *peek = (const guchar *)value_peek_string (argv[0]);
	gnm_float count = argv[1] ? value_get_as_float (argv[1]) : 1;
	int len = strlen (peek);
	int icount;
	gchar *res;

	if (count < 0)
		return value_new_error_VALUE (ei->pos);
	icount = (int)MIN ((gnm_float)INT_MAX, count);
	if (icount >= len)
		return value_new_string (peek);

	res = g_utf8_find_next_char (peek + len - icount - 1, peek + len);
	return value_new_string ((res == NULL) ? "" : res);
}

/***************************************************************************/

static GnmFuncHelp const help_upper[] = {
        { GNM_FUNC_HELP_NAME, F_("UPPER:an upper-case version of the string @{text}")},
	{ GNM_FUNC_HELP_ARG, F_("text:string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=UPPER(\"Gnumeric\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=UPPER(\"L\xc3\xa9vy\")" },
        { GNM_FUNC_HELP_SEEALSO, "LOWER"},
        { GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_upper (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	return value_new_string_nocopy (g_utf8_strup (value_peek_string (argv[0]), -1));
}

/***************************************************************************/

static GnmFuncHelp const help_concatenate[] = {
	{ GNM_FUNC_HELP_NAME, F_("CONCATENATE:the concatenation of the strings @{s1}, @{s2},\xe2\x80\xa6")},
	{ GNM_FUNC_HELP_ARG, F_("s1:first string")},
	{ GNM_FUNC_HELP_ARG, F_("s2:second string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CONCATENATE(\"aa\",\"bb\")" },
	{ GNM_FUNC_HELP_SEEALSO, "LEFT,MID,RIGHT"},
	{ GNM_FUNC_HELP_END}
};


static GnmValue *
gnumeric_concatenate (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return string_range_function (argc, argv, ei,
				      range_concatenate,
				      NULL,
				      COLLECT_IGNORE_BLANKS,
				      GNM_ERROR_VALUE);
}

static GnmFuncHelp const help_concat[] = {
	{ GNM_FUNC_HELP_NAME, F_("CONCAT:the concatenation of the strings @{s1}, @{s2},\xe2\x80\xa6")},
	{ GNM_FUNC_HELP_ARG, F_("s1:first string")},
	{ GNM_FUNC_HELP_ARG, F_("s2:second string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=CONCAT(\"aa\",\"bb\")" },
	{ GNM_FUNC_HELP_NOTE, F_("This function is identical to CONCATENATE") },
	{ GNM_FUNC_HELP_SEEALSO, "LEFT,MID,RIGHT"},
	{ GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_concat (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	return gnumeric_concatenate (ei, argc, argv);
}

/***************************************************************************/

static GnmFuncHelp const help_textjoin[] = {
	{ GNM_FUNC_HELP_NAME, F_("TEXTJOIN:the concatenation of the strings @{s1}, @{s2},\xe2\x80\xa6 delimited by @{del}")},
	{ GNM_FUNC_HELP_ARG, F_("del:delimiter")},
	{ GNM_FUNC_HELP_ARG, F_("blank:ignore blanks")},
	{ GNM_FUNC_HELP_ARG, F_("s1:first string")},
	{ GNM_FUNC_HELP_ARG, F_("s2:second string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
	{ GNM_FUNC_HELP_EXAMPLES, "=TEXTJOIN(\"::\",FALSE,\"aa\",\"bb\")" },
	{ GNM_FUNC_HELP_SEEALSO, "CONCATENATE"},
	{ GNM_FUNC_HELP_END}
};

struct cb_textjoin {
	char *delim;
	gboolean ignore_blanks;
};

static int
range_textjoin (GPtrArray *data, char **pres, gpointer user_)
{
	struct cb_textjoin *user = user_;
	GString *res = g_string_new (NULL);
	gboolean first = TRUE;
	unsigned ui;

	for (ui = 0; ui < data->len; ui++) {
		const char *s = g_ptr_array_index (data, ui);

		if (s[0] == 0 && user->ignore_blanks)
			continue;

		if (first)
			first = FALSE;
		else
			g_string_append (res, user->delim);

		g_string_append (res, s);
	}

	*pres = g_string_free (res, FALSE);
	return 0;
}

static GnmValue *
gnumeric_textjoin (GnmFuncEvalInfo *ei, int argc, GnmExprConstPtr const *argv)
{
	GnmValue *v;
	gboolean err;
	struct cb_textjoin data;

	data.delim = NULL;

	if (argc < 3)
		return value_new_error_VALUE (ei->pos);

	v = gnm_expr_eval (argv[0], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (VALUE_IS_ERROR (v))
		goto done;
	data.delim = value_get_as_string (v);
	value_release (v);

	v = gnm_expr_eval (argv[1], ei->pos, GNM_EXPR_EVAL_SCALAR_NON_EMPTY);
	if (VALUE_IS_ERROR (v))
		goto done;
	data.ignore_blanks = value_get_as_bool (v, &err); // What about err?
	value_release (v);

	v = string_range_function (argc - 2, argv + 2, ei,
				   range_textjoin,
				   &data,
				   data.ignore_blanks ? COLLECT_IGNORE_BLANKS : 0,
				   GNM_ERROR_VALUE);

done:
	g_free (data.delim);

	return v;
}

/***************************************************************************/

static GnmFuncHelp const help_rept[] = {
        { GNM_FUNC_HELP_NAME, F_("REPT:@{num} repetitions of string @{text}")},
        { GNM_FUNC_HELP_ARG, F_("text:string")},
        { GNM_FUNC_HELP_ARG, F_("num:non-negative integer")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=REPT(\"x\",3)" },
        { GNM_FUNC_HELP_SEEALSO, "CONCATENATE"},
        { GNM_FUNC_HELP_END}
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
        { GNM_FUNC_HELP_NAME, F_("CLEAN:@{text} with any non-printable characters removed")},
        { GNM_FUNC_HELP_ARG, F_("text:string")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("CLEAN removes non-printable characters from its argument leaving only regular characters and white-space.") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=CLEAN(\"Gnumeric\"&char(7))" },
        { GNM_FUNC_HELP_END}
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
        { GNM_FUNC_HELP_NAME, F_("FIND:first position of @{string1} in @{string2} following position @{start}")},
        { GNM_FUNC_HELP_ARG, F_("string1:search string")},
        { GNM_FUNC_HELP_ARG, F_("string2:search field")},
        { GNM_FUNC_HELP_ARG, F_("start:starting position, defaults to 1")},
        { GNM_FUNC_HELP_NOTE, F_("This search is case-sensitive.")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=FIND(\"num\",\"Gnumeric\")" },
        { GNM_FUNC_HELP_SEEALSO, "EXACT,LEN,MID,SEARCH"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_find (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *needle   = value_peek_string (argv[0]);
	char const *haystack = value_peek_string (argv[1]);
	gnm_float count      = argv[2] ? value_get_as_float (argv[2]) : 1;
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
        { GNM_FUNC_HELP_NAME, F_("FIXED:formatted string representation of @{num}")},
        { GNM_FUNC_HELP_ARG, F_("num:number")},
        { GNM_FUNC_HELP_ARG, F_("decimals:number of decimals")},
        { GNM_FUNC_HELP_ARG, F_("no_commas:TRUE if no thousand separators should be used, "
				"defaults to FALSE")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=FIXED(1234.567,2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=FIXED(1234.567,2,TRUE)" },
        { GNM_FUNC_HELP_SEEALSO, "TEXT,VALUE,DOLLAR"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_fixed (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	gnm_float num = value_get_as_float (argv[0]);
	gnm_float decimals = argv[1] ? value_get_as_float (argv[1]) : 2;
	gboolean no_commas = argv[2] ? value_get_as_checked_bool (argv[2]) : FALSE;
	GString *format;
	GOFormat *fmt;
	GnmValue *v;
	char *res;
	GOFormatDetails *details;

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
		decimals = 0;
	}
	v = value_new_float (num);

	details = go_format_details_new (GO_FORMAT_NUMBER);
	details->num_decimals = decimals;
	details->thousands_sep = !no_commas;
	format = g_string_new (NULL);
	go_format_generate_str (format, details);
	go_format_details_free (details);

	fmt = go_format_new_from_XL (format->str);
	g_string_free (format, TRUE);

	res = format_value (fmt, v, -1,
			    sheet_date_conv (ei->pos->sheet));

	go_format_unref (fmt);
	value_release (v);

	return value_new_string_nocopy (res);
}

/***************************************************************************/

static GnmFuncHelp const help_proper[] = {
        { GNM_FUNC_HELP_NAME, F_("PROPER:@{text} with initial of each word capitalised")},
        { GNM_FUNC_HELP_ARG, F_("text:string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=PROPER(\"j. f. kennedy\")" },
        { GNM_FUNC_HELP_SEEALSO, "LOWER,UPPER"},
        { GNM_FUNC_HELP_END}
};

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
        { GNM_FUNC_HELP_NAME, F_("REPLACE:string @{old} with @{num} characters "
				 "starting at @{start} replaced by @{new}")},
        { GNM_FUNC_HELP_ARG, F_("old:original text")},
        { GNM_FUNC_HELP_ARG, F_("start:starting position")},
        { GNM_FUNC_HELP_ARG, F_("num:number of characters to be replaced")},
        { GNM_FUNC_HELP_ARG, F_("new:replacement string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=REPLACE(\"Gnumeric\",2,6,\"*6*\")" },
        { GNM_FUNC_HELP_SEEALSO, "MID,SEARCH,SUBSTITUTE,TRIM"},
        { GNM_FUNC_HELP_END}
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

static GnmFuncHelp const help_replaceb[] = {
        { GNM_FUNC_HELP_NAME, F_("REPLACEB:string @{old} with up to @{num} bytes "
				 "starting at @{start} replaced by @{new}")},
        { GNM_FUNC_HELP_ARG, F_("old:original text")},
        { GNM_FUNC_HELP_ARG, F_("start:starting byte position")},
        { GNM_FUNC_HELP_ARG, F_("num:number of bytes to be replaced")},
        { GNM_FUNC_HELP_ARG, F_("new:replacement string")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("REPLACEB replaces the string of valid unicode characters starting "
					"at the byte @{start} and ending at @{start}+@{num}-1 with the string @{new}.")},
	{ GNM_FUNC_HELP_NOTE, F_("The semantics of this function is subject to change as various applications implement it.")},
	{ GNM_FUNC_HELP_EXCEL, F_("While this function is syntactically Excel compatible, "
				  "the differences in the underlying text encoding will usually yield different results.")},
	{ GNM_FUNC_HELP_ODF, F_("While this function is OpenFormula compatible, most of its behavior is, at this time, implementation specific.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=REPLACEB(\"L\xc3\xa9vy\",2,1,\"*\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=REPLACEB(\"L\xc3\xa9vy\",2,2,\"*\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=REPLACEB(\"L\xc3\xa9vy\",2,3,\"*\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=REPLACEB(\"L\xc3\xa9vy\",2,4,\"*\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=REPLACEB(\"L\xc3\xa9vy\",3,2,\"*\")" },
	{ GNM_FUNC_HELP_EXAMPLES, "=REPLACEB(\"L\xc3\xa9vy\",3,3,\"*\")" },
        { GNM_FUNC_HELP_SEEALSO, "MID,SEARCH,SUBSTITUTE,TRIM"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_replaceb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *old = value_peek_string (argv[0]);
	gnm_float pos = value_get_as_float (argv[1]);
	gnm_float len = value_get_as_float (argv[2]);
	char const *new = value_peek_string (argv[3]);
 	int slen = strlen (old);
	int ipos, ilen, newlen;
	char *res;

	if ((len < 0) || (pos < 1))
		return value_new_error_VALUE (ei->pos);
	ipos = (int)MIN ((gnm_float)INT_MAX / 2, pos) - 1;
	ilen = (int)MIN ((gnm_float)INT_MAX / 2, len);
	if ((ipos > slen) ||
	    (ipos + ilen > slen) ||
	    ((gunichar)-1 == g_utf8_get_char_validated (old + ipos, -1)) ||
	    ((gunichar)-1 == g_utf8_get_char_validated (old + ipos + ilen, -1)) ||
	    !g_utf8_validate (old + ipos, ilen, NULL))
		return value_new_error_VALUE (ei->pos);
	newlen = strlen (new);
	res = g_malloc (slen - ilen + newlen + 1);
	memcpy (res, old, ipos);
	memcpy (res + ipos, new, newlen);
	memcpy (res + ipos + newlen, old + ipos + ilen, slen - ipos - ilen + 1);
	return value_new_string_nocopy (res);
}

/***************************************************************************/
/* Note: help_t is a reserved symbol.  */

static GnmFuncHelp const help_t_[] = {
        { GNM_FUNC_HELP_NAME, F_("T:@{value} if and only if @{value} is text, otherwise empty")},
        { GNM_FUNC_HELP_ARG, F_("value:original value")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=T(\"Gnumeric\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=T(64)"},
        { GNM_FUNC_HELP_SEEALSO, "CELL,N,VALUE"},
        { GNM_FUNC_HELP_END}
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
        { GNM_FUNC_HELP_NAME, F_("TEXT:@{value} as a string formatted as @{format}")},
        { GNM_FUNC_HELP_ARG, F_("value:value to be formatted")},
        { GNM_FUNC_HELP_ARG, F_("format:desired format")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=TEXT(3.223,\"$0.00\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=TEXT(date(1999,4,15),\"mmmm, dd, yy\")" },
        { GNM_FUNC_HELP_SEEALSO, "DOLLAR,FIXED,VALUE"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_text (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GnmValue *res, *match = NULL;
	GnmValue const *v  = argv[0];
	GODateConventions const *conv =
		sheet_date_conv (ei->pos->sheet);
	char *lfmt;

	/* Why do we have to do these here?  */
	if (VALUE_IS_STRING (v)) {
		match = format_match (value_peek_string (v), NULL, conv);
		if (match != NULL)
			v = match;
	} else if (VALUE_IS_EMPTY (v))
		v = value_zero;

	lfmt = go_format_str_delocalize (value_peek_string (argv[1]));
	if (lfmt) {
		GOFormat *fmt = go_format_new_from_XL (lfmt);
		GString *str = g_string_sized_new (80);
		GOFormatNumberError err;

		g_free (lfmt);
		err = format_value_gstring (str, fmt, v, -1, conv);
		if (err) {
			g_string_free (str, TRUE);
			res = value_new_error_VALUE (ei->pos);
		} else {
			res = value_new_string_nocopy (g_string_free (str, FALSE));
		}
		go_format_unref (fmt);
	} else {
		res = value_new_error_VALUE (ei->pos);
	}

	value_release (match);

	return res;
}

/***************************************************************************/

static GnmFuncHelp const help_trim[] = {
        { GNM_FUNC_HELP_NAME, F_("TRIM:@{text} with only single spaces between words")},
        { GNM_FUNC_HELP_ARG, F_("text:string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=TRIM(\"  a bbb  cc \")" },
        { GNM_FUNC_HELP_SEEALSO, "CLEAN,MID,REPLACE,SUBSTITUTE"},
        { GNM_FUNC_HELP_END}
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
        { GNM_FUNC_HELP_NAME, F_("VALUE:numeric value of @{text}")},
        { GNM_FUNC_HELP_ARG, F_("text:string")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=VALUE(\"$1,000\")" },
        { GNM_FUNC_HELP_SEEALSO, "DOLLAR,FIXED,TEXT"},
        { GNM_FUNC_HELP_END}
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
			sheet_date_conv (ei->pos->sheet));

		if (v != NULL)
			return v;
		return value_new_error_VALUE (ei->pos);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_numbervalue[] = {
        { GNM_FUNC_HELP_NAME, F_("NUMBERVALUE:numeric value of @{text}")},
        { GNM_FUNC_HELP_ARG, F_("text:string")},
        { GNM_FUNC_HELP_ARG, F_("separator:decimal separator")},
	{ GNM_FUNC_HELP_NOTE, F_("If @{text} does not look like a decimal number, "
				 "NUMBERVALUE returns the value VALUE would "
				 "return (ignoring the given @{separator}).")},
 	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=NUMBERVALUE(\"$1,000\",\",\")" },
        { GNM_FUNC_HELP_SEEALSO, "VALUE"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_numbervalue (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *sep = value_peek_string (argv[1]);
	if (strlen(sep) != 1 || (*sep != '.' && *sep != ',')) {
		return value_new_error_VALUE (ei->pos);
	}

	if (VALUE_IS_EMPTY (argv[0]) || VALUE_IS_NUMBER (argv[0]))
		return value_dup (argv[0]);
	else {
		GnmValue *v;
		char const *p = value_peek_string (argv[0]);
		GString *curr;
		GString *thousand;
		GString *decimal;
		GOFormatFamily family = GO_FORMAT_GENERAL;

		decimal = g_string_new (sep);
		thousand = g_string_new ((*sep == '.') ? ",":".");
		curr = g_string_new ("$");

		/* Skip leading spaces */
		while (*p && g_unichar_isspace (g_utf8_get_char (p)))
		       p = g_utf8_next_char (p);

		v = format_match_decimal_number_with_locale
			(p, &family, curr, thousand, decimal);

		g_string_free (decimal, TRUE);
		g_string_free (thousand, TRUE);
		g_string_free (curr, TRUE);

		if (v == NULL)
			v = format_match_number
				(p, NULL,
				 sheet_date_conv (ei->pos->sheet));

		if (v != NULL)
			return v;
		return value_new_error_VALUE (ei->pos);
	}
}

/***************************************************************************/

static GnmFuncHelp const help_substitute[] = {
        { GNM_FUNC_HELP_NAME, F_("SUBSTITUTE:@{text} with all occurrences of @{old} replaced by @{new}")},
        { GNM_FUNC_HELP_ARG, F_("text:original text")},
        { GNM_FUNC_HELP_ARG, F_("old:string to be replaced")},
        { GNM_FUNC_HELP_ARG, F_("new:replacement string")},
        { GNM_FUNC_HELP_ARG, F_("num:if @{num} is specified and a number "
				"only the @{num}th occurrence of @{old} is replaced")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=SUBSTITUTE(\"United Nations Educational, Scientific and Cultural Organization\",\"ation\",\"-5-\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=SUBSTITUTE(\"United Nations Educational, Scientific and Cultural Organization\",\"ation\",\"-5-\",2)" },
        { GNM_FUNC_HELP_SEEALSO, "REPLACE,TRIM"},
        { GNM_FUNC_HELP_END}
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
        { GNM_FUNC_HELP_NAME, F_("DOLLAR:@{num} formatted as currency")},
        { GNM_FUNC_HELP_ARG, F_("num:number")},
        { GNM_FUNC_HELP_ARG, F_("decimals:decimals")},
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=DOLLAR(12345)" },
        { GNM_FUNC_HELP_SEEALSO, "FIXED,TEXT,VALUE"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_dollar (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	GOFormat *sf;
	gnm_float p10;
	GnmValue *v;
	char *s;
        gnm_float number = value_get_as_float (argv[0]);
        gnm_float decimals = argv[1] ? value_get_as_float (argv[1]) : 2;
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
	s = format_value (sf, v, -1,
			  sheet_date_conv (ei->pos->sheet));
	value_release (v);
	go_format_unref (sf);

	g_string_free (fmt_str, TRUE);

	return value_new_string_nocopy (s);
}

/***************************************************************************/

static GnmFuncHelp const help_search[] = {
        { GNM_FUNC_HELP_NAME, F_("SEARCH:the location of the @{search} string within "
				 "@{text} after position @{start}")},
        { GNM_FUNC_HELP_ARG, F_("search:search string")},
        { GNM_FUNC_HELP_ARG, F_("text:search field")},
        { GNM_FUNC_HELP_ARG, F_("start:starting position, defaults to 1")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("@{search} may contain wildcard characters (*) and "
					"question marks (?). A question mark matches any "
					"single character, and a wildcard matches any "
					"string including the empty string. To search for "
					"* or ?, precede the symbol with ~.")},
	{ GNM_FUNC_HELP_NOTE, F_("This search is not case sensitive.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{search} is not found, SEARCH returns #VALUE!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{start} is less than one or it is greater than "
				 "the length of @{text}, SEARCH returns #VALUE!") },
	{ GNM_FUNC_HELP_EXCEL, F_("This function is Excel compatible.") },
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCH(\"c\",\"Canc\xc3\xban\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCH(\"c\",\"Canc\xc3\xban\",2)" },
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCH(\"c*c\",\"Canc\xc3\xban\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCH(\"c*c\",\"Canc\xc3\xban\",2)" },
        { GNM_FUNC_HELP_SEEALSO, "FIND,SEARCHB"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_search (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *needle = value_peek_string (argv[0]);
	char const *haystack = value_peek_string (argv[1]);
	gnm_float start = argv[2] ? value_get_as_float (argv[2]) : 1;
	int res;

	if (start < 1 || start >= INT_MAX)
		return value_new_error_VALUE (ei->pos);

	res = gnm_excel_search_impl (needle, haystack, (int)start - 1);
	return res == -1
		? value_new_error_VALUE (ei->pos)
		: value_new_int (1 + res);
}

/***************************************************************************/

static GnmFuncHelp const help_searchb[] = {
        { GNM_FUNC_HELP_NAME, F_("SEARCHB:the location of the @{search} string within "
				 "@{text} after byte position @{start}")},
        { GNM_FUNC_HELP_ARG, F_("search:search string")},
        { GNM_FUNC_HELP_ARG, F_("text:search field")},
        { GNM_FUNC_HELP_ARG, F_("start:starting byte position, defaults to 1")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("@{search} may contain wildcard characters (*) and "
					"question marks (?). A question mark matches any "
					"single character, and a wildcard matches any "
					"string including the empty string. To search for "
					"* or ?, precede the symbol with ~.")},
	{ GNM_FUNC_HELP_NOTE, F_("This search is not case sensitive.") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{search} is not found, SEARCHB returns #VALUE!") },
	{ GNM_FUNC_HELP_NOTE, F_("If @{start} is less than one or it is greater than "
				 "the byte length of @{text}, SEARCHB returns #VALUE!") },
	{ GNM_FUNC_HELP_NOTE, F_("The semantics of this function is subject to change as various applications implement it.")},
	{ GNM_FUNC_HELP_EXCEL, F_("While this function is syntactically Excel compatible, "
				  "the differences in the underlying text encoding will usually yield different results.")},
	{ GNM_FUNC_HELP_ODF, F_("While this function is OpenFormula compatible, most of its behavior is, at this time, implementation specific.")},
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCHB(\"n\",\"Canc\xc3\xban\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCHB(\"n\",\"Canc\xc3\xban\",4)" },
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCHB(\"n\",\"Canc\xc3\xban\",6)" },
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCHB(\"n*n\",\"Canc\xc3\xban\")" },
        { GNM_FUNC_HELP_EXAMPLES, "=SEARCHB(\"n*n\",\"Canc\xc3\xban\",4)" },
        { GNM_FUNC_HELP_SEEALSO, "FINDB,SEARCH"},
        { GNM_FUNC_HELP_END}
};

static GnmValue *
gnumeric_searchb (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *needle = value_peek_string (argv[0]);
	char const *haystack = value_peek_string (argv[1]);
	gnm_float start = argv[2] ? value_get_as_float (argv[2]) : 1;
	size_t istart;
	GORegexp r;

	if (start < 1 || start >= INT_MAX || start > strlen (haystack))
		return value_new_error_VALUE (ei->pos);
	/* Make istart zero-based.  */
	istart = (int)(start - 1);

	if (istart > 0)
		istart = g_utf8_next_char(haystack + istart - 1) - haystack;

	if (gnm_regcomp_XL (&r, needle, GO_REG_ICASE, FALSE, FALSE) == GO_REG_OK) {
		GORegmatch rm;

		switch (go_regexec (&r, haystack + istart, 1, &rm, 0)) {
		case GO_REG_NOMATCH:
			break;
		case GO_REG_OK:
			go_regfree (&r);
			return value_new_int
				(1 + istart + rm.rm_so);
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
	{ GNM_FUNC_HELP_NAME, F_("ASC:text with full-width katakana and ASCII characters converted to half-width")},
	{ GNM_FUNC_HELP_ARG, F_("text:string")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("ASC converts full-width katakana and ASCII characters to half-width equivalent characters, copying all others.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The distinction between half-width and full-width characters is described in http://www.unicode.org/reports/tr11/.")},
	{ GNM_FUNC_HELP_EXCEL, F_("For most strings, this function has the same effect as in Excel.")},
	{ GNM_FUNC_HELP_NOTE, F_("While in obsolete encodings ASC used to translate between 2-byte and 1-byte characters, this is not the case in UTF-8.")},
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=ASC(\"\xef\xbc\xa1\xef\xbc\xa2\xef\xbc\xa3\")"},
	{ GNM_FUNC_HELP_SEEALSO, "JIS"},
	{ GNM_FUNC_HELP_END }
};

static gunichar
gnm_asc_half (gunichar c, GString *str)
{
	if (c < 0x2015)
		return c;
	if (c == 0x2015)
		return (0xff70);
	if (c == 0x2018)
		return (0x0060);
	if (c == 0x2019)
		return (0x0027);
	if (c == 0x201d)
		return (0x0022);
	if (c < 0x3001)
		return c;
	if (c == 0x3001)
		return (0xff64);
	if (c == 0x3002)
		return (0xff61);
	if (c == 0x300c)
		return (0xff62);
	if (c == 0x300d)
		return (0xff63);
	if (c == 0x309b)
		return (0xff9e);
	if (c == 0x309c)
		return (0xff9f);
	if (c < 0x30a1)
		return c;
	if (0x30a1 <= c && c <= 0x30aa)
		return (c%2 == 0) ? ((c - 0x30a2)/2 + 0xff71)
			: ((c - 0x30a1)/2 + 0xff67);
	if (c <= 0x30c2) {
		if (c%2 == 1)
			return ((c - 0x30ab)/2 + 0xff76);
		else {
			g_string_append_unichar
				(str, (c - 0x30ac)/2 + 0xff76);
			return 0xff9e;
		}
	}
	if (c == 0x30c3)
		return (0xff6f);
	if (c <= 0x30c9) {
		if (c%2 == 0)
			return ((c - 0x30c4)/2 + 0xff82);
		else {
			g_string_append_unichar
				(str, (c - 0x30c5)/2 + 0xff82);
			return 0xff9e;
		}
	}
	if (c <= 0x30ce)
		return (c - 0x30ca + 0xff85);
	if (c <= 0x30dd)
		switch (c%2) {
		case 0:
			return ((c - 0x30cf)/3 + 0xff8a);
		case 1:
			g_string_append_unichar
				(str, (c - 0x30d0)/3 + 0xff8a);
			return 0xff9e;
		case 2:
		default:
			g_string_append_unichar
				(str, (c - 0x30d1)/3 + 0xff8a);
			return 0xff9f;
		}
	if (c <= 30e2)
		return (c - 0x30de + 0xff8f);
	if (c <= 0x30e8) {
		if (c%2 == 0)
			return ((c - 0x30e4)/2 + 0xff94);
		else
			return ((c - 0x30e3)/2 + 0xff6c);
	}
	if (c <= 0x30ed)
		return (c - 0x30e9 + 0xff97);
	if (c == 0x30ef)
		return (0xff9c);
	if (c == 0x30f2)
		return (0xff66);
	if (c == 0x30f3)
		return (0xff9d);
	if (c == 0x30fb)
		return (0xff65);
	if (c == 0x30fc)
		return (0xff70);
	if (c < 0xff01)
		return c;
	if (c <= 0xff5e)
		return (c - 0xff01 + 0x0021);
	if (c == 0xffe5)
		return (0x005c);
	return c;
}

static GnmValue *
gnumeric_asc (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *peek = value_peek_string (argv[0]);
	GString *str = g_string_new (NULL);

	while ((*peek) != '\0') {
		gunichar wc = gnm_asc_half (g_utf8_get_char (peek), str);
		g_string_append_unichar (str, wc);
		peek = g_utf8_next_char(peek);
	}

	return value_new_string_nocopy (g_string_free (str, FALSE));
}

/***************************************************************************/

static GnmFuncHelp const help_jis[] = {
	{ GNM_FUNC_HELP_NAME, F_("JIS:text with half-width katakana and ASCII characters converted to full-width")},
	{ GNM_FUNC_HELP_ARG, F_("text:original text")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("JIS converts half-width katakana and ASCII characters "
					"to full-width equivalent characters, copying all others.")},
	{ GNM_FUNC_HELP_DESCRIPTION, F_("The distinction between half-width and full-width characters "
					"is described in http://www.unicode.org/reports/tr11/.")},
	{ GNM_FUNC_HELP_EXCEL, F_("For most strings, this function has the same effect as in Excel.")},
	{ GNM_FUNC_HELP_NOTE, F_("While in obsolete encodings JIS used to translate between 1-byte "
				 "and 2-byte characters, this is not the case in UTF-8.")},
	{ GNM_FUNC_HELP_ODF, F_("This function is OpenFormula compatible.")},
	{ GNM_FUNC_HELP_EXAMPLES, "=JIS(\"ABC\")"},
	{ GNM_FUNC_HELP_SEEALSO, "ASC"},
	{ GNM_FUNC_HELP_END }
};

static gunichar
gnm_asc_full (gunichar c, gunichar fc)
{
	if (c < 0x0021)
		return c;
	if (c == 0x0022)
		return (0x201d);
	if (c == 0x0027)
		return (0x2019);
	if (c == 0x005c)
		return (0xffe5);
	if (c == 0x0060)
		return (0x2018);
	if (c <= 0x007e)
		return (c - 0x0021 + 0xff01);
	if (c < 0xff61)
		return c;
	if (c == 0xff61)
		return (0x3002);
	if (c == 0xff62)
		return (0x300c);
	if (c == 0xff63)
		return (0x300d);
	if (c == 0xff64)
		return (0x3001);
	if (c == 0xff65)
		return (0x30fb);
	if (c == 0xff66)
		return (0x30f2);
	if (c <= 0xff6b)
		return ((c - 0xff67)*2 + 0x30a1);
	if (c <= 0xff6e)
		return ((c - 0xff6c)*2 + 0x30e3);
	if (c == 0xff6f)
		return (0x30c3);
	if (c == 0xff70)
		return (0x30fc);
	if (c <= 0xff75)
		return ((c - 0xff71)*2 + 0x30a2);
	if (c <= 0xff81) {
		if (fc == 0xff9e)
			return ((c - 0xff76)*2 + 0x30ac);
		else
			return ((c - 0xff76)*2 + 0x30ab);
	}
	if (c <= 0xff84) {
		if (fc == 0xff9e)
			return ((c - 0xff82)*2 + 0x30c5);
		else
			return ((c - 0xff82)*2 + 0x30c4);
	}
	if (c <= 0xff89)
		return ((c - 0xff85)*2 + 0x30ca);
	if (c <= 0xff8e) {
		if (fc == 0xff9e)
			return ((c - 0xff8a)*3 + 0x30d0);
		else if (fc == 0xff9f)
			return ((c - 0xff8a)*3 + 0x30d1);
		else
			return ((c - 0xff8a)*3 + 0x30cf);
	}
	if (c <= 0xff93)
		return (c - 0xff8f + 0x30de);
	if (c <= 0xff96)
		return ((c - 0xff94)*2 + 0x30e4);
	if (c <= 0xff9b)
		return (c - 0xff97 + 0x30e9);
	if (c == 0xff9c)
		return (0x30ef);
	if (c == 0xff9d)
		return (0x30f3);
	if (c == 0xff9e)
		return (0x309b);
	if (c == 0xff9f)
		return (0x309c);
	return c;
}

static GnmValue *
gnumeric_jis (GnmFuncEvalInfo *ei, GnmValue const * const *argv)
{
	char const *peek = value_peek_string (argv[0]);
	GString *str = g_string_new (NULL);
	gunichar tc = g_utf8_get_char (peek);

	while ((*peek) != '\0') {
		gunichar fc;
		char const *next = g_utf8_next_char(peek);
		fc = g_utf8_get_char (next);
		g_string_append_unichar (str, gnm_asc_full (tc, fc));
		peek = next;
		tc = fc;
	}

	return value_new_string_nocopy (g_string_free (str, FALSE));
}
/***************************************************************************/
GnmFuncDescriptor const string_functions[] = {
        { "asc",       "s",                       help_asc,
	  gnumeric_asc, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "char",       "f",                       help_char,
	  gnumeric_char, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "unichar",    "f",                       help_unichar,
	  gnumeric_unichar, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
        { "clean",      "S",                         help_clean,
          gnumeric_clean, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "code",       "S",                         help_code,
	  gnumeric_code, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "unicode",    "S",                         help_unicode,
	  gnumeric_unicode, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
        { "concat", NULL,               help_concat,
	  NULL, gnumeric_concat,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "concatenate", NULL,               help_concatenate,
	  NULL, gnumeric_concatenate,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "dollar",     "f|f",               help_dollar,
	  gnumeric_dollar, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "encodeurl",      "S",                 help_encodeurl,
	  gnumeric_encodeurl, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "exact",      "SS",                 help_exact,
	  gnumeric_exact, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "find",       "SS|f",           help_find,
	  gnumeric_find, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "findb",       "SS|f",           help_findb,
	  gnumeric_findb, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "fixed",      "f|fb",        help_fixed,
	  gnumeric_fixed, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "jis",       "s",                       help_jis,
	  gnumeric_jis, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "left",       "S|f",             help_left,
	  gnumeric_left, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "leftb",       "S|f",             help_leftb,
	  gnumeric_leftb, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "len",        "S",                         help_len,
	  gnumeric_len, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "lenb",       "S",                         help_lenb,
	  gnumeric_lenb, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "lower",      "S",                         help_lower,
	  gnumeric_lower, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "mid",        "Sff",               help_mid,
	  gnumeric_mid, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "midb",        "Sff",               help_midb,
	  gnumeric_midb, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "numbervalue",      "SS",          help_numbervalue,
	  gnumeric_numbervalue, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_UNIQUE_TO_GNUMERIC, GNM_FUNC_TEST_STATUS_BASIC },
        { "proper",     "S",                         help_proper,
	  gnumeric_proper, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "replace",    "SffS",         help_replace,
	  gnumeric_replace, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "replaceb",    "SffS",         help_replaceb,
	  gnumeric_replaceb, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "rept",       "Sf",                    help_rept,
	  gnumeric_rept, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "right",      "S|f",             help_right,
	  gnumeric_right, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "rightb",       "S|f",             help_rightb,
	  gnumeric_rightb, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "search",     "SS|f",     help_search,
	  gnumeric_search, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "searchb",     "SS|f",     help_searchb,
	  gnumeric_searchb, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "substitute", "SSS|f",        help_substitute,
	  gnumeric_substitute, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "t",          "S",                        help_t_,
          gnumeric_t_, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "text",       "Ss",           help_text,
	  gnumeric_text, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "textjoin", NULL, help_textjoin,
	  NULL, gnumeric_textjoin,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_NO_TESTSUITE },
        { "trim",       "S",                         help_trim,
	  gnumeric_trim, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "upper",      "S",                         help_upper,
	  gnumeric_upper, NULL,
	  GNM_FUNC_SIMPLE, GNM_FUNC_IMPL_STATUS_COMPLETE, GNM_FUNC_TEST_STATUS_BASIC },
        { "value",      "S",                         help_value,
	  gnumeric_value, NULL,
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

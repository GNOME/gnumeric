/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * number-match.c: This file includes the support for matching
 * entered strings as numbers (by trying to apply one of the existing
 * cell formats).
 *
 * The idea is simple: we create a regular expression from the format
 * string that would match a value entered in that format.  Then, on
 * lookup we try to match the string against every regular expression
 * we have: if a match is found, then we decode the number using a
 * precomputed parallel-list of subexpressions.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include "number-match.h"
#include "formats.h"
#include "dates.h"
#include "numbers.h"
#include "gutils.h"
#include "datetime.h"
#include "style.h"
#include "format.h"
#include "value.h"
#include "mathfunc.h"
#include "str.h"

#undef DEBUG_NUMBER_MATCH

/*
 * Takes a list of strings (optionally include an * at the beginning
 * that gets stripped, for i18n purposes). and returns a regexp that
 * would match them
 */
static char *
create_option_list (char const *const *list)
{
	int len = 0;
	char const *const *p;
	char *res;

	for (p = list; *p; p++) {
		char const *v = _(*p);

		if (*v == '*')
			v++;
		len += strlen (v) + 1;
	}
	len += 5;

	res = g_malloc (len);
	res[0] = '(';
	res[1] = 0;
	for (p = list; *p; p++) {
		char const *v = _(*p);

		if (*v == '*')
			v++;

		strcat (res, v);
		if (*(p + 1))
		    strcat (res, "|");
	}
	strcat (res, ")");

	return res;
}

typedef enum {
	MATCH_DAY_FULL 		= 1,
	MATCH_DAY_NUMBER	= 2,
	MATCH_MONTH_FULL	= 3,
	MATCH_MONTH_SHORT	= 4,
	MATCH_MONTH_NUMBER	= 5,
	MATCH_YEAR_FULL		= 6,
	MATCH_YEAR_SHORT	= 7,
	MATCH_HOUR		= 8,
	MATCH_MINUTE		= 9,
	MATCH_SECOND		= 10,
	MATCH_AMPM		= 11,
	MATCH_NUMBER		= 12,
	MATCH_NUMBER_DECIMALS	= 13,
	MATCH_PERCENT		= 14,
	MATCH_SKIP		= 15,
	MATCH_STRING_CONSTANT	= 16,
} MatchType;

static void
char_to_re (char dst[3], char c)
{
	switch (c) {
	case '^': case '$': case '.': case '[':
	case '+': case '*': case '?':
	case '\\':
		dst[0] = '\\';
		dst[1] = c;
		dst[2] = 0;
		break;

	default:
		dst[0] = c;
		dst[1] = 0;
	}
}

#define append_type(t) do { guint8 x = t; match_types = g_byte_array_append (match_types, &x, 1); } while (0)

static char *
format_create_regexp (unsigned char const *format, GByteArray **dest)
{
	GString *regexp;
	GByteArray *match_types;
	char *str;
	gboolean hour_seen = FALSE;
	char re_thousands_sep[3], re_decimal[3];

	g_return_val_if_fail (format != NULL, NULL);

	char_to_re (re_thousands_sep, format_get_thousand ());
	char_to_re (re_decimal, format_get_decimal ());

	regexp = g_string_new ("");
	match_types = g_byte_array_new ();

	for (; *format; format++) {
		switch (*format) {
		case '*':
			if (format[1]) {
				g_string_append_c (regexp, '[');
				g_string_append_c (regexp, format[1]);
				g_string_append_c (regexp, ']');
				g_string_append_c (regexp, '*');
				format++;
			}
			break;

		case 'P': case 'p':
			if (tolower (*(format + 1)) == 'm')
				format++;
			break;

		case '\\': {
			char buf[3];
			if (format[1] != '\0')
				format++;
			char_to_re (buf, *format);
			g_string_append (regexp, buf);
			break;
		}

		case '[' :
			/* Currency symbol */
			if (format[1] == '$') {
				for (format += 2; *format && *format != ']' ; ++format)
					g_string_append_c (regexp, *format);
				if (*format == ']')
					++format;
				break;
			}

		case '^':
		case '|':
		case ']' :
		case '$' :
		case '£' :
		case '¥' :
		case '¤' :
			g_string_append_c (regexp, '\\');
			g_string_append_c (regexp, *format);
			break;

		case '%':
			g_string_append (regexp, "%");
			append_type (MATCH_PERCENT);
			break;

		case '(':
		case ')':
			g_string_append_c (regexp, '\\');
			g_string_append_c (regexp, *format);
			break;

		case '#': case '0': case '.': case '+': case '?': {
			gboolean include_sep = FALSE, include_decimal = FALSE;

			while (*format == '#' || *format == '0' || *format == '.' ||
			       *format == '-' || *format == 'E' || *format == 'e' ||
			       *format == '+' || *format == '?' || *format == ',') {
				if (*format == ',')
					include_sep = TRUE;
				else if (*format == '.')
					include_decimal = TRUE;
				format++;
			}
			format--;

			append_type (MATCH_NUMBER);
			if (include_sep) {
				/* Not strictly correct.
				 * There should be a limit of 1-3 digits.
				 * However, that creates problems when
				 * There are formats like
				 *  $#,##0.00
				 * but not
				 *  $###0.00
				 * as a result $1000 would not be recognized.
				 */
				g_string_append (regexp, "([-+]?[0-9]+(\\");
				g_string_append (regexp, re_thousands_sep);
				g_string_append (regexp, "[0-9]{3})*)");
				append_type (MATCH_SKIP);
			} else
				g_string_append (regexp, "([-+]?[0-9]+)");

			if (include_decimal) {
				g_string_append (regexp, "?(\\");
				g_string_append (regexp, re_decimal);
				g_string_append (regexp, "[0-9]+([Ee][-+]?[0-9]+)?)");
				append_type (MATCH_NUMBER_DECIMALS);
			}
			break;
		}

		case 'h':
		case 'H':
			hour_seen = TRUE;
			if (tolower (*(format + 1)) == 'h')
				format++;

			g_string_append (regexp, "([0-9][0-9]?)");
			append_type (MATCH_HOUR);
			break;

		case 'M':
		case 'm':
			if (hour_seen) {
				if (tolower (*(format + 1)) == 'm')
					format++;
				g_string_append (regexp, "([0-9][0-9]?)");
				append_type (MATCH_MINUTE);
				hour_seen = FALSE;
			} else {
				if (tolower (*(format + 1)) == 'm') {
					if (tolower (*(format + 2)) == 'm') {
						if (tolower (*(format + 3)) == 'm') {
							char *l;

							l = create_option_list (month_long);
							g_string_append (regexp, l);
							g_free (l);

							append_type (MATCH_MONTH_FULL);
							format++;
						} else {
							char *l;

							l = create_option_list (month_short);
							g_string_append (regexp, l);
							g_free (l);

							append_type (MATCH_MONTH_SHORT);
						}
						format++;
					} else {
						g_string_append (regexp, "([0-9][0-9]?)");
						append_type (MATCH_MONTH_NUMBER);
					}
					format++;
				} else {
					g_string_append (regexp, "([0-9][0-9]?)");
					append_type (MATCH_MONTH_NUMBER);
				}
			}
			break;

		case 's':
		case 'S':
			/* ICK!
			 * ICK!
			 * 'm' is ambiguous.  It can be months or minutes.
			 */
			{
				int l = match_types->len;
				if (l > 0 && match_types->data[l - 1] == MATCH_MONTH_NUMBER)
					match_types->data[l - 1] = MATCH_MINUTE;
			}

			if (tolower (*(format + 1) == 's'))
				format++;
			g_string_append (regexp, "([0-9][0-9]?)");
			append_type (MATCH_SECOND);
			break;

		case 'D':
		case 'd':
			if (tolower (*(format + 1) == 'd')) {
				if (tolower (*(format + 2) == 'd')) {
					if (tolower (*(format + 3) == 'd')) {
						char *l;

						l = create_option_list (day_long);
						g_string_append (regexp, l);
						g_free (l);

						append_type (MATCH_DAY_FULL);
						format++;
					} else {
						char *l;

						l = create_option_list (day_short);
						g_string_append (regexp, l);
						g_free (l);
					}
					format++;
				} else {
					g_string_append (regexp, "([0-9][0-9]?)");
					append_type (MATCH_DAY_NUMBER);
				}
				format++;
			} else {
				g_string_append (regexp, "([0-9][0-9]?)");
				append_type (MATCH_DAY_NUMBER);
			}
			break;

		case 'Y':
		case 'y':
			if (tolower (*(format + 1) == 'y')) {
				if (tolower (*(format + 2) == 'y')) {
					if (tolower (*(format + 3) == 'y')) {
						g_string_append (regexp, "([0-9][0-9][0-9][0-9])");
						append_type (MATCH_YEAR_FULL);
						format++;
					}
					format++;
				} else {
					g_string_append (regexp, "([0-9][0-9]?)");
					append_type (MATCH_YEAR_SHORT);
				}
				format++;
			} else {
				g_string_append (regexp, "([0-9][0-9]?)");
				append_type (MATCH_YEAR_SHORT);
			}
			break;

		case ';':
			/* TODO : Is it ok to only match the first entry ?? */
			while (*format)
				format++;
			format--;
			break;

		case 'A': case 'a':
			if (*(format + 1) == 'm' || *(format + 1) == 'M') {
				if (*(format + 2) == '/') {
					if (*(format + 3) == 'P' || *(format + 3) == 'p') {
						if (*(format + 4) == 'm' || *(format + 4) == 'M') {
							format++;
						}
						format++;
					}
					format++;
				}
				format++;
			}
			g_string_append (regexp, "([Aa]|[Pp])[Mm]?");
			append_type (MATCH_AMPM);
			break;

			/* Matches a string */
		case '"': {
			const unsigned char *p, *q;

			for (p = format + 1; *p && *p != '"'; p++)
				; /* Nothing */

			if (*p != '"')
				goto error;

			for (q = format + 1; q < p; q++) {
				switch (*q) {
				case '\\': case '$': case '^': case '.':
				case '+': case '*': case '?': case '|':
				case '[': case ']':
					g_string_append_c (regexp, '\\');
					/* Fall through.  */
				default:
					g_string_append_c (regexp, *q);
				}
			}

			format = p;
			break;
		}

		case '@':
			g_string_append (regexp, "(.*)");
			append_type (MATCH_STRING_CONSTANT);
			break;

		case '_':
			if (format[1]) {
				g_string_append (regexp, "[ ]?");
				format++;
			}
			break;

		/* these were here explicitly before adding default.
		 * Leave them explicit for now as documentation.
		 */
		case ':':
		case '/':
		case '-':
		case ' ':
		default :
			g_string_append_c (regexp, *format);
		}
	}

	g_string_prepend_c (regexp, '^');
	g_string_append_c (regexp, '$');

	str = g_strdup (regexp->str);
	g_string_free (regexp, TRUE);
	*dest = match_types;

	return str;
 error:
	g_string_free (regexp, TRUE);
	g_byte_array_free (match_types, TRUE);
	return NULL;
}

static void
print_regex_error (int ret)
{
	switch (ret) {
	case REG_BADBR:
		fprintf (stderr,
			 "There was an invalid `\\{...\\}' construct in the regular\n"
			 "expression.  A valid `\\{...\\}' construct must contain either a\n"
			 "single number, or two numbers in increasing order separated by a\n"
			 "comma.\n");
		break;

	case REG_BADPAT:
		fprintf (stderr,
			 "There was a syntax error in the regular expression.\n");
		break;

	case REG_BADRPT:
		fprintf (stderr,
			 "A repetition operator such as `?' or `*' appeared in a bad\n"
			 "position (with no preceding subexpression to act on).\n");
		break;

	case REG_ECOLLATE:
		fprintf (stderr,
			 "The regular expression referred to an invalid collating element\n"
			 "(one not defined in the current locale for string collation).\n");
		break;

	case REG_ECTYPE:
		fprintf (stderr,
			 "The regular expression referred to an invalid character class name.\n");
		break;

	case REG_EESCAPE:
		fprintf (stderr,
			 "The regular expression ended with `\\'.\n");
		break;

	case REG_ESUBREG:
		fprintf (stderr,
			 "There was an invalid number in the `\\DIGIT' construct.\n");
		break;

	case REG_EBRACK:
		fprintf (stderr,
			 "There were unbalanced square brackets in the regular expression.\n");
		break;

	case REG_EPAREN:
		fprintf (stderr,
			 "An extended regular expression had unbalanced parentheses, or a\n"
			 "basic regular expression had unbalanced `\\(' and `\\)'.\n");
		break;

	case REG_EBRACE:
		fprintf (stderr,
			 "The regular expression had unbalanced `\\{' and `\\}'.\n");
		break;

#ifdef REG_EBOL
	case REG_EBOL:
		fprintf (stderr, "Found ^ not at the beginning.\n");
		break;
#endif

#ifdef REG_EEOL
	case REG_EEOL:
		fprintf (stderr, "Found $ not at the end.\n");
		break;
#endif

	case REG_ERANGE:
		fprintf (stderr,
			 "One of the endpoints in a range expression was invalid.\n");
		break;

	case REG_ESPACE:
		fprintf (stderr,
			 "`regcomp' ran out of memory.\n");
		break;

	default:
		fprintf (stderr, "regexp error %d\n", ret);
	}
}

static GSList *format_match_list = NULL;
static GSList *format_failed_match_list = NULL;

void
format_match_release (StyleFormat *fmt)
{
	if (fmt->regexp_str != NULL) {
		g_free (fmt->regexp_str);
		regfree (&fmt->regexp);
		g_byte_array_free (fmt->match_tags, TRUE);
	}
}

gboolean
format_match_create (StyleFormat *fmt)
{
	GByteArray *match_tags;
	char *regexp;
	regex_t r;
	int ret;

	g_return_val_if_fail (fmt != NULL, FALSE);
	g_return_val_if_fail (fmt->regexp_str == NULL, FALSE);
	g_return_val_if_fail (fmt->match_tags == NULL, FALSE);
	g_return_val_if_fail (strcmp (fmt->format, "General"), FALSE);

	regexp = format_create_regexp (fmt->format, &match_tags);
	if (!regexp) {
		fmt->regexp_str = NULL;
		fmt->match_tags = NULL;
		return FALSE;
	}

	ret = regcomp (&r, regexp, REG_EXTENDED | REG_ICASE);
	if (ret != 0) {
		g_warning ("expression %s\nproduced:%s\n", fmt->format, regexp);
		print_regex_error (ret);
		g_free (regexp);
		return FALSE;
	}

	fmt->regexp_str = regexp;
	fmt->regexp     = r;
	fmt->match_tags = match_tags;

	return TRUE;
}

/*
 * Initialize temporarily with statics.  The real versions from the locale
 * will be setup in constants_init
 */
char const *gnumeric_err_NULL  = "#NULL!";
char const *gnumeric_err_DIV0  = "#DIV/0!";
char const *gnumeric_err_VALUE = "#VALUE!";
char const *gnumeric_err_REF   = "#REF!";
char const *gnumeric_err_NAME  = "#NAME?";
char const *gnumeric_err_NUM   = "#NUM!";
char const *gnumeric_err_NA    = "#N/A";
char const *gnumeric_err_RECALC= "#RECALC!";

static struct gnumeric_error_info
{
	char const *str;
	int len;
} gnumeric_error_data[8];

static char const *
gnumeric_error_init (int const indx, char const * str)
{
	const int gnumeric_error_data_count =
		sizeof (gnumeric_error_data) / sizeof (struct gnumeric_error_info);

	g_return_val_if_fail (indx >= 0, str);
	g_return_val_if_fail (indx < gnumeric_error_data_count, str);

	gnumeric_error_data[indx].str = str;
	gnumeric_error_data[indx].len = strlen (str);
	return str;
}

/*
 * value_is_error : Check to see if a string begins with one of the magic
 * error strings.
 *
 * @str : The string to test
 *
 * returns : an error if there is one, or NULL.
 */
static Value *
value_is_error (char const * const str)
{
	int i = sizeof (gnumeric_error_data) / sizeof (struct gnumeric_error_info);

	g_return_val_if_fail (str != NULL, NULL);

	while (--i >= 0) {
		int const len = gnumeric_error_data[i].len;
		if (strncmp (str, gnumeric_error_data[i].str, len) == 0)
			return value_new_error (NULL, gnumeric_error_data[i].str);
	}
	return NULL;
}

/*
 * Loads the initial formats that we will recognize
 */
void
format_match_init (void)
{
	int i;
	StyleFormat *fmt;

	currency_date_format_init ();

	for (i = 0; cell_formats[i]; i++) {
		char const * const * p = cell_formats[i];

		for (; *p; p++) {
			/*  do not include text formats in the standard set */
			if (!strcmp ("@", *p))
				continue;

			fmt = style_format_new_XL (*p, FALSE);
			if (fmt->regexp_str != NULL) {
				/* TODO : we could hash the regexp and only
				 * check those that are not already in use.  We
				 * could also keep track of the ones that
				 * General would match.  and avoid putting them
				 * in the list too.
				 */
				format_match_list = g_slist_append (format_match_list, fmt);
			} else {
				format_failed_match_list = g_slist_append (format_failed_match_list, fmt);
			}
		}
	}

	i = 0;
	gnumeric_err_NULL	= gnumeric_error_init (i++, _("#NULL!"));
	gnumeric_err_DIV0	= gnumeric_error_init (i++, _("#DIV/0!"));
	gnumeric_err_VALUE	= gnumeric_error_init (i++, _("#VALUE!"));
	gnumeric_err_REF	= gnumeric_error_init (i++, _("#REF!"));
	gnumeric_err_NAME	= gnumeric_error_init (i++, _("#NAME?"));
	gnumeric_err_NUM	= gnumeric_error_init (i++, _("#NUM!"));
	gnumeric_err_NA		= gnumeric_error_init (i++, _("#N/A"));
	gnumeric_err_RECALC	= gnumeric_error_init (i++, _("#RECALC!"));
}

void
format_match_finish (void)
{
	GSList *l;

	for (l = format_match_list; l; l = l->next)
		style_format_unref (l->data);
	g_slist_free (format_match_list);

	for (l = format_failed_match_list; l; l = l->next)
		style_format_unref (l->data);
	g_slist_free (format_failed_match_list);

	currency_date_format_shutdown ();
}

/*
 * table_lookup:
 *
 * Looks the string in the table passed
 */
static int
table_lookup (char const *str, char const *const *table)
{
	char const *const *p = table;
	int i = 0;

	for (p = table; *p; p++, i++) {
		char const *v  = *p;
		char const *iv = _(*p);

		if (*v == '*') {
			v++;
			iv++;
		}

		if (g_strcasecmp (str, v) == 0)
			return i;

		if (g_strcasecmp (str, iv) == 0)
			return i;
	}

	return -1;
}

/*
 * extract_text:
 *
 * Returns a newly allocated string which is a region from
 * STR.   The ranges are defined in the regmatch_t variable MP
 * in the fields rm_so and rm_eo
 */
static char *
extract_text (char const *str, const regmatch_t *mp)
{
	char *p;

	p = g_malloc (mp->rm_eo - mp->rm_so + 1);
	strncpy (p, &str[mp->rm_so], mp->rm_eo - mp->rm_so);
	p[mp->rm_eo - mp->rm_so] = 0;

	return p;
}

/*
 * Given a number of matches described by MP on S,
 * compute the number based on the information on ARRAY
 *
 * Currently the code cannot mix a MATCH_NUMBER with any
 * of the date/time matching.
 */
static Value *
compute_value (char const *s, const regmatch_t *mp,
	       GByteArray *array)
{
	int const len = array->len;
	gnum_float number = 0.0;
	gchar *data = array->data;
	gboolean percentify = FALSE;
	gboolean is_number  = FALSE;
	gboolean is_pm      = FALSE;
	gboolean is_explicit_am = FALSE;
	gboolean is_neg = FALSE;
	int i;
	int month, day, year, year_short;
	int hours, minutes;
	gnum_float seconds;

	char const thousands_sep = format_get_thousand ();
	char const decimal = format_get_decimal ();

	month = day = year = year_short = -1;
	hours = minutes = -1;
	seconds = -1.;

	for (i = 0; i < len; ) {
		MatchType type = *(data++);
		char *str;

		str = extract_text (s, &mp[++i]);

#ifdef DEBUG_NUMBER_MATCH
		printf ("Item[%d] = \'%s\' is a %d\n", i, str, type);
#endif
		switch (type) {
		case MATCH_MONTH_FULL:
			month = table_lookup (str, month_long);
			if (month == -1) {
				g_free (str);
				return NULL;
			}
			month++;
			break;

		case MATCH_MONTH_NUMBER:
			month = atoi (str);
			break;

		case MATCH_MONTH_SHORT:
			month = table_lookup (str, month_short);
			if (month == -1) {
				g_free (str);
				return NULL;
			}
			month++;
			break;

		case MATCH_DAY_FULL:
			/* FIXME: handle weekday */
			break;

		case MATCH_DAY_NUMBER:
			day = atoi (str);
			break;

		case MATCH_NUMBER:
			if (*str != '\0') {
				char *ptr = str;

				switch (*ptr) {
				case '-':
					is_neg = TRUE;
					ptr++;
					break;
				case '+':
					ptr++;
					/* Fall through.  */
				default:
					is_neg = FALSE;
				}

				number = 0.;
				/* FIXME: this loop is bogus.  */
				do {
					int thisnumber;
					if (number > DBL_MAX / 1000.0) {
						g_free (str);
						return NULL;
					}

					number *= 1000.0;

					errno = 0; /* strtol sets errno, but does not clear it.  */
					thisnumber = strtoul (ptr, &ptr, 10);
					if (errno == ERANGE) {
						g_free (str);
						return NULL;
					}

					number += thisnumber;
				} while (*(ptr++) == thousands_sep);
				is_number = TRUE;
				if (is_neg) number = -number;
			}
			break;

		case MATCH_NUMBER_DECIMALS: {
			char *exppart = NULL;
			if (*str == decimal) {
				char *end;
				errno = 0; /* strtod sets errno, but does not clear it.  */
				if (seconds < 0) {
					gnum_float fraction;

					for (end = str; *end && *end != 'e' && *end != 'E'; )
						end++;
					if (*end) {
						exppart = end + 1;
						*end = 0;
					}

					fraction = strtod (str, &end);
					if (is_neg)
						number -= fraction;
					else
						number += fraction;
					is_number = TRUE;
				} else
					seconds += strtod (str, &end);
			}
			if (exppart) {
				char *end;
				int exponent;

				errno = 0; /* strtol sets errno, but does not clear it.  */
				exponent = strtol (exppart, &end, 10);
				number *= gpow10 (exponent);
			}
			break;
		}

		case MATCH_HOUR:
			hours = atoi (str);
			break;

		case MATCH_MINUTE:
			minutes = atoi (str);
			break;

		case MATCH_SECOND:
			seconds = atoi (str);
			break;

		case MATCH_PERCENT:
			percentify = TRUE;
			break;

		case MATCH_YEAR_FULL:
			year = atoi (str);
			break;

		case MATCH_YEAR_SHORT:
			year_short = atoi (str);
			break;

		case MATCH_AMPM:
			if (tolower ((unsigned char) *str) == 'p')
				is_pm = TRUE;
			else
				is_explicit_am = TRUE;
			break;

		case MATCH_SKIP:
			break;

		case MATCH_STRING_CONSTANT:
			return value_new_string_str (string_get_nocopy (str));

		default :
			g_warning ("compute_value: This should not happen\n");
			break;
		}

		g_free (str);
	}

	if (is_number) {
		if (percentify)
			number *= 0.01;
		return value_new_float (number);
	}

	if (!(year == -1 && month == -1 && day == -1)) {
		time_t t = time (NULL);
		GDate *date;
		struct tm *tm = localtime (&t);
		if (year == -1) {
			if (year_short != -1) {
				/* Window of -75 thru +24 years. */
				/* (TODO: See what current
				 * version of MS Excel uses.) */
				/* Earliest allowable interpretation
				 * is 75 years ago. */
				int earliest_ccyy
					= tm->tm_year + 1900 - 75;
				int earliest_cc = earliest_ccyy / 100;

				g_return_val_if_fail (year_short >= 0 &&
						      year_short <= 99,
						      NULL);
				year = earliest_cc * 100 + year_short;
				/*
				 * Our first guess at year has the same
				 * cc part as EARLIEST_CCYY, so is
				 * guaranteed to be in [earliest_ccyy -
				 * 99, earliest_ccyy + 99].  The yy
				 * part is of course year_short.
				 */
				if (year < earliest_ccyy)
					year += 100;
				/*
				 * year is now guaranteed to be in
				 * [earliest_ccyy, earliest_ccyy + 99],
				 * i.e. -75 thru +24 years from current
				 * year; and year % 100 == short_year.
				 */
			} else if (month != -1) {
				/* Window of -6 thru +5 months. */
				/* (TODO: See what current
				 * version of MS Excel uses.) */
				int earliest_yyymm
					= (tm->tm_year * 12 +
					   tm->tm_mon - 6);
				year = earliest_yyymm / 12;
				/* First estimate of yyy (i.e. years
				 * since 1900) is the yyy part of
				 * earliest_yyymm.  year*12+month-1 is
				 * guaranteed to be in [earliest_yyymm
				 * - 11, earliest_yyymm + 11].
				 */
				year += (year * 12 + month <=
					 earliest_yyymm);
				/* year*12+month-1 is now guaranteed
				 * to be in [earliest_yyymm,
				 * earliest_yyymm + 11], i.e.
				 * representing -6 thru +5 months
				 * from now.
				 */
				year += 1900;
				/* Finally convert from years since
				 * 1900 (yyy) to a proper 4-digit
				 * year.
				 */
			} else
				year = 1900 + tm->tm_year;
		}
		if (month == -1)
			month = tm->tm_mon + 1;
		if (day == -1)
			day = tm->tm_mday;

		if (year < 1900 || !g_date_valid_dmy (day, month, year))
			return NULL;

		date = g_date_new_dmy (day, month, year);
		number = datetime_g_to_serial (date);
		g_date_free (date);
	}

	if (seconds == -1 && minutes == -1 && hours == -1)
		return value_new_int (number);

	if (seconds == -1)
		seconds = 0;

	if (minutes == -1)
		minutes = 0;

	if (hours == -1)
		hours = 0;

	if (is_pm) {
		if (hours < 12)
			hours += 12;
	} else if (is_explicit_am && hours == 12)
		hours = 0;

	if (hours < 0 || hours > 23)
		return NULL;

	if (minutes < 0 || minutes > 59)
		return NULL;

	if (seconds < 0 || seconds > 59)
		return NULL;

	if (hours == 0 && minutes == 0 && seconds == 0)
		return value_new_int (number);

	number += (hours * 3600 + minutes * 60 + seconds) / (3600*24.0);
	return value_new_float (number);
}

/**
 * format_match_simple :
 * @s : A String to match against.
 *
 * Attempt to match the the supplied string as a simple value.
 */
Value *
format_match_simple (char const *text)
{
	/* Is it a boolean?  */
	if (0 == g_strcasecmp (text, _("TRUE")))
		return value_new_bool (TRUE);
	if (0 == g_strcasecmp (text, _("FALSE")))
		return value_new_bool (FALSE);

	/* Is it an error?  */
	if (*text == '#') {
		Value *err = value_is_error (text);
		if (err != NULL)
			return err;
	}

	/* Is it an integer?  */
	{
		char *end;
		long l;

		errno = 0; /* strtol sets errno, but does not clear it.  */
		l = strtol (text, &end, 10);
		if (text != end && errno != ERANGE && l == (int)l) {
			/* Allow and ignore spaces at the end.  */
			while (*end == ' ')
				end++;
			if (*end == '\0')
				return value_new_int ((int)l);
		}
	}

	/* Is it a double?  */
	{
		char *end;
		double d;

		errno = 0; /* strtod sets errno, but does not clear it.  */
		d = strtod (text, &end);
		if (text != end && errno != ERANGE && d == (gnum_float)d) {
			/* Allow and ignore spaces at the end.  */
			while (*end == ' ')
				end++;
			if (*end == '\0')
				return value_new_float ((gnum_float)d);
		}
	}

	return NULL;
}

#define NM 40

/**
 * format_match :
 *
 * @text : The text to parse
 * @cur_fmt : The current format for the value (potentially NULL)
 * @format : An optional place to store the target format
 *
 * Attempts to parse the supplied string to see if it matches a known value format.
 * If @format is supplied it will get a copy of the matching format with no
 * additional references.   The caller is responsible for releasing the
 * resulting value.  @matching_format does NOT have a reference added.
 */
Value *
format_match (char const *text, StyleFormat *cur_fmt,
	      StyleFormat **matching_format)
{
	Value  *v;
	GSList *l;
	regmatch_t mp[NM + 1];

	if (matching_format)
		*matching_format = NULL;

	if (text[0] == '\0')
		return value_new_empty ();

	/* If it begins with a '\'' it is a string */
	if (text[0] == '\'')
		return value_new_string (text + 1);

	if (cur_fmt) {
		if (style_format_is_text (cur_fmt))
			return value_new_string (text);
		if (cur_fmt->regexp_str != NULL &&
		    regexec (&cur_fmt->regexp, text, NM, mp, 0) != REG_NOMATCH &&
		    NULL != (v = compute_value (text, mp, cur_fmt->match_tags))) {
#ifdef DEBUG_NUMBER_MATCH
		{
			int i;
			printf ("matches expression: %s %s\n", cur_fmt->format, cur_fmt->regexp_str);
			for (i = 0; i < NM; i++) {
				char *p;

				if (mp[i].rm_so == -1)
					break;

				p = extract_text (text, &mp[i]);
				printf ("%d %d->%s\n", mp[i].rm_so, mp[i].rm_eo, p);
			}
		}
#endif

			if (matching_format)
				*matching_format = cur_fmt;
			return v;
		}
	}

	/* Check basic types */
	v = format_match_simple (text);
	if (v != NULL)
		return v;

	/* Fall back to checking the set of canned formats */
	for (l = format_match_list; l; l = l->next) {
		StyleFormat *fmt = l->data;
#ifdef DEBUG_NUMBER_MATCH
		printf ("test: %s \'%s\'\n", fmt->format, fmt->regexp_str);
#endif
		if (regexec (&fmt->regexp, text, NM, mp, 0) == REG_NOMATCH)
			continue;

#ifdef DEBUG_NUMBER_MATCH
		{
			int i;
			printf ("matches expression: %s %s\n", fmt->format, fmt->regexp_str);
			for (i = 0; i < NM; i++) {
				char *p;

				if (mp[i].rm_so == -1)
					break;

				p = extract_text (text, &mp[i]);
				printf ("%d %d->%s\n", mp[i].rm_so, mp[i].rm_eo, p);
			}
		}
#endif

		v = compute_value (text, mp, fmt->match_tags);

#ifdef DEBUG_NUMBER_MATCH
		if (v) {
			printf ("value = ");
			value_dump (v);
		} else
			printf ("unable to compute value\n");
#endif
		if (v != NULL) {
			if (matching_format)
				*matching_format = fmt;
			return v;
		}
	}

	return NULL;
}

/**
 * format_match_number :
 *
 * @text : The text to parse
 * @cur_fmt : The current format for the value (potentially NULL)
 * @format : An optional place to store the target format
 *
 * Attempts to parse the supplied string to see if it matches a known value format.
 * Will eventually use the current cell format in preference to canned formats.
 * If @format is supplied it will get a copy of the matching format with no
 * additional references.   The caller is responsible for releasing the
 * resulting value.  Will ONLY return numbers.
 */
Value *
format_match_number (char const *s, StyleFormat *current_format,
		     StyleFormat **format)
{
	Value *res = format_match (s, current_format, format);

	if (res != NULL) {
		if (VALUE_IS_NUMBER (res))
			return res;
		value_release (res);
	}
	return NULL;
}

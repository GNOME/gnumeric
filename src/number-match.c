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
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "number-match.h"

#include "gutils.h"
#include "style.h"
#include "gnm-format.h"
#include "value.h"
#include "mathfunc.h"
#include "str.h"
#include "numbers.h"
#include <goffice/utils/go-format-match.h>
#include <goffice/utils/format-impl.h>
#include <goffice/utils/regutf8.h>
#include <goffice/utils/datetime.h>
#include <goffice/utils/go-glib-extras.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <time.h>
#undef DEBUG_NUMBER_MATCH

static GSList *format_match_list = NULL;
static GSList *builtin_fmts = NULL;

/*
 * value_is_error : Check to see if a string begins with one of the magic
 * error strings.
 *
 * @str : The string to test
 *
 * returns : an error if there is one, or NULL.
 */
static GnmValue *
value_is_error (char const *str)
{
	GnmStdError e;

	for (e = (GnmStdError)0; e < GNM_ERROR_UNKNOWN; e++)
		if (0 == strcmp (str, value_error_name (e, TRUE)))
			return value_new_error_std (NULL, e);

	return NULL;
}

/*
 * Loads the initial formats that we will recognize
 */
void
format_match_init (void)
{
	int i;
	GSList		*ptr;
	GOFormat	*fmt;
	GOFormatElement	*entry;
	GHashTable	*unique = g_hash_table_new (g_str_hash, g_str_equal);

	currency_date_format_init ();

	for (i = 0; go_format_builtins[i]; i++) {
		char const * const *p = go_format_builtins[i];

		for (; *p; p++) {
			fmt = go_format_new_from_XL (*p, FALSE);
			builtin_fmts = g_slist_prepend (builtin_fmts, fmt);
			for (ptr = fmt->entries ; ptr != NULL ; ptr = ptr->next) {
				entry = ptr->data;
				/* TODO : * We could keep track of the regexps
				 * that General would match.  and avoid putting
				 * them in the list. */

				if (!entry->forces_text &&
				    NULL != entry->regexp_str &&
				    NULL == g_hash_table_lookup (unique, entry->regexp_str)) {
					format_match_list = g_slist_prepend (format_match_list, entry);
					g_hash_table_insert (unique, entry->regexp_str, entry);
				}
			}
		}
	}
	g_hash_table_destroy (unique);

	format_match_list = g_slist_reverse (format_match_list);
}

void
format_match_finish (void)
{
	go_slist_free_custom (builtin_fmts, (GFreeFunc) go_format_unref);
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

		if (g_ascii_strcasecmp (str, v) == 0)
			return i;

		if (g_ascii_strcasecmp (str, iv) == 0)
			return i;
	}

	return -1;
}

/*
 * extract_text:
 *
 * Returns a newly allocated string which is a region from
 * STR.   The ranges are defined in the GORegmatch variable MP
 * in the fields rm_so and rm_eo
 */
static char *
extract_text (char const *str, const GORegmatch *mp)
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
static GnmValue *
compute_value (char const *s, const GORegmatch *mp,
	       GByteArray *array, GODateConventions const *date_conv)
{
	int const len = array->len;
	gnm_float number = 0.0;
	guchar *data = array->data;
	gboolean percentify = FALSE;
	gboolean is_number  = FALSE;
	gboolean is_pm      = FALSE;
	gboolean is_explicit_am = FALSE;
	gboolean is_neg = FALSE;
	gboolean hours_are_cummulative   = FALSE;
	gboolean minutes_are_cummulative = FALSE;
	gboolean seconds_are_cummulative = FALSE;
	gboolean hours_set   = FALSE;
	gboolean minutes_set = FALSE;
	gboolean seconds_set = FALSE;
	int i;
	int month, day, year, year_short;
	int hours, minutes;
	gnm_float seconds;
	int numerator = 0, denominator = 1;

	GString const *thousands_sep = format_get_thousand ();
	GString const *decimal = format_get_decimal ();

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

		case MATCH_NUMERATOR:
			numerator = atoi (str);
			break;

		case MATCH_DENOMINATOR:
			denominator = atoi (str);
			if (denominator <= 0)
				return NULL;
			if (is_neg && numerator < 0)
				return NULL;

			is_number = TRUE;
			if (is_neg)
				number -= numerator / (gnm_float)denominator;
			else
				number += numerator / (gnm_float)denominator;
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
				while (1) {
					unsigned long thisnumber;
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

					if (strncmp (ptr, thousands_sep->str, thousands_sep->len) != 0)
						break;

					ptr += thousands_sep->len;
				}
				is_number = TRUE;
				if (is_neg) number = -number;
			}
			break;

		case MATCH_NUMBER_DECIMALS: {
			char *exppart = NULL;
			if (strncmp (str, decimal->str, decimal->len) == 0) {
				char *end;
				errno = 0; /* gnm_strto sets errno, but does not clear it.  */
				if (seconds < 0) {
					gnm_float fraction;

					for (end = str; *end && *end != 'e' && *end != 'E'; )
						end++;
					if (*end) {
						exppart = end + 1;
						*end = 0;
					}

					fraction = gnm_strto (str, &end);
					if (is_neg)
						number -= fraction;
					else
						number += fraction;
					is_number = TRUE;
				} else
					seconds += gnm_strto (str, &end);
			}
			if (exppart) {
				char *end;
				long exponent;

				errno = 0; /* strtol sets errno, but does not clear it.  */
				exponent = strtol (exppart, &end, 10);
				number *= gnm_pow10 (exponent);
			}
			break;
		}

		case MATCH_CUMMULATIVE_HOURS:
			hours_are_cummulative = TRUE;
			if (str[0] == '-') is_neg = TRUE;
		case MATCH_HOUR:
			hours_set = TRUE;
			hours = abs (atoi (str));
			break;

		case MATCH_CUMMULATIVE_MINUTES:
			minutes_are_cummulative = TRUE;
			if (str[0] == '-') is_neg = TRUE;
		case MATCH_MINUTE:
			minutes_set = TRUE;
			minutes = abs (atoi (str));
			break;

		case MATCH_CUMMULATIVE_SECONDS :
			seconds_are_cummulative = TRUE;
			if (str[0] == '-') is_neg = TRUE;
		case MATCH_SECOND:
			seconds_set = TRUE;
			seconds = abs (atoi (str));
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
			if (*str == 'p' || *str == 'P')
				is_pm = TRUE;
			else
				is_explicit_am = TRUE;
			break;

		case MATCH_SKIP:
			break;

		case MATCH_STRING_CONSTANT:
			return value_new_string_str (gnm_string_get_nocopy (str));

		default :
			g_warning ("compute_value: This should not happen.");
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
		static time_t lastt;
		static struct tm tm;
		GDate *date;

		if (t != lastt) {
			/*
			 * Since localtime is moderately expensive, do
			 * at most one call per second.  One per day
			 * would be enough but is harder to check for.
			 */
			lastt = t;
			tm = *localtime (&t);
		}

		if (year == -1) {
			if (year_short != -1) {
				/* Window of -75 thru +24 years. */
				/* (TODO: See what current
				 * version of MS Excel uses.) */
				/* Earliest allowable interpretation
				 * is 75 years ago. */
				int earliest_ccyy
					= tm.tm_year + 1900 - 75;
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
					= (tm.tm_year * 12 +
					   tm.tm_mon - 6);
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
				year = 1900 + tm.tm_year;
		}
		if (month == -1)
			month = tm.tm_mon + 1;
		if (day == -1)
			day = tm.tm_mday;

		if (year < 1900 || !g_date_valid_dmy (day, month, year))
			return NULL;

		date = g_date_new_dmy (day, month, year);
		number = datetime_g_to_serial (date, date_conv);
		g_date_free (date);
	}

	if (!seconds_set && !minutes_set && !hours_set)
		return value_new_int (number);

	if (!seconds_set)
		seconds = 0;

	if (!minutes_set)
		minutes = 0;

	if (!hours_set)
		hours = 0;

	if (!hours_are_cummulative) {
		if (is_pm) {
			if (hours < 12)
				hours += 12;
		} else if (is_explicit_am && hours == 12)
			hours = 0;
	}

	if ((hours < 0 || hours > 23) && !hours_are_cummulative)
		return NULL;

	if ((minutes < 0 || minutes > 59) && !minutes_are_cummulative)
		return NULL;

	if ((seconds < 0 || seconds > 59) && !seconds_are_cummulative)
		return NULL;

	if (hours == 0 && minutes == 0 && seconds == 0)
		return value_new_int (number);

	number += (hours * 3600 + minutes * 60 + seconds) / (3600*24.0);
	if (is_neg) number = -number;

	return value_new_float (number);
}

/**
 * format_match_simple :
 * @s : A String to match against.
 *
 * Attempt to match the the supplied string as a simple value.
 *
 * WARNING WARNING WARNING : This routine should NEVER be changed to match
 * 				VALUE_STRING that will break the parsers
 * 				handling of named expressions.
 */
GnmValue *
format_match_simple (char const *text)
{
	/* Is it a boolean?  */
	if (0 == g_ascii_strcasecmp (text, format_boolean (TRUE)))
		return value_new_bool (TRUE);
	if (0 == g_ascii_strcasecmp (text, format_boolean (FALSE)))
		return value_new_bool (FALSE);

	/* Is it an error?  */
	if (*text == '#') {
		GnmValue *err = value_is_error (text);
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
		gnm_float d;

		errno = 0; /* gnm_strto sets errno, but does not clear it.  */
		d = gnm_strto (text, &end);
		if (text != end && errno != ERANGE) {
			/* Allow and ignore spaces at the end.  */
			while (*end == ' ')
				end++;
			if (*end == '\0')
				return value_new_float (d);
		}
	}

	return NULL;
}

#define NM 40

/**
 * format_match :
 * @text    : The text to parse
 * @cur_fmt : The current format for the value (potentially NULL)
 * @date_conv: optional date convention
 *
 * Attempts to parse the supplied string to see if it matches a known value
 * format.  The caller is responsible for releasing the resulting value.
 **/
GnmValue *
format_match (char const *text, GOFormat *cur_fmt,
	      GODateConventions const *date_conv)
{
	GnmValue *v;
	GSList	 *ptr;
	GOFormatElement const *entry;
	GORegmatch mp[NM + 1];

	if (text[0] == '\0')
		return value_new_empty ();

	/* If it begins with a '\'' it is a string */
	if (text[0] == '\'')
		return value_new_string (text + 1);

	if (cur_fmt) {
		if (cur_fmt->family == GO_FORMAT_TEXT)
			return value_new_string (text);

		for (ptr = cur_fmt->entries; ptr != NULL ; ptr = ptr->next) {
			entry = ptr->data;
			if (!entry->forces_text &&
			    entry->regexp_str != NULL &&
			    go_regexec (&entry->regexp, text, NM, mp, 0) != REG_NOMATCH &&
			    NULL != (v = compute_value (text, mp, entry->match_tags, date_conv))) {
#ifdef DEBUG_NUMBER_MATCH
				int i;
				g_warning ("matches fmt: '%s' with regex '%s'\n", entry->format, entry->regexp_str);
				for (i = 0; i < NM; i++) {
					char *p;

					if (mp[i].rm_so == -1)
						break;

					p = extract_text (text, &mp[i]);
					g_print ("%d %d->%s\n", mp[i].rm_so, mp[i].rm_eo, p);
				}
#endif

				value_set_fmt (v, cur_fmt);
				return v;
			} else {
#ifdef DEBUG_NUMBER_MATCH
				g_print ("does not match expression: %s %s\n",
					 entry->format,
					 entry->regexp_str ? entry->regexp_str : "(null)");
#endif
			}
		}
	}

	/* Check basic types */
	v = format_match_simple (text);
	if (v != NULL)
		return v;

	/* Fall back to checking the set of canned formats */
	for (ptr = format_match_list; ptr; ptr = ptr->next) {
		entry = ptr->data;
#ifdef DEBUG_NUMBER_MATCH
		printf ("test: %s \'%s\'\n", entry->format, entry->regexp_str);
#endif
		if (go_regexec (&entry->regexp, text, NM, mp, 0) == REG_NOMATCH)
			continue;

#ifdef DEBUG_NUMBER_MATCH
		{
			int i;
			printf ("matches expression: %s %s\n", entry->format, entry->regexp_str);
			for (i = 0; i < NM; i++) {
				char *p;

				if (mp[i].rm_so == -1)
					break;

				p = extract_text (text, &mp[i]);
				printf ("%d %d->%s\n", mp[i].rm_so, mp[i].rm_eo, p);
			}
		}
#endif

		v = compute_value (text, mp, entry->match_tags, date_conv);

#ifdef DEBUG_NUMBER_MATCH
		if (v) {
			printf ("value = ");
			value_dump (v);
		} else
			printf ("unable to compute value\n");
#endif
		if (v != NULL) {
			value_set_fmt (v, entry->container);
			return v;
		}
	}

	return NULL;
}

/**
 * format_match_number :
 *
 * @text    : The text to parse
 * @cur_fmt : The current format for the value (potentially NULL)
 * @date_conv: optional date convention
 *
 * Attempts to parse the supplied string to see if it matches a known value format.
 * Will eventually use the current cell format in preference to canned formats.
 * If @format is supplied it will get a copy of the matching format with no
 * additional references.   The caller is responsible for releasing the
 * resulting value.  Will ONLY return numbers.
 */
GnmValue *
format_match_number (char const *text, GOFormat *cur_fmt,
		     GODateConventions const *date_conv)
{
	GnmValue *res = format_match (text, cur_fmt, date_conv);

	if (res != NULL) {
		if (VALUE_IS_NUMBER (res))
			return res;
		value_release (res);
	}
	return NULL;
}

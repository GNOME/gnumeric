/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* format.c - attempts to emulate excel's number formatting ability.
 * Copyright (C) 1998 Chris Lahey, Miguel de Icaza
 *
 * Redid the format parsing routine to make it accept more of the Excel
 * formats.  The number rendeing code from Chris has not been touched,
 * that routine is pretty good.
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
#include "gnumeric.h"
#include "numbers.h"
#include "format.h"

#include "style-color.h"
#include "expr.h"
#include "dates.h"
#include "value.h"
#include "parse-util.h"
#include "datetime.h"
#include "mathfunc.h"
#include "str.h"
#include "number-match.h"
#include "formats.h"

#include <locale.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#ifdef HAVE_LANGINFO_H
#    include <langinfo.h>
#endif

/***************************************************************************/

/* Points to the locale information for number display */
static struct lconv *lc = NULL;
static char *locale_currency = NULL; /* in UTF-8 */
static gboolean date_order_cached = FALSE;

char const *
gnumeric_setlocale (int category, char const *val)
{
	lc = NULL;
	g_free (locale_currency); locale_currency = NULL;
	date_order_cached = FALSE;
	return setlocale (category, val);
}

char
format_get_decimal (void)
{
	char res;
	if (lc == NULL)
		lc = localeconv ();

	/* NOTE : Use decimal_point _not_ mon_decimal_point.  strtognum uses this
	 * and we get very confused when they are different (eg ru_RU)
	 */
	res = lc->decimal_point[0];
	return (res != '\0') ? res : '.';
}

char
format_get_thousand (void)
{
	char res;
	if (lc == NULL)
		lc = localeconv ();

	res = lc->mon_thousands_sep[0];
	if (res != '\0')
		return res;

	/* Provide a decent default for countries using ',' as a decimal */
	if (format_get_decimal () != ',')
		return ',';
	return '.';
}

/**
 * format_get_currency :
 * @precedes : a pointer to a boolean which is set to TRUE if the currency
 * 		should precede
 * @space_sep: a pointer to a boolean which is set to TRUE if the currency
 * 		should have a space separating it from the the value
 *
 * Play with the default logic so that things come out nicely for the default
 * case.
 */
char const *
format_get_currency (gboolean *precedes, gboolean *space_sep)
{
	if (lc == NULL)
		lc = localeconv ();

	/* Use != 0 rather than == 1 so that CHAR_MAX (undefined) is true */
	if (precedes)
		*precedes = (lc->p_cs_precedes != 0);
	/* Use == 1 rather than != 0 so that CHAR_MAX (undefined) is false */
	if (space_sep)
		*space_sep = (lc->p_sep_by_space == 1);

	if (locale_currency == NULL) {
		if (lc->currency_symbol && *lc->currency_symbol) {
			locale_currency =
				g_locale_to_utf8 (lc->currency_symbol, -1,
						  NULL, NULL, NULL);
			if (!locale_currency)
				g_warning ("Failed to convert locale currency symbol \"%s\" to UTF-8.",
					   lc->currency_symbol);
		}
		if (!locale_currency)
			locale_currency = g_strdup ("$");
	}
	return locale_currency;
}

/*
 * format_month_before_day :
 *
 * A quick utility routine to guess whether the default date format
 * uses day/month or month/day
 */
gboolean
format_month_before_day (void)
{
#ifdef HAVE_LANGINFO_H
	static gboolean month_first = TRUE;

	if (!date_order_cached) {
		char const *ptr = nl_langinfo (D_FMT);

		date_order_cached = TRUE;
		month_first = TRUE;
		if (ptr)
			while (*ptr) {
				char c = *ptr++;
				if (c == 'd' || c == 'D') {
					month_first = FALSE;
					break;
				} else if (c == 'm' || c == 'M')
					break;
			}
	}

	return month_first;
#else
	static gboolean warning = TRUE;
	if (warning) {
		g_warning ("Incomplete locale library, dates will be month day year");
		warning = FALSE;
	}
	return TRUE;
#endif
}

/* Use comma as the arg separator unless the decimal point is a
 * comma, in which case use a semi-colon
 */
char
format_get_arg_sep (void)
{
	if (format_get_decimal () == ',')
		return ';';
	return ',';
}

char
format_get_col_sep (void)
{
	if (format_get_decimal () == ',')
		return '\\';
	return ',';
}
/***************************************************************************/

/* WARNING : Global */
static GHashTable *style_format_hash = NULL;

typedef struct {
        char const *format;
	gboolean    want_am_pm;
	gboolean    has_fraction;
        char        restriction_type;
        gnm_float  restriction_value;
	StyleColor *color;
} StyleFormatEntry;

/*
 * The returned string is newly allocated.
 *
 * Current format is an optional date specification followed by an
 * optional number specification.
 *
 * A date specification is an arbitrary sequence of characters (other
 * than '#', '0', '?', or '.') which is copied to the output.  The
 * standard date fields are substituted for.  If it ever finds an a or
 * a p it lists dates in 12 hour time, otherwise, it lists dates in 24
 * hour time.
 *
 * A number specification is as described in the relevant portions of
 * the excel formatting information.  Commas can currently only appear
 * at the end of the number specification.  Fractions are supported
 * but the parsing is not as nice as it should be.
 */


/*
 * Parses the year field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_year (GString *string, guchar const *format, struct tm const *time_split)
{
	char temp[5];
	int year = time_split->tm_year + 1900;

	if (format[1] != 'y' && format[1] != 'Y') {
		g_string_append_c (string, 'y');
		return 1;
	}

	if ((format[2] != 'y' && format[2] != 'Y') ||
	    (format[3] != 'y' && format[3] != 'Y')) {
		sprintf (temp, "%02d", year % 100);
		g_string_append (string, temp);
		return 2;
	}

	sprintf (temp, "%04d", year);
	g_string_append (string, temp);

	return 4;
}

/*
 * Parses the month field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_month (GString *string, int n, struct tm const *time_split)
{
	char temp[3];

	if (n == 1){
		sprintf (temp, "%d", time_split->tm_mon + 1);
		g_string_append (string, temp);
		return 1;
	}

	if (n == 2){
		sprintf (temp, "%02d", time_split->tm_mon + 1);
		g_string_append (string, temp);
		return 2;
	}

	if (n == 3){
		g_string_append (string, _(month_short[time_split->tm_mon]) + 1);
		return 3;
	}

	g_string_append (string, _(month_long[time_split->tm_mon]));
	return 4;
}

/*
 * Parses the day field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_day (GString *string, guchar const *format, struct tm const *time_split)
{
	char temp[3];

	if (format[1] != 'd' && format[1] != 'D') {
		sprintf (temp, "%d", time_split->tm_mday);
		g_string_append (string, temp);
		return 1;
	}

	if (format[2] != 'd' && format[2] != 'D') {
		sprintf (temp, "%02d", time_split->tm_mday);
		g_string_append (string, temp);
		return 2;
	}

	if (format[3] != 'd' && format[3] != 'D') {
		g_string_append (string, _(day_short[time_split->tm_wday]) + 1);
		return 3;
	}

	g_string_append (string, _(day_long[time_split->tm_wday]));

	return 4;
}

/*
 * Renders the hour.
 */
static void
append_hour (GString *string, int n, struct tm const *time_split,
	     gboolean want_am_pm)
{
	char *temp = g_alloca (n + 4 * sizeof (int));

	sprintf (temp, "%0*d", n,
		 want_am_pm
		 ? (((time_split->tm_hour + 11) % 12) + 1)
		 : time_split->tm_hour);
	g_string_append (string, temp);
}

/*
 * Renders the hour.
 */
static void
append_hour_elapsed (GString *string, struct tm const *time_split, int number)
{
	char buf[(DBL_MANT_DIG + DBL_MAX_EXP) * 2 + 1];
	double hours = number * 24. + time_split->tm_hour;
	snprintf (buf, sizeof (buf), "%.0f", hours);
	g_string_append (string, buf);
}

/*
 * Renders the number of minutes.
 */
static void
append_minute (GString *string, int n, struct tm const *time_split)
{
	char *temp = g_alloca (n + 4 * sizeof (int));
	sprintf (temp, "%0*d", n, time_split->tm_min);
	g_string_append (string, temp);
}

/*
 * Renders the number of minutes, in elapsed format
 */
static void
append_minute_elapsed (GString *string, struct tm const *time_split, int number)
{
	char buf[(DBL_MANT_DIG + DBL_MAX_EXP) * 2 + 1];
	double minutes = ((number * 24.) + time_split->tm_hour) * 60. + time_split->tm_min;
	snprintf (buf, sizeof (buf), "%.0f", minutes);
	g_string_append (string, buf);
}

/*
 * Renders the second field.
 */
static void
append_second (GString *string, int n, struct tm const *time_split)
{
	char *temp = g_alloca (n + 4 * sizeof (int));
	sprintf (temp, "%0*d", n, time_split->tm_sec);
	g_string_append (string, temp);
}

/*
 * Renders the second field in elapsed
 */
static void
append_second_elapsed (GString *string, int n, struct tm const *time_split, int number)
{
	char buf[(DBL_MANT_DIG + DBL_MAX_EXP) * 2 + 1];
	double seconds = (((number * 24. + time_split->tm_hour) * 60. + time_split->tm_min) * 60.) + time_split->tm_sec;
	snprintf (buf, sizeof (buf), "%.0f", seconds);
	g_string_append (string, buf);
}

static StyleFormatEntry *
format_entry_ctor (void)
{
	StyleFormatEntry *entry;

	entry = g_new (StyleFormatEntry, 1);
	entry->restriction_type = '*';
	entry->restriction_value = 0.;
	entry->want_am_pm = entry->has_fraction = FALSE;
	entry->color = NULL;
	return entry;
}

/**
 * format_entry_dtor :
 *
 * WARNING : do not call this for temporary formats generated for
 * 'General'.
 */
static void
format_entry_dtor (gpointer data, gpointer user_data)
{
	StyleFormatEntry *entry = data;
	if (entry->color != NULL) {
		style_color_unref (entry->color);
		entry->color = NULL;
	}
	g_free ((char *)entry->format);
	g_free (entry);
}

static void
format_entry_set_fmt (StyleFormatEntry *entry,
		      gchar const *begin,
		      gchar const *end)
{
	/* empty formats are General if there is a color, or a condition */
	entry->format = (begin != NULL && end != begin)
		? g_strndup (begin, end - begin)
		: strdup ((entry->color != NULL ||
			   entry->restriction_type != '*')
			  ? "General" : "");
}

static StyleColor * lookup_color (char const *str, char const *end);

/*
 * Since the Excel formating codes contain a number of ambiguities, this
 * routine does some analysis on the format first.  This routine should always
 * return, it cannot fail, in the worst case it should just downgrade to
 * simplistic formatting
 */
static void
format_compile (StyleFormat *format)
{
	gchar const *fmt, *real_start = NULL;
	StyleFormatEntry *entry = format_entry_ctor ();
	int num_entries = 1, counter = 0;
	GSList *ptr;

	format_match_create (format);
	for (fmt = format->format; *fmt ; fmt++) {
		if (NULL == real_start && '[' != *fmt)
			real_start = fmt;

		switch (*fmt) {
		case '[': {
			gchar const *begin = fmt + 1;
			gchar const *end = begin;

			/* find end checking for escapes but not quotes ?? */
			for (; end[0] != ']' && end[1] != '\0' ; ++end)
				if (*end == '\\')
					end++;

			/* Check for conditional */
			if (*begin == '<') {
				if (begin[1] == '=') {
					entry->restriction_type = ',';
					begin += 2;
				} else if (begin[1] == '>') {
					entry->restriction_type = '+';
					begin += 2;
				} else {
					entry->restriction_type = '<';
					begin++;
				}
			} else if (*begin == '>') {
				if (begin[1] == '=') {
					entry->restriction_type = '.';
					begin += 2;
				} else {
					entry->restriction_type = '>';
					begin++;
				}
			} else if (*begin == '=') {
				entry->restriction_type = '=';
			} else if (*begin != '$' && entry->color == NULL) {
				entry->color = lookup_color (begin, end);
				/* Only the first colour counts */
				if (NULL == entry->color) {
					if (NULL == real_start)
						real_start = fmt;
					continue;
				}
				fmt = end;
				continue;
			} else {
				if (NULL == real_start)
					real_start = fmt;
				continue;
			}
			fmt = end;

			/* fall back on 0 for errors */
			errno = 0;
			entry->restriction_value = strtognum (begin, (char **)&end);
			if (errno == ERANGE || begin == end)
				entry->restriction_value = 0.;
			break;
		}

		case '\\' :
			if (fmt[1] != '\0')
				fmt++; /* skip escaped characters */
			break;

		case '\'' :
		case '\"' : {
			/* skip quoted strings */
			char const match = *fmt;
			for (; fmt[0] != match && fmt[1] != '\0'; fmt++)
				if (*fmt == '\\')
					fmt++;
			break;
		}

		case '/':
			if (fmt[1] == '?' || (fmt[1] >= '0' && fmt[1] <= '9')) {
				entry->has_fraction = TRUE;
				fmt++;
			}
			break;

		case 'a': case 'A':
		case 'p': case 'P':
			if (fmt[1] == 'm' || fmt[1] == 'M')
				entry->want_am_pm = TRUE;
			break;

		case ';':
			format_entry_set_fmt (entry, real_start, fmt);
			format->entries = g_slist_append (format->entries, entry);
			num_entries++;

			entry = format_entry_ctor ();
			real_start = NULL;
			break;

		default :
			break;
		}
	}

	format_entry_set_fmt (entry, real_start, fmt);
	format->entries = g_slist_append (format->entries, entry);

	for (ptr = format->entries; ptr && counter++ < 4 ; ptr = ptr->next) {
		StyleFormatEntry *entry = ptr->data;

		/*
		 * Use default conditional instead of catch-all, except
		 * for the last entry.
		 */
		if (entry->restriction_type == '*' && counter != num_entries) {
			entry->restriction_value = 0.;

			switch (counter) {
			case 1 : entry->restriction_type = (num_entries > 2) ? '>' : '.';
				 break;
			case 2 : entry->restriction_type = '<'; break;
			case 3 : entry->restriction_type = '='; break;
			case 4 : entry->restriction_type = '@'; break;
			default :
				 break;
			}
		}
	}
}

/*
 * This routine is invoked when the last user of the
 * format is gone (ie, refcount has reached zero) just
 * before the StyleFormat structure is actually released.
 *
 * resources allocated in format_compile should be disposed here
 */
void
format_destroy (StyleFormat *format)
{
	g_slist_foreach (format->entries, &format_entry_dtor, NULL);
	g_slist_free (format->entries);
	format->entries = NULL;
	format_match_release (format);
}

/* used to generate formats when delocalizing so keep the leadings caps */
static struct FormatColor {
	char const * const name;
	StyleColor *color;
} format_colors [] = {
	{ N_("Black")   },
	{ N_("Blue")    },
	{ N_("Cyan")    },
	{ N_("Green")   },
	{ N_("Magenta") },
	{ N_("Red")     },
	{ N_("White")   },
	{ N_("Yellow")  }
};

void
format_color_init (void)
{
	int i;

	for (i = G_N_ELEMENTS (format_colors) ; i-- > 0 ; )
		format_colors[i].color =
			style_color_new_name (format_colors[i].name);
}

void
format_color_shutdown (void)
{
	int i;

	for (i = G_N_ELEMENTS (format_colors) ; i-- > 0 ; )
		style_color_unref (format_colors[i].color);
}

static struct FormatColor const *
lookup_color_by_name (gchar const *str, gchar const *end,
		      gboolean translate)
{
	int i, len;

	len = end - str;
	for (i = G_N_ELEMENTS (format_colors) ; i-- > 0 ; ) {
		gchar const *name = format_colors[i].name;
		if (translate)
			name = _(name);

		if (0 == g_ascii_strncasecmp (name, str, len) && name[len] == '\0')
			return format_colors + i;
	}
	return NULL;
}

static StyleColor *
lookup_color (gchar const *str, gchar const *end)
{
	struct FormatColor const *color = lookup_color_by_name (str, end, FALSE);

	if (color != NULL) {
		style_color_ref (color->color);
		return color->color;
	}
	return NULL;
}

static gnm_float beyond_precision;
void
render_number (GString *result,
	       gnm_float number,
	       format_info_t const *info)
{
	char thousands_sep = format_get_thousand ();
	char num_buf[(GNUM_MANT_DIG + GNUM_MAX_EXP) * 2 + 1];
	gchar *num = num_buf + sizeof (num_buf) - 1;
	gnm_float frac_part, int_part;
	int group, zero_count, digit_count = 0;
	int left_req = info->left_req;
	int right_req = info->right_req;
	int left_spaces = info->left_spaces;
	int right_spaces = info->right_spaces;
	int right_allowed = info->right_allowed + info->right_optional;

	if (right_allowed >= 0 && !info->has_fraction) {
		/* Change "rounding" into "truncating".   */
		/* Note, that we assume number >= 0 here. */
		gnm_float delta = 5 * gpow10 (-right_allowed - 1);
		number += delta;
	}
	frac_part = modfgnum (gnumeric_add_epsilon (number), &int_part);

	*num = '\0';
	group = (info->group_thousands) ? 3 : -1;
	for (; int_part > beyond_precision ; int_part /= 10., digit_count++) {
		if (group-- == 0) {
			group = 2;
			*(--num) = thousands_sep;
		}
		*(--num) = '0';
	}

	for (; int_part >= 1. ; int_part /= 10., digit_count++) {
		gnm_float r = floorgnum (int_part);
		int digit = r - floorgnum (r / 10) * 10;

		if (group-- == 0) {
			group = 2;
			*(--num) = thousands_sep;
		}
		*(--num) = digit + '0';
	}

	/* TODO : What ifthe only visible digits are zeros ? -0.00 looks bad */
	if (info->negative && !info->supress_minus)
		g_string_append_c (result, '-');
	if (left_req > digit_count) {
		for (left_spaces -= left_req ; left_spaces-- > 0 ;)
			g_string_append_c (result, ' ');
		for (left_req -= digit_count ; left_req-- > 0 ;)
			g_string_append_c (result, '0');
	}

	g_string_append (result, num);

	/* If the format contains only "#"s to the left of the decimal
	 * point, number in the [0.0,1.0] range are prefixed with a
	 * decimal point
	 */
	if (info->decimal_separator_seen ||
	    (number > 0.0 &&
	     number < 1.0 &&
	     info->right_allowed == 0 &&
	     info->right_optional > 0))
		g_string_append_c (result, format_get_decimal ());

	/* TODO : clip this a DBL_DIG */
	/* TODO : What if is a fraction ? */
	right_allowed -= right_req;
	right_spaces  -= right_req;
	while (right_req-- > 0) {
		gint digit;
		frac_part *= 10.0;
		digit = (gint)frac_part;
		frac_part -= digit;
		g_string_append_c (result, digit + '0');
	}

	zero_count = 0;

	while (right_allowed-- > 0) {
		gint digit;
		frac_part *= 10.0;
		digit = (gint)frac_part;
		frac_part -= digit;

		if (digit == 0) {
			right_spaces -= zero_count + 1;
			zero_count = 0;
		} else
			zero_count ++;

		g_string_append_c (result, digit + '0');
	}

	g_string_truncate (result, result->len - zero_count);

	while (right_spaces-- > 0)
		g_string_append_c (result, ' ');
}

static void
do_render_number (gnm_float number, format_info_t *info, GString *result)
{
	info->rendered = TRUE;

#if 0
	printf ("Rendering: %g with:\n", number);
	printf ("left_req:    %d\n"
		"right_req:   %d\n"
		"left_spaces: %d\n"
		"right_spaces:%d\n"
		"right_allow: %d\n"
		"negative:    %d\n"
		"supress:     %d\n"
		"decimalseen: %d\n"
		"decimalp:    %s\n",
		info->left_req,
		info->right_req,
		info->left_spaces,
		info->right_spaces,
		info->right_allowed + info->right_optional,
		info->negative,
		info->supress_minus,
		info->decimal_separator_seen,
		decimal_point);
#endif

	render_number (result, info->scale * number, info);
}

/*
 * Microsoft Excel has a bug in the handling of year 1900,
 * I quote from http://catless.ncl.ac.uk/Risks/19.64.html#subj9.1
 *
 * > Microsoft EXCEL version 6.0 ("Office 95 version") and version 7.0 ("Office
 * > 97 version") believe that year 1900 is a leap year.  The extra February 29
 * > cause the following problems.
 * >
 * > 1)  All day-of-week before March 1, 1900 are incorrect;
 * > 2)  All date sequence (serial number) on and after March 1, 1900 are incorrect.
 * > 3)  Calculations of number of days across March 1, 1900 are incorrect.
 * >
 * > The risk of the error will cause must be little.  Especially case 1.
 * > However, import or export date using serial date number will be a problem.
 * > If no one noticed anything wrong, it must be that no one did it that way.
 */
static struct tm *
split_time (gnm_float number, GnmDateConventions const *date_conv)
{
	static struct tm tm;
	guint secs;
	GDate date;

	datetime_serial_to_g (&date,
		datetime_serial_raw_to_serial (number), date_conv);
	g_date_to_struct_tm (&date, &tm);

	secs = datetime_serial_raw_to_seconds (number);
	tm.tm_hour = secs / 3600;
	secs -= tm.tm_hour * 3600;
	tm.tm_min  = secs / 60;
	secs -= tm.tm_min * 60;
	tm.tm_sec  = secs;

	return &tm;
}

/*
 * Finds the decimal char in @str doing the proper parsing of a
 * format string
 */
static char const *
find_decimal_char (char const *str)
{
	for (;*str; str++){
		if (*str == '.')
			return str;

		if (*str == ',')
			continue;

		switch (*str){
			/* These ones do not have any argument */
		case '#': case '?': case '0': case '%':
		case '-': case '+': case ')': case '£':
		case ':': case '$': case '¥': case '¤':
		case 'M': case 'm': case 'D': case 'd':
		case 'Y': case 'y': case 'S': case 's':
		case '*': case 'h': case 'H': case 'A':
		case 'a': case 'P': case 'p':
			break;

			/* Quoted string */
		case '"':
			for (str++; *str && *str != '"'; str++)
				;
			break;

			/* Escaped char and spacing format */
		case '\\': case '_':
			if (*(str + 1))
				str++;
			break;

			/* Scientific number */
		case 'E': case 'e':
			for (str++; *str;){
				if (*str == '+')
					str++;
				else if (*str == '-')
					str++;
				else if (*str == '0')
					str++;
				else
					break;
			}
		}
	}
	return NULL;
}

/* An helper function which modify the number of decimals displayed
 * and recreate the format string by calling the good function */
static char *
reformat_decimals(FormatCharacteristics *fc,
		  void (*format_function) (GString *res, FormatCharacteristics const * fmt),
		  int step)
{
	GString *res;

	/* Be sure that the number of decimals displayed will remain correct */
	if ((fc->num_decimals+step > 30) || (fc->num_decimals+step <0))
		return NULL;
	fc->num_decimals += step;

	/* Regenerate the format with the good function */
	res = g_string_new ("");
	(*format_function) (res, fc);

	return g_string_free (res, FALSE);
}

/*
 * This routine scans the format_string for a decimal dot,
 * and if it finds it, it removes the first zero after it to
 * reduce the display precision for the number.
 *
 * Returns NULL if the new format would not change things
 */
char *
format_remove_decimal (StyleFormat const *fmt)
{
	int offset = 1;
	char *ret, *p;
	char const *tmp;
	char const *format_string = fmt->format;
	FormatFamily ff;
	FormatCharacteristics fc;

	/* First try to classify the format so we can regenerate it */
	ff = cell_format_classify (fmt, &fc);

	switch (ff) {	
	case FMT_NUMBER:
	case FMT_CURRENCY:
		return reformat_decimals (&fc, &style_format_number, -1);
	case FMT_ACCOUNT:
		return reformat_decimals (&fc, &style_format_account, -1);
	case FMT_PERCENT:
		return reformat_decimals (&fc, &style_format_percent, -1);
	case FMT_SCIENCE:
		return reformat_decimals (&fc, &style_format_science, -1);
	case FMT_DATE:
	case FMT_TIME:
	case FMT_FRACTION:
	case FMT_TEXT:
	case FMT_SPECIAL:
		/* Nothing to remove for these formats ! */
		return NULL;
	case FMT_UNKNOWN:
	case FMT_GENERAL:
		; /* Nothing.  */
	}

	/* Use the old code for more special formats to try to add a
	   decimal */

	/*
	 * Consider General format as 0. with several optional decimal places.
	 * This is WRONG.  FIXME FIXME FIXME
	 * We need to look at the number of decimals in the current value
	 * and use that as a base.
	 */
	if (strcmp (format_string, "General") == 0)
		format_string = "0.########";

	tmp = find_decimal_char (format_string);
	if (!tmp)
		return NULL;

	ret = g_strdup (format_string);
	p = ret + (tmp - format_string);

	/* If there is more than 1 thing after the decimal place
	 * leave the decimal.
	 * If there is only 1 thing after the decimal remove the decimal too.
	 */
	if ((p[1] == '0' || p[1] == '#') && (p[2] == '0' || p[2] == '#'))
		++p;
	else
		offset = 2;

	strcpy (p, p + offset);

	return ret;
}

/*
 * This routine scans format_string for the decimal
 * character and when it finds it, it adds a zero after
 * it to force the rendering of the number with one more digit
 * of decimal precision.
 *
 * Returns NULL if the new format would not change things
 */
char *
format_add_decimal (StyleFormat const *fmt)
{
	char const *pre = NULL;
	char const *post = NULL;
	char *res;
	char const *format_string = fmt->format;
	FormatFamily ff;
	FormatCharacteristics fc;

	/* First try to classify the format so we can regenerate it */
	ff = cell_format_classify(fmt, &fc);

	switch (ff) {	
	case FMT_NUMBER:
	case FMT_CURRENCY:
		return reformat_decimals (&fc, &style_format_number, 1);
	case FMT_ACCOUNT:
		return reformat_decimals (&fc, &style_format_account, 1);
	case FMT_PERCENT:
		return reformat_decimals (&fc, &style_format_percent, 1);
	case FMT_SCIENCE:
		return reformat_decimals (&fc, &style_format_science, 1);
	case FMT_DATE:
	case FMT_TIME:
	case FMT_FRACTION:
	case FMT_TEXT:
	case FMT_SPECIAL:
		/* Nothing to add for these formats ! */
		return NULL;
	case FMT_UNKNOWN:
	case FMT_GENERAL:
		; /* Nothing.  */
	}

	/* Use the old code for more special formats to try to add a
	   decimal */	

	if (strcmp (format_string, "General") == 0) {
		format_string = "0";
		pre = format_string + 1;
		post = pre;
	} else {
		pre = find_decimal_char (format_string);

		/* If there is no decimal append to the last '0' */
		if (pre == NULL) {
			pre = strrchr (format_string, '0');

			/* If there are no 0s append to the ':s' */
			if (pre == NULL) {
				pre = strrchr (format_string, 's');
				if (pre > format_string && pre[-1] == ':') {
					if (pre[1] == 's')
						pre += 2;
					else
						++pre;
				} else
					return NULL;
			} else
				++pre;
			post = pre;
		} else
			post = pre + 1;
	}
	res = g_malloc ((pre - format_string + 1) +
		      1 + /* for the decimal */
		      1 + /* for the extra 0 */
		      strlen (post) +
		      1 /*terminate */);
	if (!res)
		return NULL;

	strncpy (res, format_string, pre - format_string);
	res[pre-format_string + 0] = '.';
	res[pre-format_string + 1] = '0';
	strcpy (res + (pre - format_string) + 2, post);

	return res;
}

/*********************************************************************/

static gchar *
format_number (gnm_float number, int col_width, StyleFormatEntry const *entry,
	       GnmDateConventions const *date_conv)
{
	GString *result = g_string_new ("");
	guchar const *format = (guchar *)(entry->format);
	format_info_t info;
	gboolean can_render_number = FALSE;
	int hour_seen = 0;
	gboolean time_display_elapsed = FALSE;
	gboolean ignore_further_elapsed = FALSE;

	gunichar fill_char = 0;
	int fill_start = -1;

	struct tm *time_split = 0;
	gnm_float signed_number;

	memset (&info, 0, sizeof (info));
	signed_number = number;
	if (number < 0.0){
		info.negative = TRUE;
		number = -number;
	}
	info.has_fraction = entry->has_fraction;
	info.scale = 1;

	while (*format) {
		gunichar c = g_utf8_get_char (format);

		switch (c) {

		case '[':
			/* Currency symbol */
			if (format[1] == '$') {
				gboolean no_locale = TRUE;
				for (format += 2; *format && *format != ']' ; ++format)
					/* strip digits from [$<currency>-{digit}+] */
					if (*format == '-')
						no_locale = FALSE;
					else if (no_locale)
						g_string_append_c (result, *format);
				if (!*format)
					continue;
			} else if (!ignore_further_elapsed)
				time_display_elapsed = TRUE;
			break;

		case '#':
			can_render_number = TRUE;
			if (info.decimal_separator_seen)
				info.right_optional++;
			break;

		case '?':
			can_render_number = TRUE;
			if (info.decimal_separator_seen)
				info.right_spaces++;
			else
				info.left_spaces++;
			break;

		case '0':
			can_render_number = TRUE;
			if (info.decimal_separator_seen){
				info.right_req++;
				info.right_allowed++;
				info.right_spaces++;
			} else {
				info.left_spaces++;
				info.left_req++;
			}
			break;

		case '.': {
			int c = *(format + 1);

			can_render_number = TRUE;
			if (c && (c != '0' && c != '#' && c != '?'))
				number /= 1000;
			else
				info.decimal_separator_seen = TRUE;
			break;
		}

		case ',':
			if (can_render_number) {
				guchar const *tmp = format;
				while (*++tmp == ',')
					;
				if (*tmp == '\0' || *tmp == '.' || *tmp == ';')
					/* NOTE : format-tmp is NEGATIVE */
					info.scale = gpow10 (3*(format-tmp));
				info.group_thousands = TRUE;
				format = tmp;
				continue;
			} else
				g_string_append_c (result, format_get_thousand ());
			break;

		/* FIXME: this is a gross hack */
		case 'E': case 'e': {
			gboolean const is_lower = (*format == 'e');
			gboolean shows_plus = FALSE;
			int prec = info.right_optional + info.right_req;
			char *buffer = g_alloca (40 + prec + 2);

			can_render_number = TRUE;
			while (*(++format))
				if (*format == '+')
					shows_plus = TRUE;
				else if (*format == '-' || *format == '0')
					;
				else
					break;

			sprintf (buffer, is_lower ? "%s%.*" GNUM_FORMAT_e : "%s%.*" GNUM_FORMAT_E,
				 info.negative ? "-" :
				 shows_plus ? "+" : "",
				 prec, number);

			g_string_append (result, buffer);
			goto finish;
		}

		case '\\':
			if (format[1] != '\0') {
				/* TODO : Other chars here ?? ('+' or ':') ? */
				if (format[1] == '-' || format[1] == '(')
					info.supress_minus = TRUE;
				else if (can_render_number && !info.rendered)
					do_render_number (number, &info, result);

				format++;
				g_string_append_len (result, format,
					g_utf8_skip[*(format)]);
			}
			break;

		case '"': {
			guchar const *tmp = ++format;
			if (can_render_number && !info.rendered)
				do_render_number (number, &info, result);

			for (; *tmp && *tmp != '"'; tmp++)
				;
			g_string_append_len (result, format, tmp-format);
			format = tmp;
			if (!*format)
				continue;
			break;
		}

		case '/': /* fractions */
			if (can_render_number && info.left_spaces > info.left_req) {
				int size = 0;
				int numerator = -1, denominator = -1;

				while (format[size + 1] == '?')
					++size;

				/* check for explicit denominator */
				if (size == 0) {
					char *end;

					errno = 0;
					denominator = strtol ((char *)format + 1, &end, 10);
					if ((char *)format + 1 != end && errno != ERANGE) {
						format = (guchar *)end;
						numerator = (int)((number - (int)number) * denominator + 0.5);
					}
				} else {
					static int const powers[3] = { 10, 100, 1000 };

					format += size + 1;
					if (size > 3)
						size = 3;
					stern_brocot (number - (int)number, powers[size - 1],
						      &numerator, &denominator);
				}

				if (denominator > 0) {
					/* improper fractions */
					if (!info.rendered) {
						info.rendered = TRUE;
						numerator += ((int)number) * denominator;
						if (info.negative && !info.supress_minus)
							g_string_prepend_c (result, '-');
					}
					if (numerator > 0) {
						char buffer[4 * sizeof (numerator) + 4 * sizeof (denominator)];
						sprintf (buffer, "%d/%d", numerator, denominator);
						g_string_append (result, buffer);
					}
					continue;
				}
			}

		case '-':
		case '(':
		case '+':
		case ':':
			info.supress_minus = TRUE;
			/* fall down */

		case ' ': /* eg # ?/? */
		case '$':
		case 0x00A3 : /* pound */
		case 0x00A5 : /* yen */
		case 0x20AC : /* Euro */
		case ')':
			if (can_render_number && !info.rendered)
				do_render_number (number, &info, result);
			g_string_append_unichar (result, c);
			break;

		/* percent */
		case '%':
			if (!info.rendered) {
				number *= 100;
				if (can_render_number)
					do_render_number (number, &info, result);
				else
					can_render_number = TRUE;
			}
			g_string_append_c (result, '%');
			break;

		case '_':
			if (can_render_number && !info.rendered)
				do_render_number (number, &info, result);
			if (format[1])
				format++;
			g_string_append_c (result, ' ');
			break;

		case '*':
			/* Intentionally forget any previous fill characters
			 * (no need to be smart).
			 * FIXME : make the simplifying assumption that we are
			 * not going to fill in the middle of a number.  This
			 * assumption is WRONG! but ok until we rewrite the
			 * format engine.
			 */
			if (format[1]) {
				if (can_render_number && !info.rendered)
					do_render_number (number, &info, result);
				++format;
				fill_char = g_utf8_get_char (format);
				fill_start = result->len;
			}
			break;

		case 'M':
		case 'm': {
			int n;

			if (!time_split)
				time_split = split_time (signed_number, date_conv);

			/* FIXME : Yuck
			 * This is a problem waiting to happen.
			 * rewrite.
			 */
			for (n = 1; format[1] == 'M' || format[1] == 'm'; format++)
				n++;
			if (format[1] == ']')
				format++;
			if (time_display_elapsed) {
				time_display_elapsed = FALSE;
				ignore_further_elapsed = TRUE;
				append_minute_elapsed (result, time_split, number);
			} else if (hour_seen ||
				   (format[1] == ':' &&
				    (format[2] == 's' || format[2] == 'S'))) {
				append_minute (result, n, time_split);
			} else
				append_month (result, n, time_split);
			break;
		}

		case 'D':
		case 'd':
			if (!time_split)
				time_split = split_time (signed_number, date_conv);
			format += append_day (result, format, time_split) - 1;
			break;

		case 'Y':
		case 'y':
			if (!time_split)
				time_split = split_time (signed_number, date_conv);
			format += append_year (result, format, time_split) - 1;
			break;

		case 'S':
		case 's': {
			int n;

			if (!time_split)
				time_split = split_time (signed_number, date_conv);

			for (n = 1; format[1] == 's' || format[1] == 'S'; format++)
				n++;
			if (format[1] == ']')
				format++;
			if (time_display_elapsed){
				time_display_elapsed = FALSE;
				ignore_further_elapsed = TRUE;
				append_second_elapsed (result, n, time_split, number);
			} else
				append_second (result, n, time_split);
			break;
		}

		case 'H':
		case 'h': {
			int n;

			if (!time_split)
				time_split = split_time (signed_number, date_conv);

			for (n = 1; format[1] == 'h' || format[1] == 'H'; format++)
				n++;
			if (format[1] == ']')
				format++;
			if (time_display_elapsed){
				time_display_elapsed = FALSE;
				ignore_further_elapsed = TRUE;
				append_hour_elapsed (result, time_split, number);
			} else
				/* h == hour optionally in 24 hour mode
				 * h followed by am/pm puts it in 12 hout mode
				 *
				 * multiple h eg 'hh' force 12 hour mode.
				 * NOTE : This is a non-XL extension
				 */
				append_hour (result, n, time_split,
					     entry->want_am_pm || (n > 1));
			hour_seen = TRUE;
			break;
		}

		case 'A':
		case 'a':
			if (!time_split)
				time_split = split_time (signed_number, date_conv);
			if (time_split->tm_hour < 12){
				g_string_append_c (result, *format);
				format++;
				if (*format == 'm' || *format == 'M'){
					g_string_append_c (result, *format);
					if (*(format + 1) == '/')
						format++;
				}
			} else {
				if (*(format + 1) == 'm' || *(format + 1) == 'M')
					format++;
				if (*(format + 1) == '/')
					format++;
			}
			break;

		case 'P': case 'p':
			if (!time_split)
				time_split = split_time (signed_number, date_conv);
			if (time_split->tm_hour >= 12){
				g_string_append_c (result, *format);
				if (*(format + 1) == 'm' || *(format + 1) == 'M'){
					format++;
					g_string_append_c (result, *format);
				}
			} else {
				if (*(format + 1) == 'm' || *(format + 1) == 'M')
					format++;
			}
			break;

		default:
			/* TODO : After release check this.
			 * shouldn't we tack on the explicit characters here ?
			 */
			break;
		}
		format = g_utf8_next_char (format);
	}

	if (!info.rendered && can_render_number)
		do_render_number (number, &info, result);

	/* This is kinda ugly.  It does not handle variable width fonts */
	if (fill_char != '\0') {
		int count = col_width - result->len;
		while (count-- > 0)
			g_string_insert_unichar (result, fill_start, fill_char);
	}

 finish:
	return g_string_free (result, FALSE);
}

static gboolean
style_format_condition (StyleFormatEntry const *entry, Value const *value)
{
	if (entry->restriction_type == '*')
		return TRUE;

	switch (value->type) {
	case VALUE_ERROR:
	case VALUE_STRING:
		return entry->restriction_type == '@';

	case VALUE_FLOAT:
		switch (entry->restriction_type) {
		case '<': return value->v_float.val < entry->restriction_value;
		case '>': return value->v_float.val > entry->restriction_value;
		case '=': return value->v_float.val == entry->restriction_value;
		case ',': return value->v_float.val <= entry->restriction_value;
		case '.': return value->v_float.val >= entry->restriction_value;
		case '+': return value->v_float.val != entry->restriction_value;
		default:
			return FALSE;
		}

	case VALUE_INTEGER:
		switch (entry->restriction_type) {
		case '<': return value->v_int.val < entry->restriction_value;
		case '>': return value->v_int.val > entry->restriction_value;
		case '=': return value->v_int.val == entry->restriction_value;
		case ',': return value->v_int.val <= entry->restriction_value;
		case '.': return value->v_int.val >= entry->restriction_value;
		case '+': return value->v_int.val != entry->restriction_value;
		default:
			return FALSE;
		}

	default:
		return FALSE;
	}
}

/**
 * fmt_general_float:
 *
 * @val : the integer value being formated.
 * @col_width : the approximate width in characters.
 */
static char *
fmt_general_float (gnm_float val, double col_width)
{
	gnm_float tmp;
	int log_val, prec;

	if (col_width < 0.)
		return g_strdup_printf ("%.*" GNUM_FORMAT_g, GNUM_DIG, val);

	if (val < 0.) {
		/* leave space for minus sign */
		/* FIXME : idealy we would use the width of a minus sign */
		col_width -= 1.;
		tmp = log10gnum (-val);
	} else
		tmp = (val > 0.) ? log10gnum (val) : 0;

	/* leave space for the decimal */
	/* FIXME : idealy we would use the width of a decimal point */
	prec = (int) floor (col_width - 1.);
	if (prec < 0)
		prec = 0;

	if (tmp > 0.) {
		log_val = ceilgnum (tmp);

		/* Decrease precision to leave space for the E+00 */
		if (log_val > prec)
			for (prec -= 4; log_val >= 100 ; log_val /= 10)
				prec--;
	} else {
		log_val = floorgnum (tmp);

		/* Display 0 for cols that are too narrow for scientific
		 * notation with abs (value) < 1 */
		if (col_width < 5. && -log_val >= prec)
			return g_strdup ("0");

		/* Include leading zeros eg 0.0x has 2 leading zero */
		if (log_val >= -4)
			prec += log_val;

		/* Decrease precision to leave space for the E+00 */
		else for (prec -= 4; log_val <= -100 ; log_val /= 10)
			prec--;
	}

	if (prec < 1)
		prec = 1;
	else if (prec > GNUM_DIG)
		prec = GNUM_DIG;

	/* FIXME : glib bug.  it does not handle G, use g (fixed in 1.2.9) */
	return g_strdup_printf ("%.*" GNUM_FORMAT_g, prec, val);
}

/**
 * fmt_general_int :
 *
 * @val : the integer value being formated.
 * @col_width : the approximate width in characters.
 */
static char *
fmt_general_int (int val, int col_width)
{
	if (col_width > 0) {
		int log_val;

		if (val < 0) {
			/* leave space for minus sign */
			col_width--;
			log_val = ceil (log10 ((unsigned int)-val));
		} else
			log_val = (val > 0) ? ceil (log10 (val)) : 0;

		/* Switch to scientific notation if things are too wide */
		if (log_val > col_width)
			/* FIXME : glib bug.  it does not handle G, use g */
			/* Decrease available width by 5 to account for .+E00 */
			return g_strdup_printf ("%.*g", col_width - 5, (double)val);
	}
	return g_strdup_printf ("%d", val);
}

/*
 * Returns NULL when the value should be formated as text
 */
gchar *
format_value (StyleFormat const *format, Value const *value, StyleColor **color,
	      double col_width, GnmDateConventions const *date_conv)
{
	char *v = NULL;
	StyleFormatEntry const *entry = NULL; /* default to General */
	GSList *list;
	gboolean need_abs = FALSE;

	if (color)
		*color = NULL;

	g_return_val_if_fail (value != NULL, g_strdup ("<ERROR>"));

	if (format == NULL)
		format = VALUE_FMT (value);

	/* Use top left corner of an array result.
	 * This wont work for ranges because we dont't have a location
	 */
	if (value->type == VALUE_ARRAY)
		value = value_area_fetch_x_y (value, 0, 0, NULL);

	if (format) {
		/* get format */
		for (list = format->entries; list; list = list->next)
			if (style_format_condition (list->data, value))
				break;

		/* If nothing matches treat it as General */
		if (list != NULL) {
			entry = list->data;

			/* Empty formats should be ignored */
			if (entry->format[0] == '\0')
				return g_strdup ("");

			if (color && entry->color != NULL)
				*color = style_color_ref (entry->color);

			if (strcmp (entry->format, "@") == 0) {
				/* FIXME : Formatting a value as a text returns
				 * the entered text.  We need access to the
				 * parse format */
				entry = NULL;
			} else if (strcmp (entry->format, "General") == 0)
				entry = NULL;
		}

		/* More than one format? -- abs the value.  */
		need_abs = entry && format->entries->next;
	}

	switch (value->type) {
	case VALUE_EMPTY:
		return g_strdup ("");
	case VALUE_BOOLEAN:
		return g_strdup (value->v_bool.val ? _("TRUE"):_("FALSE"));
	case VALUE_INTEGER: {
		int val = value->v_int.val;
		if (need_abs)
			val = ABS (val);

		if (entry == NULL)
			return fmt_general_int (val, col_width);
		v = format_number (val, (int)col_width, entry, date_conv);
		break;
	}
	case VALUE_FLOAT: {
		gnm_float val = value->v_float.val;

		if (!finitegnum (val))
			return g_strdup (value_error_name (GNM_ERROR_VALUE, TRUE));

		if (need_abs)
			val = gnumabs (val);

		if (entry == NULL) {
			if (INT_MAX >= val && val >= INT_MIN && val == floorgnum (val))
				return fmt_general_int ((int)val, col_width);
			return fmt_general_float (val, col_width);
		}
		v = format_number (val, (int)col_width, entry, date_conv);
		break;
	}
	case VALUE_ERROR:
		return g_strdup (value->v_err.mesg->str);
	case VALUE_STRING:
		return g_strdup (value->v_str.val->str);
	case VALUE_CELLRANGE:
		return g_strdup (value_error_name (GNM_ERROR_VALUE, TRUE));
	case VALUE_ARRAY: /* Array of arrays ?? */
		return g_strdup (_("ARRAY"));

	default:
		return g_strdup ("Internal error");
	}

	/* Format error, return a default value */
	if (v == NULL)
		return value_get_as_string (value);

	return v;
}

void
number_format_init (void)
{
	style_format_hash = g_hash_table_new (g_str_hash, g_str_equal);
	/* FIXME: should be related to gnm_float, not double:  */
	beyond_precision = gpow10 (GNUM_DIG + 1);
}

void
number_format_shutdown (void)
{
	g_hash_table_destroy (style_format_hash);
	style_format_hash = NULL;
}

/****************************************************************************/

static char *
translate_format_color (GString *res, char const *ptr, gboolean translate_to_en)
{
	char *end;
	struct FormatColor const *color;

	g_string_append_c (res, '[');

	/*
	 * Special [h*], [m*], [*s] is using for
	 * and [$*] are for currencies.
	 * measuring times, not for specifying colors.
	 */
	if (ptr[1] == 'h' || ptr[1] == 's' || ptr[1] == 'm' || ptr[1] == '$')
		return NULL;

	end = strchr (ptr, ']');
	if (end == NULL)
		return NULL;

	color = lookup_color_by_name (ptr+1, end, translate_to_en);
	if (color != NULL) {
		g_string_append (res, translate_to_en
			? color->name : _(color->name));
		g_string_append_c (res, ']');
		return end;
	}
	return NULL;
}

char *
style_format_delocalize (char const *descriptor_string)
{
	g_return_val_if_fail (descriptor_string != NULL, NULL);

	if (strcmp (descriptor_string, _("General"))) {
		char const thousands_sep = format_get_thousand ();
		char const decimal = format_get_decimal ();
		char const *ptr = descriptor_string;
		GString *res = g_string_sized_new (strlen (ptr));

		for ( ; *ptr ; ++ptr) {
			if (*ptr == decimal)
				g_string_append_c (res, '.');
			else if (*ptr == thousands_sep)
				g_string_append_c (res, ',');
			else if (*ptr == '\"') {
				do {
					g_string_append_c (res, *ptr++);
				} while (*ptr && *ptr != '\"');
				if (*ptr)
					g_string_append_c (res, *ptr);
			} else if (*ptr == '[') {
				char *tmp = translate_format_color (res, ptr, TRUE);
				if (tmp != NULL)
					ptr = tmp;
			} else {
				if (*ptr == '\\' && ptr[1] != '\0') {
					ptr++;
					/* Ignore '\' if we probably added it */
					if (*ptr != decimal && *ptr != thousands_sep)
						g_string_append_c (res, '\\');
				}
				g_string_append_c (res, *ptr);
			}
		}
		return g_string_free (res, FALSE);
	} else
		return g_strdup ("General");
}

/**
 * style_format_new_XL :
 *
 * Looks up and potentially creates a StyleFormat from the supplied string in
 * XL format.
 */
StyleFormat *
style_format_new_XL (char const *descriptor_string, gboolean delocalize)
{
	StyleFormat *format;

	/* Safety net */
	if (descriptor_string == NULL) {
		g_warning ("Invalid format descriptor string, using General");
		descriptor_string = "General";
	} else if (delocalize)
		descriptor_string = style_format_delocalize (descriptor_string);

	format = (StyleFormat *) g_hash_table_lookup (style_format_hash, descriptor_string);

	if (!format) {
		format = g_new0 (StyleFormat, 1);
		format->format = g_strdup (descriptor_string);
		format->entries = NULL;
		format->regexp_str = NULL;
		format->match_tags = NULL;
		if (strcmp ("General", format->format))
			format_compile (format);
		g_hash_table_insert (style_format_hash, format->format, format);
	}
	format->ref_count++;

	if (delocalize)
		g_free ((char *)descriptor_string);
	return format;
}

/**
 * style_format_str_as_XL
 *
 * The caller is responsible for freeing the resulting string.
 */
char *
style_format_str_as_XL (char const *ptr, gboolean localized)
{
	if (localized) {
		g_return_val_if_fail (ptr != NULL, g_strdup (_("General")));
	} else {
		g_return_val_if_fail (ptr != NULL, g_strdup ("General"));
	}

	if (!localized)
		return g_strdup (ptr);

	if (!strcmp (ptr, "General"))
		return g_strdup (_("General"));

	{
	char const thousands_sep = format_get_thousand ();
	char const decimal = format_get_decimal ();
	char *tmp;
	GString *res = g_string_sized_new (strlen (ptr));

	/* TODO : XL seems to do an adaptive escaping of
	 * things.
	 * eg '#,##0.00 ' in a locale that uses ' '
	 * as the thousands would become
	 *    '# ##0.00 '
	 * rather than
	 *    '# ##0.00\ '
	 *
	 * TODO : Minimal quotes.
	 * It also seems to have a display mode vs a storage mode.
	 * Internally it adds a few quotes around strings.
	 * Then tries not to display the quotes unless needed.
	 */
	for ( ; *ptr ; ++ptr)
		switch (*ptr) {
		case '.'  : g_string_append_c (res, decimal);
			    break;
		case ','  : g_string_append_c (res, thousands_sep);
			    break;

		case '\"' : do {
				    g_string_append_c (res, *ptr++);
			    } while (*ptr && *ptr != '\"');
			    if (*ptr)
				    g_string_append_c (res, *ptr);
			    break;

		case '\\' : g_string_append_c (res, '\\');
			    if (ptr[1] != '\0') {
				    g_string_append_c (res, ptr[1]);
				    ++ptr;
			    }
			    break;

		case '[' : tmp = translate_format_color (res, ptr, FALSE);
			   if (tmp != NULL)
				   ptr = tmp;
			   break;

		default   : if (*ptr == decimal || *ptr == thousands_sep)
				    g_string_append_c (res, '\\');
			    g_string_append_c (res, *ptr);
		};

	return g_string_free (res, FALSE);
	}
}

/**
 * style_format_as_XL :
 * @sf :
 * @localized : should the string be in cannonical or locale specific form.
 *
 * Return a string which the caller is responsible for freeing.
 */
char *
style_format_as_XL (StyleFormat const *fmt, gboolean localized)
{
	if (localized) {
		g_return_val_if_fail (fmt != NULL, g_strdup (_("General")));
	} else {
		g_return_val_if_fail (fmt != NULL, g_strdup ("General"));
	}

	return style_format_str_as_XL (fmt->format, localized);
}

/**
 * style_format_ref :
 * @sf :
 *
 * Add a reference to a StyleFormat
 */
void
style_format_ref (StyleFormat *sf)
{
	g_return_if_fail (sf != NULL);

	sf->ref_count++;
}

/**
 * style_format_unref :
 * @sf :
 *
 * Remove a reference to a StyleFormat, freeing when it goes to zero.
 */
void
style_format_unref (StyleFormat *sf)
{
	if (sf == NULL)
		return;

	g_return_if_fail (sf->ref_count > 0);

	sf->ref_count--;
	if (sf->ref_count != 0)
		return;

	g_hash_table_remove (style_format_hash, sf->format);

	format_destroy (sf);
	g_free (sf->format);
	g_free (sf);
}

/**
 * style_format_is_general :
 * @sf : the format to check
 *
 * A small utility to check whether a format is 'General'
 */
gboolean
style_format_is_general (StyleFormat const *sf)
{
	return 0 == strcmp (sf->format, "General");
}

/**
 * style_format_is_general :
 * @sf : the format to check
 *
 * A small utility to check whether a format is 'General'
 */
gboolean
style_format_is_text (StyleFormat const *sf)
{
	return 0 == strcmp (sf->format, "@");
}

StyleFormat *
style_format_general (void)
{
	static StyleFormat *fmt = NULL;
	if (fmt == NULL)
		fmt = style_format_new_XL (cell_formats[FMT_GENERAL][0], FALSE);
	return fmt;
}
StyleFormat *
style_format_default_date (void)
{
	static StyleFormat *fmt = NULL;
	if (fmt == NULL)
		fmt = style_format_new_XL (cell_formats[FMT_DATE][0], FALSE);
	return fmt;
}

StyleFormat *
style_format_default_time (void)
{
	static StyleFormat *fmt = NULL;
	if (fmt == NULL)
		fmt = style_format_new_XL (cell_formats[FMT_TIME][0], FALSE);
	return fmt;
}

StyleFormat *
style_format_default_percentage	(void)
{
	static StyleFormat *fmt = NULL;
	if (fmt == NULL)
		fmt = style_format_new_XL (cell_formats[FMT_PERCENT][1], FALSE);
	return fmt;
}

StyleFormat *
style_format_default_money (void)
{
	static StyleFormat *fmt = NULL;
	if (fmt == NULL)
		fmt = style_format_new_XL (cell_formats[FMT_CURRENCY][3], FALSE);
	return fmt;
}

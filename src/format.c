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

#include <config.h>
#include <gnome.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>
#include <locale.h>
#ifdef HAVE_LANGINFO_H
#    include <langinfo.h>
#endif
#ifdef HAVE_IEEEFP_H
#    include <ieeefp.h>
#endif
#include "style-color.h"
#include "format.h"
#include "expr.h"
#include "dates.h"
#include "value.h"
#include "parse-util.h"
#include "portability.h"
#include "datetime.h"
#include "mathfunc.h"

/***************************************************************************/

/* Points to the locale information for number display */
static struct lconv *lc = NULL;
static gboolean date_order_cached = FALSE;

char const *
gnumeric_setlocale (int category, char const *val)
{
	lc = NULL;
	date_order_cached = FALSE;
	return setlocale (category, val);
}

char
format_get_decimal (void)
{
	char res;
	if (lc == NULL)
		lc = localeconv ();

	res = lc->mon_decimal_point [0];
	return (res != '\0') ? res : '.';
}

char
format_get_thousand (void)
{
	char res;
	if (lc == NULL)
		lc = localeconv ();

	res = lc->mon_thousands_sep [0];
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
 */
char const *
format_get_currency (gboolean *precedes, gboolean *space_sep)
{
	if (lc == NULL)
		lc = localeconv ();

	if (precedes)
		*precedes = (lc->p_cs_precedes != 0);
	if (space_sep)
		*space_sep = (lc->p_sep_by_space != 0);

	if (lc->currency_symbol == NULL || *lc->currency_symbol == '\0')
		return "$";
	return lc->currency_symbol;
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
		char const *ptr = nl_langinfo(D_FMT);

		date_order_cached = TRUE;
		month_first = TRUE;
		if (ptr)
			while (*ptr) {
				char const tmp = tolower((unsigned char)*ptr++);
				if (tmp == 'd') {
					month_first = FALSE;
					break;
				} else if (tmp == 'm')
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

/* Use comma as the arg seperator unless the decimal point is a
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
        char        restriction_type;
        int         restriction_value;
} StyleFormatEntry;

struct _StyleFormat {
	int    ref_count;
	char  *format;
        GList *entries;  /* Of type StyleFormatEntry. */
};

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
 * at the end of the number specification.  Fractions are not yet
 * supported.
 */


/*
 * Parses the year field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_year (GString *string, const guchar *format, const struct tm *time_split)
{
	char temp [5];

	if (tolower (format [1]) != 'y'){
		g_string_append_c (string, 'y');
		return 1;
	}

	if (tolower (format [2]) != 'y' || tolower (format [3]) != 'y'){
		sprintf (temp, "%02d", time_split->tm_year % 100);
		g_string_append (string, temp);
		return 2;
	}

	sprintf (temp, "%04d", time_split->tm_year + 1900);
	g_string_append (string, temp);

	return 4;
}

/*
 * Parses the month field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_month (GString *string, int n, const struct tm *time_split)
{
	char temp [3];

	if (n == 1){
		sprintf (temp, "%d", time_split->tm_mon+1);
		g_string_append (string, temp);
		return 1;
	}

	if (n == 2){
		sprintf (temp, "%02d", time_split->tm_mon+1);
		g_string_append (string, temp);
		return 2;
	}

	if (n == 3){
		g_string_append (string, _(month_short [time_split->tm_mon])+1);
		return 3;
	}

	g_string_append (string, _(month_long [time_split->tm_mon]));
	return 4;
}

/*
 * Parses the day field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_day (GString *string, const guchar *format, const struct tm *time_split)
{
	char temp[3];

	if (tolower (format [1]) != 'd'){
		sprintf (temp, "%d", time_split->tm_mday);
		g_string_append (string, temp);
		return 1;
	}

	if (tolower (format [2]) != 'd'){
		sprintf (temp, "%02d", time_split->tm_mday);
		g_string_append (string, temp);
		return 2;
	}

	if (tolower (format [3]) != 'd'){
		g_string_append (string, _(day_short[time_split->tm_wday])+1);
		return 3;
	}

	g_string_append (string, _(day_long[time_split->tm_wday]));

	return 4;
}

/*
 * Renders the hour.
 */
static void
append_hour (GString *string, int n, const struct tm *time_split,
	     gboolean want_am_pm)
{
	char *temp = g_alloca (n + 4 * sizeof (int));

	sprintf (temp, "%0*d", n,
		 want_am_pm
		 ? (((time_split->tm_hour + 11) % 12) + 1)
		 : time_split->tm_hour);
	g_string_append (string, temp);

	return;
}

/*
 * Renders the hour.
 */
static void
append_hour_elapsed (GString *string, int n, const struct tm *time_split, int number)
{
	char *temp = g_alloca (n + 4 * sizeof (int));
	int hours;

	hours = number * 24 + time_split->tm_hour;
	sprintf (temp, "%0*d", n, hours);
	g_string_append (string, temp);

	return;
}

/*
 * Renders the number of minutes.
 */
static void
append_minute (GString *string, int n, const struct tm *time_split)
{
	char *temp = g_alloca (n + 4 * sizeof (int));

	sprintf (temp, "%0*d", n, time_split->tm_min);
	g_string_append (string, temp);

	return;
}

/*
 * Renders the number of minutes, in elapsed format
 */
static void
append_minute_elapsed (GString *string, int n, const struct tm *time_split, int number)
{
	char *temp = g_alloca (n + 4 * sizeof (int));
	int minutes;

	minutes = ((number * 24) + time_split->tm_hour) * 60 + time_split->tm_min;

	sprintf (temp, "%0*d", n, minutes);
	g_string_append (string, temp);

	return;
}

/*
 * Renders the second field.
 */
static void
append_second (GString *string, int n, const struct tm *time_split)
{
	char *temp = g_alloca (n + 4 * sizeof (int));

	sprintf (temp, "%0*d", n, time_split->tm_sec);
	g_string_append (string, temp);

	return;
}

/*
 * Renders the second field in elapsed
 */
static void
append_second_elapsed (GString *string, int n, const struct tm *time_split, int number)
{
	char *temp = g_alloca (n + 4 * sizeof (int));
	int seconds;

	seconds = (((number * 24 + time_split->tm_hour) * 60 + time_split->tm_min) * 60) + time_split->tm_sec;
	sprintf (temp, "%0*d", n, seconds);
	g_string_append (string, temp);

	return;
}

#if 0
/*
 * Parses the day part field at the beginning of the format.  Returns
 * the number of characters used.
 */
static int
append_half (GString *string, const guchar *format, const struct tm *time_split)
{
	if (time_split->tm_hour <= 11){
		if (tolower (format [0]) == 'a' || tolower (format [0]) == 'p')
			g_string_append_c (string, 'a');
		else
			g_string_append_c (string, 'A');
	}
	else {
		if (tolower (format [0]) == 'a' || tolower (format [0]) == 'p')
			g_string_append_c (string, 'p');
		else
			g_string_append_c (string, 'P');
	}

	if (tolower (format [1]) == 'm'){
		g_string_append_c (string, format [1]);
		return 2;
	} else
		return 1;
}
#endif

/*
 * Since the Excel formating codes contain a number of ambiguities,
 * this routine does some analysis on the format first.
 */
static void
pre_parse_format (StyleFormatEntry *style)
{
	const unsigned char *format;

	style->want_am_pm = FALSE;
	for (format = style->format; *format; format++){
		switch (*format){
		case '"':
			format++;
			while (*format && *format != '"')
				format++;

			if (!*format)
				return;
			break;

		case '\\':
			if (*(format+1))
				format++;
			else
				return;
			break;

		case 'a':
		case 'p':
		case 'A':
		case 'P':
			if (tolower (format[1]) == 'm')
				style->want_am_pm = TRUE;
			break;
		}
	}
}

typedef struct
{
	int decimal;
	int timeformat;
	int hasnumbers;
} xformat_info;

/*
 * This routine should always return, it cannot fail, in the worst
 * case it should just downgrade to simplistic formatting
 */
static void
format_compile (StyleFormat *format)
{
	GString *string = g_string_new ("");
	int i;
	int which = 0;
	int length = strlen (format->format);
	StyleFormatEntry standard_entries[4];
	StyleFormatEntry *temp;

	/* g_string_maybe_expand (string, length); */

	for (i = 0; i < length; i++){

		switch (format->format[i]){
		case ';':
			if (which < 4) {
				standard_entries [which].restriction_type = '*';
				standard_entries [which].format =
					g_strndup (string->str, string->len + 1);
				which++;
			}
			string = g_string_truncate (string, 0);
			break;
		default:
			string = g_string_append_c (string, format->format [i]);
			break;
		}
	}

	if (which < 4) {
		standard_entries[which].restriction_type = '*';
		standard_entries[which].format =
			g_strndup (string->str, string->len + 1);
		which++;
	}

	/* Set up restriction types. */
	standard_entries[1].restriction_type = '<';
	standard_entries[1].restriction_value = 0;
	switch (which){

	case 4:
		standard_entries[3].restriction_type = '@';
		/* Fall through. */
	case 3:
		standard_entries[2].restriction_type = '=';
		standard_entries[2].restriction_value = 0;
		standard_entries[0].restriction_type = '>';
		standard_entries[0].restriction_value = 0;
		break;
	case 2:
		standard_entries[0].restriction_type = '.';  /* . >= */
		standard_entries[0].restriction_value = 0;
		break;
	}

	for (i = 0; i < which; i++){
		temp = g_new (StyleFormatEntry, 1);
		*temp = standard_entries[i];
		pre_parse_format (temp);
		format->entries = g_list_append (format->entries, temp);
	}

	g_string_free (string, TRUE);
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
	g_free ((char *)entry->format);
	g_free (entry);
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
	g_list_foreach (format->entries, &format_entry_dtor, NULL);
	g_list_free (format->entries);
	format->entries = NULL;
}

static struct FormatColor {
	char       *name;
	StyleColor *color;
} format_colors [] = {
	{ N_("black")   },
	{ N_("blue")    },
	{ N_("cyan")    },
	{ N_("green")   },
	{ N_("magenta") },
	{ N_("red")     },
	{ N_("white")   },
	{ N_("yellow")  },
	{ NULL, NULL }
};

void
format_color_init (void)
{
	int i;

	for (i = 0; format_colors [i].name; i++){
		StyleColor *sc;
		GdkColor c;

		gdk_color_parse (format_colors [i].name, &c);
		sc = style_color_new (c.red, c.green, c.blue);

		format_colors [i].color = sc;
	}
}

void
format_color_shutdown (void)
{
	int i;

	for (i = 0; format_colors [i].name; i++)
		style_color_unref (format_colors [i].color);
}

static struct FormatColor const *
lookup_color_by_name (const char *str, const char *end,
		      gboolean const translate)
{
	int i, len;

	len = end - str;
	for (i = 0; format_colors [i].name; i++) {
		char const *name = format_colors [i].name;
		if (translate)
			name = _(name);

		if (0 == g_strncasecmp (name, str, len) && name [len] == '\0')
			return format_colors + i;
	}
	return NULL;
}

static StyleColor *
lookup_color (const char *str, const char *end)
{
	struct FormatColor const *color = lookup_color_by_name (str, end, FALSE);

	if (color != NULL) {
		style_color_ref (color->color);
		return color->color;
	}
	return NULL;
}

static GString *
render_number (gdouble number,
	       int left_req,
	       int right_req,
	       int left_spaces,
	       int right_spaces,
	       int right_allowed,
	       int use_thousand_sep,
	       int negative,
	       int supress_minus,
	       int decimal,
	       const char *show_decimal)
{
	GString *number_string = g_string_new ("");
	gint zero_count;
	gdouble temp;
	int group = 0;
	char thousands_sep;
	static gdouble beyond_precision = 0;

	if (!beyond_precision)
		beyond_precision = gpow10 (DBL_DIG + 1);

	if (right_allowed >= 0) {
		/* Change "rounding" into "truncating".  */
		gdouble delta = 0.5;
		int i;
		for (i = 0; i < right_allowed; i++)
			delta /= 10.0;
		/* Note, that we assume number >= 0 here.  */
		number += delta;
	}

	thousands_sep = format_get_thousand ();

	for (temp = number; temp >= 1.0; temp /= 10.0) {
		int digit;

		if (use_thousand_sep) {
			group++;
			if (group == 4){
				group = 1;
				g_string_prepend_c (number_string, thousands_sep);
			}
		}

		if (temp > beyond_precision)
			digit = 0;
		else {
			double r = floor (temp);
			digit = r - floor (r / 10) * 10;
		}
		g_string_prepend_c (number_string, digit + '0');
		if (left_req > 0)
			left_req--;
		if (left_spaces > 0)
			left_spaces--;
	}

	for (; left_req > 0; left_req--, left_spaces--)
		g_string_prepend_c (number_string, '0');

	for (; left_spaces > 0; left_spaces--)
		g_string_prepend_c (number_string, ' ');

	if (negative && !supress_minus)
		g_string_prepend_c (number_string, '-');

	if (decimal > 0)
		g_string_append_c (number_string, format_get_decimal ());
	else
		g_string_append (number_string, show_decimal);

	temp = number - floor (number);

	for (; right_req > 0; right_req --, right_allowed --, right_spaces --) {
		gint digit;
		temp *= 10.0;
		digit = (gint)temp;
		temp -= digit;
		g_string_append_c (number_string, digit + '0');
	}

	zero_count = 0;

	for (; right_allowed > 0; right_allowed --) {
		gint digit;
		temp *= 10.0;
		digit = (gint)temp;
		temp -= digit;

		if (digit == 0)
			zero_count ++;
		else {
			right_spaces -= zero_count + 1;
			zero_count = 0;
		}

		g_string_append_c (number_string, digit + '0');
	}

	g_string_truncate (number_string, number_string->len - zero_count);

	for (; right_spaces > 0; right_spaces--) {
		g_string_append_c (number_string, ' ');
	}

	return number_string;
}

typedef struct {
	int  right_optional, right_spaces, right_req, right_allowed;
	int  left_spaces, left_req;
	int  scientific;
	int  scientific_shows_plus;
	int  scientific_exp;
	int  rendered;
	int  negative;
	int  decimal_separator_seen;
	int  supress_minus;
	int  comma_separator_seen;
} format_info_t;

static void
do_render_number (gdouble number, format_info_t *info, GString *result)
{
	GString *number_str;
	char decimal_point [2];

	info->rendered = 1;

	/*
	 * If the format contains only "#"s to the left of the decimal
	 * point, number in the [0.0,1.0] range are prefixed with a
	 * decimal point
	 */
	if (number > 0.0 && number < 1.0 && info->right_allowed == 0 && info->right_optional > 0){
		decimal_point [0] = format_get_decimal ();
		decimal_point [1] = 0;
	} else
		decimal_point [0] = 0;

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

	number_str = render_number (
		number,
		info->left_req,
		info->right_req,
		info->left_spaces,
		info->right_spaces,
		info->right_allowed + info->right_optional,
		info->comma_separator_seen,
		info->negative,
		info->supress_minus,
		info->decimal_separator_seen,
		decimal_point);

	g_string_append (result, number_str->str);
	g_string_free (number_str, TRUE);
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
split_time (gdouble number)
{
	static struct tm tm;
	guint secs;
	GDate* date;

	date = datetime_serial_to_g (datetime_serial_raw_to_serial (number));
	g_date_to_struct_tm (date, &tm);
	g_date_free (date);

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
			if (*(str+1))
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

	if (strcmp (format_string, "General") == 0) {
		format_string = "0";
		pre = format_string+1;
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
			post = pre+1;
	}
	res = g_malloc ((pre - format_string + 1) +
		      1 + /* for the decimal */
		      1 + /* for the extra 0 */
		      strlen (post) +
		      1 /*terminate */);
	if (!res)
		return NULL;

	strncpy (res, format_string, pre - format_string);
	res [pre-format_string + 0] = '.';
	res [pre-format_string + 1] = '0';
	strcpy (res + (pre - format_string) + 2, post);

	return res;
}

/*********************************************************************/

static gchar *
format_number (gdouble number, int const col_width, const StyleFormatEntry *style_format_entry)
{
	GString *result = g_string_new ("");
	char const *format = style_format_entry->format;
	format_info_t info;
	gboolean can_render_number = FALSE;
	int hour_seen = 0;
	gboolean time_display_elapsed = FALSE;
	gboolean ignore_further_elapsed = FALSE;

	char fill_char = '\0';
	int fill_start = -1;

	struct tm *time_split = 0;
	char *res;
	gdouble signed_number;

	memset (&info, 0, sizeof (info));
	signed_number = number;
	if (number < 0.0){
		info.negative = TRUE;
		number = -number;
	}

	while (*format) {
		int c = *format;

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
					--format;
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
			int c = *(format+1);

			can_render_number = TRUE;
			if (c && (c != '0' && c != '#' && c != '?'))
				number /= 1000;
			else
				info.decimal_separator_seen = TRUE;
			break;
		}

		case ',': {
			if (!can_render_number) {
				char const sep = format_get_thousand ();
				g_string_append_c (result, sep);
			} else
				info.comma_separator_seen = TRUE;
			break;
		}

		case 'E': case 'e':
			can_render_number = TRUE;
			info.scientific = *format;
			for (format++; *format;){
				if (*format == '+'){
					info.scientific_shows_plus = TRUE;
					format++;
				} else if (*format == '-')
					format++;
				else if (*format == '0'){
					info.scientific_exp++;
					format++;
				} else
					break;
			}
			/* FIXME: this is a gross hack */
			{
				int prec = info.right_optional + info.right_req;
				char *buffer = g_alloca (40 + prec + 2);

				sprintf (buffer, (info.scientific == 'e')
					 ? "%s%.*e"
					 : "%s%.*E",
					 info.negative
					 ? "-" :
					 info.scientific_shows_plus
					 ? "+" : "",
					 prec, number);

				g_string_append (result, buffer);
				goto finish;
			}

		case '\\':
			if (format[1] != '\0') {
				/* TODO TODO TODO : Do we need any more chars here ?? */
				if (format[1] == '-')
					info.supress_minus = TRUE;
				else if (can_render_number && !info.rendered)
					do_render_number (number, &info, result);

				format++;
				g_string_append_c (result, *format);
			}
			break;

		case '"': {
			if (can_render_number && !info.rendered)
				do_render_number (number, &info, result);

			for (format++; *format && *format != '"'; format++)
				g_string_append_c (result, *format);
			if (!*format)
				--format;
			break;
		}

		case '-':
		case '/':
		case '(':
		case '+':
		case ' ':
		case ':':
			info.supress_minus = TRUE;
			/* fall down */

		case '$':
		case '£':
		case '¥':
		case '¤':
		case ')':
			if (can_render_number && !info.rendered)
				do_render_number (number, &info, result);
			g_string_append_c (result, *format);
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
			if (format [1]) {
				if (can_render_number && !info.rendered)
					do_render_number (number, &info, result);
				fill_char = *(++format);
				fill_start = result->len;
			}
			break;

		case 'M':
		case 'm': {
			int n;

			if (!time_split)
				time_split = split_time (signed_number);

			/* FIXME : Yuck
			 * This is a problem waiting to happen.
			 * rewrite.
			 */
			for (n = 1; format [1] == 'M' || format [1] == 'm'; format++)
				n++;
			if (format [1] == ']')
				format++;
			if (time_display_elapsed) {
				time_display_elapsed = FALSE;
				ignore_further_elapsed = TRUE;
				append_minute_elapsed (result, n, time_split, number);
			} else if (hour_seen ||
				   (format[1] == ':' && tolower (format[2]) == 's')){
				append_minute (result, n, time_split);
			} else
				append_month (result, n, time_split);
			break;
		}

		case 'D':
		case 'd':
			if (!time_split)
				time_split = split_time (signed_number);
			format += append_day (result, format, time_split) - 1;
			break;

		case 'Y':
		case 'y':
			if (!time_split)
				time_split = split_time (signed_number);
			format += append_year (result, format, time_split) - 1;
			break;

		case 'S':
		case 's': {
			int n;

			if (!time_split)
				time_split = split_time (signed_number);

			for (n = 1; format [1] == 's' || format [1] == 'S'; format++)
				n++;
			if (format [1] == ']')
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
				time_split = split_time (signed_number);

			for (n = 1; format [1] == 'h' || format [1] == 'H'; format++)
				n++;
			if (format [1] == ']')
				format++;
			if (time_display_elapsed){
				time_display_elapsed = FALSE;
				ignore_further_elapsed = TRUE;
				append_hour_elapsed (result, n, time_split, number);
			} else
				append_hour (result, n, time_split,
					     style_format_entry->want_am_pm);
			hour_seen = TRUE;
			break;
		}

		case 'A':
		case 'a':
			if (!time_split)
				time_split = split_time (signed_number);
			if (time_split->tm_hour < 12){
				g_string_append_c (result, *format);
				format++;
				if (*format == 'm' || *format == 'M'){
					g_string_append_c (result, *format);
					if (*(format+1) == '/')
						format++;
				}
			} else {
				if (*(format+1) == 'm' || *(format+1) == 'M')
					format++;
				if (*(format+1) == '/')
					format++;
			}
			break;

		case 'P': case 'p':
			if (!time_split)
				time_split = split_time (signed_number);
			if (time_split->tm_hour >= 12){
				g_string_append_c (result, *format);
				if (*(format+1) == 'm' || *(format+1) == 'M'){
					format++;
					g_string_append_c (result, *format);
				}
			} else {
				if (*(format+1) == 'm' || *(format+1) == 'M')
					format++;
			}
			break;

		default:
			break;
		}
		format++;
	}

	if (!info.rendered && can_render_number)
		do_render_number (number, &info, result);

	/* This is kinda ugly.  It does not handle variable width fonts */
	if (fill_char != '\0') {
		int count = col_width - result->len;
		while (count-- > 0)
			g_string_insert_c (result, fill_start, fill_char);
	}

 finish:
	res = g_strdup (result->str);
	g_string_free (result, TRUE);
	return res;
}

static gboolean
check_valid (const StyleFormatEntry *entry, const Value *value)
{
	switch (value->type){

	case VALUE_STRING:
		return entry->restriction_type == '@';

	case VALUE_FLOAT:
		switch (entry->restriction_type){

		case '*':
			return TRUE;
		case '<':
			return value->v_float.val < entry->restriction_value;
		case '>':
			return value->v_float.val > entry->restriction_value;
		case '=':
			return value->v_float.val == entry->restriction_value;
		case ',':
			return value->v_float.val <= entry->restriction_value;
		case '.':
			return value->v_float.val >= entry->restriction_value;
		case '+':
			return value->v_float.val != entry->restriction_value;
		default:
			return FALSE;
		}

	case VALUE_INTEGER:
		switch (entry->restriction_type){

		case '*':
			return TRUE;
		case '<':
			return value->v_int.val < entry->restriction_value;
		case '>':
			return value->v_int.val > entry->restriction_value;
		case '=':
			return value->v_int.val == entry->restriction_value;
		case ',':
			return value->v_int.val <= entry->restriction_value;
		case '.':
			return value->v_int.val >= entry->restriction_value;
		case '+':
			return value->v_int.val != entry->restriction_value;
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
fmt_general_float (gnum_float val, float col_width)
{
	double tmp;
	int log_val, prec;

	if (col_width < 0.)
		return g_strdup_printf ("%.*g", DBL_DIG, val);

	if (val < 0.) {
		/* leave space for minus sign */
		/* FIXME : idealy we would use the width of a minus sign */
		col_width -= 1.;
		tmp = log10 (-val);
	} else
		tmp = (val > 0.) ? log10 (val) : 0;

	/* leave space for the decimal */
	/* FIXME : idealy we would use the width of a decimal point */
	prec = (int) floor (col_width - 1.);
	if (prec < 0)
		prec = 0;

	if (tmp > 0.) {
		log_val = ceil (tmp);

		/* Decrease precision to leave space for the E+00 */
		if (log_val > prec)
			for (prec -= 4; log_val >= 100 ; log_val /= 10)
				prec--;
	} else {
		log_val = floor (tmp);

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
	else if (prec > DBL_DIG)
		prec = DBL_DIG;

	/* FIXME : glib bug.  it does not handle G, use g (fixed in 1.2.9) */
	return g_strdup_printf ("%.*g", prec, val);
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
			log_val = ceil (log10 (-val));
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
format_value (StyleFormat *format, const Value *value, StyleColor **color,
	      float col_width)
{
	char *v = NULL;
	StyleFormatEntry entry;
	gboolean is_general = FALSE;
	GList *list;

	if (color)
		*color = NULL;

	g_return_val_if_fail (value != NULL, "<ERROR>");

	if (format) {
		/* get format */
		for (list = format->entries; list; list = g_list_next (list))
			if (check_valid (list->data, value))
				break;

		if (list)
			entry = *(StyleFormatEntry *)(list->data);
		else
			entry.format = format->format;

		/* Try to parse a color specification */
		if (entry.format [0] == '['){
			char *end = strchr (entry.format, ']');

			if (end) {
				char first_after_bracket = entry.format [1];

				/*
				 * Special [h*], [m*], [*s] is using for
				 * and [$*] are for currencies.
				 * measuring times, not for specifying colors.
				 */
				if (!(first_after_bracket == 'h' ||
				      first_after_bracket == 's' ||
				      first_after_bracket == 'm' ||
				      first_after_bracket == '$')){
					if (color)
						*color = lookup_color (&entry.format [1], end);
					entry.format = end+1;
				}
			}
		}

		/* Empty formats should be ignored */
		if (entry.format [0] == '\0')
			return g_strdup ("");

		/* Formatting a value as a text returns the entered text */
		if (strcmp (entry.format, "@") == 0) {
			if (value->type == VALUE_STRING)
				return g_strdup (value->v_str.val->str);

			is_general = TRUE;
		} else if (strcmp (entry.format, "General") == 0)
			is_general = TRUE;
	} else
		is_general = TRUE;

	/*
	 * Use top left corner of an array result.
	 * This wont work for ranges because we dont't have a location
	 */
	if (value->type == VALUE_ARRAY)
		value = value_area_fetch_x_y (NULL, value, 0, 0);

	switch (value->type){
	case VALUE_FLOAT:
		if (!FINITE (value->v_float.val))
			return g_strdup (gnumeric_err_VALUE);

		if (is_general) {
			gnum_float val = value->v_float.val;
			if ((gnum_float)INT_MAX >= val && val >= (gnum_float)INT_MIN) {
				double int_val = floor (value->v_float.val);
				if (int_val == value->v_float.val)
					return fmt_general_int (int_val, col_width);
			}
			return fmt_general_float (val, col_width);
		}
		v = format_number (value->v_float.val, (int)col_width, &entry);
		break;

	case VALUE_INTEGER:
		if (is_general)
			return fmt_general_int (value->v_int.val, col_width);
		v = format_number (value->v_int.val, (int)col_width, &entry);
		break;

	case VALUE_BOOLEAN:
		return g_strdup (value->v_bool.val ? _("TRUE"):_("FALSE"));

	case VALUE_ERROR:
		return g_strdup (value->v_err.mesg->str);

	case VALUE_STRING:
		return g_strdup (value->v_str.val->str);

	case VALUE_CELLRANGE:
		return g_strdup (gnumeric_err_VALUE);

	case VALUE_ARRAY:
		/* Array of arrays ?? */
		return g_strdup (_("ARRAY"));

	case VALUE_EMPTY:
		return g_strdup ("");

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

	color = lookup_color_by_name (ptr, end, translate_to_en);
	if (color != NULL) {
		g_string_append (res, translate_to_en
				 ?  _(color->name)
				 : color->name);
		g_string_append_c (res, ']');
		return end;
	}
	return NULL;
}

char *
style_format_delocalize (const char *descriptor_string)
{
	g_return_val_if_fail (descriptor_string != NULL, NULL);

	if (strcmp (descriptor_string, _("General"))) {
		char const thousands_sep = format_get_thousand ();
		char const decimal = format_get_decimal ();
		char const *ptr = descriptor_string;
		GString *res = g_string_sized_new (strlen (ptr));

		for ( ; *ptr ; ++ptr)
			if (*ptr == decimal)
				g_string_append_c (res, '.');
			else if (*ptr == thousands_sep)
				g_string_append_c (res, ',');
			else if (*ptr == '\\') {
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
					    if (ptr [1] != '\0') {
						    g_string_append_c (res, ptr[1]);
						    ++ptr;
					    }
					    break;
				default   : g_string_append_c (res, *ptr);
				};
			} else
				g_string_append_c (res, *ptr);


		{
			char *tmp = g_strdup (res->str);
			g_string_free (res, TRUE);
			return tmp;
		}
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
			    if (ptr [1] != '\0') {
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

	tmp = g_strdup (res->str);
	g_string_free (res, TRUE);
	return tmp;
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

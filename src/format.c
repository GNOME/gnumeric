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
#include <locale.h>
#include <limits.h>
#include <ctype.h>
#ifdef HAVE_IEEEFP_H
#    include <ieeefp.h>
#endif
#include "gnumeric.h"
#include "format.h"
#include "dates.h"
#include "parse-util.h"
#include "portability.h"
#include "datetime.h"
#include "mathfunc.h"

/* Points to the locale information for number display */
static struct lconv *lc = NULL;

#define DECIMAL_CHAR_OF_LC(lc) ((lc)->decimal_point[0] ? (lc)->decimal_point[0] : '.')
#define THOUSAND_CHAR_OF_LC(lc) ((lc)->thousands_sep[0] ? (lc)->thousands_sep[0] : ',')
#define CHAR_DECIMAL (CHAR_MAX + 1)
#define CHAR_THOUSAND (CHAR_MAX + 2)

static void style_entry_free (gpointer data, gpointer user_data);

typedef struct {
        char     *format;
	gboolean  want_am_pm;
        char      restriction_type;
        int       restriction_value;
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
 * A number specification is as described in the relavent portions of
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
	char *temp = g_alloca (n + 4);

	sprintf (temp, "%0*d", n,
		 want_am_pm
		 ? (time_split->tm_hour % 12)
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
	char *temp = g_alloca (n + 4);
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
	char *temp = g_alloca (n + 4);

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
	char *temp = g_alloca (n + 50);
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
	char *temp = g_alloca (n + 4);

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
	char *temp = g_alloca (n + 50);
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
void
format_compile (StyleFormat *format)
{
	GString *string = g_string_new ("");
	int i;
	int which = 0;
	int length = strlen (format->format);
	StyleFormatEntry standard_entries[4];
	StyleFormatEntry *temp;

	format_destroy (format);

	/* g_string_maybe_expand (string, length); */

	for (i = 0; i < length; i++){

		switch (format->format[i]){

		case ';':
			if (which < 4){
				standard_entries [which].format = g_malloc0 (string->len + 1);
				strncpy (standard_entries[which].format, string->str, string->len);
				standard_entries [which].format[string->len] = 0;
				standard_entries [which].restriction_type = '*';
				which++;
			}
			string = g_string_truncate (string, 0);
			break;
		default:
			string = g_string_append_c (string, format->format [i]);
			break;
		}
	}

	if (which < 4){
		standard_entries[which].format = g_malloc0 (string->len + 1);
		strncpy (standard_entries[which].format, string->str, string->len);
		standard_entries[which].format[string->len] = 0;
		standard_entries[which].restriction_type = '*';
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
		format->format_list = g_list_append (format->format_list, temp);
	}

	g_string_free (string, TRUE);
}

static void
style_entry_free (gpointer data, gpointer user_data)
{
	StyleFormatEntry *entry = data;

	g_free (entry->format);
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
	g_list_foreach (format->format_list, style_entry_free, NULL);
	g_list_free (format->format_list);
	format->format_list = NULL;
}

static struct {
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

static StyleColor *
lookup_color (const char *str, const char *end)
{
	int i;

	for (i = 0; format_colors [i].name; i++){
		int len = strlen (format_colors [i].name);

		if ((strncasecmp (format_colors [i].name, str, len) == 0) ||
		    (strncasecmp (_(format_colors [i].name), str, len) == 0)){
			style_color_ref (format_colors [i].color);
			return format_colors [i].color;
		}
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
	char c1000;
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

	c1000 = (lc->thousands_sep[0]) ? lc->thousands_sep[0] : ',';

	for (temp = number; temp >= 1.0; temp /= 10.0) {
		int digit;

		if (use_thousand_sep) {
			group++;
			if (group == 4){
				group = 1;
				g_string_prepend_c (number_string, c1000);
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
		g_string_append (number_string, lc->decimal_point);
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
	char *decimal_point, *append_after_number;
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

static char *
do_render_number (gdouble number, format_info_t *info)
{
	GString *res;
	char *result;
	char decimal_point [2];

	info->rendered = 1;

	/*
	 * If the format contains only "#"s to the left of the decimal
	 * point, number in the [0.0,1.0] range are prefixed with a
	 * decimal point
	 */
	if (number > 0.0 && number < 1.0 && info->right_allowed == 0 && info->right_optional > 0){
		decimal_point [0] = lc->decimal_point [0];
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

	res = render_number (
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

	if (info->append_after_number)
		g_string_append (res, info->append_after_number);

	result = g_strdup (res->str);
	g_string_free (res, TRUE);

	return result;
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
 * Returns a new format string with the thousand separator
 * or NULL if the format string already contains the thousand
 * separator
 */
char *
format_add_thousand (const char *format_string)
{
	char *b;

	if (!lc)
		lc = localeconv ();

	if (strcmp (format_string, "General") == 0){
		char *s = g_strdup ("#!##0");

		s [1] = THOUSAND_CHAR_OF_LC (lc);
		return s;
	}

	if (strchr (format_string, THOUSAND_CHAR_OF_LC (lc)) != NULL)
		return NULL;

	b = g_malloc (strlen (format_string) + 7);
	if (!b)
		return NULL;

	strcpy (b, "#!##0");
	b [1] = THOUSAND_CHAR_OF_LC (lc);

	strcpy (&b[5], format_string);

	return b;
}

/*
 * Finds the decimal char in @str doing the proper parsing of a
 * format string
 */
static char const *
find_decimal_char (char const *str)
{
	char dc = DECIMAL_CHAR_OF_LC (lc);

	for (;*str; str++){
		if (*str == dc)
			return str;

		if (*str == THOUSAND_CHAR_OF_LC (lc))
			continue;

		switch (*str){
			/* These ones do not have any argument */
		case '#': case '?': case '0': case '%':
		case '-': case '+': case ')': case '£':
		case ':': case '$':
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
format_remove_decimal (const char *format_string)
{
	int offset = 1;
	char *ret, *p;
	char const *tmp;

	if (!lc)
		lc = localeconv ();

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

	ret = p = g_strdup (tmp);

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
format_add_decimal (const char *format_string)
{
	char const *pre = NULL;
	char const *post = NULL;
	char *res;

	if (!lc)
		lc = localeconv ();

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
	res [pre-format_string + 0] = DECIMAL_CHAR_OF_LC(lc);
	res [pre-format_string + 1] = '0';
	strcpy (res + (pre - format_string) + 2, post);

	return res;
}

static gchar *
format_number (gdouble number, const StyleFormatEntry *style_format_entry)
{
	GString *result = g_string_new ("");
	const char *format = style_format_entry->format;
	format_info_t info;
	int can_render_number = 0;
	int hour_seen = 0;
	gboolean time_display_elapsed = FALSE;
	gboolean ignore_further_elapsed = FALSE;
	struct tm *time_split = 0;
	char *res;
	gdouble signed_number;

	if (!lc)
		lc = localeconv ();

	memset (&info, 0, sizeof (info));
	signed_number = number;
	if (number < 0.0){
		info.negative = TRUE;
		number = -number;
	}

	while (*format){
		int c = *format;
		if (c == DECIMAL_CHAR_OF_LC (lc))
			c = CHAR_DECIMAL;
		else if (c == THOUSAND_CHAR_OF_LC (lc))
			c = CHAR_THOUSAND;

		switch (c) {

		case '[':
			/* Currency symbol */
			if (format[1] == '$') {
				for (format += 2; *format && *format != ']' ; ++format)
					g_string_append_c (result, *format);
				if (*format == ']')
					++format;
			} else if (!ignore_further_elapsed)
				time_display_elapsed = TRUE;
			break;

		case '#':
			can_render_number = 1;
			if (info.decimal_separator_seen)
				info.right_optional++;
			break;

		case '?':
			can_render_number = 1;
			if (info.decimal_separator_seen)
				info.right_spaces++;
			else
				info.left_spaces++;
			break;

		case '0':
			can_render_number = 1;
			if (info.decimal_separator_seen){
				info.right_req++;
				info.right_allowed++;
				info.right_spaces++;
			} else {
				info.left_spaces++;
				info.left_req++;
			}
			break;

		case CHAR_DECIMAL: {
			int c = *(format+1);

			can_render_number = 1;
			if (c && (c != '0' && c != '#' && c != '?'))
				number /= 1000;
			else
				info.decimal_separator_seen = TRUE;
			break;
		}

		case CHAR_THOUSAND: {
			if (!can_render_number) {
				char c = lc->thousands_sep [0];
				if (c == 0)
					c = ',';
				g_string_append_c (result, c);
			} else
				info.comma_separator_seen = TRUE;
			break;
		}

		case 'E': case 'e':
			can_render_number = 1;
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
				char *buffer = g_alloca (40 + info.right_req + 2);

				sprintf (buffer, (info.scientific == 'e')
					 ? "%s%.*e"
					 : "%s%.*E",
					 info.negative
					 ? "-" :
					 info.scientific_shows_plus
					 ? "+" : "",
					 info.right_req, number);

				g_string_append (result, buffer);
				goto finish;
			}

		/* percent */
		case '%':
			can_render_number = 1;
			number *= 100;
			info.append_after_number = "%";
			break;

		case '\\':
			if (format[1] != '\0') {
				/* TODO TODO TODO : Do we need any more chars here ?? */
				if (format[1] == '-')
					info.supress_minus = TRUE;
				else if (can_render_number && !info.rendered) {
					char *s;
					s = do_render_number (number, &info);
					g_string_append (result, s);
					g_free (s);
				}

				format++;
				g_string_append_c (result, *format);
			}
			break;

		case '"': {
			if (can_render_number && !info.rendered) {
				char *s;
				s = do_render_number (number, &info);
				g_string_append (result, s);
				g_free (s);
			}

			for (format++; *format && *format != '"'; format++)
				g_string_append_c (result, *format);
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
			g_string_append_c (result, *format);
			break;

		case ')':
			if (can_render_number && !info.rendered) {
				char *ntxt;
				ntxt = do_render_number (number, &info);
				g_string_append (result, ntxt);
				g_free (ntxt);
			}
			g_string_append_c (result, *format);
			break;

		case '_':
			if (*(format+1))
				format++;
			g_string_append_c (result, ' ');
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
			format += append_day (result, format, time_split) -1;
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

		case '*':
		{
			/* FIXME FIXME FIXME
			 * this will require an interface change to pass in
			 * the size of the cell being formated as well as a
			 * minor reworking of the routines logic to fill in the
			 * space later.
			 */
			static gboolean quiet_warning = FALSE;
			if (quiet_warning)
				break;
			quiet_warning = TRUE;
			g_warning ("REPEAT FORMAT NOT YET SUPPORTED '%s' %g\n",
				   style_format_entry->format, number);
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
	if (!info.rendered && can_render_number){
		char *rendered_string = do_render_number (number, &info);

		g_string_append (result, rendered_string);
		g_free (rendered_string);
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

/*
 * Returns NULL when the value should be formated as text
 */
gchar *
format_value (StyleFormat *format, const Value *value, StyleColor **color,
	      char const * entered_text)
{
	char *v = NULL;
	StyleFormatEntry entry;
	GList *list;
	gboolean is_general = FALSE;

	g_return_val_if_fail (value != NULL, "<ERROR>");

	if (color)
		*color = NULL;

	/* get format */
	for (list = format->format_list; list; list = g_list_next (list))
		if (check_valid (list->data, value))
			break;

	if (list)
		entry = *(StyleFormatEntry *)(list->data);
	else
		entry.format = format->format;

	/* Try to parse a color specification */
	if (entry.format [0] == '['){
		char *end = strchr (entry.format, ']');

		if (end){
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
		if (entered_text != NULL)
			return g_strdup (entered_text);
		if (value->type == VALUE_STRING)
			return g_strdup (value->v_str.val->str);

		/* FIXME : What does it mean to format a value as text
		 * without specifying the entered text ??
		 * use General as a failsafe */
		is_general = TRUE;
	}

	/* FIXME: what about translated "General"?  */
	if (strcmp (entry.format, "General") == 0)
		is_general = TRUE;

	/*
	 * Use top left corner of an array result.
	 * This wont work for ranges because we dont't have a location
	 */
	if (value->type == VALUE_ARRAY)
		value = value_area_fetch_x_y (NULL, value, 0, 0);

	switch (value->type){
	case VALUE_FLOAT:
		if (finite (value->v_float.val)) {
			if (is_general) {
				/* FIXME FIXME FIXME : This is pathetic
				 * rendering for General should be done
				 * with knowledge of the cell width to
				 * compute the optimal format
				 */
				if (floor (value->v_float.val) == value->v_float.val)
					entry.format = "0";
				else
					entry.format = _("0.0########");
			}
			v = format_number (value->v_float.val, &entry);
		} else
			return g_strdup (gnumeric_err_VALUE);
		break;

	case VALUE_INTEGER:
		if (is_general)
			entry.format = "0";
		v = format_number (value->v_int.val, &entry);
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

char *
format_get_thousand (void)
{
	if (!lc)
		lc = localeconv ();

	return (lc)->thousands_sep[0] ? (lc)->thousands_sep : ",";
}

char *
format_get_decimal (void)
{
	if (!lc)
		lc = localeconv ();

	return (lc)->decimal_point[0] ? (lc)->decimal_point : ".";
}

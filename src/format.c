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
#include <libgnome/lib_date.h>
#include <locale.h>
#include "gnumeric.h"
#include "format.h"
#include "dates.h"

/* Points to the locale information for number display */
static struct lconv *lc;

static void style_entry_free (gpointer data, gpointer user_data);


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


static void
do_roundup (GString *string)
{
	int i;
  
	for (i = string->len - 1; string->str [i] == '9'; i--)
		string->str[i] = '0';

	if (string->str [i] == '.')
	{
		/* FIXME */
	}
	else
	{
		string->str [i]++;
	}
}

/*
 * Parses the year field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_year (GString *string, gchar *format, struct tm *time_split)
{
	char temp [5];
	
	if (format [1] != 'y'){
		g_string_append_c (string, 'y');
		return 1;
	}
	
	if (format [2] != 'y' || format [3] != 'y'){
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
append_month (GString *string, gchar *format, struct tm *time_split)
{
	char temp [3];
	
	if (format [1] != 'm'){
		sprintf (temp, "%d", time_split->tm_mon+1);
		g_string_append( string, temp);
		return 1;
	}
	
	if (format [2] != 'm')
	{
		sprintf (temp, "%02d", time_split->tm_mon+1);
		g_string_append (string, temp);
		return 2;
	}
	
	if (format [3] != 'm'){
		g_string_append (string, _(month_short [time_split->tm_mon])+1);
		return 3;
	}

	g_string_append (string, _(month_long [time_split->tm_mon]));
	return 4;
}

/*
 * Parses the hour field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_hour (GString *string, gchar *format, struct tm *time_split, int timeformat)
{
	char temp[3];

	if (format [1] != 'h'){
	    sprintf (temp, "%d", timeformat ? (time_split->tm_hour % 12) : time_split->tm_hour);
	    g_string_append (string, temp);
	    return 1;
	}
	sprintf (temp, "%02d", timeformat ? (time_split->tm_hour % 12) : time_split->tm_hour);
	g_string_append (string, temp);
	return 2;
}

/*
 * Parses the day field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_day (GString *string, gchar *format, struct tm *time_split)
{
	char temp[3];

	if (format [1] != 'd'){
		sprintf (temp, "%d", time_split->tm_mday);
		g_string_append (string, temp);
		return 1;
	}

	if (format [2] != 'd'){
		sprintf (temp, "%02d", time_split->tm_mday);
		g_string_append (string, temp);
		return 2;
	}

	if (format [3] != 'd'){
		g_string_append (string, _(day_short[time_split->tm_wday])+1);
		return 3;
	}

	g_string_append (string, _(day_long[time_split->tm_wday]));

	return 4;
}

/*
 * Parses the minute field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_minute (GString *string, gchar *format, struct tm *time_split)
{
	char temp [3];

	if (format [1] != 'm'){
		sprintf (temp, "%d", time_split->tm_min);
		g_string_append (string, temp);
		return 1;
	}

	sprintf (temp, "%02d", time_split->tm_min);
	g_string_append (string, temp);

	return 2;
}

/*
 * Parses the second field at the beginning of the format.  Returns the
 * number of characters used.
 */
static int
append_second (GString *string, gchar *format, struct tm *time_split)
{
	char temp[3];
	
	if (format [1] != 's'){
		sprintf (temp, "%d", time_split->tm_sec);
		g_string_append (string, temp);
		return 1;
	}

	sprintf (temp, "%02d", time_split->tm_sec);
	g_string_append (string, temp);

	return 2;
}

/*
 * Parses the day part field at the beginning of the format.  Returns
 * the number of characters used.
 */
static int
append_half (GString *string, gchar *format, struct tm *time_split)
{
	if (time_split->tm_hour <= 11){
		if (format [0] == 'a' || format [0] == 'p')
			g_string_append_c (string, 'a');
		else
			g_string_append_c (string, 'A');
	}
	else {
		if (format [0] == 'a' || format [0] == 'p')
			g_string_append_c (string, 'p');
		else
			g_string_append_c (string, 'P');
	}
	
	if (format [1] == 'm' || format [1] == 'M'){
		g_string_append_c (string, format [1]);
		return 2;
	} else
		return 1;
}

/*
 * Does some analisis before hand on the format
 */
static void
pre_parse_format (StyleFormatEntry *style)
{
	char *format;

	style->want_am_pm = 0;
	for (format = style->format; *format; format++){
		switch (*format){
		case '"':
			for (format++; *format && *format != '"'; format++)
				;
			if (*format)
				format++;
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
			if ((*(format+1) == 'm') ||
			   (*(format+1) == 'M'))
				style->want_am_pm = 1;
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
 * This routine should always return, it cant fail, in the worst
 * case it should just downgrade to stupid formatting
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
	
	g_list_free (format->format_list);
	format->format_list = 0;
	
	/* g_string_maybe_expand (string, length); */
	
	for (i = 0; i < length; i++){

		switch (format->format[i])
		{
		case ';':
			if (which < 4)
			{
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
	switch (which)
	{
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
	for (i = 0; i < which; i++)
	{
		temp = g_new (StyleFormatEntry, 1);
		*temp = standard_entries[i];
		pre_parse_format (temp);
		format->format_list = g_list_append (format->format_list, temp);
	}
	g_string_free (string, TRUE);
}

static void
style_entry_free(gpointer data, gpointer user_data)
{
	StyleFormatEntry *entry = data;
	
	g_free (entry->format);
	g_free (entry);
}

void
format_destroy (StyleFormat *format)
{
	/* This routine is invoked when the last user of the
	 * format is gone (ie, refcount has reached zero) just
	 * before the StyleFormat structure is actually released.
	 *
	 * resources allocated in format_compile should be disposed here
	 */
	
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
lookup_color (char *str, char *end)
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
	       char *show_decimal)
{
	GString *number_string = g_string_new ("");
	gint zero_count, nine_count;
	gdouble temp;
	int group = 0;
	
	for (temp = number; temp >= 1.0; temp /= 10.0){

		double r = floor (temp);
		int digit;
				  
		if (use_thousand_sep){
			group++;
			if (group == 4){
				int c;
				
				group = 1;
				if (lc->thousands_sep [0] == 0)
					c = ',';
				else
					c = lc->thousands_sep [0];
				
				g_string_prepend_c (number_string, c);
			}
		}
		
		digit = r - floor (r / 10) * 10;
		g_string_prepend_c (number_string, (digit) + '0');
		if (left_req > 0)
			left_req --;
		if (left_spaces > 0)
			left_spaces --;
		
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

      for (; right_req > 0; right_req --, right_allowed --, right_spaces --)
      {
	      gint digit;
	      temp *= 10.0;
	      digit = floor (temp);
	      temp -= floor (temp);
	      if (right_allowed == 1 && floor (temp * 10.0) >= 5)
	      {
		      if (digit < 9)
			      digit ++;
		      else
		      {
			      digit = 0;
			      do_roundup (number_string);
		      }
	      }
	      g_string_append_c (number_string, digit + '0');
      }
      
      zero_count = 0;
      nine_count = 0;
      
      for (; right_allowed > 0; right_allowed --)
      {
	      gint digit;
	      temp *= 10.0;
	      digit = floor (temp);
	      temp -= floor (temp);
	      
	      if (right_allowed == 1 && floor (temp * 10.0) >= 5)
	      {
		      if (digit < 9)
			      digit ++;
		      else
		      {
			      digit = 0;
			      right_spaces -= zero_count;
			      zero_count = nine_count;
			      right_spaces += zero_count;
			      do_roundup (number_string);
		      }
	      }
	      if (digit == 0)
		      zero_count ++;
	      else
	      {
		      right_spaces -= zero_count + 1;
		      zero_count = 0;
	      }
	      if (digit == 9)
		      nine_count ++;
	      else
		      nine_count = 0;
	      
	      g_string_append_c (number_string, digit + '0');
      }
      
      g_string_truncate (number_string, number_string->len - zero_count);
      
      for (; right_spaces > 0; right_spaces--)
      {
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
	double secs;

	tm.tm_year = 1900;
	tm.tm_mon  = 1;
	tm.tm_mday = 1;

	/* Day 1 means 1st of jannuary of 1900 */

	calc_new_date (&tm.tm_year, &tm.tm_mon, &tm.tm_mday, number-1.0);
	tm.tm_wday = day_of_week (tm.tm_year, tm.tm_mon, tm.tm_mday);
	if (tm.tm_wday == 7)
		tm.tm_wday = 0;
	
/*	printf ("Year: %d Month: %d, Day: %d\n", tm.tm_year, tm.tm_mon, tm.tm_mday); */
	tm.tm_mon--;

	tm.tm_year -= 1900;

	secs = (number - floor (number)) * 86400.0;
	tm.tm_hour = secs / 3600;
	secs -= tm.tm_hour * 3600;
	tm.tm_min  = secs / 60;
	secs -= tm.tm_min * 60;
	tm.tm_sec  = floor (secs);

	return &tm;
}

static gchar *
format_number (gdouble number, StyleFormatEntry *style_format_entry)
{
	GString *result = g_string_new ("");
	char *format = style_format_entry->format;
	format_info_t info;
	int can_render_number = 0;
	int hour_seen = 0;
	struct tm *time_split = 0;
	char *res;
	
	memset (&info, 0, sizeof (info));
	if (number < 0.0){
		info.negative = TRUE;
		number = -number;
	}

	if (!lc)
		lc = localeconv ();

	while (*format){
		switch (*format){
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
			
		case ',': case '.': {
			if (*format == lc->decimal_point [0]){
				int c = *(format+1);
				
				can_render_number = 1;
				if (c && (c != '0' && c != '#' && c != '?'))
					number /= 1000;
				else
					info.decimal_separator_seen = TRUE;
				break;
			} else {
				info.comma_separator_seen = TRUE;
				break;
			}
		}
		
		case 'E': case 'e':
			can_render_number = 1;
			info.scientific = TRUE;
			format++;
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
				char buffer [40];
				sprintf (buffer, "%g", number);
				
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
			if (*(format+1)){
				if (can_render_number && !info.rendered)
					g_string_append (result, do_render_number (number, &info));
				g_string_append_c (result, *format);
			}
			break;

		case '"': {
			if (can_render_number && !info.rendered)
				g_string_append (result, do_render_number (number, &info));

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
			g_string_append_c (result, *format);
			break;
			
		case ')':
			if (can_render_number && !info.rendered)
				g_string_append (result, do_render_number (number, &info));
			g_string_append_c (result, *format);
			break;

		case '_':
			if (*(format+1))
				format++;
			g_string_append_c (result, ' ');
			break;

		case 'm':
			if (!time_split)
				time_split = split_time (number);
			if (hour_seen)
				format += append_minute (result, format, time_split) - 1;
			else
				format += append_month (result, format, time_split) - 1;
			break;

		case 'd':
			if (!time_split)
				time_split = split_time (number);
			format += append_day (result, format, time_split) -1;
			break;

		case 'y':
			if (!time_split)
				time_split = split_time (number);
			format += append_year (result, format, time_split) - 1;
			break;

		case 's':
			if (!time_split)
				time_split = split_time (number);
			format += append_second (result, format, time_split) - 1;
			break;

		case '*':
			g_warning ("REPEAT FORMAT NOT YET SUPPORTED\n");
			break;

		case 'h':
			if (!time_split)
				time_split = split_time (number);
			format += append_hour (result, format, time_split, style_format_entry->want_am_pm) - 1;
			hour_seen = TRUE;
			break;
			
		case 'A':
		case 'a':
			if (!time_split)
				time_split = split_time (number);
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
				time_split = split_time (number);
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
check_valid (StyleFormatEntry *entry, Value *value)
{
  switch (value->type)
    {
    case VALUE_STRING:
      return entry->restriction_type == '@';
    case VALUE_FLOAT:
      switch (entry->restriction_type)
	{
	case '*': 
	  return TRUE;
	case '<':
	  return value->v.v_float < entry->restriction_value;
	case '>':
	  return value->v.v_float > entry->restriction_value;
	case '=':
	  return value->v.v_float == entry->restriction_value;
	case ',':
	  return value->v.v_float <= entry->restriction_value;
	case '.':
	  return value->v.v_float >= entry->restriction_value;
	case '+':
	  return value->v.v_float != entry->restriction_value;
	default:
	  return FALSE;
	}
    case VALUE_INTEGER:
      switch (entry->restriction_type)
	{
	case '*': 
	  return TRUE;
	case '<':
	  return value->v.v_int < entry->restriction_value;
	case '>':
	  return value->v.v_int > entry->restriction_value;
	case '=':
	  return value->v.v_int == entry->restriction_value;
	case ',':
	  return value->v.v_int <= entry->restriction_value;
	case '.':
	  return value->v.v_int >= entry->restriction_value;
	case '+':
	  return value->v.v_int != entry->restriction_value;
	default:
	  return FALSE;
	}      
    default:
      return FALSE;
    }
}

gchar *
format_value (StyleFormat *format, Value *value, StyleColor **color)
{
	char *v = NULL;
	StyleFormatEntry entry;
	GList *list;
	int is_general = 0;
	
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
			if (color)
				*color = lookup_color (&entry.format [1], end);
			entry.format = end+1;
		}
	}

	if (strcmp (entry.format, "General") == 0){
		entry.format += 7;
		is_general = 1;
	} 
	
	switch (value->type){
	case VALUE_FLOAT:
		if (is_general){
			if (floor (value->v.v_float) == value->v.v_float)
				entry.format = "0";
			else
				entry.format = "0.##";
		}
		if (finite (value->v.v_float))
			v = format_number (value->v.v_float, &entry);
		else
			return g_strdup ("#VAL");
		break;
		
	case VALUE_INTEGER:
		if (is_general)
			entry.format = "0";
		v = format_number (value->v.v_int, &entry);
		break;
		
	case VALUE_STRING:
		return g_strdup (value->v.str->str);
		
	default:
		return g_strdup ("Internal error");
	}
	
	/* Format error, return a default value */
	if (v == NULL)
		return value_string (value);
	
	return v;
}


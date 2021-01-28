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
 * Authors:
 *   Morten Welinder (terra@gnome.org)
 *   Miguel de Icaza (miguel@gnu.org)
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <number-match.h>

#include <gutils.h>
#include <style.h>
#include <gnm-format.h>
#include <value.h>
#include <mathfunc.h>
#include <numbers.h>
#include <gnm-datetime.h>
#include <goffice/goffice.h>

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <time.h>
#undef DEBUG_NUMBER_MATCH

/*
 * value_is_error: Check to see if a string begins with one of the magic
 * error strings.
 *
 * @str: The string to test
 *
 * Returns: an error if there is one, or %NULL.
 */
static GnmValue *
value_is_error (char const *str)
{
	GnmStdError e;

	if (str[0] != '#')
		return NULL;

	for (e = (GnmStdError)0; e < GNM_ERROR_UNKNOWN; e++)
		if (0 == strcmp (str, value_error_name (e, TRUE)))
			return value_new_error_std (NULL, e);

	return NULL;
}

/**
 * format_match_simple:
 * @text: A String to match against.
 *
 * Attempt to match the supplied string as a simple value.
 *
 * WARNING WARNING WARNING : This routine should NEVER be changed to match
 *				VALUE_STRING that will break the parsers
 *				handling of named expressions.
 */
GnmValue *
format_match_simple (char const *text)
{
	/* Is it a boolean?  */
	if (0 == g_ascii_strcasecmp (text, go_locale_boolean_name (TRUE)))
		return value_new_bool (TRUE);
	if (0 == g_ascii_strcasecmp (text, go_locale_boolean_name (FALSE)))
		return value_new_bool (FALSE);

	/* Is it an error?  */
	{
		GnmValue *err = value_is_error (text);
		if (err != NULL)
			return err;
	}

	/* Is it a floating-point number  */
	{
		char *end;
		gnm_float d;

		d = gnm_utf8_strto (text, &end);
		if (text != end && errno != ERANGE && gnm_finite (d)) {
			/* Allow and ignore spaces at the end.  */
			while (g_ascii_isspace (*end))
				end++;
			if (*end == '\0')
				return value_new_float (d);
		}
	}

	return NULL;
}

static struct {
	char *lc_time;
	GORegexp re_MMMMddyyyy;
	GORegexp re_ddMMMMyyyy;
	GORegexp re_yyyymmdd1;
	GORegexp re_yyyymmdd2;
	GORegexp re_yyyymmdd3;
	GORegexp re_yyyymmdd4;
	GORegexp re_mmddyyyy;
	GORegexp re_mmdd;
	GORegexp re_hhmmss1;
	GORegexp re_hhmmss2;
	GORegexp re_hhmmssds;
	GORegexp re_hhmmss_ampm;
} datetime_locale;


static void
datetime_locale_clear (void)
{
	g_free (datetime_locale.lc_time);
	go_regfree (&datetime_locale.re_MMMMddyyyy);
	go_regfree (&datetime_locale.re_ddMMMMyyyy);
	go_regfree (&datetime_locale.re_yyyymmdd1);
	go_regfree (&datetime_locale.re_yyyymmdd2);
	go_regfree (&datetime_locale.re_yyyymmdd3);
	go_regfree (&datetime_locale.re_yyyymmdd4);
	go_regfree (&datetime_locale.re_mmddyyyy);
	go_regfree (&datetime_locale.re_mmdd);
	go_regfree (&datetime_locale.re_hhmmss1);
	go_regfree (&datetime_locale.re_hhmmss2);
	go_regfree (&datetime_locale.re_hhmmssds);
	go_regfree (&datetime_locale.re_hhmmss_ampm);
	memset (&datetime_locale, 0, sizeof (datetime_locale));
}

static char const *
my_regerror (int err, GORegexp const *preg)
{
	static char buffer[1024];
	go_regerror (err, preg, buffer, sizeof (buffer));
	return buffer;
}

static void
datetime_locale_setup1 (GORegexp *rx, char const *pat)
{
	int ret = go_regcomp (rx, pat, GO_REG_ICASE);
	if (ret) {
		g_warning ("Failed to compile rx \"%s\": %s\n",
			   pat,
			   my_regerror (ret, rx));
	}
}


static void
datetime_locale_setup (char const *lc_time)
{
	GString *p_MMMM = g_string_sized_new (200);
	GString *p_MMM = g_string_sized_new (200);
	GString *p_decimal = g_string_sized_new (10);
	char *s;
	int m;

	datetime_locale.lc_time = g_strdup (lc_time);

	for (m = 1; m <= 12; m++) {
		if (m != 1)
			g_string_append_c (p_MMMM, '|');
		g_string_append_c (p_MMMM, '(');
		s = go_date_month_name (m, FALSE);
		go_regexp_quote (p_MMMM, s);
		g_free (s);
		g_string_append_c (p_MMMM, ')');

		if (m != 1)
			g_string_append_c (p_MMM, '|');
		g_string_append_c (p_MMM, '(');
		s = go_date_month_name (m, TRUE);
		go_regexp_quote (p_MMM, s);
		/* nb_NO actually adds a "." for these abbreviations.  */
		if (g_unichar_ispunct (g_utf8_get_char (g_utf8_prev_char (p_MMM->str + p_MMM->len))))
			g_string_append_c (p_MMM, '?');
		g_free (s);
		g_string_append_c (p_MMM, ')');
	}

	go_regexp_quote (p_decimal, go_locale_get_decimal ()->str);

	/*
	 * "Dec 1, 2000"
	 * "Dec/1/04"
	 * "December 1, 2000"
	 * "December 1 2000"
	 * "Dec-1-2000"
	 * "Dec 1"
	 * "Dec/1"
	 * "December 1"
	 * "Dec-1"
	 * "Jan 2010"
	 * "January 2010"
	 */
	s = g_strconcat ("^(",
			 p_MMMM->str,
			 "|",
			 p_MMM->str,
			 ")(-|/|\\s)(\\d+)((,?\\s+|-|/)(\\d+))?\\b",
			 NULL);
	datetime_locale_setup1 (&datetime_locale.re_MMMMddyyyy, s);
	g_free (s);

	/*
	 * "1-Dec-2000"
	 * "1/Dec/04"
	 * "1-December-2000"
	 * "1. december 2000"
	 * "1. december, 2000"
	 * "1-Dec"
	 * "1/Dec"
	 * "1-December"
	 * "1. december"
	 */
	s = g_strconcat ("^(\\d+)(-|/|\\.?\\s*)(",
			 p_MMMM->str,
			 "|",
			 p_MMM->str,
			 ")((,?\\s*|-|/)(\\d+))?\\b",
			 NULL);
	datetime_locale_setup1 (&datetime_locale.re_ddMMMMyyyy, s);
	g_free (s);

	/*
	 * "20001231"
	 * (with special support for 20001231:123456)
	 */
	s = g_strconcat ("^(\\d\\d\\d\\d)(\\d\\d)(\\d\\d)(:\\d\\d\\d\\d\\d\\d(",
			 p_decimal->str,
			 "\\d*)?)?\\s*$",
			 NULL);
	datetime_locale_setup1 (&datetime_locale.re_yyyymmdd1, s);
	g_free (s);

	/*
	 * ISO 8601 UTC date+time (date + "T" + time + "Z")
	 * Date: "2000-12-31" or without dashes.
	 * Time: "12:34:56.123" or without colons or with fewer parts.
	 */
	s = g_strconcat ("^(\\d\\d\\d\\d)(-?)(\\d\\d)\\2(\\d\\d)T"
			 "\\d+(:?)\\d+(\\5\\d+(",
			 p_decimal->str,
			 "\\d*)?)?(Z)\\s*$",
			 NULL);
	datetime_locale_setup1 (&datetime_locale.re_yyyymmdd2, s);
	g_free (s);

	/*
	 * "1900/01/01"
	 * "1900-1-1"
	 */
	datetime_locale_setup1 (&datetime_locale.re_yyyymmdd3,
				"^(\\d\\d\\d\\d)[-/.](\\d+)[-/.](\\d+)\\b");

	/*
	 * "2000-Oct-10"
	 */
	s = g_strconcat ("^(\\d\\d\\d\\d)[-/.](",
			 p_MMM->str,
			 ")[-/.](\\d+)\\b",
			 NULL);
	datetime_locale_setup1 (&datetime_locale.re_yyyymmdd4, s);
	g_free (s);

	/*
	 * "01/31/2001"    [Jan 31] if month_before_day
	 * "1/2/88"        [Jan 2]  if month_before_day
	 * "1/2/88"        [Feb 1]  if !month_before_day
	 * "31/1/2001"     [Jan 31] if !month_before_day
	 */
	datetime_locale_setup1 (&datetime_locale.re_mmddyyyy,
				"^(\\d+)[-/.](\\d+)[-/.](\\d+)\\b");

	/*
	 * "2005/2"   [Feb 1]
	 * "2/2005"   [Feb 1]
	 * "01/31"    [Jan 31] if month_before_day
	 * "31/1"     [Jan 31] if !month_before_day
	 */
	datetime_locale_setup1 (&datetime_locale.re_mmdd,
				"^(\\d+)([-/.])(\\d+)\\b");

	/*
	 * "15:30:00.3"
	 * "30:00.3"            [A little more than 30min]
	 * "115:30:00.3"
	 */
	/* ^(((\d+):)?(\d+):)?(\d+.\d*)\s*$ */
	s = g_strconcat ("^(((\\d+):)?(\\d+):)?(\\d+",
			 p_decimal->str,
			 "\\d*)\\s*$",
			 NULL);
	datetime_locale_setup1 (&datetime_locale.re_hhmmssds, s);
	g_free (s);

	/*
	 * "15:30:00"
	 * "15:30"          [15:30:00] if prefer_hour
	 * "15:30"          [00:15:30] if !prefer_hour
	 */
	datetime_locale_setup1 (&datetime_locale.re_hhmmss1,
				"^(\\d+):(\\d+)(:(\\d+))?\\s*$");

	/*
	 * "153000"
	 * "153000.2"
	 */
	s = g_strconcat ("^(\\d\\d)(\\d\\d)(\\d\\d)?(",
			 p_decimal->str,
			 "\\d*)?\\s*$",
			 NULL);
	datetime_locale_setup1 (&datetime_locale.re_hhmmss2, s);
	g_free (s);

	/*
	 * "12:30:01.3 am"
	 * "12:30:01 am"
	 * "12:30 am"
	 * "12am"
	 */
	s = g_strconcat ("^(\\d+)(:(\\d+)(:(\\d+(",
			 p_decimal->str,
			 "\\d*)?))?)?\\s*((am)|(pm))\\s*$",
			 NULL);
	datetime_locale_setup1 (&datetime_locale.re_hhmmss_ampm, s);
	g_free (s);

	g_string_free (p_MMMM, TRUE);
	g_string_free (p_MMM, TRUE);
	g_string_free (p_decimal, TRUE);
}

static int
find_month (GORegmatch const *pm)
{
	int m;

	for (m = 1; m <= 12; m++) {
		if (pm->rm_so != pm->rm_eo)
			return m;
		pm++;
	}

	return -1;
}

static int
handle_int (char const *text, GORegmatch const *pm, int min, int max, int maxlen)
{
	int i = 0;
	char const *p = text + pm->rm_so;
	char const *end = text + pm->rm_eo;
	int len = 0;

	while (p != end) {
		gunichar uc = g_utf8_get_char (p);
		p = g_utf8_next_char (p);
		i = (10 * i) + g_unichar_digit_value (uc);
		len++;

		if (i > max || len > maxlen)
			return -1;
	}

	if (i >= min)
		return i;
	else
		return -1;
}

static int
handle_day (char const *text, GORegmatch const *pm)
{
	return handle_int (text, pm, 1, 31, 2);
}

static int
handle_month (char const *text, GORegmatch const *pm)
{
	return handle_int (text, pm, 1, 12, 2);
}

static int
current_year (void)
{
	time_t now = time (NULL);
	struct tm *tm = localtime (&now);
	return 1900 + tm->tm_year;
}

static int
handle_year (char const *text, GORegmatch const *pm)
{
	int y;

	if (pm->rm_so == pm->rm_eo)
		return current_year ();

	y = handle_int (text, pm, 0, 9999, 4);

	if (y < 0)
		return -1;
	else if (y <= 29)
		return 2000 + y;
	else if (y <= 99)
		return 1900 + y;
	else if (y < (gnm_datetime_allow_negative () ? 1582 : 1900))
		return -1;
	else
		return y;
}


static gnm_float
handle_float (char const *text, GORegmatch const *pm)
{
	gnm_float val = 0;
	char const *p;
	char const *end;
	gnm_float num = 10;

	/* Empty means zero.  */
	if (pm->rm_so == pm->rm_eo)
		return 0;

	p = text + pm->rm_so;
	end = text + pm->rm_eo;
	while (p != end) {
		gunichar uc = g_utf8_get_char (p);
		int d = g_unichar_digit_value (uc);
		p = g_utf8_next_char (p);
		if (d < 0) break;  /* Must be decimal sep.  */
		val = (10 * val) + d;
	}

	while (p != end) {
		gunichar uc = g_utf8_get_char (p);
		int d = g_unichar_digit_value (uc);
		p = g_utf8_next_char (p);
		val += d / num;
		num *= 10;
	}

	return val;
}

static void
fixup_hour_ampm (gnm_float *hour, const GORegmatch *pm)
{
	gboolean is_am = (pm->rm_so != pm->rm_eo);

	if (*hour < 1 || *hour > 12) {
		*hour = -1;
		return;
	}

	if (*hour == 12)
		*hour = 0;
	if (!is_am)
		*hour += 12;
}

static gboolean
valid_hms (gnm_float h, gnm_float m, gnm_float s,
	   gboolean allow_elapsed, char *elapsed)
{
	gboolean h_ok = h >= 0 && h < 24;
	gboolean m_ok = m >= 0 && m < 60;
	gboolean s_ok = s >= 0 && s < 60;

	/* Boring old clock time.  */
	if (h_ok && m_ok && s_ok) {
		if (elapsed)
			*elapsed = 0;
		return TRUE;
	}

	if (!allow_elapsed)
		return FALSE;

	if (*elapsed == 'h' && m_ok && s_ok)
		return TRUE;

	if (*elapsed == 'm' && h == 0 && s_ok)
		return TRUE;

	if (*elapsed == 's' && h == 0 && m == 0)
		return TRUE;

	return FALSE;
}

#define DO_SIGN(sign,uc,action)					\
	{							\
		if (uc == '-' || uc == UNICODE_MINUS_SIGN_C) {	\
			sign = '-';				\
			action;					\
		} else if (uc == '+') {				\
			sign = '+';				\
			action;					\
		}						\
	}

#define SKIP_DIGITS(text) while (g_ascii_isdigit (*(text))) (text)++

#define SKIP_SPACES(text)						\
	while (*(text) && g_unichar_isspace (g_utf8_get_char (text)))	\
		(text) = g_utf8_next_char (text)


static GnmValue *
format_match_time (char const *text, gboolean allow_elapsed,
		   gboolean prefer_hour, gboolean add_format)
{
	char sign = 0;
	gunichar uc;
	gnm_float hour, minute, second;
	gnm_float time_val;
	GORegmatch match[10];
	char const *time_format = NULL;
	GnmValue *v;

	SKIP_SPACES (text);

	/* AM/PM means hour is needed.  No sign allowed.     */
	/* ^(\d+)(:(\d+)(:(\d+(.\d*)?))?)?\s*((am)|(pm))\s*$ */
	/*  1    2 3    4 5   6              78    9         */
	if (go_regexec (&datetime_locale.re_hhmmss_ampm, text, G_N_ELEMENTS (match), match, 0) == 0) {
		hour = handle_float (text, match + 1);
		fixup_hour_ampm (&hour, match + 8);
		minute = handle_float (text, match + 3);
		second = handle_float (text, match + 5);
		if (valid_hms (hour, minute, second, FALSE, NULL)) {
			time_format = "h:mm:ss AM/PM";
			goto got_time;
		}
	}

	uc = g_utf8_get_char (text);
	if (allow_elapsed) {
		DO_SIGN (sign, uc, {
			text = g_utf8_next_char (text);
		});
	}

	/* If fractional seconds are present, we know the layout.  */
	/* ^(((\d+):)?(\d+):)?(\d+.\d*)\s*$ */
	/*  123       4       5             */
	if (go_regexec (&datetime_locale.re_hhmmssds, text, G_N_ELEMENTS (match), match, 0) == 0) {
		char elapsed =
			match[3].rm_so != match[3].rm_eo
			? 'h'
			: (match[4].rm_so != match[4].rm_eo
			   ? 'm'
			   : 's');

		hour = handle_float (text, match + 3);
		minute = handle_float (text, match + 4);
		second = handle_float (text, match + 5);

		if (valid_hms (hour, minute, second, allow_elapsed, &elapsed)) {
			time_format = elapsed ? "[h]:mm:ss" : "h:mm:ss";
			goto got_time;
		}
	}

	/* ^(\d+):(\d+)(:(\d+))?\s*$ */
	/*  1     2    3 4           */
	if (go_regexec (&datetime_locale.re_hhmmss1, text, G_N_ELEMENTS (match), match, 0) == 0) {
		gboolean has_all = (match[4].rm_so != match[4].rm_eo);
		char elapsed;
		const char *time_format_elapsed;

		if (prefer_hour || has_all) {
			hour = handle_float (text, match + 1);
			minute = handle_float (text, match + 2);
			second = handle_float (text, match + 4);
			time_format = has_all ? "h:mm:ss" : "h:mm";
			time_format_elapsed = has_all ? "[h]:mm:ss" : "[h]:mm";
			elapsed = 'h';
		} else {
			hour = 0;
			minute = handle_float (text, match + 1);
			second = handle_float (text, match + 2);
			time_format = "mm:ss";
			time_format_elapsed = "[m]:ss";
			elapsed = 'm';
		}

		if (valid_hms (hour, minute, second, allow_elapsed, &elapsed)) {
			if (elapsed)
				time_format = time_format_elapsed;
			goto got_time;
		}
	}

	/* ^(\d\d)(\d\d)(\d\d)?(\.\d*)?\s*$   */
	/*  1     2     3      4              */
	if (go_regexec (&datetime_locale.re_hhmmss2, text, G_N_ELEMENTS (match), match, 0) == 0) {
		gboolean has3 = (match[3].rm_so != match[3].rm_eo);
		gboolean hasfrac = (match[4].rm_so != match[4].rm_eo);
		char elapsed;
		const char *time_format_elapsed;

		if ((prefer_hour && !hasfrac) || has3) {
			hour = handle_float (text, match + 1);
			minute = handle_float (text, match + 2);
			second = handle_float (text, match + 3) + handle_float (text, match + 4);
			time_format = "h:mm:ss";
			time_format_elapsed = "[h]:mm:ss";
			elapsed = 'h';
		} else {
			hour = 0;
			minute = handle_float (text, match + 1);
			second = handle_float (text, match + 2) + handle_float (text, match + 4);
			time_format = "mm:ss";
			time_format_elapsed = "[m]:ss";
			elapsed = 'm';
		}

		if (valid_hms (hour, minute, second, allow_elapsed, &elapsed)) {
			if (elapsed)
				time_format = time_format_elapsed;
			goto got_time;
		}
	}

	return NULL;

 got_time:
	time_val = (second + 60 * (minute + 60 * hour)) / (24 * 60 * 60);
	if (sign == '-')
		time_val = 0 - time_val;
	v = value_new_float (time_val);

	if (add_format) {
		GOFormat *fmt = go_format_new_from_XL (time_format);
		value_set_fmt (v, fmt);
		go_format_unref (fmt);
	}

	return v;
}

static gboolean
valid_dmy (int d, int m, int y)
{
	/* Avoid sign-induced problem.  d and m are capped.  */
	return y >= 0 && g_date_valid_dmy (d, m, y);
}


GnmValue *
format_match_datetime (char const *text,
		       GODateConventions const *date_conv,
		       gboolean month_before_day,
		       gboolean add_format,
		       gboolean presume_date)
{
	int day, month, year;
	GDate date;
	gnm_float time_val, date_val;
	char const *lc_time = setlocale (LC_TIME, NULL);
	GORegmatch match[31];
	gunichar uc;
	int dig1;
	char *date_format = NULL;
	GnmValue *res = NULL;
	char *time_format = NULL;
	int timeend = 0;
	const char *format_sep = " ";

	if (lc_time != datetime_locale.lc_time &&
	    (lc_time == NULL ||
	     datetime_locale.lc_time == NULL ||
	     strcmp (lc_time, datetime_locale.lc_time))) {
		datetime_locale_clear ();
		datetime_locale_setup (lc_time);
	}

	SKIP_SPACES (text);
	uc = g_utf8_get_char (text);
	dig1 = g_unichar_digit_value (uc);

	/* ^(MMMM)(-|/|\s)(\d+)((,\s+|-|/)(\d+))?\b */
	/*  1     26      27   28         30        */
	/*                      29                  */
	if (dig1 < 0 &&
	    go_regexec (&datetime_locale.re_MMMMddyyyy, text, G_N_ELEMENTS (match), match, 0) == 0) {
		month = find_month (&match[2]);
		if (month == -1) month = find_month (&match[2 + 12]);
		day = handle_day (text, match + 27);
		if (day == -1 &&
		    match[27].rm_eo - match[27].rm_so >= 4 &&
		    match[28].rm_so == match[28].rm_eo) {
			/* Only one number with 4+ digits -- might be a year.  */
			year = handle_year (text, match + 27);
			day = 1;
		} else {
			year = handle_year (text, match + 30);
		}
		if (valid_dmy (day, month, year)) {
			date_format = gnm_format_frob_slashes ("mmm/dd/yyyy");
			text += match[0].rm_eo;
			goto got_date;
		}
	}

	/* ^(\d+)(-|/|\.?\s*)(MMMM)((,?\s*|-|/)(\d+))?\b */
	/*  1    2           3     28          30        */
	/*                          29                   */
	if (dig1 >= 0 &&
	    go_regexec (&datetime_locale.re_ddMMMMyyyy, text, G_N_ELEMENTS (match), match, 0) == 0) {
		day = handle_day (text, match + 1);
		month = find_month (&match[4]);
		if (month == -1) month = find_month (&match[4 + 12]);
		year = handle_year (text, match + 30);
		if (valid_dmy (day, month, year)) {
			date_format = g_strdup ("d-mmm-yyyy");
			text += match[0].rm_eo;
			goto got_date;
		}
	}

	/* ^(\d\d\d\d)(\d\d)(\d\d)(:\d\d\d\d\d\d(\.\d*)?)?\s*$ */
	/*  1         2     3     4             5              */
	if (dig1 > 0 &&  /* Exclude zero.  */
	    go_regexec (&datetime_locale.re_yyyymmdd1, text, G_N_ELEMENTS (match), match, 0) == 0) {
		year = handle_year (text, match + 1);
		month = handle_month (text, match + 2);
		day = handle_day (text, match + 3);
		if (valid_dmy (day, month, year)) {
			date_format = g_strdup ("yyyy-mmm-dd");
			text += match[3].rm_eo;
			if (*text == ':')
				text++;
			goto got_date;
		}
	}

	// ^(\d\d\d\d)(-?)(\d\d)\2(\d\d)T\d+(:?)\d+(\5\d+(.\d*)?)?(Z)\s*$
	//  1         2   3       4         5      6     7        8
	if (dig1 > 0 &&  /* Exclude zero.  */
	    go_regexec (&datetime_locale.re_yyyymmdd2, text, G_N_ELEMENTS (match), match, 0) == 0) {
		year = handle_year (text, match + 1);
		month = handle_month (text, match + 3);
		day = handle_day (text, match + 4);
		if (valid_dmy (day, month, year)) {
			date_format = g_strdup ("yyyy-mm-dd");
			time_format = g_strdup ("hh:mm:ss\"Z\"");
			format_sep = "\"T\"";
			text += match[4].rm_eo + 1;
			timeend = match[8].rm_so - (match[4].rm_eo + 1);
			goto got_date;
		}
	}

	/* ^(\d\d\d\d)[-/.](\d\d)[-/.](\d\d)\b */
	/*  1              2          3        */
	if (dig1 > 0 &&  /* Exclude zero.  */
	    go_regexec (&datetime_locale.re_yyyymmdd3, text, G_N_ELEMENTS (match), match, 0) == 0) {
		year = handle_year (text, match + 1);
		month = handle_month (text, match + 2);
		day = handle_day (text, match + 3);
		if (valid_dmy (day, month, year)) {
			date_format = g_strdup ("yyyy-mmm-dd");
			text += match[0].rm_eo;
			goto got_date;
		}
	}

	/* ^(\d\d\d\d)[-/.](MMM)[-/.](\d+)\b */
	/*  1              2         15      */
	if (dig1 > 0 &&  /* Exclude zero.  */
	    go_regexec (&datetime_locale.re_yyyymmdd4, text, G_N_ELEMENTS (match), match, 0) == 0) {
		year = handle_year (text, match + 1);
		month = find_month (match + 3);
		day = handle_day (text, match + 15);
		if (valid_dmy (day, month, year)) {
			date_format = g_strdup ("yyyy-mmm-dd");
			text += match[0].rm_eo;
			goto got_date;
		}
	}

	/* ^(\d+)[-/.](\d+)[-/.](\d+)\b */
	/*  1         2         3   */
	if (dig1 >= 0 &&
	    go_regexec (&datetime_locale.re_mmddyyyy, text, G_N_ELEMENTS (match), match, 0) == 0) {
		if (month_before_day) {
			month = handle_month (text, match + 1);
			day = handle_day (text, match + 2);
		} else {
			month = handle_month (text, match + 2);
			day = handle_day (text, match + 1);
		}
		year = handle_year (text, match + 3);
		if (valid_dmy (day, month, year)) {
			date_format = gnm_format_frob_slashes (month_before_day
							       ? "m/d/yyyy"
							       : "d/m/yyyy");
			text += match[0].rm_eo;
			goto got_date;
		}
	}

	/* ^(\d+)([-/.])(\d+)\b */
	/*  1    2      3       */
	if (dig1 >= 0 &&
	    go_regexec (&datetime_locale.re_mmdd, text, G_N_ELEMENTS (match), match, 0) == 0) {
		/*
		 * Unless we already have a date format, do not accept
		 * 1-10, for example.  See bug 376090.
		 */
		gboolean good_ddmmsep =
			presume_date ||
			text[match[2].rm_so] == '/';
		if (match[1].rm_eo - match[1].rm_so == 4) {
			year = handle_year (text, match + 1);
			month = handle_month (text, match + 3);
			day = 1;
			date_format = g_strdup ("yyyy/m");
		} else if (match[3].rm_eo - match[3].rm_so == 4) {
			month = handle_month (text, match + 1);
			year = handle_year (text, match + 3);
			day = 1;
			date_format = g_strdup ("m/yyyy");
		} else if (good_ddmmsep && month_before_day) {
			month = handle_month (text, match + 1);
			day = handle_day (text, match + 3);
			year = current_year ();
			date_format = gnm_format_frob_slashes ("m/d/yyyy");
		} else if (good_ddmmsep) {
			month = handle_month (text, match + 3);
			day = handle_day (text, match + 1);
			year = current_year ();
			date_format = gnm_format_frob_slashes ("d/m/yyyy");
		} else
			year = month = day = -1;
		if (valid_dmy (day, month, year)) {
			text += match[0].rm_eo;
			goto got_date;
		}
	}

	g_free (date_format);
	return NULL;

 got_date:
	g_date_clear (&date, 1);
	g_date_set_dmy (&date, day, month, year);
	if (!g_date_valid (&date))
		goto out;
	date_val = go_date_g_to_serial (&date, date_conv);

	SKIP_SPACES (text);

	if (*text) {
		GnmValue *v;
		const char *subtext = text;
		char *textcopy = NULL;

		if (timeend)
			subtext = textcopy = g_strndup (text, timeend);

		v = format_match_time (subtext, FALSE,
				       TRUE, add_format);
		g_free (textcopy);
		if (!v)
			goto out;
		time_val = value_get_as_float (v);
		if (!time_format) {
			GOFormat const *fmt = VALUE_FMT (v);
			if (fmt)
				time_format = g_strdup (go_format_as_XL (fmt));
		}
		value_release (v);
	} else
		time_val = 0;

	res = value_new_float (date_val + time_val);
	if (add_format) {
		GOFormat *fmt;
		if (time_format) {
			char *format = g_strconcat (date_format,
						    format_sep,
						    time_format,
						    NULL);
			fmt = go_format_new_from_XL (format);
			g_free (format);
		} else
			fmt = go_format_new_from_XL (date_format);
		value_set_fmt (res, fmt);
		go_format_unref (fmt);
	}

 out:
	g_free (date_format);
	g_free (time_format);
	return res;
}

/*
 * Match "12/23", "-12/23", "1 2/3", "-1 2/3", and even "-123".
 * Does not match "1/0".
 *
 * Spaces are allowed anywhere but between digits and between
 * sign and digits.
 *
 * The number of digits in the denominator is stored in @denlen.
 */
static GnmValue *
format_match_fraction (char const *text, int *denlen, gboolean mixed_only)
{
	char sign = 0;
	gnm_float whole, num, den, f;
	char const *start;
	gunichar uc;

	SKIP_SPACES (text);

	uc = g_utf8_get_char (text);
	DO_SIGN (sign, uc, { text = g_utf8_next_char (text); });

	if (*text == 0 || !g_ascii_isdigit (*text))
		return NULL;

	start = text;
	SKIP_DIGITS (text);
	SKIP_SPACES (text);

	if (*text == '/') {
		if (mixed_only)
			return NULL;
		whole = 0;
	} else {
		whole = gnm_utf8_strto (start, NULL);
		if (errno == ERANGE)
			return NULL;
		if (*text == 0) {
			num = 0;
			den = 1;
			*denlen = 0;
			goto done;
		} else if (!g_ascii_isdigit (*text))
			return NULL;

		start = text;
		SKIP_DIGITS (text);
		SKIP_SPACES (text);

		if (*text != '/')
			return NULL;
	}

	num = gnm_utf8_strto (start, NULL);
	if (errno == ERANGE)
		return NULL;

	text++;
	SKIP_SPACES (text);
	start = text;
	SKIP_DIGITS (text);
	*denlen = text - start;
	SKIP_SPACES (text);

	if (*text != 0)
		return NULL;

	den = gnm_utf8_strto (start, NULL);
	if (errno == ERANGE)
		return NULL;
	if (den == 0)
		return NULL;

 done:
	f = whole + num / den;
	if (sign == '-')
		f = -f;

	return value_new_float (f);
}

// Are we looking at a thousands separator?  (FR has a narrow space as the
// separator.  In that case we allow a plain space too.  We could allow any
// whitespace, but allowing \n, \r, \t, and \f certainly feels wrong.)
static gboolean
is_thousands_sep (const char *text, GString const *thousand, gboolean space1000)
{
	if (strncmp (thousand->str, text, thousand->len) == 0)
		text += thousand->len;
	else if (space1000 && *text == ' ')
		text++;
	else
		return FALSE;

	return (g_ascii_isdigit (text[0]) &&
		g_ascii_isdigit (text[1]) &&
		g_ascii_isdigit (text[2]));
}


GnmValue *
format_match_decimal_number_with_locale (char const *text, GOFormatFamily *family,
					 GString const *curr, GString const *thousand,
					 GString const *decimal)
{
	gboolean par_open = FALSE;
	gboolean par_close = FALSE;
	gboolean has_curr = FALSE;
	gboolean has_percent = FALSE;
	char sign = 0;
	GString *numstr = g_string_sized_new (20);
	gboolean last_was_digit = FALSE;
	gboolean allow1000 = (thousand != NULL) && (thousand->len != 0);
	gboolean space1000;

	g_return_val_if_fail (curr != NULL, NULL);
	g_return_val_if_fail (decimal != NULL, NULL);

	// If thousands separator is a single whitespace character, treat
	// any whitespace character as such.
	space1000 = allow1000 &&
		g_utf8_strlen (thousand->str, thousand->len) == 1 &&
		g_unichar_isspace (g_utf8_get_char (thousand->str));

	while (*text) {
		gunichar uc = g_utf8_get_char (text);

		if (!has_curr && strncmp (curr->str, text, curr->len) == 0) {
			has_curr = TRUE;
			text += curr->len;
			continue;
		}

		if (g_unichar_isspace (uc)) {
			text = g_utf8_next_char (text);
			continue;
		}

		if (!sign) {
			DO_SIGN (sign, uc, {
				g_string_append_c (numstr, sign);
				text = g_utf8_next_char (text);
				continue;
			});
		}

		if (!par_open && !sign && uc == '(') {
			sign = '-';
			g_string_append_c (numstr, sign);
			par_open = TRUE;
			text++;
			continue;
		}

		break;
	}

	while (*text) {
		char c = *text;

		if (last_was_digit &&
		    allow1000 &&
		    is_thousands_sep (text, thousand, space1000)) {
			text = g_utf8_next_char (text);
			continue;
		}

		if (strncmp (decimal->str, text, decimal->len) == 0) {
			GString const *local_decimal = go_locale_get_decimal ();
			g_string_append_len (numstr, local_decimal->str, local_decimal->len);
			text += decimal->len;
			allow1000 = FALSE;
			continue;
		}

		if (g_ascii_isdigit (c)) {
			g_string_append_c (numstr, c);
			text++;
			last_was_digit = TRUE;
			continue;
		}
		last_was_digit = FALSE;

		if (c == 'e' || c == 'E') {
			char esign = 0;
			gunichar uc;

			/*
			 * Pretend to have seen a sign so we don't accept
			 * a "-" at the end.
			 */
			if (!sign)
				sign = '+';
			allow1000 = FALSE;

			g_string_append_c (numstr, c);
			text++;

			uc = g_utf8_get_char (text);
			DO_SIGN (esign, uc, {
				text = g_utf8_next_char (text);
				g_string_append_c (numstr, esign);
			});

			continue;
		}

		break;
	}

	while (*text) {
		gunichar uc = g_utf8_get_char (text);

		if (!has_curr && strncmp (curr->str, text, curr->len) == 0) {
			has_curr = TRUE;
			text += curr->len;
			continue;
		}

		if (g_unichar_isspace (uc)) {
			text = g_utf8_next_char (text);
			continue;
		}

		if (!sign) {
			DO_SIGN (sign, uc, {
				g_string_prepend_c (numstr, sign);
				text = g_utf8_next_char (text);
				continue;
			});
		}

		if (!par_close && par_open && uc == ')') {
			par_close = TRUE;
			text++;
			continue;
		}

		if (!has_percent && uc == '%') {
			has_percent = TRUE;
			text++;
			continue;
		}

		break;
	}

	if (*text ||
	    numstr->len == 0 ||
	    par_open != par_close ||
	    (has_percent && (par_open || has_curr))) {
		g_string_free (numstr, TRUE);
		return NULL;
	} else {
		gnm_float f;
		char *end;
		gboolean bad;

		f = gnm_utf8_strto (numstr->str, &end);
		bad = *end || errno == ERANGE;
		g_string_free (numstr, TRUE);

		if (bad)
			return NULL;

		if (par_open)
			*family = GO_FORMAT_ACCOUNTING;
		else if (has_curr)
			*family = GO_FORMAT_CURRENCY;
		else if (has_percent)
			*family = GO_FORMAT_PERCENTAGE;
		else
			*family = GO_FORMAT_GENERAL;

		if (has_percent)
			f /= 100;

		return value_new_float (f);
	}
}

#undef DO_SIGN
#undef SKIP_SPACES
#undef SKIP_DIGITS

static void
set_money_format (GnmValue *v, const char *fmttxt)
{
	gnm_float f = value_get_as_float (v);

	if (fmttxt) {
		GOFormat *fmt = go_format_new_from_XL (fmttxt);
		value_set_fmt (v, fmt);
		go_format_unref (fmt);
	} else
		value_set_fmt (v, go_format_default_money ());

	if (f != gnm_floor (f)) {
		int i;
		for (i = 0; i < 2; i++) {
			GOFormat *fmt =
				go_format_inc_precision (VALUE_FMT (v));
			value_set_fmt (v, fmt);
			go_format_unref (fmt);
		}
	}
}

/*
 * Major alternate currencies to try after the locale's currency.
 * We do not want three-letter currency codes in here.
 */
static const struct {
	const char *sym;
	const char *fmt;
} alternate_currencies[] = {
	{ "€", "[$€-2]0" },
	{ "£", "£0" },
	{ "¥", "¥0" },
	{ "$", "$0" }
};

static GnmValue *
format_match_decimal_number (char const *text, GOFormatFamily *family,
			     gboolean try_alternates)
{
	GString const *curr = go_locale_get_currency (NULL, NULL);
	GString const *thousand = go_locale_get_thousand ();
	GString const *decimal = go_locale_get_decimal ();
	GnmValue *v;
	unsigned ui;

	v = format_match_decimal_number_with_locale (text, family, curr, thousand, decimal);
	for (ui = 0;
	     try_alternates && v == NULL && ui < G_N_ELEMENTS (alternate_currencies);
	     ui++) {
		const char *sym = alternate_currencies[ui].sym;
		if (strstr (text, sym) == 0)
			continue;
		else {
			GString *altcurr = g_string_new (sym);
			v = format_match_decimal_number_with_locale
				(text, family, altcurr, thousand, decimal);
			g_string_free (altcurr, TRUE);
			if (v)
				set_money_format (v, alternate_currencies[ui].fmt);
		}
	}

	return v;
}

/**
 * format_match:
 * @text: The text to parse
 * @cur_fmt: (nullable): The current format for the value
 * @date_conv: optional date convention
 *
 * Attempts to parse the supplied string to see if it matches a known value
 * format.  The caller is responsible for releasing the resulting value.
 **/
GnmValue *
format_match (char const *text, GOFormat const *cur_fmt,
	      GODateConventions const *date_conv)
{
	GOFormatFamily fam;
	GnmValue *v;
	int denlen;

	if (text[0] == '\0')
		return value_new_empty ();

	/* If it begins with a '\'' it is a string */
	if (text[0] == '\'')
		return value_new_string (text + 1);

	fam = cur_fmt ? go_format_get_family (cur_fmt) : GO_FORMAT_GENERAL;
	switch (fam) {
	case GO_FORMAT_TEXT:
		return value_new_string (text);

	case GO_FORMAT_NUMBER:
	case GO_FORMAT_CURRENCY:
	case GO_FORMAT_ACCOUNTING:
	case GO_FORMAT_PERCENTAGE:
	case GO_FORMAT_SCIENTIFIC:
		v = format_match_decimal_number (text, &fam, FALSE);
		if (!v)
			v = value_is_error (text);
		if (v)
			value_set_fmt (v, cur_fmt);
		return v;

	case GO_FORMAT_DATE: {
		gboolean month_before_day =
			gnm_format_month_before_day (cur_fmt, NULL) != 0;

		v = format_match_datetime (text, date_conv,
					   month_before_day,
					   FALSE,
					   TRUE);
		if (!v)
			v = format_match_decimal_number (text, &fam, FALSE);
		if (!v)
			v = value_is_error (text);
		if (v)
			value_set_fmt (v, cur_fmt);
		return v;
	}

	case GO_FORMAT_TIME: {
		gboolean month_before_day =
			gnm_format_month_before_day (cur_fmt, NULL) != 0;

		gboolean prefer_hour =
			gnm_format_has_hour (cur_fmt, NULL);

		v = format_match_datetime (text, date_conv,
					   month_before_day,
					   FALSE,
					   FALSE);
		if (!v)
			v = format_match_time (text, TRUE, prefer_hour, FALSE);
		if (!v)
			v = format_match_decimal_number (text, &fam, FALSE);
		if (!v)
			v = value_is_error (text);
		if (v)
			value_set_fmt (v, cur_fmt);
		return v;
	}

	case GO_FORMAT_FRACTION:
		v = format_match_fraction (text, &denlen, FALSE);
		if (!v)
			v = format_match_decimal_number (text, &fam, FALSE);
		if (!v)
			v = value_is_error (text);
		if (v)
			value_set_fmt (v, cur_fmt);
		return v;

	default:
		; /* Nothing */
	}

	/* Check basic types */
	v = format_match_simple (text);
	if (v != NULL)
		return v;

	v = format_match_decimal_number (text, &fam, TRUE);
	if (v) {
		switch (fam) {
		case GO_FORMAT_PERCENTAGE:
			value_set_fmt (v, go_format_default_percentage ());
			break;
		case GO_FORMAT_CURRENCY:
			if (!VALUE_FMT (v))
				set_money_format (v, NULL);
			break;
		case GO_FORMAT_ACCOUNTING:
			value_set_fmt (v, go_format_default_accounting ());
			break;
		default:
			; /* Nothing */
		}

		return v;
	}

	v = format_match_datetime (text, date_conv,
				   go_locale_month_before_day () != 0,
				   TRUE,
				   FALSE);
	if (v)
		return v;

	v = format_match_time (text, TRUE, TRUE, TRUE);
	if (v)
		return v;

	v = format_match_fraction (text, &denlen, TRUE);
	if (v) {
		char fmtstr[20];
		char const *qqq = "?????" + 5;
		GOFormat *fmt;

		denlen = MIN (denlen, 5);
		sprintf (fmtstr, "# %s/%s", qqq - denlen, qqq - denlen);
		fmt = go_format_new_from_XL (fmtstr);
		value_set_fmt (v, fmt);
		go_format_unref (fmt);
		return v;
	}

	return NULL;
}

/**
 * format_match_number:
 * @text: The text to parse
 * @cur_fmt: (nullable): The current format for the value
 * @date_conv: optional date convention
 *
 * Attempts to parse the supplied string to see if it matches a known value format.
 * Will eventually use the current cell format in preference to canned formats.
 * If @format is supplied it will get a copy of the matching format with no
 * additional references.   The caller is responsible for releasing the
 * resulting value.  Will ONLY return numbers.
 */
GnmValue *
format_match_number (char const *text, GOFormat const *cur_fmt,
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

/*
 * number-match.c: This file includes the support for matching
 * entered strings as numbers (by trying to apply one of the existing
 * cell formats).
 *
 * The idea is simple: we create a regular expression from the format
 * string that would match a value entered in that format.  Then, on
 * lookup we try to match the string against every regular expression
 * we have: if a match is found, then we decode the number using a
 * precomputed paralel-list of subexpressions.
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <regex.h>
#include "number-match.h"
#include "formats.h"
#include "dates.h"

/*
 * Takes a list of strings (optionally include an * at the beginning
 * that gets stripped, for i18n purposes). and returns a regexp that
 * would match them
 */
static char *
create_option_list (char **list)
{
	int len = 0;
	char **p;
	char *res;
	
	for (p = list; *p; p++){
		char *v = _(*p);

		if (*v == '*')
			v++;
		len += strlen (v) + 1;
	}
	len += 5;

	res = g_malloc (len);
	res [0] = '(';
	res [1] = 0;
	for (p = list; *p; p++){
		char *v = _(*p);

		if (*v == '*')
			v++;
		
		strcat (res, v);
		if (*(p+1))
		    strcat (res, "|");
	}
	strcat (res, ")");
	
	return res;
}

typedef enum {
	MATCH_DAY_FULL = 1,
	MATCH_DAY_NUMBER,
	MATCH_MONTH_FULL,
	MATCH_MONTH_SHORT,
	MATCH_MONTH_NUMBER,
	MATCH_YEAR_FULL,
	MATCH_YEAR_SHORT,
	MATCH_HOUR,
	MATCH_MINUTE,
	MATCH_SECOND,
	MATCH_AMPM,
	MATCH_STRING_CONSTANT,
	MATCH_NUMBER,
	MATCH_PERCENT,
} MatchType;

#define append_type(t) { guint8 x = t; match_types = g_byte_array_append (match_types, &x, 1); }

static char *
format_create_regexp (char *format, GByteArray **dest)
{
	GString *regexp;
	GByteArray *match_types;
	char *str;
	int hour_seen = FALSE;
	
	g_return_val_if_fail (format != NULL, NULL);
	
	regexp = g_string_new ("");
	match_types = g_byte_array_new ();
	
	for (; *format; format++){
		switch (*format){
		case '_':
			if (*(format+1))
				format++;
			break;

		case 'P': case 'p':
			if (tolower (*(format+1)) == 'm')
				format++;
			break;
			
		case '*':
			break;
			
		case '\\':
			if (*(format+1)){
				format++;
				g_string_append_c (regexp, *format);
			}
			break;
			
		case '$':
			g_string_append (regexp, "\\$");
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
			
		case ':':
		case '/':
		case '-':
		case ' ':
			g_string_append_c (regexp, *format);
			break;
			
		case '#': case '0': case '.': case '+': case '?':
			while (*format == '#' || *format == '0' || *format == '.' ||
			       *format == '-' || *format == 'E' || *format == 'e' ||
			       *format == '+' || *format == '?' || *format == ',')
				format++;
					
			format--;
			g_string_append (regexp, _("(([-+]?[0-9,])?(\\.?[0-9]*)?([Ee][-+][0-9]+)?)"));
			append_type (MATCH_NUMBER);
			break;
			
		case 'h':
			hour_seen = TRUE;
			if (*(format+1) == 'h')
				format++;
			
			g_string_append (regexp, "([0-9][0-9]?)");
			append_type (MATCH_HOUR);
			break;
			
		case 'm':
			if (hour_seen){
				if (*(format+1) == 'm')
					format++;
				g_string_append (regexp, "([0-9][0-9]?)");
				append_type (MATCH_MINUTE);
				hour_seen = FALSE;
			} else {
				if (*(format+1) == 'm'){
					if (*(format+2) == 'm'){
						if (*(format+3) == 'm'){
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
			if (*(format+1) == 's')
				format++;
			g_string_append (regexp, "([0-9][0-9]?)");
			append_type (MATCH_SECOND);
			break;
			
		case 'd':
			if (*(format+1) == 'd'){
				if (*(format+2) == 'd'){
					if (*(format+3) == 'd'){
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

		case 'y':
			if (*(format+1) == 'y'){
				if (*(format+2) == 'y'){
					if (*(format+3) == 'y'){
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
			while (*format)
				format++;
			format--;
			break;
			
		case 'A': case 'a':
			if (*(format+1) == 'm' || *(format+1) == 'M'){
				if (*(format+2) == '/'){
					if (*(format+3) == 'P' || *(format+3) == 'p'){
						if (*(format+4) == 'm' || *(format+4) == 'M'){
							format++;
						}
						format++;
					}
					format++;
				}
				format++;
			}
			g_string_append (regexp, "(a|p)m?");
			append_type (MATCH_AMPM);
			break;
			
			/* Matches a string */
		case '"': {
			char *p;
			char *buf;

			for (p = format+1; *p && *p != '"'; p++)
				;

			if (*p != '"')
				goto error;

			if (format+1 != p){
				buf = g_malloc (p - (format+1) + 1);
				strncpy (buf, format+1, p - (format+1));
				buf [p - (format+1)] = 0;

				/* FIXME: we should escape buf */
				g_string_append (regexp, buf);

				g_free (buf);
			}
			format = p;
			break;
		}
		
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
	switch (ret){
	case REG_BADBR:
		printf ("There was an invalid `\\{...\\}' construct in the regular\n"
			"expression.  A valid `\\{...\\}' construct must contain either a\n"
			"single number, or two numbers in increasing order separated by a\n"
			"comma.\n");
		break;

	case REG_BADPAT:
		printf ("There was a syntax error in the regular expression.\n");
		break;
		
	case REG_BADRPT:
		printf ("A repetition operator such as `?' or `*' appeared in a bad\n"
			"position (with no preceding subexpression to act on).\n");
		break;

	case REG_ECOLLATE:
		printf ("The regular expression referred to an invalid collating element\n"
			"(one not defined in the current locale for string collation).\n");

	case REG_ECTYPE:
		printf ("The regular expression referred to an invalid character class name.\n");

	case REG_EESCAPE:
		printf ("The regular expression ended with `\\'.\n");
		break;
		
	case REG_ESUBREG:
		printf ("There was an invalid number in the `\\DIGIT' construct.\n");
		break;
		
	case REG_EBRACK:
		printf ("There were unbalanced square brackets in the regular expression.\n");
		break;

	case REG_EPAREN:
		printf ("An extended regular expression had unbalanced parentheses, or a\n"
			"basic regular expression had unbalanced `\\(' and `\\)'.\n");
		break;

	case REG_EBRACE:
		printf ("The regular expression had unbalanced `\\{' and `\\}'.\n");
		break;

	case REG_ERANGE:
		printf ("One of the endpoints in a range expression was invalid.\n");
		break;

	case REG_ESPACE:
		printf ("`regcomp' ran out of memory.\n");
		break;

	}
}

typedef struct {
	char       *format, *regexp_str;
	GByteArray *match_tags;
	regex_t    regexp;
} format_parse_t;

static GList *format_match_list = NULL;

int 
format_match_define (char *format)
{
	format_parse_t *fp;
	GByteArray *match_tags;
	char *regexp;
	regex_t r;
	int ret;
	
	regexp = format_create_regexp (format, &match_tags);
	if (!regexp)
		return FALSE;

	ret = regcomp (&r, regexp, REG_EXTENDED | REG_ICASE);
	if (ret != 0){
		g_warning ("expression %s\nproduced:%s\n", format, regexp);
		print_regex_error (ret);
		return FALSE;
	}

	fp = g_new (format_parse_t, 1);
	fp->format     = g_strdup (format);
	fp->regexp_str = regexp;
	fp->regexp     = r;
	fp->match_tags = match_tags;

	format_match_list = g_list_append (format_match_list, fp);
	
	return TRUE;
}

static char **formats [] = {
	cell_format_date,
	cell_format_hour,
	cell_format_money,
	cell_format_percent,
	cell_format_numbers,
	cell_format_accounting,
	cell_format_scientific,
	NULL
};

/*
 * Loads the initial formats that we will recognize
 */
void
format_match_init (void)
{
	int i;
	
	for (i = 0; formats [i]; i++){
		char **p = formats [i];

		for (; *p; p++){
			if (strcmp (*p, "General") == 0)
				continue;
			if (strcmp (*p, _("General")) == 0)
				continue;
			format_match_define (*p);
		}
	}
	
}

void
format_match_finish (void)
{
	GList *l;
	
	for (l = format_match_list; l; l = l->next){
		format_parse_t *fp = l->data;

		g_free (fp->format);
		g_free (fp->regexp_str);
		g_byte_array_free (fp->match_tags, TRUE);
		regfree (&fp->regexp);

		g_free (fp);
	}
	g_list_free (format_match_list);
}

/*
 * table_lookup:
 *
 * Looks the string in the table passed
 */
static int
table_lookup (char *str, char **table)
{
	char **p = table;
	int i = 0;
	
	for (p = table; *p; p++, i++){
		char *v  = *p;
		char *iv = _(*p);
		
		if (*v == '*'){
			v++;
			iv++;
		}

		if (strcasecmp (str, v) == 0)
			return i;

		if (strcasecmp (str, iv) == 0)
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
extract_text (char *str, regmatch_t *mp)
{
	char *p;
		
	p = g_malloc (mp->rm_eo - mp->rm_so + 1);
	strncpy (p, &str [mp->rm_so], mp->rm_eo - mp->rm_so);
	p [mp->rm_eo - mp->rm_so] = 0;

	return p;
}

/*
 * Given a number of matches described by MP on S,
 * compute the number based on the information on ARRAY
 *
 * Currently the code can not mix a MATCH_NUMBER with any
 * of the date/time matching.
 */
static gboolean
compute_value (char *s, regmatch_t *mp, GByteArray *array, double *v)
{
	const int len = array->len;
	double number = 0.0;
	gchar *data = array->data;
	gboolean percentify = FALSE;
	gboolean is_number  = FALSE;
	gboolean is_pm      = FALSE;
	int idx = 1, i;
	int month, day, year, year_short;
	int hours, minutes, seconds;
	
	month = day = year = year_short = -1;
	hours = minutes = seconds = -1;
	
	for (i = 0; i < len; i++){
		MatchType type = *(data++);
		char *str;
		
		str = extract_text (s, &mp [idx]);

		switch (type){
		case MATCH_MONTH_FULL:
			month = table_lookup (str, month_long);
			if (month == -1)
				return FALSE;
			month++;
			idx++;
			break;
			
		case MATCH_MONTH_NUMBER:
			month = atoi (str);
			idx++;
			break;

		case MATCH_MONTH_SHORT:
			month = table_lookup (str, month_short);
			if (month == -1)
				return FALSE;
			month++;
			idx++;
			break;

		case MATCH_DAY_FULL:
			/* FIXME: handle weekday */
			idx++;
			break;
			
		case MATCH_DAY_NUMBER:
			day = atoi (str);
			idx++;
			break;
			
		case MATCH_NUMBER:
			is_number = TRUE;
			number = atof (str);
			idx += 3;
			break;

		case MATCH_HOUR:
			hours = atoi (str);
			idx++;
			break;
			
		case MATCH_MINUTE:
			minutes = atoi (str);
			idx++;
			break;

		case MATCH_SECOND:
			seconds = atoi (str);
			idx++;
			break;

		case MATCH_PERCENT:
			percentify = TRUE;
			idx++;
			break;

		case MATCH_YEAR_FULL:
			year = atoi (str);
			idx++;
			break;

		case MATCH_YEAR_SHORT:
			year_short = atoi (str);
			idx++;
			break;

		case MATCH_AMPM:
			if (strcasecmp (str, "pm") == 0)
				is_pm = TRUE;
			idx++;
			break;

		case MATCH_STRING_CONSTANT:
			g_warning ("compute_value: This should not happen\n");
			idx++;
		}

		g_free (str);
	}

	if (is_number){
		if (percentify)
			number *= 0.01;
		*v = number;
		return TRUE;
	}

	{
		time_t t = time (NULL);
		struct tm *tm = localtime (&t);
		GDate *date;

		if (!(year == -1 && month == -1 && day == -1)){
			if (year == -1){
				if (year_short != -1){
					if (tm->tm_year > 99)
						year = 2000 + year_short;
					else
						year = 1900 + year_short;
				} else 
					year = 1900 + tm->tm_year;
			}
			if (month == -1)
				month = tm->tm_mon + 1;
			if (day == -1)
				day = tm->tm_mday;

			if (day < 1 || day > 31)
				return FALSE;

			if (month < 1 || month > 12)
				return FALSE;
			
			date = g_date_new_dmy (day, month, year);

			number = g_date_serial (date);

			g_date_free( date );
		}

		*v = number;
		
		if (seconds == -1 && minutes == -1 && hours == -1)
			return TRUE;
		
		if (seconds == -1)
			seconds = 0;

		if (minutes == -1)
			minutes = 0;

		if (hours == -1)
			hours = 0;

		if (is_pm && hours < 12)
			hours += 12;

		if (hours < 0 || hours > 23)
			return FALSE;

		if (minutes < 0 || minutes > 59)
			return FALSE;

		if (seconds < 0 || minutes > 60)
			return FALSE;
		
		number += (hours * 3600 + minutes * 60 + seconds) / (3600*24.0);

		*v = number;
	}
	return TRUE;
}

#define NM 40

gboolean
format_match (char *s, double *v, char **format)
{
	GList *l;
	regmatch_t mp [NM+1];

	for (l = format_match_list; l; l = l->next){
		format_parse_t *fp = l->data;
		int b;
		
		if (regexec (&fp->regexp, s, NM, mp, 0) == REG_NOMATCH)
			continue;

#if 0
		printf ("matches expression: %s %s\n", fp->format, fp->regexp_str); */
		for (i = 0; i < NM; i++){
			char *p;

			if (mp [i].rm_so == -1)
				break;

			p = extract_text (s, &mp [i]);
			printf ("%d %d->%s\n", mp [i].rm_so, mp [i].rm_eo, p);
		}
#endif

		b = compute_value (s, mp, fp->match_tags, v);
		if (b){
			*format = fp->format;
			return TRUE;
		}
	}

	return FALSE;
}


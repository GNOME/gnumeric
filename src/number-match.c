/*
 * number-match.c: This routine includes the support for matching
 * entered strings as numbers (by trying to apply one of the existing
 * cell formats).
 *
 * Author:
 *   Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include <gnome.h>
#include <string.h>
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
		char *v = *p;

		if (*v == '*')
			v++;
		len += strlen (v) + 1;
	}
	len += 5;

	res = g_malloc (len);
	res [0] = '(';
	res [1] = 0;
	for (p = list; *p; p++){
		char *v = *p;

		if (*v == '*')
			v++;
		
		strcat (res, v);
		if (*(p+1))
		    strcat (res, "|");
	}
	strcat (res, ")");
	
	return res;
}

enum {
	MATCH_MONTH_FULL = 1,
	MATCH_MONTH_SHORT,
	MATCH_MONTH_NUMBER,
	MATCH_STRING_CONSTANT,
	MATCH_NUMBER,
	MATCH_MINUTE,
	MATCH_PERCENT,
	MATCH_YEAR_FULL,
	MATCH_YEAR_SHORT,
	MATCH_SECOND,
	MATCH_DAY_FULL,
	MATCH_DAY_NUMBER,
	MATCH_AMPM,
	
};

#define append_type(t) { guint8 x = t; g_byte_array_append (match_types, &x, 1); }

static char *
format_create_regexp (char *format, GByteArray **dest)
{
	GString *regexp;
	GByteArray *match_types;
	char *str;
	int top = 0;
	int type;
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
						g_string_append (regexp, create_option_list (month_long));
						append_type (MATCH_MONTH_FULL);
						format++;
					} else {
						g_string_append (regexp, create_option_list (month_short));
						append_type (MATCH_MONTH_SHORT);
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
						g_string_append (regexp, create_option_list (day_long));
						append_type (MATCH_DAY_FULL);
						format++;
					} else {
						g_string_append (regexp, create_option_list (day_short));
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
					g_string_append (regexp, "([0-9][0-9])");
					append_type (MATCH_YEAR_SHORT);
				}
				format++;
			} else
				g_string_append (regexp, "([0-9][0-9])");
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
			g_string_append (regexp, "((am?)|(pm?))");
			append_type (MATCH_AMPM);
			break;
			
		case 'P': case 'p':
			g_string_append (regexp, "[Pp][Mm]");
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
				
				g_string_append (regexp, "(");
				g_string_append (regexp, buf);
				g_string_append (regexp, ")");

				append_type (MATCH_STRING_CONSTANT);
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
	char       *format, *regexp;
	GByteArray *match_tags;
	regex_t    r;
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
		printf ("expression %s\nproduced:%s\n", format, regexp);
		print_regex_error (ret);
		return FALSE;
	}

	fp = g_new (format_parse_t, 1);
	fp->format = g_strdup (format);
	fp->regexp = regexp;
	fp->r      = r;
	fp->match_tags = match_tags;

	format_match_list = g_list_append (format_match_list, fp);
	
	return TRUE;
}

char **formats [] = {
	cell_format_date,
	cell_format_hour,
	cell_format_money,
	cell_format_percent,
	cell_format_numbers,
	cell_format_accounting,
	cell_format_scientific,
	NULL
};

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

#define NM 40

void
format_match (char *s)
{
	GList *l;
	regmatch_t mp [NM+1];
	
	for (l = format_match_list; l; l = l->next){
		format_parse_t *fp = l->data;
		int i;
		
/*		printf ("%s -> %s >%s<\n", s, fp->format, fp->regexp); */
		if (regexec (&fp->r, s, NM, mp, 0) == REG_NOMATCH)
			continue;
		printf ("matches expression: %s %s\n", fp->format, fp->regexp);
		for (i = 0; i < NM; i++){
			char *p;

			if (mp [i].rm_so == -1)
				break;

			p = g_malloc (mp [i].rm_eo - mp [i].rm_so + 1);
			strncpy (p, &s [mp [i].rm_so], mp [i].rm_eo - mp [i].rm_so);
			p [mp [i].rm_eo - mp [i].rm_so] = 0;
			
			printf ("%d %d->%s\n", mp [i].rm_so, mp [i].rm_eo, p);
		}
		break;
	}
}


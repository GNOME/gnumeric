/*
 * regutf8.c:  A poor man's UTF-8 regexp routines.
 *
 * We should test system libraris for UTF-8 handling...
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <regutf8.h>

int
gnumeric_regcomp_XL (gnumeric_regex_t *preg, char const *pattern, int cflags)
{
	GString *res = g_string_new (NULL);
	int retval;

	while (*pattern) {
		switch (*pattern) {
		case '~':
			pattern++;
			if (*pattern == '*')
				g_string_append (res, "\\*");
			else
				g_string_append_c (res, *pattern);
			if (*pattern) pattern++;
			break;

		case '*':
			g_string_append (res, ".*");
			pattern++;
			break;

		case '?':
			g_string_append_c (res, '.');
			pattern++;
			break;

		case '.': case '[': case '\\':
		case '^': case '$':
			g_string_append_c (res, '\\');
			g_string_append_c (res, *pattern++);
			break;

		default:
			g_string_append_unichar (res, g_utf8_get_char (pattern));
			pattern = g_utf8_next_char (pattern);
		}
	}

	retval = gnumeric_regcomp (preg, res->str, cflags);
	g_string_free (res, TRUE);
	return retval;
}

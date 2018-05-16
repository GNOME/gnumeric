/*
 * rangefunc-strings.c: String Functions on ranges.
 *
 * Authors:
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <rangefunc-strings.h>

#include <string.h>

/**
 * range_concatenate:
 * @data: (element-type utf8) (transfer none):
 * @res: (out) (transfer full):
 *
 * Returns: non-zero on error.
 */
int
range_concatenate (GPtrArray *data, char **res, gpointer user)
{
	unsigned ui;
	size_t len = 0;
	GString *str;

	for (ui = 0; ui < data->len; ui++)
		len += strlen (g_ptr_array_index (data, ui));

	str = g_string_sized_new (len);

	for (ui = 0; ui < data->len; ui++)
		g_string_append (str, g_ptr_array_index (data, ui));

	*res = g_string_free (str, FALSE);
	return 0;
}

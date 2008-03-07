/*
 * History.c: Implements file menu file history
 *
 * Author:
 *  Mike Kestner (mkestner@ameritech.net)
 *
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <goffice/utils/go-file.h>
#include "gnumeric.h"
#include "history.h"
#include <string.h>

/*
 * Returns a newly allocated string containing the base name of the file
 * with any .gnumeric extension stripped off.
 */
gchar *
history_item_label (gchar const *uri, int accel_number)
{
	GString *res = g_string_new (NULL);
	char *basename, *tmp;
	int len;

	basename = go_basename_from_uri (uri);
	if (basename == NULL)
		basename = g_strdup ("(invalid file name)");

	/* Remove .gnumeric, if present.  */
	len = strlen (basename);
	if (len > 9 && strcmp (basename + len - 9, ".gnumeric") == 0)
		basename[len - 9] = 0;

	if (accel_number <= 9)
		g_string_append_printf (res, "_%d ", accel_number);
	else if (accel_number == 10)
		g_string_append (res, "1_0 ");
	else
		g_string_append_printf (res, "%d ", accel_number);

	/* Underscores need to be doubled.  */
	for (tmp = basename; *tmp; tmp++) {
		if (*tmp == '_')
			g_string_append_c (res, '_');
		g_string_append_c (res, *tmp);
	}
	g_free (basename);

	return g_string_free (res, FALSE);
}

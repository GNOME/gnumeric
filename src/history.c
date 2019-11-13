/*
 * history.c: Implements file menu file history
 *
 * Author:
 *  Mike Kestner (mkestner@ameritech.net)
 *
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <goffice/goffice.h>
#include <gnumeric.h>
#include <history.h>
#include <string.h>

/*
 * Returns a newly allocated string containing the base name of the file
 * with any .gnumeric extension stripped off.
 */
gchar *
gnm_history_item_label (gchar const *uri, int accel_number)
{
	GString *res = g_string_new (NULL);
	char *basename, *tmp;

	basename = go_basename_from_uri (uri);
	if (basename == NULL)
		basename = g_strdup ("(invalid file name)");

	/* Remove .gnumeric, if present.  */
	if (g_str_has_suffix (basename, ".gnumeric"))
		basename[strlen (basename) - 9] = 0;

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

/*
 * History.c: Implements file menu file history
 *
 * Author:
 *  Mike Kestner (mkestner@ameritech.net)
 *
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "history.h"
#include <string.h>

/*
 * Returns a newly allocated string containing the base name of the file
 * with any .gnumeric extension stripped off.
 */
gchar *
history_item_label (gchar const *filename, int accel_number)
{
	GString *res = g_string_new (NULL);
	char *menuname, *basename, *tmp;
	int len;

	basename = g_path_get_basename (filename);

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

	menuname = g_filename_to_utf8 (basename, -1, NULL, NULL, NULL);
	if (!menuname)
		menuname = g_strdup (_("(Filename in invalid encoding)"));
	g_free (basename);
	/* Underscores need to be doubled.  */
	for (tmp = menuname; *tmp; tmp++) {
		if (*tmp == '_')
			g_string_append_c (res, '_');
		g_string_append_c (res, *tmp);
	}
	g_free (menuname);

	return g_string_free (res, FALSE);
}

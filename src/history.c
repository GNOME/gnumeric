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
	char *label, *menuname, *basename, *tmp;

	basename = g_path_get_basename (filename);

	/* Remove .gnumeric, if present.  */
	tmp = strstr (basename, ".gnumeric");
	if (tmp[9] == 0)
		tmp[0] = 0;

	menuname = g_filename_to_utf8 (basename, -1, NULL, NULL, NULL);
	if (!menuname)
		menuname = g_strdup (_("(Filename in invalid encoding)"));
	g_free (basename);

	/* Translate '_' to '-' so menu will not show underline.  */
	for (tmp = menuname; (tmp = strchr (tmp, '_')); )
		*tmp = '-';

	label = g_strdup_printf ("_%d %s", accel_number, menuname);
	g_free (menuname);

	return label;
}

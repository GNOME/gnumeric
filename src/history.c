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
history_item_label (gchar const *name, int accel_number)
{
	int   i;
	char *label, *menuname, *tmp;

	/* Translate '_' to '-' so menu will not show underline.  */
	menuname = g_path_get_basename (name);
	for (tmp = menuname; *tmp; tmp++)
		if (*tmp == '_')
			*tmp = '-';
	label = g_strdup_printf ("_%d %s", accel_number, menuname);
	g_free (menuname);

	for (i = strlen (label) - 1; i >= 0; i--)
		if (label [i] == '.') {
			if (strcmp (label + i, ".gnumeric") == 0)
				label [i] = '\0';
			break;
		}

	return label;
}

/*
 * file-autoft.c : Retrieve available autoformat templates/categories
 *
 * Copyright (C) Almer. S. Tigelaar.
 * E-mail: almer1@dds.nl or almer-t@bigfoot.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <dirent.h>
#include <string.h>
#include <errno.h>

#include "file-autoft.h"

/**
 * util_list_free:
 * @list : a GList
 *
 * Free a list with items
 **/
static void
util_list_free (GList *list)
{
	GList *iterator = list;

	while (iterator) {

		g_free (iterator->data);

		iterator = g_list_next (iterator);
	}

	g_list_free (list);
}

/**
 * template_list_load:
 *
 * Load the list of available templates
 *
 * Return value: The list of templates (should be freed with templates_list_free)
 **/
GList *
template_list_load (void)
{
	DIR *dir;
	struct dirent *ent;
	int count = 0;
	GList *list = NULL;

	dir = opendir (GNUMERIC_AUTOFORMATDIR);
	if (dir == NULL) {
		g_warning ("Error while trying to open the autoformat template directory %s : %s", GNUMERIC_AUTOFORMATDIR, g_strerror (errno));

		return NULL;
	}

	errno = 0;
	while ((ent = readdir (dir))) {

		/*
		 * Look if the name is actually a valid filename
		 * A valid filename is "autoformat.<Category>.<name>.xml";
		 * The result of g_strsplit should always be 4 strings and than a \0
		 */
		if (ent->d_name && strcmp (ent->d_name, ".") != 0 && strcmp (ent->d_name, "..") != 0) {
			char **array = g_strsplit (ent->d_name, ".", -1);
			gboolean valid = TRUE;
			int elements = 0;

			/*
			 * Count number of elements in array
			 */
			while (array[elements]) {

				elements++;
			}

			if (elements == 4) {
				/*
				 * The first part should always be "autoformat"
				 */
				if (array[0]) {
					g_strdown (array[0]);
					if (strcmp (array[0], "autoformat") != 0)
						valid = FALSE;
				} else {
					valid = FALSE;
				}

				/*
				 * Now a category should come
				 */
				if (!array[1]) {
					valid = FALSE;
				}

				/*
				 * After that the name of the template
				 */
				if (!array[2]) {
					valid = FALSE;
				}

				/*
				 * And last but not least "xml"
				 */
				if (array[3]) {
					g_strdown (array[3]);
					if (strcmp (array[3], "xml") != 0)
						valid = FALSE;
				} else {
					valid = FALSE;
				}
			} else {
				valid = FALSE;
			}

			/*
			 * Only add it if valid, otherwise print an error
			 */
			if (valid) {
				list = g_list_append (list, g_strdup_printf ("%s/%s", GNUMERIC_AUTOFORMATDIR, ent->d_name));
				count++;
			} else {
				g_warning ("The file %s encountered in %s is an invalid file, please remove it from your autoformat directory",
					   ent->d_name, GNUMERIC_AUTOFORMATDIR);
			}

			g_strfreev (array);
		}

		errno = 0;
	}

	if (errno) {

		g_warning ("Error while reading listing of %s", GNUMERIC_AUTOFORMATDIR);
		closedir (dir);
		g_list_free (list);
		return NULL;
	} else if (count == 0) {

		g_warning ("The autoformat template directory %s contains no templates at all!", GNUMERIC_AUTOFORMATDIR);
		closedir (dir);
		g_list_free (list);
		return NULL;
	}

	closedir (dir);

	return list;
}

/**
 * template_list_free:
 * @list: A GList
 *
 * Free the template list
 **/
void
template_list_free (GList *list)
{
	util_list_free (list);
}

/**
 * category_list_load
 *
 * Loads the list of categories available
 *
 * Return value: NULL on failure or a pointer to a list with categories on success.
 **/
GList *
category_list_load (void)
{
	GList *template_list, *iterator, *subiterator;
	GList *list = NULL;

	template_list = template_list_load ();

	/*
	 * Strip category part from each filename and put this in a separate list
	 * In the filename the second part is always the category
	 */
	iterator = template_list;
	while (iterator) {
		char *string = iterator->data;
		char **array = g_strsplit (g_basename (string), ".", -1);
		char *category = array[1];
		gboolean exists = FALSE;

		/*
		 * Don't add this item if it already exists
		 */
		subiterator = list;
		while (subiterator) {
			char *data = subiterator->data;

			if (strcmp (data, category) == 0) {
				exists = TRUE;
				break;
			}

			subiterator = g_list_next (subiterator);
		}

		/*
		 * Add the new category if it doesn't exist yet,
		 * note that we always add the 'General' category to the
		 * top of the list if it exists
		 */
		if (!exists) {
			if (strcmp (category, "General") == 0)
				list = g_list_insert (list, g_strdup (category), 0);
			else
				list = g_list_append (list, g_strdup (category));
		}

		g_strfreev (array);

		iterator = g_list_next (iterator);
	}

	template_list_free (template_list);

	return list;
}

/**
 * category_list_free:
 * @list: A GList
 *
 * Free the template list
 **/
void
category_list_free (GList *list)
{
	util_list_free (list);
}

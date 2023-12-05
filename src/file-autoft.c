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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <file-autoft.h>

#include <gutils.h>
#include <workbook-control.h>
#include <format-template.h>
#include <gnumeric-conf.h>

#include <gsf/gsf-impl-utils.h>
#include <goffice/goffice.h>
#include <libxml/parser.h>

#include <sys/types.h>
#include <string.h>
#include <unistd.h>

#define CXML2C(s) ((char const *)(s))

static gint
gnm_ft_category_compare_name_and_dir (gconstpointer a, gconstpointer b)
{
	GnmFTCategory const *cat_a = a, *cat_b = b;
	int res;

	res = strcmp (cat_a->name, cat_b->name);
	return res != 0 ? res : strcmp (cat_a->directory, cat_b->directory);
}

static void
gnm_ft_category_free (GnmFTCategory *category)
{
	g_free (category->directory);
	g_free (category->name);
	g_free (category->description);
	g_free (category);
}

static GSList *
gnm_ft_category_get_templates_list (GnmFTCategory *category,
				    GOCmdContext *cc)
{
	GSList *templates = NULL;
	GDir *dir;
	char const *d_name;

	if (category == NULL)
		return NULL;

	dir = g_dir_open (category->directory, 0, NULL);
	if (dir == NULL)
		return NULL;

	while ((d_name = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_suffix (d_name, ".xml")) {
			gchar *full_entry_name;
			GnmFT *ft;

			full_entry_name = g_build_filename (category->directory, d_name, NULL);
			ft = gnm_ft_new_from_file (full_entry_name, cc);
			if (ft == NULL) {
				g_warning (_("Invalid template file: %s"), full_entry_name);
			} else {
				ft->category = category;
				templates = g_slist_prepend (templates, ft);
			}
			g_free (full_entry_name);
		}
	}

	g_dir_close (dir);

	return g_slist_sort (templates, gnm_ft_compare_name);
}

/**
 * gnm_ft_xml_read_category:
 * Open an XML file and read a GnmFTCategory
 */
static GnmFTCategory *
gnm_ft_xml_read_category (char const *dir_name)
{
	gchar *file_name;
	xmlDocPtr doc;
	xmlNodePtr node;
	GnmFTCategory *category = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);

	file_name = g_build_filename (dir_name, ".category", NULL);
	doc = xmlParseFile (file_name);
	if (doc != NULL && doc->xmlRootNode != NULL
	    && xmlSearchNsByHref (doc, doc->xmlRootNode, (xmlChar *)"http://www.gnome.org/gnumeric/format-template-category/v1") != NULL
	    && strcmp (CXML2C (doc->xmlRootNode->name), "FormatTemplateCategory") == 0
	    && (node = go_xml_get_child_by_name (doc->xmlRootNode, "Information")) != NULL) {
		xmlChar *name = xmlGetProp (node, (xmlChar *)"name");
		if (name != NULL) {
			xmlChar *description = xmlGetProp (node, (xmlChar *)"description");
			category = g_new (GnmFTCategory, 1);
			category->directory = g_strdup (dir_name);
			category->name = g_strdup ((gchar *)name);
			category->description = g_strdup ((gchar *)description);
			category->is_writable = (access (dir_name, W_OK) == 0);
			if (description != NULL)
				xmlFree (description);
			xmlFree (name);
		}
	}
	xmlFreeDoc (doc);
	g_free (file_name);

	return category;
}

static GList *
gnm_ft_category_list_get_from_dir_list (GSList *dir_list)
{
	GList *categories = NULL;
	GSList *dir_iterator;

	g_return_val_if_fail (dir_list != NULL, NULL);

	for (dir_iterator = dir_list; dir_iterator != NULL; dir_iterator = dir_iterator->next) {
		gchar *dir_name = dir_iterator->data;
		GDir *dir;
		char const *d_name;

		dir = g_dir_open (dir_name, 0, NULL);
		if (dir == NULL)
			continue;

		while ((d_name = g_dir_read_name (dir)) != NULL) {
			gchar *full_entry_name;

			full_entry_name = g_build_filename (dir_name, d_name, NULL);
			if (d_name[0] != '.' && g_file_test (full_entry_name, G_FILE_TEST_IS_DIR)) {
				GnmFTCategory *category;

				category = gnm_ft_xml_read_category (full_entry_name);
				if (category != NULL) {
					categories = g_list_prepend (categories, category);
				}
			}
			g_free (full_entry_name);
		}

		g_dir_close (dir);
	}

	return categories;
}

static void
gnm_ft_category_list_free (GList *categories)
{
	GList *l;

	g_return_if_fail (categories);

	for (l = categories; l != NULL; l = l->next) {
		gnm_ft_category_free ((GnmFTCategory *) l->data);
	}
	g_list_free (categories);
}

static void
add_dir (GSList **pl, const char *dir, const char *base_dir)
{
	char *dirc = NULL;
	if (g_path_is_absolute (dir))
		dirc = g_strdup (dir);
	else
		dirc = g_build_filename (base_dir, dir, NULL);
	*pl = g_slist_prepend (*pl, dirc);
}

/**
 * gnm_ft_category_group_list_get:
 *
 * Returns: (element-type GnmFTCategoryGroup) (transfer full):
 * the list of #GnmFTCategoryGroup which should be freed using
 * gnm_ft_category_group_list_free().
 **/
GList *
gnm_ft_category_group_list_get (void)
{
	GList *category_groups = NULL;
	GSList *dir_list = NULL, *sl;
	GList *categories, *l;
	GnmFTCategoryGroup *current_group;

	add_dir (&dir_list,
		 gnm_conf_get_autoformat_sys_dir (),
		 gnm_sys_data_dir ());
	add_dir (&dir_list,
		 gnm_conf_get_autoformat_usr_dir (),
		 gnm_usr_dir (FALSE));
	add_dir (&dir_list,
		 gnm_conf_get_autoformat_usr_dir (),
		 gnm_usr_dir (TRUE));

	for (sl = gnm_conf_get_autoformat_extra_dirs (); sl; sl = sl->next) {
		const char *dir = sl->data;
		add_dir (&dir_list, dir, g_get_home_dir ());
	}
	dir_list = g_slist_reverse (dir_list);
	categories = gnm_ft_category_list_get_from_dir_list (dir_list);
	g_slist_free_full (dir_list, g_free);

	categories = g_list_sort (categories, gnm_ft_category_compare_name_and_dir);

	current_group = NULL;
	for (l = categories; l != NULL; l = l->next) {
		GnmFTCategory *category = l->data;
		if (current_group == NULL || strcmp (current_group->name, category->name) != 0) {
			if (current_group != NULL) {
				category_groups = g_list_prepend (category_groups, current_group);
			}
			current_group = g_new (GnmFTCategoryGroup, 1);
			current_group->categories = g_list_append (NULL, category);
			current_group->name = g_strdup (category->name);
			current_group->description = g_strdup (category->description);
		} else {
			current_group->categories = g_list_prepend (current_group->categories, category);
		}
	}
	if (current_group != NULL)
		category_groups = g_list_prepend (category_groups, current_group);

	g_list_free (categories);

	return category_groups;
}


/**
 * gnm_ft_category_group_list_free:
 * @category_groups: (element-type GnmFTCategoryGroup): the list to free.
 *
 **/
void
gnm_ft_category_group_list_free (GList *groups)
{
	GList *ptr;

	for (ptr = groups; ptr != NULL; ptr = ptr->next) {
		GnmFTCategoryGroup *group = ptr->data;
		g_free (group->name);
		g_free (group->description);
		gnm_ft_category_list_free (group->categories);
		g_free (group);
	}
	g_list_free (groups);
}

/**
 * gnm_ft_category_group_get_templates_list:
 * @category_group: #GnmFTCategoryGroup
 * @context: #GOCmdContext
 *
 * Returns: (element-type GnmFT) (transfer container):
 **/
GSList *
gnm_ft_category_group_get_templates_list (GnmFTCategoryGroup *category_group,
					  GOCmdContext *cc)
{
	GSList *templates = NULL;
	GList *l;

	for (l = category_group->categories; l != NULL; l = l->next)
		templates = g_slist_concat (templates,
			gnm_ft_category_get_templates_list (l->data, cc));

	return g_slist_sort (templates, gnm_ft_compare_name);
}

int
gnm_ft_category_group_cmp (gconstpointer a, gconstpointer b)
{
	GnmFTCategoryGroup const *group_a = a;
	GnmFTCategoryGroup const *group_b = b;
	return g_utf8_collate (_(group_a->name), _(group_b->name));
}

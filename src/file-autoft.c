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

#include <gnumeric-config.h>
#include "gnumeric.h"
#include "file-autoft.h"

#include "gutils.h"
#include "workbook-control.h"
#include "xml-io.h"
#include "xml-io-autoft.h"
#include "format-template.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <libgnome/gnome-util.h>
#include <libgnome/gnome-i18n.h>

#include <gal/util/e-util.h>

#define TEMPLATE_FILE_EXT    ".xml"

#define AUTOFORMAT_SUFFIX	"autoformat-templates"

char *
gnumeric_sys_autoformat_dir (void)
{
	return gnumeric_sys_data_dir (AUTOFORMAT_SUFFIX);
}

char *
gnumeric_usr_autoformat_dir (void)
{
	return gnumeric_usr_dir (AUTOFORMAT_SUFFIX);
}

GList *
gnumeric_extra_autoformat_dirs (void)
{
	static GList *extra_dirs;
	static gboolean list_ready = FALSE;
	GList *new_list, *l;

	if (!list_ready) {
		extra_dirs = gnumeric_config_get_string_list ("Gnumeric/Autoformat/",
		                                              "ExtraTemplatesDir");
		list_ready = TRUE;
	}

	new_list = NULL;
	for (l = extra_dirs; l != NULL; l = l->next) {
		gchar *dir_name;

		dir_name = (gchar *) l->data;
		new_list = g_list_prepend (new_list, g_strdup (dir_name));
	}

	return g_list_reverse (new_list);
}

/**
 * category_compare_orig_name:
 *
 **/
static gint
category_compare_orig_name (const void *a, const void *b)
{
	FormatTemplateCategory *cat_a = (FormatTemplateCategory *) a,
	                       *cat_b = (FormatTemplateCategory *) b;

	g_assert (cat_a != NULL && cat_b != NULL);

	return strcmp (cat_a->orig_name, cat_b->orig_name);
}

/**
 * category_free:
 *
 **/
void
category_free (FormatTemplateCategory *category)
{
	g_free (category->directory);
	g_free (category->orig_name);
	g_free (category->name);
	g_free (category->description);
	g_free (category);
}

/**
 * category_get_templates_list:
 *
 **/
GSList *
category_get_templates_list (FormatTemplateCategory *category,
			     CommandContext *context)
{
	GSList *templates = NULL;
	DIR *dir;
	struct dirent *entry;

	if (category == NULL)
		return NULL;

	dir = opendir (category->directory);
	if (dir == NULL)
		return NULL;

	while ((entry = readdir (dir)) != NULL) {
		gint name_len;

		name_len = strlen (entry->d_name);
		if (name_len > 4 && strcmp (entry->d_name + name_len - 4, TEMPLATE_FILE_EXT) == 0) {
			gchar *full_entry_name;
			FormatTemplate *ft;

			full_entry_name = g_concat_dir_and_file (category->directory, entry->d_name);
			ft = format_template_new_from_file (context, full_entry_name);
			if (ft == NULL) {
				g_warning (_("Invalid template file: %s"), full_entry_name);
			} else {
				ft->category = category;
				templates = g_slist_prepend (templates, ft);
			}
			g_free (full_entry_name);
		}
	}

	closedir (dir);

	return g_slist_sort (templates, format_template_compare_name);
}

/**
 * category_list_get_from_dir_list:
 *
 **/
static GList *
category_list_get_from_dir_list (GList *dir_list)
{
	GList *categories = NULL;
	GList *dir_iterator;

	g_return_val_if_fail (dir_list != NULL, NULL);

	for (dir_iterator = dir_list; dir_iterator != NULL; dir_iterator = dir_iterator->next) {
		gchar *dir_name;
		DIR *dir;
		struct dirent *entry;

		dir_name = (gchar *) dir_iterator->data;
		g_assert (dir_name != NULL);

		dir = opendir (dir_name);
		if (dir == NULL) {
			continue;
		}

		while ((entry = readdir (dir)) != NULL) {
			gchar *full_entry_name;
			struct stat entry_info;

			full_entry_name = g_concat_dir_and_file (dir_name, entry->d_name);
			if (entry->d_name[0] != '.' && stat (full_entry_name, &entry_info) == 0 && S_ISDIR(entry_info.st_mode)) {
				FormatTemplateCategory *category;

				category = gnumeric_xml_read_format_template_category (full_entry_name);
				if (category != NULL) {
					categories = g_list_prepend (categories, category);
				}
			}
			g_free (full_entry_name);
		}

		closedir (dir);
	}

	return categories;
}

/**
 * category_list_free:
 *
 **/
void
category_list_free (GList *categories)
{
	GList *l;

	g_return_if_fail (categories);

	for (l = categories; l != NULL; l = l->next) {
		category_free ((FormatTemplateCategory *) l->data);
	}
	g_list_free (categories);
}

/**
 * category_group_list_get:
 *
 **/
GList *
category_group_list_get (void)
{
	GList *category_groups = NULL;
	GList *dir_list;
	GList *categories, *l;
	FormatTemplateCategoryGroup *current_group;

	dir_list = g_create_list (gnumeric_sys_autoformat_dir (),
	                          gnumeric_usr_autoformat_dir (),
	                          NULL);
	dir_list = g_list_concat (dir_list, gnumeric_extra_autoformat_dirs ());
	categories = category_list_get_from_dir_list (dir_list);

	categories = g_list_sort (categories, category_compare_orig_name);

	current_group = NULL;
	for (l = categories; l != NULL; l = l->next) {
		FormatTemplateCategory *category;

		category = (FormatTemplateCategory *) l->data;
		if (current_group == NULL || strcmp (current_group->orig_name, category->orig_name) != 0) {
			if (current_group != NULL) {
				category_groups = g_list_prepend (category_groups, current_group);
			}
			current_group = g_new (FormatTemplateCategoryGroup, 1);
			current_group->categories = g_list_append (NULL, category);
			current_group->orig_name = g_strdup (category->orig_name);
			current_group->name = g_strdup (category->name);
			current_group->description = g_strdup (category->description);
			current_group->lang_score = category->lang_score;
		} else {
			current_group->categories = g_list_prepend (current_group->categories, category);
			if (g_lang_score_is_better (category->lang_score, current_group->lang_score)) {
				g_free (current_group->name);
				g_free (current_group->description);
				current_group->name = g_strdup (category->name);
				current_group->description = g_strdup (category->description);
				current_group->lang_score = category->lang_score;
			}
		}
	}
	if (current_group != NULL) {
		category_groups = g_list_prepend (category_groups, current_group);
	}

	g_list_free (categories);
	e_free_string_list (dir_list);

	return category_groups;
}


/**
 * category_group_list_find_category_by_name:
 *
 **/
FormatTemplateCategoryGroup *
category_group_list_find_category_by_name (GList *category_groups, gchar const *name)
{
	GList *l;

	for (l = category_groups; l != NULL; l = l->next) {
		FormatTemplateCategoryGroup *category_group;

		category_group = (FormatTemplateCategoryGroup *) l->data;
		if (strcmp (category_group->name, name) == 0) {
			return category_group;
		}
	}

	return NULL;
}

/**
 * category_group_list_get_names_list:
 *
 **/
GList *
category_group_list_get_names_list (GList *category_groups)
{
	GList *names = NULL;
	GList *l;

	for (l = category_groups; l != NULL; l = l->next) {
		FormatTemplateCategoryGroup *category_group;

		category_group = (FormatTemplateCategoryGroup *) l->data;
		names = g_list_prepend (names, g_strdup (category_group->name));
	}

	return g_list_sort (names, g_str_compare);
}

/**
 * category_group_free:
 *
 **/
void
category_group_free (FormatTemplateCategoryGroup *category_group)
{
	g_free (category_group->orig_name);
	g_free (category_group->name);
	g_free (category_group->description);
	category_list_free (category_group->categories);
	g_free (category_group);
}

/**
 * category_group_list_free:
 *
 **/
void
category_group_list_free (GList *category_groups)
{
	GList *l;

	g_return_if_fail (category_groups);

	for (l = category_groups; l != NULL; l = l->next) {
		FormatTemplateCategoryGroup *group = l->data;
		category_group_free (group);
	}
	g_list_free (category_groups);
}

/**
 * category_group_get_templates_list:
 *
 **/
GSList *
category_group_get_templates_list (FormatTemplateCategoryGroup *category_group, CommandContext *context)
{
	GSList *templates = NULL;
	GList *l;

	for (l = category_group->categories; l != NULL; l = l->next)
		templates = g_slist_concat (templates,
			category_get_templates_list (l->data, context));

	return g_slist_sort (templates, format_template_compare_name);
}

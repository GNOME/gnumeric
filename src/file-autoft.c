/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "file-autoft.h"

#include "gutils.h"
#include "workbook-control.h"
#include "xml-io.h"
#include "format-template.h"
#include "gnumeric-gconf.h"

#include <gsf/gsf-impl-utils.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-libxml-extras.h>

#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define CXML2C(s) ((char const *)(s))

static gint
category_compare_name_and_dir (gconstpointer a, gconstpointer b)
{
	FormatTemplateCategory const *cat_a = a, *cat_b = b;
	int res;

	res = strcmp (cat_a->name, cat_b->name);
	return res != 0 ? res : strcmp (cat_a->directory, cat_b->directory);
}

static void
category_free (FormatTemplateCategory *category)
{
	g_free (category->directory);
	g_free (category->name);
	g_free (category->description);
	g_free (category);
}

static GSList *
category_get_templates_list (FormatTemplateCategory *category,
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
		gint name_len;

		name_len = strlen (d_name);
		if (name_len > 4 && strcmp (d_name + name_len - 4, ".xml") == 0) {
			gchar *full_entry_name;
			GnmFormatTemplate *ft;

			full_entry_name = g_build_filename (category->directory, d_name, NULL);
			ft = format_template_new_from_file (full_entry_name, cc);
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

	return g_slist_sort (templates, format_template_compare_name);
}

/**
 * gnumeric_xml_read_format_template_category :
 * Open an XML file and read a FormatTemplateCategory
 */
static FormatTemplateCategory *
gnumeric_xml_read_format_template_category (char const *dir_name)
{
	gchar *file_name;
	xmlDocPtr doc;
	xmlNodePtr node;
	FormatTemplateCategory *category = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);

	file_name = g_build_filename (dir_name, ".category", NULL);
	doc = xmlParseFile (file_name);
	if (doc != NULL && doc->xmlRootNode != NULL
	    && xmlSearchNsByHref (doc, doc->xmlRootNode, (xmlChar *)"http://www.gnome.org/gnumeric/format-template-category/v1") != NULL
	    && strcmp (CXML2C (doc->xmlRootNode->name), "FormatTemplateCategory") == 0
	    && (node = e_xml_get_child_by_name (doc->xmlRootNode, "Information")) != NULL) {
		xmlChar *name = xmlGetProp (node, (xmlChar *)"name");
		if (name != NULL) {
			xmlChar *description = xmlGetProp (node, (xmlChar *)"description");
			category = g_new (FormatTemplateCategory, 1);
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
category_list_get_from_dir_list (GSList *dir_list)
{
	GList *categories = NULL;
	GSList *dir_iterator;

	g_return_val_if_fail (dir_list != NULL, NULL);

	for (dir_iterator = dir_list; dir_iterator != NULL; dir_iterator = dir_iterator->next) {
		gchar *dir_name;
		GDir *dir;
		char const *d_name;

		dir_name = (gchar *) dir_iterator->data;
		g_assert (dir_name != NULL);

		dir = g_dir_open (dir_name, 0, NULL);
		if (dir == NULL)
			continue;

		while ((d_name = g_dir_read_name (dir)) != NULL) {
			gchar *full_entry_name;

			full_entry_name = g_build_filename (dir_name, d_name, NULL);
			if (d_name[0] != '.' && g_file_test (full_entry_name, G_FILE_TEST_IS_DIR)) {
				FormatTemplateCategory *category;

				category = gnumeric_xml_read_format_template_category (full_entry_name);
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
category_list_free (GList *categories)
{
	GList *l;

	g_return_if_fail (categories);

	for (l = categories; l != NULL; l = l->next) {
		category_free ((FormatTemplateCategory *) l->data);
	}
	g_list_free (categories);
}

GList *
category_group_list_get (void)
{
	GList *category_groups = NULL;
	GSList *dir_list;
	GList *categories, *l;
	FormatTemplateCategoryGroup *current_group;

	dir_list = go_slist_create (gnm_app_prefs->autoformat.sys_dir,
				     gnm_app_prefs->autoformat.usr_dir,
				     NULL);
	dir_list = g_slist_concat (dir_list,
		g_slist_copy ((GSList *)gnm_app_prefs->autoformat.extra_dirs));
	categories = category_list_get_from_dir_list (dir_list);

	categories = g_list_sort (categories, category_compare_name_and_dir);

	current_group = NULL;
	for (l = categories; l != NULL; l = l->next) {
		FormatTemplateCategory *category;

		category = (FormatTemplateCategory *) l->data;
		if (current_group == NULL || strcmp (current_group->name, category->name) != 0) {
			if (current_group != NULL) {
				category_groups = g_list_prepend (category_groups, current_group);
			}
			current_group = g_new (FormatTemplateCategoryGroup, 1);
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
	g_slist_free (dir_list); /* strings are owned by the gnm_app_prefs */

	return category_groups;
}


void
category_group_list_free (GList *groups)
{
	GList *ptr;

	for (ptr = groups; ptr != NULL; ptr = ptr->next) {
		FormatTemplateCategoryGroup *group = ptr->data;
		g_free (group->name);
		g_free (group->description);
		category_list_free (group->categories);
		g_free (group);
	}
	g_list_free (groups);
}

GSList *
category_group_get_templates_list (FormatTemplateCategoryGroup *category_group,
				   GOCmdContext *cc)
{
	GSList *templates = NULL;
	GList *l;

	for (l = category_group->categories; l != NULL; l = l->next)
		templates = g_slist_concat (templates,
			category_get_templates_list (l->data, cc));

	return g_slist_sort (templates, format_template_compare_name);
}

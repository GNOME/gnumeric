/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xml-io-autoft.c : Read/Write Format templates using xml encoding.
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
#include "xml-io-autoft.h"

#include "command-context.h"
#include "workbook-control.h"
#include "str.h"
#include "xml-io.h"
#include "format-template.h"
#include "gutils.h"
#include "plugin-util.h"
#include "mstyle.h"

#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <gal/util/e-xml-utils.h>
#include <unistd.h>
#include <string.h>

#define CATEGORY_FILE_NAME    ".category"
#define ERR_READ_FT_MEMBER "xml_read_format_template_member: : No %s section in template member!"

/**
 * gnumeric_xml_read_format_template_category :
 * Open an XML file and read a FormatTemplateCategory
 */
FormatTemplateCategory *
gnumeric_xml_read_format_template_category (char const *dir_name)
{
	gchar *file_name;
	xmlDocPtr doc;
	xmlNodePtr orig_info_node, translated_info_node;
	FormatTemplateCategory *category = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);

	file_name = g_concat_dir_and_file (dir_name, CATEGORY_FILE_NAME);
	doc = xmlParseFile (file_name);
	if (doc != NULL && doc->xmlRootNode != NULL
	    && xmlSearchNsByHref (doc, doc->xmlRootNode, (xmlChar *)"http://www.gnome.org/gnumeric/format-template-category/v1") != NULL
	    && strcmp (doc->xmlRootNode->name, "FormatTemplateCategory") == 0
	    && (translated_info_node = e_xml_get_child_by_name_by_lang_list (doc->xmlRootNode, "Information", NULL)) != NULL) {
		xmlChar *orig_name, *name, *description, *lang;

		orig_info_node = e_xml_get_child_by_name_no_lang (doc->xmlRootNode, "Information");
		if (orig_info_node == NULL) {
			orig_info_node = translated_info_node;
		}

		orig_name = xmlGetProp (orig_info_node, (xmlChar *)"name");
		name = xmlGetProp (translated_info_node, (xmlChar *)"name");
		description = xmlGetProp (translated_info_node, (xmlChar *)"description");
		lang = xmlGetProp (translated_info_node, (xmlChar *)"xml:lang");
		if (orig_name != NULL) {
			category = g_new (FormatTemplateCategory, 1);
			category->directory = g_strdup (dir_name);
			category->orig_name = g_strdup ((gchar *)orig_name);
			category->name = g_strdup ((gchar *)name);
			category->description = g_strdup ((gchar *)description);
			category->lang_score = g_lang_score_in_lang_list ((gchar *)lang, NULL);
			category->is_writable = (access (dir_name, W_OK) == 0);
		}
		xmlFree (orig_name);
		xmlFree (name);
		xmlFree (description);
		xmlFree (lang);
	}
	xmlFreeDoc (doc);
	g_free (file_name);

	return category;
}

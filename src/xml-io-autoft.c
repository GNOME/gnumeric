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

#include <config.h>
#include "xml-io-autoft.h"
#include "command-context.h"
#include "workbook-control.h"
#include "str.h"
#include "xml-io.h"
#include "format-template.h"
#include "gutils.h"
#include "mstyle.h"

#include <gnome.h>
#include <gnome-xml/parser.h>
#include <gnome-xml/parserInternals.h>
#include <gnome-xml/xmlmemory.h>
#include <gal/util/e-xml-utils.h>

#define CATEGORY_FILE_NAME    ".category"

/*
 * Create an XML subtree of doc equivalent to the given FormatColRowInfo
 */
static void
xml_write_format_col_row_info (XmlParseContext *ctxt, FormatColRowInfo info, xmlNodePtr node)
{
	xmlNodePtr  child;

	/*
	 * Write placement
	 */
	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "Placement", NULL);
	xml_set_value_int (child, "offset", info.offset);
	xml_set_value_int (child, "offset_gravity", info.offset_gravity);
	xmlAddChild (node, child);

	/*
	 * Write dimensions
	 */
	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "Dimensions", NULL);
	xml_set_value_int (child, "size", info.size);
	xmlAddChild (node, child);
}

/*
 * Create an XML subtree of doc equivalent to the given TemplateMember
 */
static xmlNodePtr
xml_write_format_template_member (XmlParseContext *ctxt, TemplateMember *member)
{
	xmlNodePtr  child;
	xmlNodePtr  cur;

	/*
	 * General information about member
	 */
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Member", NULL);
	if (cur == NULL)
		return NULL;

	/*
	 * Write row and col info
	 */
	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "Row", NULL);
	xml_write_format_col_row_info (ctxt, format_template_member_get_row_info (member), child);
	xmlAddChild (cur, child);

	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "Col", NULL);
	xml_write_format_col_row_info (ctxt, format_template_member_get_col_info (member), child);
	xmlAddChild (cur, child);

	/*
	 * Write frequency information
	 */
	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "Frequency", NULL);
	xml_set_value_int (child, "direction", format_template_member_get_direction (member));
	xml_set_value_int (child, "repeat", format_template_member_get_repeat (member));
	xml_set_value_int (child, "skip", format_template_member_get_skip (member));
	xml_set_value_int (child, "edge", format_template_member_get_edge (member));
	xmlAddChild (cur, child);

	/*
	 * Write style
	 */
	child = xml_write_style (ctxt, format_template_member_get_style (member));
	xmlAddChild (cur, child);

	return cur;
}

/*
 * Create an XML subtree of doc equivalent to the given FormatTemplate
 */
static xmlNodePtr
xml_write_format_template_members (XmlParseContext *ctxt, FormatTemplate *ft)
{
	xmlNodePtr root;
	xmlNsPtr gmr;
	xmlNodePtr child;
	GSList *members;
	String *author, *name, *description;
	char *author_c, *name_c, *description_c;

	/*
	 * General information about the Template
	 */

	root = xmlNewDocNode (ctxt->doc, NULL, "FormatTemplate", NULL);
	if (root == NULL)
		return NULL;

	gmr = xmlNewNs (root, "http://www.gnome.org/gnumeric/format-template/v1", "gmr");
	xmlSetNs(root, gmr);
	ctxt->ns = gmr;

	child = xmlNewChild (root, gmr, "Information", NULL);

	author_c      = format_template_get_author (ft);
	name_c        = format_template_get_name (ft);
	description_c = format_template_get_description (ft);

	xml_set_value_cstr (child, "author", author_c);
	xml_set_value_cstr (child, "name", name_c);
	xml_set_value_cstr (child, "description", description_c);

	g_free (author_c);
	g_free (name_c);
	g_free (description_c);

	/*
	 * Write members
	 */
	child = xmlNewChild (root, gmr, "Members", NULL);

	members = format_template_get_members (ft);

	while (members) {
		TemplateMember *member = members->data;
		xmlNodePtr c;

		c = xml_write_format_template_member (ctxt, member);
		xmlAddChild (child, c);

		members = g_slist_next (members);
	}

	return root;
}


/*
 * Save a Template in an XML file
 * One build an in-memory XML tree and save it to a file.
 * returns 0 in case of success, -1 otherwise.
 */
int
gnumeric_xml_write_format_template (WorkbookControl *context, FormatTemplate *ft,
				    const char *filename)
{
	XmlParseContext *ctxt;
	xmlDocPtr xml;
	int ret;

	g_return_val_if_fail (ft != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	/*
	 * Create the tree
	 */
	xml = xmlNewDoc ("1.0");
	if (xml == NULL) {
		gnumeric_error_save (COMMAND_CONTEXT (context), "");
		return -1;
	}
	ctxt = xml_parse_ctx_new (xml, NULL);
	xml->root = xml_write_format_template_members (ctxt, ft);
	xml_parse_ctx_destroy (ctxt);

	/*
	 * Dump it.
	 */
	xmlSetDocCompressMode (xml, 0);
	ret = xmlSaveFile (filename, xml);
	xmlFreeDoc (xml);
	if (ret < 0) {
		gnumeric_error_save (COMMAND_CONTEXT (context),
			"Error while trying to save autoformat template");
		return -1;
	}
	return 0;
}

#define ERR_READ_FT_MEMBER "xml_read_format_template_member: : No %s section in template member!"

/*
 * FIXME : Possibly doesn't work, do we need to pass the top-level tree instead of the child
 *         we just added for @tree?
 */
static FormatColRowInfo
xml_read_format_col_row_info (XmlParseContext *ctxt, FormatTemplate *ft, xmlNodePtr tree)
{
	FormatColRowInfo info;
	xmlNodePtr child;

	/*
	 * Read placement
	 */
	child = e_xml_get_child_by_name (tree, "Placement");
	if (child) {
		xml_get_value_int  (child, "offset", &info.offset);
		xml_get_value_int  (child, "offset_gravity", &info.offset_gravity);
	} else {
		fprintf (stderr, ERR_READ_FT_MEMBER, "Placement");
	}

	/*
	 * Read dimensions
	 */
	child = e_xml_get_child_by_name (tree, "Dimensions");
	if (child){
		xml_get_value_int (child, "size", &info.size);
	} else {
		fprintf (stderr, ERR_READ_FT_MEMBER, "Dimensions");
	}

	return info;
}

/*
 * Create a Sheet equivalent to the XML subtree of doc.
 */
static gboolean
xml_read_format_template_member (XmlParseContext *ctxt, FormatTemplate *ft, xmlNodePtr tree)
{
	xmlNodePtr child;
	TemplateMember *member;
	MStyle *mstyle = NULL;
	FormatColRowInfo row, col;
	FreqDirection direction;
	int repeat, skip, edge;

	if (strcmp (tree->name, "Member")){
		fprintf (stderr,
			 "xml_read_format_template_member: invalid element type %s, 'Member' expected\n",
			 tree->name);
		return FALSE;
	}

	/*
	 * Read row and column information
	 */
	child = e_xml_get_child_by_name (tree, "Row");
	if (child){
		row = xml_read_format_col_row_info (ctxt, ft, child);
	} else {
		fprintf (stderr, ERR_READ_FT_MEMBER, "Row");
		return FALSE;
	}

	child = e_xml_get_child_by_name (tree, "Col");
	if (child){
		col = xml_read_format_col_row_info (ctxt, ft, child);
	} else {
		fprintf (stderr, ERR_READ_FT_MEMBER, "Col");
		return FALSE;
	}

	child = e_xml_get_child_by_name (tree, "Frequency");
	if (child){
		xml_get_value_int (child, "direction", (int *) &direction);
		xml_get_value_int (child, "repeat", &repeat);
		xml_get_value_int (child, "skip", &skip);
		xml_get_value_int (child, "edge", &edge);
	} else {
		fprintf (stderr, ERR_READ_FT_MEMBER, "Frequency");
		return FALSE;
	}

	/*
	 * Read style information
	 */
	child = e_xml_get_child_by_name (tree, "Style");
	if (child) {
		mstyle = xml_read_style (ctxt, child);
	} else {
		fprintf (stderr, ERR_READ_FT_MEMBER, "Style");
		return FALSE;
	}

	member = format_template_member_new ();
        format_template_member_set_row_info (member, row);
        format_template_member_set_col_info (member, col);
	format_template_member_set_direction (member, direction);
	format_template_member_set_repeat (member, repeat);
        format_template_member_set_skip (member, skip);
	format_template_member_set_edge (member, edge);
	format_template_member_set_style (member, mstyle);

	format_template_attach_member (ft, member);

	/*
	 * We need to unref the mstyle here, the TemplateMember will
	 * take care of freeing the mstyle.
	 */
	if (mstyle)
		mstyle_unref (mstyle);

	return TRUE;
}

/*
 * Create a FormatTemplate equivalent to the XML subtree of doc.
 */
static gboolean
xml_read_format_template_members (XmlParseContext *ctxt, FormatTemplate *ft, xmlNodePtr tree)
{
	xmlNodePtr child, c;

	if (strcmp (tree->name, "FormatTemplate")){
		fprintf (stderr,
			 "xml_read_format_template_members: invalid element type %s, 'FormatTemplate' expected`\n",
			 tree->name);
		return FALSE;
	}

	/*
	 * Read some general information
	 */
	child = e_xml_get_child_by_name_by_lang_list (tree, "Information", NULL);
	if (child){
		String *author, *name, *description;

		author      = xml_get_value_string (child, "author");
		name        = xml_get_value_string (child, "name");
		description = xml_get_value_string (child, "description");

		format_template_set_author (ft, author->str);
		format_template_set_name (ft, name->str);
		format_template_set_description (ft, description->str);

		string_unref (author);
		string_unref (name);
		string_unref (description);
	} else {
		return FALSE;
	}

	/*
	 * Read Members
	 */
	child = e_xml_get_child_by_name (tree, "Members");
	if (child == NULL)
		return FALSE;

	/*
	 * Iterate trough the members and call upon
	 * xml_read_format_template_member to fill
	 * the FormatTemplate one by one
	 */
	c = child->xmlChildrenNode;

	while (c != NULL) {
		if (!xml_read_format_template_member (ctxt, ft, c))
			return FALSE;
		c = c->next;
	}

	return TRUE;
}


/*
 * Open an XML file and read a FormatTemplate
 * One parse the XML file, getting a tree, then analyze the tree to build
 * the actual in-memory structure.
 */
int
gnumeric_xml_read_format_template (WorkbookControl *context, FormatTemplate *ft,
				   const char *filename)
{
	xmlDocPtr res;
	xmlNsPtr gmr;
	XmlParseContext *ctxt;

	g_return_val_if_fail (filename != NULL, -1);

	/*
	 * Load the file into an XML tree.
	 */
	res = xmlParseFile (filename);
	if (res == NULL) {
		gnumeric_error_read (COMMAND_CONTEXT (context),
			_("Error while trying to load autoformat template"));
		return -1;
	}
	if (res->root == NULL) {
		xmlFreeDoc (res);
		gnumeric_error_read (COMMAND_CONTEXT (context),
			_("Invalid xml file. Tree is empty ?"));
		return -1;
	}

	/*
	 * Do a bit of checking, get the namespaces, and check the top elem.
	 */
	gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/format-template/v1");
	if (strcmp (res->root->name, "FormatTemplate") || (gmr == NULL)) {
		xmlFreeDoc (res);
		gnumeric_error_read (COMMAND_CONTEXT (context),
			_("Is not an autoformat template file"));
		return -1;
	}
	ctxt = xml_parse_ctx_new (res, gmr);

	/*
	 * Read information and all members
	 */
	if (!xml_read_format_template_members (ctxt, ft, res->root)) {
		gnumeric_error_read (COMMAND_CONTEXT (context),
			_("Error while trying to build tree from autoformat template file"));
		return -1;
	}

	xml_parse_ctx_destroy (ctxt);
	xmlFreeDoc (res);

	return 0;
}

/*
 * Open an XML file and read a FormatTemplateCategory
 *
 */
FormatTemplateCategory *
gnumeric_xml_read_format_template_category (const char *dir_name)
{
	gchar *file_name;
	xmlDocPtr doc;
	xmlNodePtr orig_info_node, translated_info_node;
	FormatTemplateCategory *category = NULL;

	g_return_val_if_fail (dir_name != NULL, NULL);

	file_name = g_concat_dir_and_file (dir_name, CATEGORY_FILE_NAME);
	doc = xmlParseFile (file_name);
	if (doc != NULL && doc->root != NULL
	    && xmlSearchNsByHref (doc, doc->root, "http://www.gnome.org/gnumeric/format-template-category/v1") != NULL
	    && strcmp (doc->root->name, "FormatTemplateCategory") == 0
	    && (translated_info_node = e_xml_get_child_by_name_by_lang_list (doc->root, "Information", NULL)) != NULL) {
		xmlChar *orig_name, *name, *description, *lang;

		orig_info_node = e_xml_get_child_by_name_no_lang (doc->root, "Information");
		if (orig_info_node == NULL) {
			orig_info_node = translated_info_node;
		}

		orig_name = xmlGetProp (orig_info_node, "name");
		name = xmlGetProp (translated_info_node, "name");
		description = xmlGetProp (translated_info_node, "description");
		lang = xmlGetProp (translated_info_node, "xml:lang");
		if (orig_name != NULL) {
			category = g_new (FormatTemplateCategory, 1);
			category->directory = g_strdup (dir_name);
			category->orig_name = g_strdup (orig_name);
			category->name = g_strdup (name);
			category->description = g_strdup (description);
			category->lang_score = g_lang_score_in_lang_list (lang, NULL);
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

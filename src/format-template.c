/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * format-template.c : implementation of the template handling system.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "format-template.h"

#include "mstyle.h"
#include "sheet.h"
#include "sheet-style.h"
#include "style-border.h"
#include "command-context.h"
#include "ranges.h"
#include "xml-io.h"
#include "io-context.h"
#include "plugin-util.h"	/* for gnumeric_fopen */
#include <string.h>
#include <libxml/parser.h>

#define CC2XML(s) ((const xmlChar *)(s))
#define CXML2C(s) ((const char *)(s))

#define ROW_COL_KEY(row,col) GINT_TO_POINTER (row * SHEET_MAX_COLS + col)

/******************************************************************************
 * FormatTemplateMember - Getters/setters and creation
 ******************************************************************************/

/**
 * format_template_member_new:
 *
 * Create a new TemplateMember
 *
 * Return value: the new TemplateMember
 **/
TemplateMember *
format_template_member_new (void)
{
	TemplateMember *member;

	/* Sanity check for ROW_COL_KEY.  */
	g_assert (INT_MAX / SHEET_MAX_COLS > SHEET_MAX_ROWS);

	member = g_new (TemplateMember, 1);

	member->col.offset	   = member->row.offset = 0;
	member->col.offset_gravity = member->row.offset_gravity = 1;
	member->col.size	   = member->row.size = 1;
	member->direction = FREQ_DIRECTION_NONE;
	member->repeat    = 0;
	member->skip      = 0;
	member->edge      = 0;
	member->mstyle    = NULL;

	return member;
}

/**
 * format_template_member_clone:
 *
 * Clone a template member
 *
 * Return value: a copy of @member
 **/
TemplateMember *
format_template_member_clone (TemplateMember *member)
{
	TemplateMember *clone;

	clone = format_template_member_new ();

	clone->row = member->row;
	clone->col = member->col;
	clone->direction = member->direction;
	clone->repeat    = member->repeat;
	clone->skip      = member->skip;
	clone->edge      = member->edge;
	clone->mstyle    = member->mstyle;
	mstyle_ref (member->mstyle);

	return clone;
}

/**
 * format_template_member_free:
 * @member: TemplateMember
 *
 * Frees an existing template member
 **/
void
format_template_member_free (TemplateMember *member)
{
	g_return_if_fail (member != NULL);

	if (member->mstyle) {
		mstyle_unref (member->mstyle);
		member->mstyle = NULL;
	}

	g_free (member);
}


/**
 * format_template_member_get_rect:
 * @member:
 * @r:
 *
 * Get the rectangular area covered by the TemplateMember @member in the parent
 * rectangle @r.
 * NOTE : This simply calculates the rectangle, it does not calculate repetitions
 *        or anything. That you'll have to do yourself :-)
 *
 * Return value: a GnmRange containing the effective rectangle of @member
 **/
static GnmRange
format_template_member_get_rect (TemplateMember *member, GnmRange const *r)
{
	GnmRange res;

	res.start.row = res.end.row = 0;
	res.start.col = res.end.col = 0;

	g_return_val_if_fail (member != NULL, res);

	/* Calculate where the top left of the rectangle will come */
	if (member->row.offset_gravity > 0)
		res.start.row = r->start.row + member->row.offset;
	else
		res.end.row = r->end.row - member->row.offset;

	if (member->col.offset_gravity > 0)
		res.start.col = r->start.col + member->col.offset;
	else
		res.end.col = r->end.col - member->col.offset;

	/*
	 * Now that we know these coordinates we'll calculate the
	 * bottom right coordinates
	 */
	if (member->row.offset_gravity > 0) {
		if (member->row.size > 0)
			res.end.row = res.start.row + member->row.size - 1;
		else
			res.end.row = r->end.row + member->row.size;
	} else {
		if (member->row.size > 0)
			res.start.row = res.end.row - member->row.size + 1;
		else
			res.start.row = r->start.row - member->row.size;
	}

	if (member->col.offset_gravity > 0) {
		if (member->col.size > 0)
			res.end.col = res.start.col + member->col.size - 1;
		else
			res.end.col = r->end.col + member->col.size;
	} else {
		if (member->col.size > 0)
			res.start.col = res.end.col - member->col.size + 1;
		else
			res.start.col = r->start.col - member->col.size;
	}

	return res;
}

/******************************************************************************
 * Getters and setters for FormatTemplateMember
 *
 * NOTE : MStyle are taken care of internally, there is no
 *        need to unref or ref mstyle's manually.
 */

static void
format_template_member_set_direction (TemplateMember *member, FreqDirection direction)
{
	g_return_if_fail (direction == FREQ_DIRECTION_NONE || direction == FREQ_DIRECTION_HORIZONTAL ||
			  direction == FREQ_DIRECTION_VERTICAL);

	member->direction = direction;
}

static void
format_template_member_set_repeat (TemplateMember *member, int repeat)
{
	g_return_if_fail (repeat >= -1);

	member->repeat = repeat;
}

static void
format_template_member_set_skip (TemplateMember *member, int skip)
{
	g_return_if_fail (skip >= 0);

	member->skip = skip;
}

static void
format_template_member_set_edge (TemplateMember *member, int edge)
{
	g_return_if_fail (edge >= 0);

	member->edge = edge;
}

/******************************************************************************
 * FormatTemplate - Creation/Destruction
 ******************************************************************************/

/**
 * format_template_new:
 *
 * Create a new 'empty' FormatTemplate
 *
 * Return value: the new FormatTemplate
 **/
FormatTemplate *
format_template_new (void)
{
	FormatTemplate *ft;

	ft = g_new0 (FormatTemplate, 1);

	ft->filename    = g_strdup ("");
	ft->author      = g_strdup (g_get_real_name ());
	ft->name        = g_strdup (_("Name"));
	ft->description = g_strdup ("");

	ft->category = NULL;

	ft->members = NULL;
	ft->number    = TRUE;
	ft->border    = TRUE;
	ft->font      = TRUE;
	ft->patterns  = TRUE;
	ft->alignment = TRUE;

	ft->edges.left   = TRUE;
	ft->edges.right  = TRUE;
	ft->edges.top    = TRUE;
	ft->edges.bottom = TRUE;

	ft->table     = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)mstyle_unref);
	ft->invalidate_hash = TRUE;

	range_init (&ft->dimension, 0,0,0,0);

	return ft;
}

/**
 * format_template_free:
 **/
void
format_template_free (FormatTemplate *ft)
{
	GSList *ptr;

	g_return_if_fail (ft != NULL);

	g_free (ft->filename);
	g_free (ft->author);
	g_free (ft->name);
	g_free (ft->description);

	for (ptr = ft->members; ptr != NULL ; ptr = ptr->next)
		format_template_member_free (ptr->data);
	g_slist_free (ft->members);

	g_hash_table_destroy (ft->table);

	g_free (ft);
}

/**
 * format_template_clone:
 * @ft: FormatTemplate
 *
 * Make a copy of @ft.
 *
 * Returns : a copy of @ft
 **/
FormatTemplate *
format_template_clone (FormatTemplate const *ft)
{
	FormatTemplate *clone;
	GSList *ptr = NULL;

	g_return_val_if_fail (ft != NULL, NULL);

	clone = format_template_new ();
	format_template_set_author (clone, ft->author);
	format_template_set_name (clone, ft->name);
	format_template_set_description (clone, ft->description);
	g_free (clone->filename); clone->filename = g_strdup (ft->filename);

	clone->category    = ft->category;

	for (ptr = ft->members; ptr != NULL ; ptr = ptr->next)
		format_template_attach_member (clone,
			format_template_member_clone (ptr->data));

	clone->number    = ft->number;
	clone->border    = ft->border;
	clone->font      = ft->font;
	clone->patterns  = ft->patterns;
	clone->alignment = ft->alignment;

	clone->edges.left   = ft->edges.left;
	clone->edges.right  = ft->edges.right;
	clone->edges.top    = ft->edges.top;
	clone->edges.bottom = ft->edges.bottom;

	clone->dimension = ft->dimension;

	clone->invalidate_hash = TRUE;

	return clone;
}

static void
xml_read_format_col_row_info (FormatColRowInfo *info, xmlNodePtr parent)
{
	xmlNode *child;
	int found = 0;

	for (child = parent->xmlChildrenNode; child != NULL ; child = child->next) {
		if (xmlIsBlankNode (child) || child->name == NULL)
			continue;
		if (!strcmp (child->name, "Placement")) {
			g_return_if_fail (!(found & 1));
			xml_node_get_int  (child, "offset", &info->offset);
			xml_node_get_int  (child, "offset_gravity", &info->offset_gravity);
			found |= 1;
		} else if (!strcmp (child->name, "Dimensions")) {
			g_return_if_fail (!(found & 2));
			xml_node_get_int (child, "size", &info->size);
			found |= 2;
		}
	}
	g_return_if_fail (found == 3);
}

static gboolean
xml_read_format_template_member (XmlParseContext *ctxt, FormatTemplate *ft, xmlNodePtr tree)
{
	xmlNodePtr child;
	TemplateMember *member;
	int tmp, found = 0;

	g_return_val_if_fail (!strcmp (tree->name, "Member"), FALSE);
	member = format_template_member_new ();

	for (child = tree->xmlChildrenNode; child != NULL ; child = child->next) {
		if (xmlIsBlankNode (child) || child->name == NULL)
			continue;
		if (!strcmp (child->name, "Col"))
			xml_read_format_col_row_info (&member->col, child);
		else if (!strcmp (child->name, "Row"))
			xml_read_format_col_row_info (&member->row, child);
		else if (!strcmp (child->name, "Frequency")) {
			if (found & 1) { g_warning ("Multiple Frequency specs"); }
			if (xml_node_get_int (child, "direction", &tmp))
				format_template_member_set_direction (member, tmp);
			if (xml_node_get_int (child, "repeat", &tmp))
				format_template_member_set_repeat (member, tmp);
			if (xml_node_get_int (child, "skip", &tmp))
				format_template_member_set_skip (member, tmp);
			if (xml_node_get_int (child, "edge", &tmp))
				format_template_member_set_edge (member, tmp);
			found |= 1;
		} else if (!strcmp (child->name, "Style")) {
			if (found & 2) { g_warning ("Multiple Styles"); }
			member->mstyle = xml_read_style (ctxt, child);
			found |= 2;
		}
	}
	if (found != 3) {
		g_warning ("Invalid Member, missing %s", (found & 1) ? "Style" : "Frequency");
		format_template_member_free (member);
		return FALSE;
	}

	format_template_attach_member (ft, member);
	return TRUE;
}

static gboolean
xml_read_format_template_members (XmlParseContext *ctxt, FormatTemplate *ft, xmlNodePtr tree)
{
	xmlNode *child;

	g_return_val_if_fail (!strcmp (tree->name, "FormatTemplate"), FALSE);

	child = e_xml_get_child_by_name_by_lang_list (tree, "Information", NULL);
	if (child) {
		xmlChar *author = xml_node_get_cstr (child, "author");
		xmlChar *name   = xml_node_get_cstr (child, "name");
		xmlChar *descr  = xml_node_get_cstr (child, "description");

		format_template_set_author (ft, CXML2C (author));
		format_template_set_name (ft,  CXML2C (name));
		format_template_set_description (ft,  CXML2C (descr));

		xmlFree (author);
		xmlFree (name);
		xmlFree (descr);
	} else
		return FALSE;

	child = e_xml_get_child_by_name (tree, CC2XML ("Members"));
	if (child == NULL)
		return FALSE;
	for (child = child->xmlChildrenNode; child != NULL ; child = child->next)
		if (!xmlIsBlankNode (child) &&
		    !xml_read_format_template_member (ctxt, ft, child))
			return FALSE;

	return TRUE;
}

/**
 * format_template_new_from_file:
 * @context:
 * @filename: The filename to load from
 *
 * Create a new FormatTemplate and load a template file
 * into it.
 *
 * Return value: a new FormatTemplate (or NULL on error)
 **/
FormatTemplate *
format_template_new_from_file (char const *filename, GnmCmdContext *cc)
{
	FormatTemplate	*ft = NULL;
	xmlDoc		*doc;

	g_return_val_if_fail (filename != NULL, NULL);

	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
		return NULL;

	doc = xmlParseFile (filename);
	if (doc == NULL) {
		gnm_cmd_context_error_import (cc,
			_("Error while trying to load autoformat template"));
		return NULL;
	}
	if (doc->xmlRootNode != NULL) {
		xmlNs *ns = xmlSearchNsByHref (doc, doc->xmlRootNode,
			CC2XML ("http://www.gnome.org/gnumeric/format-template/v1"));
		if (ns != NULL && !strcmp (doc->xmlRootNode->name, "FormatTemplate")) {
			XmlParseContext *ctxt = xml_parse_ctx_new (doc, ns, NULL);

			ft = format_template_new ();
			if (xml_read_format_template_members (ctxt, ft, doc->xmlRootNode)) {
				g_free (ft->filename);
				ft->filename = g_strdup (filename);
			} else {
				format_template_free (ft);
				ft = NULL;
				gnm_cmd_context_error_import (cc,
					_("Error while trying to build tree from autoformat template file"));
			}

			xml_parse_ctx_destroy (ctxt);
		} else
			gnm_cmd_context_error_import (cc,
				_("Is not an autoformat template file"));
	} else
		gnm_cmd_context_error_import (cc,
			_("Invalid xml file. Tree is empty?"));

	xmlFreeDoc (doc);
	return ft;
}

static xmlNode *
format_colrow_info_write_xml (FormatColRowInfo const *info,
			      xmlNode *parent, xmlChar const *type,
			      G_GNUC_UNUSED XmlParseContext *ctxt)
{
	xmlNode *tmp, *container;

	container = xmlNewChild (parent, parent->ns, type, NULL);
	tmp = xmlNewChild (container, container->ns, CC2XML ("Placement"), NULL);
	xml_node_set_int (tmp, "offset", info->offset);
	xml_node_set_int (tmp, "offset_gravity", info->offset_gravity);

	tmp = xmlNewChild (container, container->ns, CC2XML ("Dimensions"), NULL);
	xml_node_set_int (tmp, "size", info->size);

	return container;
}

/*
 * Create an XML subtree of doc equivalent to the given TemplateMember
 */
static xmlNodePtr
xml_write_format_template_member (XmlParseContext *ctxt, TemplateMember *member)
{
	xmlNode *tmp, *member_node;

	member_node = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Member"), NULL);
	if (member_node == NULL)
		return NULL;

	format_colrow_info_write_xml (&member->col, member_node, "Col", ctxt);
	format_colrow_info_write_xml (&member->row, member_node, "Row", ctxt);

	tmp = xmlNewChild (member_node, member_node->ns, CC2XML ("Frequency") , NULL);
	xml_node_set_int (tmp, "direction", member->direction);
	xml_node_set_int (tmp, "repeat", member->repeat);
	xml_node_set_int (tmp, "skip", member->skip);
	xml_node_set_int (tmp, "edge", member->edge);

	xmlAddChild (member_node, xml_write_style (ctxt, member->mstyle));

	return member_node;
}

static xmlNodePtr
xml_write_format_template_members (XmlParseContext *ctxt, FormatTemplate const *ft)
{
	xmlNode *root, *child;
	xmlNs   *ns;
	GSList  *member;

	root = xmlNewDocNode (ctxt->doc, NULL, CC2XML ("FormatTemplate"), NULL);
	if (root == NULL)
		return NULL;

	ns = xmlNewNs (root,
		       CC2XML ("http://www.gnome.org/gnumeric/format-template/v1"),
		       CC2XML ("gmr"));
	xmlSetNs (root, ns);
	ctxt->ns = ns;

	child = xmlNewChild (root, ns, CC2XML ("Information"), NULL);
	xml_node_set_cstr (child, "author", ft->author);
	xml_node_set_cstr (child, "name", ft->name);
	xml_node_set_cstr (child, "description", ft->description);

	child = xmlNewChild (root, ns, CC2XML ("Members"), NULL);
	for (member = ft->members ; member != NULL ; member = member->next)
		xmlAddChild (child,
			xml_write_format_template_member (ctxt, member->data));

	return root;
}

/**
 * format_template_save_to_file:
 * @ft: a FormatTemplate
 * @cc : where to report errors
 *
 * Saves template @ft to a filename set with format_template_set_filename
 *
 * Return value: return TRUE on error.
 **/
gboolean
format_template_save (FormatTemplate const *ft, GnmCmdContext *cc)
{
	FILE *file;
	IOContext *io_context;
	gboolean success = FALSE;

	g_return_val_if_fail (ft != NULL, TRUE);

	io_context = gnumeric_io_context_new (cc);
	file = gnumeric_fopen (io_context, ft->filename, "w");
	if (file != NULL) {
		xmlDoc *doc = xmlNewDoc (CC2XML ("1.0"));
		if (doc != NULL) {
			XmlParseContext *ctxt = xml_parse_ctx_new (doc, NULL, NULL);
			doc->xmlRootNode = xml_write_format_template_members (ctxt, ft);
			xml_parse_ctx_destroy (ctxt);
			xmlSetDocCompressMode (doc, 0);
			xmlDocDump (file, doc);
			xmlFreeDoc (doc);

			success = TRUE;
		} else
			gnm_cmd_context_error_export (cc, "");

		fclose (file);
	}

	g_object_unref (G_OBJECT (io_context));

	return success;
}

/**
 * format_template_attach_member:
 * @ft: FormatTemplate
 * @member: the new member to attach
 *
 * Attaches @member to template @ft
 **/
void
format_template_attach_member (FormatTemplate *ft, TemplateMember *member)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (member != NULL);

	/*
	 * NOTE : Append is slower, but that's not really an issue
	 *        here, because a FormatTemplate will most likely
	 *        not have 'that many' members anyway
	 */
	ft->members = g_slist_append (ft->members, member);
}

/**
 * format_template_detach_member:
 * @ft: FormatTemplate
 * @member: a TemplateMember
 *
 * Detaches @member from template @ft
 **/
void
format_template_detach_member (FormatTemplate *ft, TemplateMember *member)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (member != NULL);

	ft->members = g_slist_remove (ft->members, member);
}

/**
 * format_template_compare_name:
 * @ft_a: First FormatTemplate
 * @ft_b: Second FormatTemplate
 *
 **/
gint
format_template_compare_name (gconstpointer a, gconstpointer b)
{
	FormatTemplate const *ft_a = (FormatTemplate const *) a;
	FormatTemplate const *ft_b = (FormatTemplate const *) b;

	return g_utf8_collate (ft_a->name, ft_b->name);
}

/******************************************************************************
 * FormatTemplate - Actual implementation (Filtering and calculating)
 ******************************************************************************/

/**
 * format_template_filter_style:
 * @ft:
 * @mstyle:
 * @fill_defaults: If set fill in the gaps with the "default" mstyle.
 *
 * Filter an mstyle and strip and replace certain elements
 * based on what the user wants to apply.
 * Basically you should pass FALSE as @fill_defaults, unless you want to have
 * a completely filled style to be returned. If you set @fill_default to TRUE
 * the returned mstyle might have some of its elements 'not set'
 *
 * Return value: The same mstyle as @mstyle with most likely some modifications
 **/
static MStyle *
format_template_filter_style (FormatTemplate *ft, MStyle *mstyle, gboolean fill_defaults)
{
	g_return_val_if_fail (ft != NULL, NULL);
	g_return_val_if_fail (mstyle != NULL, NULL);

	/*
	 * Don't fill with defaults, this is perfect for when the
	 * mstyles are going to be 'merged' with other mstyles which
	 * have all their elements set
	 */
	if (!fill_defaults) {
		if (!ft->number) {
			mstyle_unset_element (mstyle, MSTYLE_FORMAT);
		}
		if (!ft->border) {
			mstyle_unset_element (mstyle, MSTYLE_BORDER_TOP);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_BOTTOM);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_LEFT);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_RIGHT);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_DIAGONAL);
			mstyle_unset_element (mstyle, MSTYLE_BORDER_REV_DIAGONAL);
		}
		if (!ft->font) {
			mstyle_unset_element (mstyle, MSTYLE_FONT_NAME);
			mstyle_unset_element (mstyle, MSTYLE_FONT_BOLD);
			mstyle_unset_element (mstyle, MSTYLE_FONT_ITALIC);
			mstyle_unset_element (mstyle, MSTYLE_FONT_UNDERLINE);
			mstyle_unset_element (mstyle, MSTYLE_FONT_STRIKETHROUGH);
			mstyle_unset_element (mstyle, MSTYLE_FONT_SIZE);

			mstyle_unset_element (mstyle, MSTYLE_COLOR_FORE);
		}
		if (!ft->patterns) {
			mstyle_unset_element (mstyle, MSTYLE_COLOR_BACK);
			mstyle_unset_element (mstyle, MSTYLE_COLOR_PATTERN);
			mstyle_unset_element (mstyle, MSTYLE_PATTERN);
		}
		if (!ft->alignment) {
			mstyle_unset_element (mstyle, MSTYLE_ALIGN_V);
			mstyle_unset_element (mstyle, MSTYLE_ALIGN_H);
		}
	} else {
		MStyle *mstyle_default = mstyle_new_default ();

		/*
		 * We fill in the gaps with the default mstyle
		 */

		 if (!ft->number) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FORMAT);
		 }
		 if (!ft->border) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_TOP);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_BOTTOM);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_LEFT);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_RIGHT);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_DIAGONAL);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_BORDER_REV_DIAGONAL);
		 }
		 if (!ft->font) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_NAME);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_BOLD);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_ITALIC);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_UNDERLINE);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_STRIKETHROUGH);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_FONT_SIZE);

			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_COLOR_FORE);
		 }
		 if (!ft->patterns) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_COLOR_BACK);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_COLOR_PATTERN);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_PATTERN);
		 }
		 if (!ft->alignment) {
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_ALIGN_V);
			 mstyle_replace_element (mstyle_default, mstyle, MSTYLE_ALIGN_H);
		 }

		 mstyle_unref (mstyle_default);
	}

	return mstyle;
}

/*
 * Callback used for calculating the styles
 */
typedef void (* PCalcCallback) (FormatTemplate *ft, GnmRange *r, MStyle *mstyle, gpointer data);

/**
 * format_template_range_check:
 * @ft: Format template
 * @r: Target range
 * @optional_cc : if non-NULL display an error message if @r is not
 * 			appropriate for @ft.
 *
 * Check whether range @r is big enough to apply format template @ft to it.
 *
 * Return value: TRUE if @s is big enough, FALSE if not.
 **/
static gboolean
format_template_range_check (FormatTemplate *ft, GnmRange const *r,
			     GnmCmdContext *optional_cc)
{
	GSList *iterator;
	int diff_col_high = -1;
	int diff_row_high = -1;
	gboolean invalid_range_seen = FALSE;

	g_return_val_if_fail (ft != NULL, FALSE);

	iterator = ft->members;
	while (iterator) {
		TemplateMember *member = iterator->data;
		GnmRange range = format_template_member_get_rect (member, r);

		if (!range_valid (&range)) {
			int diff_col = (range.start.col - range.end.col);
			int diff_row = (range.start.row - range.end.row);

			if (diff_col > diff_col_high)
				diff_col_high = diff_col;

			if (diff_row > diff_row_high)
				diff_row_high = diff_row;

			invalid_range_seen = TRUE;
		}

		iterator = g_slist_next (iterator);
	}

	if (invalid_range_seen && optional_cc != NULL) {
		int diff_row_high_ft = diff_row_high + range_height (r);
		int diff_col_high_ft = diff_col_high + range_width (r);
		char *errmsg;

		if (diff_col_high > 0 && diff_row_high > 0)
			errmsg = g_strdup_printf (
				_("The target region is too small.  It should be at least %d rows by %d columns"),
				diff_row_high_ft, diff_col_high_ft);
		else if (diff_col_high > 0)
			errmsg = g_strdup_printf (
				_("The target region is too small.  It should be at least %d columns wide"),
				diff_col_high_ft);
		else if (diff_row_high > 0)
			errmsg = g_strdup_printf (
				_("The target region is too small.  It should be at least %d rows high"),
				diff_row_high_ft);
		else {
			errmsg = NULL;
			g_warning ("Internal error while verifying ranges! (this should not happen!)");
		}

		if (errmsg != NULL) {
			gnm_cmd_context_error_system (optional_cc, errmsg);
			g_free (errmsg);
		}
	}
	return !invalid_range_seen;
}

/**
 * format_template_transform_edges:
 * @ft: The template to transform
 *
 * Transforms a template by remove edge styles. This routine
 * will return a copy of @ft which should be freed by the
 * caller.
 *
 * Return value: A new tranformed format template
 **/
static FormatTemplate *
format_template_transform_edges (FormatTemplate const *origft)
{
	FormatTemplate *ft;
	GSList *ptr0, *ptr1;

	g_return_val_if_fail (origft != NULL, NULL);

	ft = format_template_clone (origft);
	for (ptr0 = ft->members; ptr0 != NULL ; ptr0 = ptr0->next) {
		TemplateMember *member = ptr0->data;

		gboolean left   = FALSE;
		gboolean right  = FALSE;
		gboolean top    = FALSE;
		gboolean bottom = FALSE;

		if (member->col.size == 1 &&
		    member->direction == FREQ_DIRECTION_NONE) {
			left   = (member->col.offset_gravity > 0
				  && !ft->edges.left);
			right  = (member->col.offset_gravity < 0
				  && !ft->edges.right);
		}
		if (member->row.size == 1 &&
		    member->direction == FREQ_DIRECTION_NONE) {
			top    = (member->row.offset_gravity > 0
				  && !ft->edges.top);
			bottom = (member->row.offset_gravity < 0
				  && !ft->edges.bottom);
		}

		if (left || right || top || bottom) {

			for (ptr1 = ft->members; ptr1 != NULL ; ptr1 = ptr1->next) {
				TemplateMember *submember = ptr1->data;

				if (left
				    && submember->col.offset_gravity == member->col.offset_gravity) {
					if (submember->direction != FREQ_DIRECTION_NONE) {
						submember->edge = 0;
						submember->col.offset--;
					} else if (submember->col.offset == 1)
						submember->col.offset = 0;
				}

				if (right
				    && submember->col.offset_gravity == -member->col.offset_gravity) {
					if (submember->col.size == -member->col.size)
						submember->col.size = 0;
					else if (submember->direction != FREQ_DIRECTION_NONE)
						submember->edge = 0;
				}

				if (top
				    && submember->row.offset_gravity == member->row.offset_gravity) {
					if (submember->direction != FREQ_DIRECTION_NONE) {
						submember->edge = 0;
						submember->row.offset--;
					} else if (submember->row.offset == 1)
						submember->row.offset = 0;
				}

				if (bottom
				    && submember->row.offset_gravity == -member->row.offset_gravity) {
					if (submember->row.size == -member->row.size)
						submember->row.size = 0;
					else if (submember->direction != FREQ_DIRECTION_NONE)
						submember->edge = 0;
				}
			}

			format_template_member_free (ptr0->data);
			ft->members = g_slist_remove (ft->members, ptr0->data);
		}
	}
	return ft;
}

/**
 * format_template_calculate:
 * @origft: FormatTemplate
 * @s: Target range
 * @pc: Callback function
 * @cb_data: Data to pass to the callback function
 *
 * Calculate all styles for a range of @s. This routine will invoke the callback function
 * and pass all styles and ranges for those styles to the callback function.
 * The callback function should UNREF the mstyle passed!
 *
 **/
static void
format_template_calculate (FormatTemplate *origft, GnmRange const *r, PCalcCallback pc, gpointer cb_data)
{
	FormatTemplate *ft = origft;
	GSList *iterator;

	g_return_if_fail (origft != NULL);

	if (!ft->edges.left || !ft->edges.right || !ft->edges.top || !ft->edges.bottom)
		ft = format_template_transform_edges (origft);

	/*
	 * Apply all styles
	 */
	iterator = ft->members;
	while (iterator) {
		TemplateMember *member = iterator->data;
		MStyle const *mstyle = member->mstyle;
		GnmRange range = format_template_member_get_rect (member, r);

		if (member->direction == FREQ_DIRECTION_NONE)
			pc (ft, &range, mstyle_copy (mstyle), cb_data);

		else if (member->direction == FREQ_DIRECTION_HORIZONTAL) {
			int col_repeat = member->repeat;
			GnmRange hr = range;

			while (col_repeat != 0) {
				pc (ft, &hr, mstyle_copy (mstyle), cb_data);

				hr.start.col += member->skip + member->col.size;
				hr.end.col   += member->skip + member->col.size;

				if (member->repeat != -1)
					col_repeat--;
				else {
					if (hr.start.row > r->end.row)
						break;
				}

				if (hr.start.row > r->end.row - member->edge)
					break;
			}
		} else if (member->direction == FREQ_DIRECTION_VERTICAL) {
			int row_repeat = member->repeat;
			GnmRange vr = range;

			while (row_repeat != 0) {
				pc (ft, &vr, mstyle_copy (mstyle), cb_data);

				vr.start.row += member->skip + member->row.size;
				vr.end.row   += member->skip + member->row.size;

				if (member->repeat != -1)
					row_repeat--;
				else {
					if (vr.start.row > r->end.row)
						break;
				}

				if (vr.start.row > r->end.row - member->edge)
					break;
			}
		}

		iterator = g_slist_next (iterator);
	}

	if (ft != origft)
		format_template_free (ft);
}

/******************************************************************************
 * FormatTemplate - Application for the hashtable (previews)
 ******************************************************************************/

static void
cb_format_hash_style (FormatTemplate *ft, GnmRange *r, MStyle *mstyle, GHashTable *table)
{
	int row, col;

	/*
	 * Filter out undesired elements
	 */
	mstyle = format_template_filter_style (ft, mstyle, TRUE);

	for (row = r->start.row; row <= r->end.row; row++) {
		for (col = r->start.col; col <= r->end.col; col++) {
			g_hash_table_insert (table, ROW_COL_KEY (row, col),
					     mstyle_copy (mstyle));
		}
	}

	/*
	 * Unref here, the hashtable will take care of its own
	 * resources
	 */
	mstyle_unref (mstyle);
}

/**
 * format_template_recalc_hash:
 * @ft: FormatTemplate
 *
 * Refills the hashtable based on new dimensions
 **/
static void
format_template_recalc_hash (FormatTemplate *ft)
{
	GnmRange r;

	g_return_if_fail (ft != NULL);

	g_hash_table_foreach_remove (ft->table, (GHRFunc)g_direct_hash /* :-) */, NULL);

	r = ft->dimension;

	/* If the range check fails then the template it simply too *huge*
	 * so we don't display an error dialog.
	 */
	if (!format_template_range_check (ft, &r, NULL)) {
		g_warning ("Template %s is too large, hash can't be calculated", ft->name);
		return;
	}

	format_template_calculate (ft, &r, (PCalcCallback) cb_format_hash_style, ft->table);
}

/**
 * format_template_get_style:
 * @ft:
 * @row:
 * @col:
 *
 * Returns the MStyle associated with coordinates row, col.
 * This routine uses the hash to do this.
 * NOTE : You MAY NOT free the result of this operation,
 *        you may also NOT MODIFY the MStyle returned.
 *        (make a copy first)
 *
 * Return value: an MStyle
 **/
MStyle *
format_template_get_style (FormatTemplate *ft, int row, int col)
{
	g_return_val_if_fail (ft != NULL, NULL);
	g_return_val_if_fail (ft->table != NULL, NULL);

	/*
	 * If the hash isn't filled (as result of resizing) or whatever,
	 * then refill it
	 */
	if (ft->invalidate_hash) {
		ft->invalidate_hash = FALSE;
		format_template_recalc_hash (ft);
	}

	return g_hash_table_lookup (ft->table, ROW_COL_KEY (row, col));
}



/******************************************************************************
 * FormatTemplate - Application to Sheet
 ******************************************************************************/

static void
cb_format_sheet_style (FormatTemplate *ft, GnmRange *r, MStyle *mstyle, Sheet *sheet)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (r != NULL);
	g_return_if_fail (mstyle != NULL);

	mstyle = format_template_filter_style (ft, mstyle, FALSE);

	/*
	 * We need not unref the mstyle, sheet will
	 * take care of the mstyle
	 */
	sheet_apply_style (sheet, r, mstyle);
}

/**
 * format_template_check_valid :
 * @ft :
 * @regions :
 * @cc : where to report errors
 *
 * check to see if the @regions are able to contain the support template @ft.
 * Returns TRUE if ok, else FALSE.  Will report an error to @cc if it is
 * supplied.
 */
gboolean
format_template_check_valid (FormatTemplate *ft, GSList *regions, GnmCmdContext *cc)
{
	g_return_val_if_fail (cc != NULL, FALSE);

	for (; regions != NULL ; regions = regions->next)
		if (!format_template_range_check (ft, regions->data, cc))
			return FALSE;

	return TRUE;
}

/**
 * format_template_apply_to_sheet_regions:
 * @ft: FormatTemplate
 * @sheet: the Target sheet
 * @regions: Region list
 *
 * Apply the template to all selected regions in @sheet.
 **/
void
format_template_apply_to_sheet_regions (FormatTemplate *ft, Sheet *sheet, GSList *regions)
{
	for (; regions != NULL ; regions = regions->next)
		format_template_calculate (ft, regions->data,
			(PCalcCallback) cb_format_sheet_style, sheet);
}

/******************************************************************************
 * setters for FormatTemplate
 */

void
format_template_set_name (FormatTemplate *ft, char const *name)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (name != NULL);

	g_free (ft->name);
	ft->name = g_strdup (name);
}

void
format_template_set_author (FormatTemplate *ft, char const *author)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (author != NULL);

	g_free (ft->author);
	ft->author = g_strdup (author);
}

void
format_template_set_description (FormatTemplate *ft, char const *description)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (description != NULL);

	g_free (ft->description);
	ft->description = g_strdup (description);
}

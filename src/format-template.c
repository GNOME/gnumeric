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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "format-template.h"

#include "mstyle.h"
#include "gutils.h"
#include "sheet.h"
#include "sheet-style.h"
#include "style-border.h"
#include "command-context.h"
#include "ranges.h"
#include "xml-io.h"
#include <goffice/app/io-context.h>
#include <string.h>
#include <goffice/utils/go-libxml-extras.h>
#include <goffice/utils/go-glib-extras.h>
#include <libxml/parser.h>

#define CC2XML(s) ((xmlChar const *)(s))
#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

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
	gnm_style_ref (member->mstyle);

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
		gnm_style_unref (member->mstyle);
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
format_template_member_get_rect (TemplateMember const *member, GnmRange const *r)
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
 * NOTE : GnmStyle are taken care of internally, there is no
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
 * GnmFormatTemplate - Creation/Destruction
 ******************************************************************************/

/**
 * format_template_new:
 *
 * Create a new 'empty' GnmFormatTemplate
 *
 * Return value: the new GnmFormatTemplate
 **/
GnmFormatTemplate *
format_template_new (void)
{
	GnmFormatTemplate *ft;

	ft = g_new0 (GnmFormatTemplate, 1);

	ft->filename    = g_strdup ("");
	ft->author      = g_strdup (go_get_real_name ());
	ft->name        = g_strdup (N_("Name"));
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

	ft->table     = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)gnm_style_unref);
	ft->invalidate_hash = TRUE;

	range_init (&ft->dimension, 0,0,0,0);

	return ft;
}

/**
 * format_template_free:
 **/
void
format_template_free (GnmFormatTemplate *ft)
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
 * @ft: GnmFormatTemplate
 *
 * Make a copy of @ft.
 *
 * Returns : a copy of @ft
 **/
GnmFormatTemplate *
format_template_clone (GnmFormatTemplate const *ft)
{
	GnmFormatTemplate *clone;
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
		if (attr_eq (child->name, "Placement")) {
			g_return_if_fail (!(found & 1));
			xml_node_get_int  (child, "offset", &info->offset);
			xml_node_get_int  (child, "offset_gravity", &info->offset_gravity);
			found |= 1;
		} else if (attr_eq (child->name, "Dimensions")) {
			g_return_if_fail (!(found & 2));
			xml_node_get_int (child, "size", &info->size);
			found |= 2;
		}
	}
	g_return_if_fail (found == 3);
}

static gboolean
xml_read_format_template_member (XmlParseContext *ctxt, GnmFormatTemplate *ft, xmlNodePtr tree)
{
	xmlNodePtr child;
	TemplateMember *member;
	int tmp, found = 0;

	g_return_val_if_fail (attr_eq (tree->name, "Member"), FALSE);
	member = format_template_member_new ();

	for (child = tree->xmlChildrenNode; child != NULL ; child = child->next) {
		if (xmlIsBlankNode (child) || child->name == NULL)
			continue;
		if (attr_eq (child->name, "Col"))
			xml_read_format_col_row_info (&member->col, child);
		else if (attr_eq (child->name, "Row"))
			xml_read_format_col_row_info (&member->row, child);
		else if (attr_eq (child->name, "Frequency")) {
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
		} else if (attr_eq (child->name, "Style")) {
			if (found & 2) { g_warning ("Multiple Styles"); }
			member->mstyle = xml_read_style (ctxt, child, FALSE);
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
xml_read_format_template_members (XmlParseContext *ctxt, GnmFormatTemplate *ft, xmlNodePtr tree)
{
	xmlNode *child;

	g_return_val_if_fail (attr_eq (tree->name, "FormatTemplate"), FALSE);

	child = e_xml_get_child_by_name_by_lang (tree, "Information");
	if (child) {
		xmlChar *author = xml_node_get_cstr (child, "author");
		xmlChar *name   = xml_node_get_cstr (child, "name");
		xmlChar *descr  = xml_node_get_cstr (child, "description");

		format_template_set_author (ft, _(CXML2C (author)));
		format_template_set_name (ft,  _(CXML2C (name)));
		format_template_set_description (ft,  _(CXML2C (descr)));

		xmlFree (author);
		xmlFree (name);
		xmlFree (descr);
	} else
		return FALSE;

	child = e_xml_get_child_by_name (tree, "Members");
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
 * Create a new GnmFormatTemplate and load a template file
 * into it.
 *
 * Return value: a new GnmFormatTemplate (or NULL on error)
 **/
GnmFormatTemplate *
format_template_new_from_file (char const *filename, GOCmdContext *cc)
{
	GnmFormatTemplate	*ft = NULL;
	xmlDoc		*doc;

	g_return_val_if_fail (filename != NULL, NULL);

	if (!g_file_test (filename, G_FILE_TEST_EXISTS))
		return NULL;

	doc = xmlParseFile (filename);
	if (doc == NULL) {
		go_cmd_context_error_import (cc,
			_("Error while trying to load autoformat template"));
		return NULL;
	}
	if (doc->xmlRootNode != NULL) {
		xmlNs *ns = xmlSearchNsByHref (doc, doc->xmlRootNode,
			CC2XML ("http://www.gnome.org/gnumeric/format-template/v1"));
		if (ns != NULL && attr_eq (doc->xmlRootNode->name, "FormatTemplate")) {
			XmlParseContext *ctxt = xml_parse_ctx_new (doc, ns, NULL);

			ft = format_template_new ();
			if (xml_read_format_template_members (ctxt, ft, doc->xmlRootNode)) {
				g_free (ft->filename);
				ft->filename = g_strdup (filename);
			} else {
				format_template_free (ft);
				ft = NULL;
				go_cmd_context_error_import (cc,
					_("Error while trying to build tree from autoformat template file"));
			}

			xml_parse_ctx_destroy (ctxt);
		} else
			go_cmd_context_error_import (cc,
				_("Is not an autoformat template file"));
	} else
		go_cmd_context_error_import (cc,
			_("Invalid xml file. Tree is empty?"));

	xmlFreeDoc (doc);
	return ft;
}

#if 0
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
xml_write_format_template_members (XmlParseContext *ctxt, GnmFormatTemplate const *ft)
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
#endif

/**
 * format_template_attach_member:
 * @ft: GnmFormatTemplate
 * @member: the new member to attach
 *
 * Attaches @member to template @ft
 **/
void
format_template_attach_member (GnmFormatTemplate *ft, TemplateMember *member)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (member != NULL);

	/*
	 * NOTE : Append is slower, but that's not really an issue
	 *        here, because a GnmFormatTemplate will most likely
	 *        not have 'that many' members anyway
	 */
	ft->members = g_slist_append (ft->members, member);
}

/**
 * format_template_detach_member:
 * @ft: GnmFormatTemplate
 * @member: a TemplateMember
 *
 * Detaches @member from template @ft
 **/
void
format_template_detach_member (GnmFormatTemplate *ft, TemplateMember *member)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (member != NULL);

	ft->members = g_slist_remove (ft->members, member);
}

/**
 * format_template_compare_name:
 * @ft_a: First GnmFormatTemplate
 * @ft_b: Second GnmFormatTemplate
 *
 **/
gint
format_template_compare_name (gconstpointer a, gconstpointer b)
{
	GnmFormatTemplate const *ft_a = (GnmFormatTemplate const *) a;
	GnmFormatTemplate const *ft_b = (GnmFormatTemplate const *) b;

	return g_utf8_collate (_(ft_a->name), _(ft_b->name));
}

/******************************************************************************
 * GnmFormatTemplate - Actual implementation (Filtering and calculating)
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
static GnmStyle *
format_template_filter_style (GnmFormatTemplate *ft, GnmStyle *mstyle, gboolean fill_defaults)
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
			gnm_style_unset_element (mstyle, MSTYLE_FORMAT);
		}
		if (!ft->border) {
			gnm_style_unset_element (mstyle, MSTYLE_BORDER_TOP);
			gnm_style_unset_element (mstyle, MSTYLE_BORDER_BOTTOM);
			gnm_style_unset_element (mstyle, MSTYLE_BORDER_LEFT);
			gnm_style_unset_element (mstyle, MSTYLE_BORDER_RIGHT);
			gnm_style_unset_element (mstyle, MSTYLE_BORDER_DIAGONAL);
			gnm_style_unset_element (mstyle, MSTYLE_BORDER_REV_DIAGONAL);
		}
		if (!ft->font) {
			gnm_style_unset_element (mstyle, MSTYLE_FONT_NAME);
			gnm_style_unset_element (mstyle, MSTYLE_FONT_BOLD);
			gnm_style_unset_element (mstyle, MSTYLE_FONT_ITALIC);
			gnm_style_unset_element (mstyle, MSTYLE_FONT_UNDERLINE);
			gnm_style_unset_element (mstyle, MSTYLE_FONT_STRIKETHROUGH);
			gnm_style_unset_element (mstyle, MSTYLE_FONT_SIZE);

			gnm_style_unset_element (mstyle, MSTYLE_FONT_COLOR);
		}
		if (!ft->patterns) {
			gnm_style_unset_element (mstyle, MSTYLE_COLOR_BACK);
			gnm_style_unset_element (mstyle, MSTYLE_COLOR_PATTERN);
			gnm_style_unset_element (mstyle, MSTYLE_PATTERN);
		}
		if (!ft->alignment) {
			gnm_style_unset_element (mstyle, MSTYLE_ALIGN_V);
			gnm_style_unset_element (mstyle, MSTYLE_ALIGN_H);
		}
	} else {
		GnmStyle *gnm_style_default = gnm_style_new_default ();

		/*
		 * We fill in the gaps with the default mstyle
		 */

		 if (!ft->number) {
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_FORMAT);
		 }
		 if (!ft->border) {
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_BORDER_TOP);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_BORDER_BOTTOM);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_BORDER_LEFT);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_BORDER_RIGHT);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_BORDER_DIAGONAL);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_BORDER_REV_DIAGONAL);
		 }
		 if (!ft->font) {
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_FONT_NAME);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_FONT_BOLD);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_FONT_ITALIC);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_FONT_UNDERLINE);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_FONT_STRIKETHROUGH);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_FONT_SIZE);

			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_FONT_COLOR);
		 }
		 if (!ft->patterns) {
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_COLOR_BACK);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_COLOR_PATTERN);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_PATTERN);
		 }
		 if (!ft->alignment) {
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_ALIGN_V);
			 gnm_style_merge_element (mstyle, gnm_style_default, MSTYLE_ALIGN_H);
		 }

		 gnm_style_unref (gnm_style_default);
	}

	return mstyle;
}

/*
 * Callback used for calculating the styles
 */
typedef void (* PCalcCallback) (GnmFormatTemplate *ft, GnmRange *r, GnmStyle *mstyle, gpointer data);

/**
 * format_template_range_check:
 * @ft: Format template
 * @r: Target range
 * @optional_cc : if non-NULL display an error message if @r is not
 *			appropriate for @ft.
 *
 * Check whether range @r is big enough to apply format template @ft to it.
 *
 * Return value: TRUE if @s is big enough, FALSE if not.
 **/
static gboolean
format_template_range_check (GnmFormatTemplate *ft, GnmRange const *r,
			     GOCmdContext *optional_cc)
{
	GSList *ptr;
	int diff_col_high = -1;
	int diff_row_high = -1;
	gboolean invalid_range_seen = FALSE;

	g_return_val_if_fail (ft != NULL, FALSE);

	for (ptr = ft->members; NULL != ptr ; ptr = ptr->next) {
		TemplateMember *member = ptr->data;
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
			go_cmd_context_error_system (optional_cc, errmsg);
			g_free (errmsg);
		}
	}
	return !invalid_range_seen;
}

/* Remove edge styles from a template and shift items that anchor on a filtered
 * edge.  Returns a filtered copy of @origft. */
static GnmFormatTemplate *
gnm_auto_fmt_filter_edges (GnmFormatTemplate const *origft)
{
	GSList *ptr;
	GnmFormatTemplate *ft = format_template_clone (origft);
	TemplateMember *member;
	gboolean is_edge, l = FALSE, r = FALSE, t = FALSE, b = FALSE;

	for (ptr = ft->members; ptr != NULL ; ) {
		member = ptr->data;
		ptr = ptr->next;
		if (!member->direction == FREQ_DIRECTION_NONE)
			continue;

		is_edge = FALSE;
		if (member->col.size == 1) {
			if (!ft->edges.left && member->col.offset_gravity > 0)
				l |= (is_edge = TRUE);
			if (!ft->edges.right && member->col.offset_gravity < 0)
				r |= (is_edge = TRUE);
		}
		if (member->row.size == 1) {
			if (!ft->edges.top && member->row.offset_gravity > 0)
				t |= (is_edge = TRUE);
			if (!ft->edges.bottom && member->row.offset_gravity < 0)
				b |= (is_edge = TRUE);
		}
		if (is_edge) {
			format_template_member_free (member);
			ft->members = g_slist_remove (ft->members, member);
		}
	}

	if (!l && !r && !t && !b)
		return ft;
	for (ptr = ft->members; ptr != NULL ; ptr = ptr->next) {
		TemplateMember *submember = ptr->data;

		if (l && submember->col.offset_gravity > 0) {
			if (submember->col.offset >= 1)
				submember->col.offset--;
			submember->edge = 0;
		}

		if (r && submember->col.offset_gravity < 0) {
			if (submember->col.offset >= 1)
				submember->col.offset--;
			submember->edge = 0;
		}

		if (t && submember->row.offset_gravity > 0) {
			if (submember->row.offset >= 1)
				submember->row.offset--;
			submember->edge = 0;
		}

		if (b && submember->row.offset_gravity < 0) {
			if (submember->row.offset >= 1)
				submember->row.offset--;
			submember->edge = 0;
		}
	}
	return ft;
}

/**
 * format_template_calculate:
 * @origft: GnmFormatTemplate
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
format_template_calculate (GnmFormatTemplate *origft, GnmRange const *r, PCalcCallback pc, gpointer cb_data)
{
	GnmFormatTemplate *ft = origft;
	GSList *ptr;

	g_return_if_fail (origft != NULL);

	if (!ft->edges.left || !ft->edges.right || !ft->edges.top || !ft->edges.bottom)
		ft = gnm_auto_fmt_filter_edges (origft);

	for (ptr = ft->members; NULL != ptr ; ptr = ptr->next) {
		TemplateMember const *member = ptr->data;
		GnmStyle const *mstyle = member->mstyle;
		GnmRange range = format_template_member_get_rect (member, r);

		g_return_if_fail (range_valid (&range));

		if (member->direction == FREQ_DIRECTION_NONE)
			pc (ft, &range, gnm_style_dup (mstyle), cb_data);

		else if (member->direction == FREQ_DIRECTION_HORIZONTAL) {
			int col_repeat = member->repeat;
			GnmRange hr = range;

			while (col_repeat != 0) {
				pc (ft, &hr, gnm_style_dup (mstyle), cb_data);

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
				pc (ft, &vr, gnm_style_dup (mstyle), cb_data);

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
	}

	if (ft != origft)
		format_template_free (ft);
}

/******************************************************************************
 * GnmFormatTemplate - Application for the hashtable (previews)
 ******************************************************************************/

static void
cb_format_hash_style (GnmFormatTemplate *ft, GnmRange *r, GnmStyle *mstyle, GHashTable *table)
{
	int row, col;

	/*
	 * Filter out undesired elements
	 */
	mstyle = format_template_filter_style (ft, mstyle, TRUE);

	for (row = r->start.row; row <= r->end.row; row++)
		for (col = r->start.col; col <= r->end.col; col++)
			g_hash_table_insert (table, ROW_COL_KEY (row, col),
				gnm_style_dup (mstyle));

	/*
	 * Unref here, the hashtable will take care of its own
	 * resources
	 */
	gnm_style_unref (mstyle);
}

/**
 * format_template_recalc_hash:
 * @ft: GnmFormatTemplate
 *
 * Refills the hashtable based on new dimensions
 **/
static void
format_template_recalc_hash (GnmFormatTemplate *ft)
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
 * Returns the GnmStyle associated with coordinates row, col.
 * This routine uses the hash to do this.
 * NOTE : You MAY NOT free the result of this operation,
 *        you may also NOT MODIFY the GnmStyle returned.
 *        (make a copy first)
 *
 * Return value: an GnmStyle
 **/
GnmStyle *
format_template_get_style (GnmFormatTemplate *ft, int row, int col)
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
 * GnmFormatTemplate - Application to Sheet
 ******************************************************************************/

static void
cb_format_sheet_style (GnmFormatTemplate *ft, GnmRange *r, GnmStyle *mstyle, Sheet *sheet)
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
format_template_check_valid (GnmFormatTemplate *ft, GSList *regions, GOCmdContext *cc)
{
	g_return_val_if_fail (cc != NULL, FALSE);

	for (; regions != NULL ; regions = regions->next)
		if (!format_template_range_check (ft, regions->data, cc))
			return FALSE;

	return TRUE;
}

/**
 * format_template_apply_to_sheet_regions:
 * @ft: GnmFormatTemplate
 * @sheet: the Target sheet
 * @regions: Region list
 *
 * Apply the template to all selected regions in @sheet.
 **/
void
format_template_apply_to_sheet_regions (GnmFormatTemplate *ft, Sheet *sheet, GSList *regions)
{
	for (; regions != NULL ; regions = regions->next)
		format_template_calculate (ft, regions->data,
			(PCalcCallback) cb_format_sheet_style, sheet);
}

/******************************************************************************
 * setters for GnmFormatTemplate
 */

void
format_template_set_name (GnmFormatTemplate *ft, char const *name)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (name != NULL);

	g_free (ft->name);
	ft->name = g_strdup (name);
}

void
format_template_set_author (GnmFormatTemplate *ft, char const *author)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (author != NULL);

	g_free (ft->author);
	ft->author = g_strdup (author);
}

void
format_template_set_description (GnmFormatTemplate *ft, char const *description)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (description != NULL);

	g_free (ft->description);
	ft->description = g_strdup (description);
}

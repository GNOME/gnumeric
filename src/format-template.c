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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <format-template.h>

#include <mstyle.h>
#include <gutils.h>
#include <sheet.h>
#include <command-context.h>
#include <ranges.h>
#include <xml-sax.h>
#include <goffice/goffice.h>
#include <string.h>
#include <gsf/gsf-input-stdio.h>

#define CC2XML(s) ((xmlChar const *)(s))
#define CXML2C(s) ((char const *)(s))

static inline gboolean
attr_eq (const xmlChar *a, const char *s)
{
	return !strcmp (CXML2C (a), s);
}

/******************************************************************************
 * FormatTemplateMember - Getters/setters and creation
 ******************************************************************************/

/**
 * gnm_ft_member_new:
 *
 * Create a new GnmFTMember
 *
 * Return value: the new GnmFTMember
 **/
static GnmFTMember *
gnm_ft_member_new (void)
{
	GnmFTMember *member;

	member = g_new (GnmFTMember, 1);

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
 * gnm_ft_member_clone:
 *
 * Clone a template member
 *
 * Return value: a copy of @member
 **/
static GnmFTMember *
gnm_ft_member_clone (GnmFTMember *member)
{
	GnmFTMember *clone = gnm_ft_member_new ();

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
 * gnm_ft_member_free:
 * @member: GnmFTMember
 *
 * Frees an existing template member
 **/
static void
gnm_ft_member_free (GnmFTMember *member)
{
	g_return_if_fail (member != NULL);

	if (member->mstyle) {
		gnm_style_unref (member->mstyle);
		member->mstyle = NULL;
	}

	g_free (member);
}


/**
 * gnm_ft_member_get_rect:
 * @member:
 * @r:
 *
 * Get the rectangular area covered by the GnmFTMember @member in the parent
 * rectangle @r.
 * NOTE : This simply calculates the rectangle, it does not calculate repetitions
 *        or anything. That you'll have to do yourself :-)
 *
 * Return value: a GnmRange containing the effective rectangle of @member
 **/
static GnmRange
gnm_ft_member_get_rect (GnmFTMember const *member, GnmRange const *r)
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

/****************************************************************************/

static gboolean
gnm_ft_member_valid (GnmFTMember const *member)
{
	return (member &&
		member->mstyle &&
		member->direction >= FREQ_DIRECTION_NONE &&
		member->direction <= FREQ_DIRECTION_VERTICAL &&
		member->repeat >= -1 &&
		member->skip >= 0 &&
		member->edge >= 0);
}

/******************************************************************************
 * GnmFT - Creation/Destruction
 ******************************************************************************/

/**
 * gnm_ft_new:
 *
 * Create a new 'empty' GnmFT
 *
 * Return value: the new GnmFT
 **/
static GnmFT *
gnm_ft_new (void)
{
	GnmFT *ft;

	ft = g_new0 (GnmFT, 1);

	ft->filename    = NULL;
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

	ft->table = g_hash_table_new_full ((GHashFunc)gnm_cellpos_hash,
					   (GEqualFunc)gnm_cellpos_equal,
					   (GDestroyNotify)g_free,
					   (GDestroyNotify)gnm_style_unref);
	ft->invalidate_hash = TRUE;

	range_init (&ft->dimension, 0,0,0,0);

	return ft;
}

/**
 * gnm_ft_free:
 **/
void
gnm_ft_free (GnmFT *ft)
{
	g_return_if_fail (ft != NULL);

	g_free (ft->filename);
	g_free (ft->author);
	g_free (ft->name);
	g_free (ft->description);
	g_slist_free_full (ft->members, (GDestroyNotify)gnm_ft_member_free);
	g_hash_table_destroy (ft->table);

	g_free (ft);
}


static void
gnm_ft_set_name (GnmFT *ft, char const *name)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (name != NULL);

	g_free (ft->name);
	ft->name = g_strdup (name);
}

static void
gnm_ft_set_author (GnmFT *ft, char const *author)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (author != NULL);

	g_free (ft->author);
	ft->author = g_strdup (author);
}

static void
gnm_ft_set_description (GnmFT *ft, char const *description)
{
	g_return_if_fail (ft != NULL);
	g_return_if_fail (description != NULL);

	g_free (ft->description);
	ft->description = g_strdup (description);
}

/**
 * gnm_ft_clone:
 * @ft: GnmFT
 *
 * Make a copy of @ft.
 *
 * Returns: transfer full): a copy of @ft
 **/
GnmFT *
gnm_ft_clone (GnmFT const *ft)
{
	GnmFT *clone;

	g_return_val_if_fail (ft != NULL, NULL);

	clone = gnm_ft_new ();
	gnm_ft_set_author (clone, ft->author);
	gnm_ft_set_name (clone, ft->name);
	gnm_ft_set_description (clone, ft->description);
	g_free (clone->filename); clone->filename = g_strdup (ft->filename);

	clone->category    = ft->category;

	clone->members =
		g_slist_copy_deep (ft->members,
				   (GCopyFunc)gnm_ft_member_clone, NULL);

	clone->number    = ft->number;
	clone->border    = ft->border;
	clone->font      = ft->font;
	clone->patterns  = ft->patterns;
	clone->alignment = ft->alignment;
	clone->edges     = ft->edges;
	clone->dimension = ft->dimension;

	clone->invalidate_hash = TRUE;

	return clone;
}

GType
gnm_ft_get_type (void)
{
	static GType t = 0;

	if (t == 0) {
		t = g_boxed_type_register_static ("GnmFT",
			 (GBoxedCopyFunc)gnm_ft_clone,
			 (GBoxedFreeFunc)gnm_ft_free);
	}
	return t;
}

#define GNM 100
#define GMR 200

static GsfXMLInNS const template_ns[] = {
	GSF_XML_IN_NS (GMR, "http://www.gnome.org/gnumeric/format-template/v1"),
	GSF_XML_IN_NS (GNM, "http://www.gnumeric.org/v10.dtd"),
	GSF_XML_IN_NS_END
};

static void
sax_information (GsfXMLIn *xin, xmlChar const **attrs)
{
	GnmFT *ft = (GnmFT *)xin->user_state;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (attr_eq (attrs[0], "author"))
			gnm_ft_set_author (ft, CXML2C (attrs[1]));
		else if (attr_eq (attrs[0], "name"))
			gnm_ft_set_name (ft, CXML2C (attrs[1]));
		else if (attr_eq (attrs[0], "description"))
			gnm_ft_set_description (ft, CXML2C (attrs[1]));
	}
}

static void
sax_members_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	ft->members = g_slist_reverse (ft->members);
}

static void
sax_member (GsfXMLIn *xin, xmlChar const **attrs)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	GnmFTMember *member = gnm_ft_member_new ();

	/* Order reversed in sax_members_end.  */
	ft->members = g_slist_prepend (ft->members, member);
}

static void
sax_member_end (GsfXMLIn *xin, G_GNUC_UNUSED GsfXMLBlob *blob)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	GnmFTMember *member = ft->members->data;

	if (!gnm_ft_member_valid (member)) {
		g_warning ("Invalid template member in %s\n", ft->filename);
		ft->members = g_slist_remove (ft->members, member);
		gnm_ft_member_free (member);
	}
}

static void
sax_placement (GnmFTColRowInfo *info, xmlChar const **attrs)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "offset", &info->offset) ||
		    gnm_xml_attr_int (attrs, "offset_gravity", &info->offset_gravity))
			; /* Nothing */
	}
}

static void
sax_row_placement (GsfXMLIn *xin, xmlChar const **attrs)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	GnmFTMember *member = ft->members->data;
	sax_placement (&member->row, attrs);
}

static void
sax_col_placement (GsfXMLIn *xin, xmlChar const **attrs)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	GnmFTMember *member = ft->members->data;
	sax_placement (&member->col, attrs);
}

static void
sax_dimensions (GnmFTColRowInfo *info, xmlChar const **attrs)
{
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		if (gnm_xml_attr_int (attrs, "size", &info->size))
			; /* Nothing */
	}
}

static void
sax_row_dimensions (GsfXMLIn *xin, xmlChar const **attrs)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	GnmFTMember *member = ft->members->data;
	sax_dimensions (&member->row, attrs);
}

static void
sax_col_dimensions (GsfXMLIn *xin, xmlChar const **attrs)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	GnmFTMember *member = ft->members->data;
	sax_dimensions (&member->col, attrs);
}

static void
sax_frequency (GsfXMLIn *xin, xmlChar const **attrs)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	GnmFTMember *member = ft->members->data;

	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2) {
		int i;

		if (gnm_xml_attr_int (attrs, "direction", &i))
			member->direction = i;
		else if (gnm_xml_attr_int (attrs, "repeat", &member->repeat) ||
			 gnm_xml_attr_int (attrs, "skip", &member->skip) ||
			 gnm_xml_attr_int (attrs, "edge", &member->edge))
			; /* Nothing */
	}
}

static void
sax_style_handler (GsfXMLIn *xin, GnmStyle *style, gpointer user)
{
	GnmFT *ft = (GnmFT *)xin->user_state;
	GnmFTMember *member = ft->members->data;
	gnm_style_ref (style);
	member->mstyle = style;
}

static gboolean
template_sax_unknown (GsfXMLIn *xin, xmlChar const *elem, xmlChar const **attrs)
{
	g_return_val_if_fail (xin != NULL, FALSE);
	g_return_val_if_fail (xin->doc != NULL, FALSE);
	g_return_val_if_fail (xin->node != NULL, FALSE);

	if (GMR == xin->node->ns_id &&
	    0 == strcmp (xin->node->id, "MEMBERS_MEMBER")) {
		char const *type_name = gsf_xml_in_check_ns (xin, CXML2C (elem), GNM);
		if (type_name && strcmp (type_name, "Style") == 0) {
			gnm_xml_prep_style_parser (xin, attrs,
						   sax_style_handler,
						   NULL);
			return TRUE;
		}
	}
	return FALSE;
}

static GsfXMLInNode template_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, GSF_XML_NO_CONTENT, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE (START, TEMPLATE, GMR, "FormatTemplate", GSF_XML_NO_CONTENT, NULL, NULL),
  GSF_XML_IN_NODE (TEMPLATE, TEMPLATE_INFORMATION, GMR, "Information", GSF_XML_NO_CONTENT, sax_information, NULL),
  GSF_XML_IN_NODE (TEMPLATE, TEMPLATE_MEMBERS, GMR, "Members", GSF_XML_NO_CONTENT, NULL, sax_members_end),
    GSF_XML_IN_NODE (TEMPLATE_MEMBERS, MEMBERS_MEMBER, GMR, "Member", GSF_XML_NO_CONTENT, sax_member, sax_member_end),
      GSF_XML_IN_NODE (MEMBERS_MEMBER, MEMBER_ROW, GMR, "Row", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MEMBER_ROW, ROW_PLACEMENT, GMR, "Placement", GSF_XML_NO_CONTENT, sax_row_placement, NULL),
        GSF_XML_IN_NODE (MEMBER_ROW, ROW_DIMENSIONS, GMR, "Dimensions", GSF_XML_NO_CONTENT, sax_row_dimensions, NULL),
      GSF_XML_IN_NODE (MEMBERS_MEMBER, MEMBER_COL, GMR, "Col", GSF_XML_NO_CONTENT, NULL, NULL),
        GSF_XML_IN_NODE (MEMBER_COL, COL_PLACEMENT, GMR, "Placement", GSF_XML_NO_CONTENT, sax_col_placement, NULL),
        GSF_XML_IN_NODE (MEMBER_COL, COL_DIMENSIONS, GMR, "Dimensions", GSF_XML_NO_CONTENT, sax_col_dimensions, NULL),
      GSF_XML_IN_NODE (MEMBERS_MEMBER, MEMBER_FREQUENCY, GMR, "Frequency", GSF_XML_NO_CONTENT, sax_frequency, NULL),
  GSF_XML_IN_NODE_END
};

/**
 * gnm_ft_new_from_file:
 * @filename: The filename to load from
 * @context:
 *
 * Create a new GnmFT and load a template file
 * into it.
 *
 * Return value: (transfer full): a new GnmFT (or %NULL on error)
 **/
GnmFT *
gnm_ft_new_from_file (char const *filename, GOCmdContext *cc)
{
	GnmFT *ft = NULL;
	GsfXMLInDoc *doc = NULL;
	GnmLocale *locale;
	gboolean ok = FALSE;
	GsfInput *input = NULL;

	g_return_val_if_fail (filename != NULL, NULL);

	input = gsf_input_stdio_new (filename, NULL);
	if (!input) {
		go_cmd_context_error_import
			(cc,
			 _("Error while opening autoformat template"));
		goto done;
	}

	doc = gsf_xml_in_doc_new (template_dtd, template_ns);
	if (doc == NULL)
		goto done;
	gsf_xml_in_doc_set_unknown_handler (doc, &template_sax_unknown);

	ft = gnm_ft_new ();
	ft->filename = g_strdup (filename);

	locale = gnm_push_C_locale ();
	ok = gsf_xml_in_doc_parse (doc, input, ft);
	gnm_pop_C_locale (locale);

 done:
	if (input) g_object_unref (input);
	if (doc) gsf_xml_in_doc_free (doc);

	if (ft && !ok) {
		gnm_ft_free (ft);
		ft = NULL;
	}

	return ft;
}


/**
 * gnm_ft_compare_name:
 * @a: First GnmFT
 * @b: Second GnmFT
 *
 **/
gint
gnm_ft_compare_name (gconstpointer a, gconstpointer b)
{
	GnmFT const *ft_a = (GnmFT const *) a;
	GnmFT const *ft_b = (GnmFT const *) b;

	return g_utf8_collate (_(ft_a->name), _(ft_b->name));
}

/******************************************************************************
 * GnmFT - Actual implementation (Filtering and calculating)
 ******************************************************************************/

/**
 * format_template_filter_style:
 * @ft:
 * @mstyle:
 * @fill_defaults: If set fill in the gaps with the "default" mstyle.
 *
 * Filter an mstyle and strip and replace certain elements
 * based on what the user wants to apply.
 * Basically you should pass %FALSE as @fill_defaults, unless you want to have
 * a completely filled style to be returned. If you set @fill_default to TRUE
 * the returned mstyle might have some of its elements 'not set'
 *
 * Return value: The same mstyle as @mstyle with most likely some modifications
 **/
static GnmStyle *
format_template_filter_style (GnmFT *ft, GnmStyle *mstyle, gboolean fill_defaults)
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
typedef void (* PCalcCallback) (GnmFT *ft, GnmRange *r, GnmStyle *mstyle, gpointer data);

/**
 * format_template_range_check:
 * @ft: Format template
 * @r: Target range
 * @optional_cc: (nullable): if non-%NULL display an error message if @r is not
 *			appropriate for @ft.
 *
 * Check whether range @r is big enough to apply format template @ft to it.
 *
 * Returns: %TRUE if @s is big enough, %FALSE if not.
 **/
static gboolean
format_template_range_check (GnmFT *ft, GnmRange const *r,
			     GOCmdContext *optional_cc)
{
	GSList *ptr;
	int diff_col_high = -1;
	int diff_row_high = -1;
	gboolean invalid_range_seen = FALSE;

	g_return_val_if_fail (ft != NULL, FALSE);

	for (ptr = ft->members; NULL != ptr ; ptr = ptr->next) {
		GnmFTMember *member = ptr->data;
		GnmRange range = gnm_ft_member_get_rect (member, r);

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
		char *rows, *cols;

		if (diff_col_high > 0 && diff_row_high > 0) {
			rows = g_strdup_printf (ngettext ("%d row", "%d rows", diff_row_high_ft), diff_row_high_ft);
			cols = g_strdup_printf (ngettext ("%d col", "%d cols", diff_col_high_ft), diff_col_high_ft);
			errmsg = g_strdup_printf (
				_("The target region is too small.  It should be at least %s by %s"),
				rows, cols);
			g_free (rows);
			g_free (cols);
		} else if (diff_col_high > 0)
			errmsg = g_strdup_printf (
				ngettext ("The target region is too small.  It should be at least %d column wide",
					"The target region is too small.  It should be at least %d columns wide",
					diff_col_high_ft),
				diff_col_high_ft);
		else if (diff_row_high > 0)
			errmsg = g_strdup_printf (
				ngettext ("The target region is too small.  It should be at least %d row high",
					"The target region is too small.  It should be at least %d rows high",
					diff_row_high_ft),
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
static GnmFT *
gnm_auto_fmt_filter_edges (GnmFT const *origft)
{
	GSList *ptr;
	GnmFT *ft = gnm_ft_clone (origft);
	gboolean is_edge, l = FALSE, r = FALSE, t = FALSE, b = FALSE;

	for (ptr = ft->members; ptr != NULL ; ) {
		GnmFTMember *member = ptr->data;
		ptr = ptr->next;
		if (member->direction != FREQ_DIRECTION_NONE)
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
			gnm_ft_member_free (member);
			ft->members = g_slist_remove (ft->members, member);
		}
	}

	if (!l && !r && !t && !b)
		return ft;
	for (ptr = ft->members; ptr != NULL ; ptr = ptr->next) {
		GnmFTMember *submember = ptr->data;

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
 * gnm_ft_calculate:
 * @origft: GnmFT
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
gnm_ft_calculate (GnmFT *origft, GnmRange const *r,
			   PCalcCallback pc, gpointer cb_data)
{
	GnmFT *ft = origft;
	GSList *ptr;

	g_return_if_fail (origft != NULL);

	if (!ft->edges.left || !ft->edges.right || !ft->edges.top || !ft->edges.bottom)
		ft = gnm_auto_fmt_filter_edges (origft);

	for (ptr = ft->members; NULL != ptr ; ptr = ptr->next) {
		GnmFTMember const *member = ptr->data;
		GnmStyle const *mstyle = member->mstyle;
		GnmRange range = gnm_ft_member_get_rect (member, r);

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
		gnm_ft_free (ft);
}

/******************************************************************************
 * GnmFT - Application for the hashtable (previews)
 ******************************************************************************/

static void
cb_format_hash_style (GnmFT *ft, GnmRange *r, GnmStyle *mstyle, gpointer user)
{
	GHashTable *table = user;
	int row, col;

	/*
	 * Filter out undesired elements
	 */
	mstyle = format_template_filter_style (ft, mstyle, TRUE);

	for (row = r->start.row; row <= r->end.row; row++)
		for (col = r->start.col; col <= r->end.col; col++) {
			GnmCellPos key;
			key.col = col;
			key.row = row;
			g_hash_table_insert (table,
					     go_memdup (&key, sizeof (key)),
					     gnm_style_dup (mstyle));
		}

	/*
	 * Unref here, the hashtable will take care of its own
	 * resources
	 */
	gnm_style_unref (mstyle);
}

/**
 * format_template_recalc_hash:
 * @ft: GnmFT
 *
 * Refills the hashtable based on new dimensions
 **/
static void
format_template_recalc_hash (GnmFT *ft)
{
	GnmRange r;

	g_return_if_fail (ft != NULL);

	g_hash_table_remove_all (ft->table);

	r = ft->dimension;

	/* If the range check fails then the template it simply too *huge*
	 * so we don't display an error dialog.
	 */
	if (!format_template_range_check (ft, &r, NULL)) {
		g_warning ("Template %s is too large, hash can't be calculated", ft->name);
		return;
	}

	gnm_ft_calculate (ft, &r, cb_format_hash_style, ft->table);
}

/**
 * gnm_ft_get_style:
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
gnm_ft_get_style (GnmFT *ft, int row, int col)
{
	GnmCellPos key;

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

	key.col = col;
	key.row = row;
	return g_hash_table_lookup (ft->table, &key);
}



/******************************************************************************
 * GnmFT - Application to Sheet
 ******************************************************************************/

static void
cb_format_sheet_style (GnmFT *ft, GnmRange *r, GnmStyle *mstyle, gpointer user)
{
	Sheet *sheet = user;

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
 * gnm_ft_check_valid:
 * @ft:
 * @regions: (element-type GnmRange):
 * @cc: (nullable): where to report errors
 *
 * check to see if the @regions are able to contain the support template @ft.
 *
 * Returns: %TRUE if ok, else %FALSE.  Will report an error to @cc if it is
 * supplied.
 */
gboolean
gnm_ft_check_valid (GnmFT *ft, GSList *regions, GOCmdContext *cc)
{
	g_return_val_if_fail (cc != NULL, FALSE);

	for (; regions != NULL ; regions = regions->next)
		if (!format_template_range_check (ft, regions->data, cc))
			return FALSE;

	return TRUE;
}

/**
 * gnm_ft_apply_to_sheet_regions:
 * @ft: GnmFT
 * @sheet: the Target sheet
 * @regions: (element-type GnmRange): Region list
 *
 * Apply the template to all selected regions in @sheet.
 **/
void
gnm_ft_apply_to_sheet_regions (GnmFT *ft, Sheet *sheet, GSList *regions)
{
	for (; regions != NULL ; regions = regions->next)
		gnm_ft_calculate (ft, regions->data,
					   cb_format_sheet_style, sheet);
}

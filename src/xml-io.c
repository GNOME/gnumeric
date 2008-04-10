/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xml-io.c: Old slow DOM based gnumeric-1.0 format importer.
 *
 * Authors:
 *   Daniel Veillard <Daniel.Veillard@w3.org>
 *   Miguel de Icaza <miguel@gnu.org>
 *   Jody Goldberg <jody@gnome.org>
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "xml-io.h"
#include "xml-sax.h"
#include "xml-io-version.h"

#include "style-border.h"
#include "style-color.h"
#include "style-conditions.h"
#include "style.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-object-impl.h"
#include "sheet-object-cell-comment.h"
#include "gnm-so-line.h"
#include "gnm-so-filled.h"
#include "sheet-object-graph.h"
#include "str.h"
#include "solver.h"
#include "scenarios.h"
#include "print-info.h"
#include <goffice/app/file.h>
#include "expr.h"
#include "expr-impl.h"
#include "expr-name.h"
#include "cell.h"
#include "value.h"
#include "validation.h"
#include "sheet-merge.h"
#include "sheet-filter.h"
#include <goffice/app/io-context.h>
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook-priv.h" /* Workbook::names */
#include "selection.h"
#include "clipboard.h"
#include "gnm-format.h"
#include "ranges.h"
#include "str.h"
#include "hlink.h"
#include "input-msg.h"
#include "gutils.h"
#include "gnumeric-gconf.h"

#include <goffice/utils/go-libxml-extras.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/app/error-info.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-input-gzip.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-output-gzip.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-input-memory.h>

#include <errno.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

/* Precision to use when saving point measures. */
#define POINT_SIZE_PRECISION 4

/* FIXME - tune the values below */
#define XML_INPUT_BUFFER_SIZE      4096
#define N_ELEMENTS_BETWEEN_UPDATES 20

#define CC2XML(s) ((xmlChar const *)(s))
#define C2XML(s) ((xmlChar *)(s))
#define CXML2C(s) ((char const *)(s))
#define XML2C(s) ((char *)(s))

/* ------------------------------------------------------------------------- */

static GnmConventions *
gnm_xml_conventions_new (void)
{
	GnmConventions *conv = gnm_conventions_new ();

	conv->decimal_sep_dot		= TRUE;
	conv->output.range_ref		= gnm_1_0_rangeref_as_string;
	conv->input.range_ref		= rangeref_parse;
	conv->range_sep_colon		= TRUE;

	conv->arg_sep			= ',';
	conv->array_col_sep		= ',';
	conv->array_row_sep		= ';';
	conv->output.translated		= FALSE;

	return conv;
}


XmlParseContext *
xml_parse_ctx_new (xmlDocPtr     doc,
		   xmlNsPtr      ns,
		   WorkbookView *wb_view)
{
	XmlParseContext *ctxt = g_new0 (XmlParseContext, 1);

	/* HACK : For now we cheat.
	 * We should always be able to read versions from 1.0.x
	 * That means that older 1.0.x should read newer 1.0.x
	 * the current matching code precludes that.
	 * Old versions fail reading things from the future.
	 * Freeze the exported version at V9 for now.
	 */
	ctxt->version      = GNM_XML_V9;
	ctxt->doc          = doc;
	ctxt->ns           = ns;
	ctxt->expr_map     = g_hash_table_new (g_direct_hash, g_direct_equal);
	ctxt->shared_exprs = g_ptr_array_new ();
	ctxt->wb_view      = wb_view;
	ctxt->wb	   = (wb_view != NULL) ? wb_view_get_workbook (wb_view) : NULL;
	ctxt->convs        = gnm_xml_conventions_new ();

	return ctxt;
}

void
xml_parse_ctx_destroy (XmlParseContext *ctxt)
{
	g_return_if_fail (ctxt != NULL);

	g_hash_table_destroy (ctxt->expr_map);
	g_ptr_array_free (ctxt->shared_exprs, TRUE);
	gnm_conventions_free (ctxt->convs);

	g_free (ctxt);
}

/* ------------------------------------------------------------------------- */

void
gnm_xml_out_add_gocolor (GsfXMLOut *o, char const *id, GOColor c)
{
	GdkColor tmp;
	go_color_to_gdk (c, &tmp);
	gsf_xml_out_add_color (o, id, tmp.red, tmp.green, tmp.blue);
}
void
gnm_xml_out_add_color (GsfXMLOut *o, char const *id, GnmColor const *c)
{
	g_return_if_fail (c != NULL);
	gsf_xml_out_add_color (o, id,
		c->gdk_color.red, c->gdk_color.green, c->gdk_color.blue);
}

void
gnm_xml_out_add_cellpos (GsfXMLOut *o, char const *id, GnmCellPos const *p)
{
	g_return_if_fail (p != NULL);
	gsf_xml_out_add_cstr_unchecked (o, id, cellpos_as_string (p));
}

/*****************************************************************************/
/*
 * Reads a value which is stored using a format '%d:%s' where %d is the
 * GnmValueType and %s is the string containing the value.
 */
static GnmValue *
xml_node_get_value (xmlNodePtr node, char const *name)
{
	xmlChar   *str;
	GnmValue     *value;
	GnmValueType type;
	gchar     *vstr;

	str  = xml_node_get_cstr (node, name);
	if (!str) {
		/* This happens because the sax writer as-of 1.6.1 does
		   not write these fields.  */
		return value_new_error_NA (NULL);
	}
	type = (GnmValueType) atoi (str);

	vstr = g_strrstr (str, ":") + 1;
	if (!vstr) {
		g_warning ("File corruption [%s] [%s]", name, str);
		return value_new_error_NA (NULL);
	}

	value = value_new_from_string (type, vstr, NULL, FALSE);
	xmlFree (str);

	return value;
}

GnmColor *
xml_node_get_color (xmlNodePtr node, char const *name)
{
	GnmColor *res = NULL;
	xmlChar *color;
	int red, green, blue;

	color = xmlGetProp (node, CC2XML (name));
	if (color == NULL)
		return NULL;
	if (sscanf (CXML2C (color), "%X:%X:%X", &red, &green, &blue) == 3)
		res = style_color_new (red, green, blue);
	xmlFree (color);
	return res;
}

void
xml_node_set_color (xmlNodePtr node, char const *name, GnmColor const *val)
{
	char str[4 * sizeof (val->gdk_color)];
	sprintf (str, "%X:%X:%X",
		 val->gdk_color.red, val->gdk_color.green, val->gdk_color.blue);
	xml_node_set_cstr (node, name, str);
}

static gboolean
xml_node_get_cellpos (xmlNodePtr node, char const *name, GnmCellPos *val)
{
	xmlChar *buf;
	gboolean res;

	buf = xml_node_get_cstr (node, name);
	if (val == NULL)
		return FALSE;
	res = cellpos_parse (CXML2C (buf), val, TRUE) != NULL;
	xmlFree (buf);
	return res;
}

static void
xml_node_get_print_margin (xmlNodePtr node, double *points, GtkUnit *unit)
{
	gchar       *txt;

	g_return_if_fail (node != NULL);
	g_return_if_fail (points != NULL);
	g_return_if_fail (unit != NULL);

	xml_node_get_double (node, "Points", points);
	txt = (gchar *)xmlGetProp  (node, CC2XML ("PrefUnit"));
	if (txt) {
		*unit = unit_name_to_unit (txt);
		xmlFree (txt);
	}
}

static gboolean
xml_node_get_range (xmlNodePtr tree, GnmRange *r)
{
	gboolean res =
	    xml_node_get_int (tree, "startCol", &r->start.col) &&
	    xml_node_get_int (tree, "startRow", &r->start.row) &&
	    xml_node_get_int (tree, "endCol",   &r->end.col) &&
	    xml_node_get_int (tree, "endRow",   &r->end.row);

	/* Older versions of gnumeric had some boundary problems */
	range_ensure_sanity (r);

	return res;
}

static void
xml_read_selection_info (XmlParseContext *ctxt, xmlNodePtr tree)
{
	GnmRange r;
	GnmCellPos pos;
	xmlNodePtr sel, selections;
	SheetView *sv = sheet_get_view (ctxt->sheet, ctxt->wb_view);

	if (!sv) return;  /* Hidden.  */

	selections = e_xml_get_child_by_name (tree, CC2XML ("Selections"));
	if (selections == NULL)
		return;

	sv_selection_reset (sv);
	for (sel = selections->xmlChildrenNode; sel; sel = sel->next)
		if (!xmlIsBlankNode (sel) && xml_node_get_range (sel, &r))
			sv_selection_add_range (sv, &r);

	if (xml_node_get_int (selections, "CursorCol", &pos.col) &&
	    xml_node_get_int (selections, "CursorRow", &pos.row))
		sv_set_edit_pos (sv, &pos);
}

/*
 * Create an XML subtree of doc equivalent to the given GnmBorder.
 */
static char const *const StyleSideNames[6] =
{
	"Top",
	"Bottom",
	"Left",
	"Right",
	"Diagonal",
	"Rev-Diagonal"
};

static void
xml_read_style_border (XmlParseContext *ctxt, xmlNodePtr tree, GnmStyle *style)
{
	xmlNodePtr side;
	int        i;

	if (strcmp (tree->name, "StyleBorder")){
		g_printerr ("xml_read_style_border: invalid element type %s, "
			    "'StyleBorder' expected`\n", tree->name);
	}

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
		if ((side = e_xml_get_child_by_name (tree,
					      CC2XML (StyleSideNames [i - MSTYLE_BORDER_TOP]))) != NULL) {
			int		 t;
			GnmColor      *color = NULL;
			GnmBorder    *border;
			xml_node_get_int (side, "Style", &t);
			if (t != GNM_STYLE_BORDER_NONE)
				color = xml_node_get_color (side, "Color");
			border = gnm_style_border_fetch ((GnmStyleBorderType)t, color,
						     gnm_style_border_get_orientation (i));
			gnm_style_set_border (style, i, border);
		}
	}
}

static void
xml_read_names (XmlParseContext *ctxt, xmlNodePtr tree,
		Workbook *wb, Sheet *sheet)
{
	xmlNode *id;
	xmlNode *expr_node;
	xmlNode *position;
	xmlNode *name = e_xml_get_child_by_name (tree, CC2XML ("Names"));
	xmlChar *name_str;
	xmlChar *expr_str;

	if (name == NULL)
		return;

	for (name = name->xmlChildrenNode; name ; name = name->next) {
		GnmExprTop const *texpr;
		GnmParseError  perr;
		GnmParsePos    pp;

		if (xmlIsBlankNode (name) ||
		    name->name == NULL || strcmp (name->name, "Name"))
			continue;

		id = e_xml_get_child_by_name (name, CC2XML ("name"));
		expr_node = e_xml_get_child_by_name (name, CC2XML ("value"));
		position = e_xml_get_child_by_name (name, CC2XML ("position"));

		g_return_if_fail (id != NULL && expr_node != NULL);

		name_str = xml_node_get_cstr (id, NULL);
		expr_str = xml_node_get_cstr (expr_node, NULL);
		g_return_if_fail (name_str != NULL && expr_str != NULL);

		parse_pos_init (&pp, wb, sheet, 0, 0);
		if (position != NULL) {
			xmlChar *pos_txt = xml_node_get_cstr (position, NULL);
			if (pos_txt != NULL) {
				GnmCellRef tmp;
				char const *res = cellref_parse (&tmp, CXML2C (pos_txt), &pp.eval);
				if (res != NULL && *res == '\0') {
					pp.eval.col = tmp.col;
					pp.eval.row = tmp.row;
				}
				xmlFree (pos_txt);
			}
		}

		parse_error_init (&perr);
		texpr = gnm_expr_parse_str (CXML2C (expr_str), &pp,
					   GNM_EXPR_PARSE_DEFAULT,
					   ctxt->convs, &perr);
		/* See http://bugzilla.gnome.org/show_bug.cgi?id=317427 */
		if (!texpr)
			texpr = gnm_expr_parse_str (CXML2C (expr_str), &pp,
						   GNM_EXPR_PARSE_DEFAULT,
						   gnm_conventions_default,
						   NULL);

		if (texpr != NULL) {
			char *err = NULL;
			expr_name_add (&pp, CXML2C (name_str),
				       texpr,
				       &err, TRUE, NULL);
			if (err != NULL) {
				gnm_io_warning (ctxt->io_context, err);
				g_free (err);
			}
		} else
			gnm_io_warning (ctxt->io_context, perr.err->message);
		parse_error_free (&perr);

		xmlFree (name_str);
		xmlFree (expr_str);
	}
}

static void
dom_read_meta_data (XmlParseContext *ctxt, xmlNodePtr tree, GsfDocMetaData *meta_data)
{
	xmlNodePtr child;
	xmlChar *val_str, *type_str, *name;

	for (child = tree->xmlChildrenNode; child != NULL ; child = child->next) {
		if (xmlIsBlankNode (child) || NULL == child->name || strcmp (child->name, "Property") ||
		    NULL == (name	= xml_node_get_cstr (child, "name")) ||
		    NULL == (type_str	= xml_node_get_cstr (child, "type")) ||
		    NULL == (val_str	= xml_node_get_cstr (child, NULL)))
			continue;

		xmlFree (val_str);
		xmlFree (type_str);
		xmlFree (name);
	}
}

static void
xml_node_get_print_hf (xmlNodePtr node, PrintHF *hf)
{
	xmlChar *txt;

	g_return_if_fail (hf != NULL);
	g_return_if_fail (node != NULL);

	txt = xmlGetProp (node, CC2XML ("Left"));
	if (txt) {
		g_free (hf->left_format);
		hf->left_format = g_strdup ((gchar *)txt);
		xmlFree (txt);
	}

	txt = xmlGetProp (node, CC2XML ("Middle"));
	if (txt) {
		g_free (hf->middle_format);
		hf->middle_format = g_strdup ((gchar *)txt);
		xmlFree (txt);
	}

	txt = xmlGetProp (node, CC2XML ("Right"));
	if (txt) {
		g_free (hf->right_format);
		hf->right_format = g_strdup ((gchar *)txt);
		xmlFree (txt);
	}
}

static void
xml_read_wbv_attributes (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNode *attr, *tmp;
	xmlChar *name, *value;

	for (attr = tree->xmlChildrenNode; attr ; attr = attr->next) {
		if (xmlIsBlankNode (attr) ||
		    attr->name == NULL || strcmp (attr->name, "Attribute"))
			continue;

		tmp = e_xml_get_child_by_name (attr, CC2XML ("name"));
		if (tmp == NULL)
			continue;
		name = xml_node_get_cstr (tmp, NULL);
		if (name == NULL)
			continue;

		tmp = e_xml_get_child_by_name (attr, CC2XML ("value"));
		if (tmp == NULL) {
			xmlFree (name);
			continue;
		}
		value = xml_node_get_cstr (tmp, NULL);
		if (value == NULL) {
			xmlFree (name);
			continue;
		}

		wb_view_set_attribute (ctxt->wb_view, CXML2C (name), CXML2C (value));
		xmlFree (name);
		xmlFree (value);
	}
}

static void
xml_read_print_margins (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	PrintInformation *pi;
	double header = -1.;
	double footer = -1.;
	double left = -1.;
	double right = -1.;
	double edge_to_above_footer = -1.;
	double edge_to_below_header = -1.;

	g_return_if_fail (ctxt != NULL);
	g_return_if_fail (tree != NULL);
	g_return_if_fail (IS_SHEET (ctxt->sheet));

	pi = ctxt->sheet->print_info;

	g_return_if_fail (pi != NULL);

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("top"))))
		xml_node_get_print_margin (child, &edge_to_below_header,
					   &pi->desired_display.header);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("bottom"))))
		xml_node_get_print_margin (child, &edge_to_above_footer,
					   &pi->desired_display.footer);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("left"))))
		xml_node_get_print_margin (child, &left,
					   &pi->desired_display.left);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("right"))))
		xml_node_get_print_margin (child, &right,
					   &pi->desired_display.right);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("header"))))
		xml_node_get_print_margin (child, &header,
					   &pi->desired_display.top);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("footer"))))
		xml_node_get_print_margin (child, &footer,
					   &pi->desired_display.bottom);

	print_info_set_margins (pi, header, footer, left, right);
	if (!(edge_to_below_header < 0))
		print_info_set_edge_to_below_header (pi, edge_to_below_header);
	if (!(edge_to_above_footer < 0))
		print_info_set_edge_to_above_footer (pi, edge_to_above_footer);

}

static void
xml_read_print_repeat_range (XmlParseContext *ctxt, xmlNodePtr tree,
			     char const *name, PrintRepeatRange *range)
{
	xmlNodePtr child;

	g_return_if_fail (ctxt != NULL);
	g_return_if_fail (tree != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (range != NULL);

	range->use = FALSE;
	if (ctxt->version > GNM_XML_V4 &&
	    (child = e_xml_get_child_by_name (tree, CC2XML (name)))) {
		xmlChar *s = xml_node_get_cstr (child, "value");

		if (s) {
			GnmRange r;
			if (range_parse (&r, CXML2C (s))) {
				range->range = r;
				range->use   = TRUE;
			}
			xmlFree (s);
		}
	}
}

static void
xml_read_breaks (XmlParseContext *ctxt, xmlNodePtr tree, gboolean is_vert)
{
	GnmPageBreaks *page_breaks;
	int pos, count = 0;
	xmlChar *type_str;
	GnmPageBreakType  type;

	xml_node_get_int (tree, "count", &count);
	page_breaks = gnm_page_breaks_new (count, is_vert);

	for (tree = tree->xmlChildrenNode; tree != NULL ; tree = tree->next)
		if (!xmlIsBlankNode (tree) &&
		    0 == strcmp (tree->name, "break") &&
		    xml_node_get_int (tree, "pos", &pos) &&
		    pos >= 0) {
			if (NULL != (type_str = xml_node_get_cstr  (tree, "type")))
				type = gnm_page_break_type_from_str (CXML2C (type_str));
			else
				type = GNM_PAGE_BREAK_AUTO;

			/* drops invalid positions */
			gnm_page_breaks_append_break (page_breaks, pos, type);
		}

	print_info_set_breaks (ctxt->sheet->print_info, page_breaks);
}

static void
xml_read_print_info (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	PrintInformation *pi;
	int b;

	g_return_if_fail (ctxt != NULL);
	g_return_if_fail (tree != NULL);
	g_return_if_fail (IS_SHEET (ctxt->sheet));

	pi = ctxt->sheet->print_info;

	g_return_if_fail (pi != NULL);

	tree = e_xml_get_child_by_name (tree, CC2XML ("PrintInformation"));
	if (tree == NULL)
		return;

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("Margins")))) {
		xml_read_print_margins (ctxt, child);
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("Scale")))) {
		xmlChar *type = xml_node_get_cstr  (child, "type");
		if (type != NULL) {
			if (!strcmp (type, "percentage")) {
				double tmp;
				pi->scaling.type = PRINT_SCALE_PERCENTAGE;
				if (xml_node_get_double (child, "percentage", &tmp))
					pi->scaling.percentage.x =
						pi->scaling.percentage.y = tmp;
			} else {
				int cols, rows;
				pi->scaling.type = PRINT_SCALE_FIT_PAGES;
				if (xml_node_get_int (child, "cols", &cols) &&
				    xml_node_get_int (child, "rows", &rows)) {
					pi->scaling.dim.cols = cols;
					pi->scaling.dim.rows = rows;
				}
			}

			xmlFree (type);
		}
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("vcenter")))) {
		xml_node_get_int  (child, "value", &b);
		pi->center_vertically   = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("hcenter")))) {
		xml_node_get_int  (child, "value", &b);
		pi->center_horizontally = (b == 1);
	}

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("grid")))) {
		xml_node_get_int  (child, "value",    &b);
		pi->print_grid_lines  = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("even_if_only_styles")))) {
		xml_node_get_int  (child, "value",    &b);
		pi->print_even_if_only_styles  = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("monochrome")))) {
		xml_node_get_int  (child, "value", &b);
		pi->print_black_and_white = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("draft")))) {
		xml_node_get_int  (child, "value",   &b);
		pi->print_as_draft        = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("comments")))) {
		xml_node_get_int  (child, "value",   &b);
		pi->comment_placement = b;  /* this was once a bool */
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("titles")))) {
		xml_node_get_int  (child, "value",  &b);
		pi->print_titles          = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("do_not_print")))) {
		xml_node_get_int  (child, "value",    &b);
		pi->do_not_print  = (b == 1);
	}


	xml_read_print_repeat_range (ctxt, tree, "repeat_top",
				     &pi->repeat_top);
	xml_read_print_repeat_range (ctxt, tree, "repeat_left",
				     &pi->repeat_left);

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("order")))) {
		xmlChar *txt = xmlNodeGetContent (child);
		/* this used to be an enum */
		pi->print_across_then_down = (0 != strcmp (CXML2C (txt), "d_then_r"));
		xmlFree (txt);
	}

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("orientation")))) {
		xmlChar *txt = xmlNodeGetContent (child);
#warning TODO: we should also handle inversion
		print_info_set_paper_orientation (pi,
						  strcmp (CXML2C (txt), "portrait")
						  ? GTK_PAGE_ORIENTATION_LANDSCAPE
						  : GTK_PAGE_ORIENTATION_PORTRAIT);
		xmlFree (txt);
	}

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("Header"))))
		xml_node_get_print_hf (child, pi->header);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("Footer"))))
		xml_node_get_print_hf (child, pi->footer);

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("paper")))) {
		xmlChar *paper = xmlNodeGetContent (child);
		print_info_set_paper (pi, paper);
		xmlFree (paper);
	}

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("vPageBreaks"))))
		xml_read_breaks (ctxt, child, TRUE);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("hPageBreaks"))))
		xml_read_breaks (ctxt, child, FALSE);
}

static char const *
font_component (char const *fontname, int idx)
{
	int i = 0;
	char const *p = fontname;

	for (; *p && i < idx; p++){
		if (*p == '-')
			i++;
	}
	if (*p == '-')
		p++;

	return p;
}

/**
 * style_font_read_from_x11:
 * @style: the style to setup to this font.
 * @fontname: an X11-like font name.
 *
 * Tries to guess the fontname, the weight and italization parameters
 * and setup style
 *
 * Returns: A valid style font.
 */
static void
style_font_read_from_x11 (GnmStyle *style, char const *fontname)
{
	char const *c;

	c = font_component (fontname, 2);
	if (strncmp (c, "bold", 4) == 0)
		gnm_style_set_font_bold (style, TRUE);

	c = font_component (fontname, 3);
	if (strncmp (c, "o", 1) == 0)
		gnm_style_set_font_italic (style, TRUE);

	if (strncmp (c, "i", 1) == 0)
		gnm_style_set_font_italic (style, TRUE);
}

/*
 * Create a Style equivalent to the XML subtree of doc.
 */
GnmStyle *
xml_read_style (XmlParseContext *ctxt, xmlNodePtr tree, gboolean leave_empty)
{
	xmlNode *e_node, *child;
	xmlChar *prop;
	int val;
	GnmColor *c;
	xmlChar	    *content;
	GnmStyle    *style;
	GnmParsePos  pp;
	GnmStyleConditions *sc = NULL;

	style = (!leave_empty &&
		 (ctxt->version >= GNM_XML_V6 ||
		  ctxt->version <= GNM_XML_V2))
		? gnm_style_new_default ()
		: gnm_style_new ();

	if (strcmp (tree->name, "Style")) {
		g_printerr ("xml_read_style: invalid element type %s, 'Style' expected\n",
			    tree->name);
	}

	if (xml_node_get_int (tree, "HAlign", &val))
		gnm_style_set_align_h (style, val);

	if (ctxt->version >= GNM_XML_V6) {
		if (xml_node_get_int (tree, "WrapText", &val))
			gnm_style_set_wrap_text (style, val);
		if (xml_node_get_bool (tree, "ShrinkToFit", &val))
			gnm_style_set_shrink_to_fit (style, val);
	} else if (xml_node_get_int (tree, "Fit", &val))
		gnm_style_set_wrap_text (style, val);

	if (xml_node_get_int (tree, "Locked", &val))
		gnm_style_set_contents_locked (style, val);
	if (xml_node_get_int (tree, "Hidden", &val))
		gnm_style_set_contents_hidden (style, val);

	if (xml_node_get_int (tree, "VAlign", &val))
		gnm_style_set_align_v (style, val);

	if (xml_node_get_int (tree, "Rotation", &val)) {
		/* Work around a bug pre 1.5.1 that would allow
		 * negative rotations.  -1 == vertical, map everything
		 * else back onto 0..359 */
		if (val < -1)
			val += 360;
		gnm_style_set_rotation (style, val);
	}

	if (xml_node_get_int (tree, "Shade", &val))
		gnm_style_set_pattern (style, val);

	if (xml_node_get_int (tree, "Indent", &val))
		gnm_style_set_indent (style, val);

	if ((c = xml_node_get_color (tree, "Fore")) != NULL)
		gnm_style_set_font_color (style, c);

	if ((c = xml_node_get_color (tree, "Back")) != NULL)
		gnm_style_set_back_color (style, c);

	if ((c = xml_node_get_color (tree, "PatternColor")) != NULL)
		gnm_style_set_pattern_color (style, c);

	prop = xmlGetProp (tree, CC2XML ("Format"));
	if (prop != NULL) {
		gnm_style_set_format_text (style, CXML2C (prop));
		xmlFree (prop);
	}

	for (child = tree->xmlChildrenNode; child != NULL ; child = child->next) {
		if (xmlIsBlankNode (child))
			continue;

		if (!strcmp (child->name, "Font")) {
			xmlChar *font;
			double size_pts = 14;
			int t;

			if (xml_node_get_double (child, "Unit", &size_pts))
				gnm_style_set_font_size (style, size_pts);

			if (xml_node_get_int (child, "Bold", &t))
				gnm_style_set_font_bold (style, t);

			if (xml_node_get_int (child, "Italic", &t))
				gnm_style_set_font_italic (style, t);

			if (xml_node_get_int (child, "Underline", &t))
				gnm_style_set_font_uline (style, (GnmUnderline)t);

			if (xml_node_get_int (child, "StrikeThrough", &t))
				gnm_style_set_font_strike (style, t ? TRUE : FALSE);

			if (xml_node_get_int (child, "Script", &t)) {
				if (t == 0)
					gnm_style_set_font_script (style, GO_FONT_SCRIPT_STANDARD);
				else if (t < 0)
					gnm_style_set_font_script (style, GO_FONT_SCRIPT_SUB);
				else
					gnm_style_set_font_script (style, GO_FONT_SCRIPT_SUPER);
			}

			font = xml_node_get_cstr (child, NULL);
			if (font) {
				if (*font == '-')
					style_font_read_from_x11 (style, CXML2C (font));
				else
					gnm_style_set_font_name (style, CXML2C (font));
				xmlFree (font);
			}

		} else if (!strcmp (child->name, "StyleBorder")) {
			xml_read_style_border (ctxt, child, style);
		} else if (!strcmp (child->name, "HyperLink")) {
			xmlChar *type, *target, *tip;

			type = xml_node_get_cstr (child, "type");
			if (type == NULL)
				continue;
			target = xml_node_get_cstr (child, "target");
			if (target != NULL) {
				GnmHLink *link = g_object_new (g_type_from_name (CXML2C (type)),
							       NULL);
				gnm_hlink_set_target (link, target);
				tip = xml_node_get_cstr (child, "tip");
				if (tip != NULL) {
					gnm_hlink_set_tip  (link, tip);
					xmlFree (tip);
				}
				gnm_style_set_hlink (style, link);
				xmlFree (target);
			}
			xmlFree (type);
		} else if (!strcmp (child->name, "Validation")) {
			ValidationStyle vstyle = VALIDATION_STYLE_NONE;
			ValidationType type = VALIDATION_TYPE_ANY;
			ValidationOp op = VALIDATION_OP_NONE;
			xmlChar *title, *msg;
			gboolean allow_blank, use_dropdown;
			GnmExprTop const *texpr0 = NULL;
			GnmExprTop const *texpr1 = NULL;

			if (xml_node_get_int (child, "Style", &val))
				vstyle = val;
			if (xml_node_get_int (child, "Type", &val))
				type = val;
			if (xml_node_get_int (child, "Operator", &val))
				op = val;

			if (!xml_node_get_bool (child, "AllowBlank", &(allow_blank)))
				allow_blank = FALSE;
			if (!xml_node_get_bool (child, "UseDropdown", &(use_dropdown)))
				use_dropdown = FALSE;

			title = xml_node_get_cstr (child, "Title");
			msg = xml_node_get_cstr (child, "Message");

			parse_pos_init_sheet (&pp, ctxt->sheet);
			if (NULL != (e_node = e_xml_get_child_by_name (child, CC2XML ("Expression0"))) &&
			    NULL != (content = xml_node_get_cstr (e_node, NULL))) {
				texpr0 = gnm_expr_parse_str
					(CXML2C (content), &pp,
					 GNM_EXPR_PARSE_DEFAULT,
					 ctxt->convs, NULL);
				xmlFree (content);
			}
			if (NULL != (e_node = e_xml_get_child_by_name (child, CC2XML ("Expression1"))) &&
			    NULL != (content = xml_node_get_cstr (e_node, NULL))) {
				texpr1 = gnm_expr_parse_str
					(CXML2C (content), &pp,
					 GNM_EXPR_PARSE_DEFAULT,
					 ctxt->convs, NULL);
				xmlFree (content);
			}

			gnm_style_set_validation
				(style,
				 validation_new
				 (vstyle, type, op,
				  CXML2C (title), CXML2C (msg),
				  texpr0,
				  texpr1,
				  allow_blank, use_dropdown));

			xmlFree (msg);
			xmlFree (title);
		} else if (!strcmp (child->name, "InputMessage")) {
			xmlChar *title = xml_node_get_cstr (child, "Title");
			xmlChar *msg   = xml_node_get_cstr (child, "Message");
			if (title || msg) {
				gnm_style_set_input_msg (style,
					gnm_input_msg_new (msg, title));
				if (msg)
					xmlFree (msg);
				if (title)
					xmlFree (title);
			}
		} else if (!strcmp (child->name, "Condition")) {
			GnmStyleCond cond;

			if (xml_node_get_int (child, "Operator", &val))
				cond.op = val;
			else
				cond.op = GNM_STYLE_COND_CUSTOM;

			parse_pos_init_sheet (&pp, ctxt->sheet);
			if (NULL != (e_node = e_xml_get_child_by_name (child, CC2XML ("Expression0"))) &&
			    NULL != (content = xml_node_get_cstr (e_node, NULL))) {
				cond.texpr[0] = gnm_expr_parse_str
					(CXML2C (content), &pp,
					 GNM_EXPR_PARSE_DEFAULT,
					 ctxt->convs, NULL);
				xmlFree (content);
			} else
				cond.texpr[0] = NULL;
			if (NULL != (e_node = e_xml_get_child_by_name (child, CC2XML ("Expression1"))) &&
			    NULL != (content = xml_node_get_cstr (e_node, NULL))) {
				cond.texpr[1] = gnm_expr_parse_str
					(CXML2C (content), &pp,
					 GNM_EXPR_PARSE_DEFAULT,
					 ctxt->convs, NULL);
				xmlFree (content);
			} else
				cond.texpr[1] = NULL;
			if (NULL != (e_node = e_xml_get_child_by_name (child, CC2XML ("Style"))))
				cond.overlay = xml_read_style (ctxt, e_node, TRUE);
			if (NULL == sc)
				sc = gnm_style_conditions_new ();
			gnm_style_conditions_insert (sc, &cond, -1);
		} else
			g_printerr ("xml_read_style: unknown type '%s'\n",
				 child->name);
	}

	if (NULL != sc)
		gnm_style_set_conditions (style, sc);

	return style;
}

/*
 * Create a GnmStyleRegion equivalent to the XML subtree of doc.
 * Return an style and a range in the @range parameter
 */
static GnmStyle *
xml_read_style_region_ex (XmlParseContext *ctxt, xmlNodePtr tree, GnmRange *range)
{
	xmlNodePtr child;
	GnmStyle    *style = NULL;

	if (strcmp (tree->name, "StyleRegion")){
		g_printerr ("xml_read_style_region_ex: invalid element type %s, 'StyleRegion' expected`\n",
			    tree->name);
		return NULL;
	}
	xml_node_get_range (tree, range);

	child = e_xml_get_child_by_name (tree, CC2XML ("Style"));
	if (child)
		style = xml_read_style (ctxt, child, FALSE);

	return style;
}

/*
 * Create a GnmStyleRegion equivalent to the XML subtree of doc.
 * Return nothing, attach it directly to the sheet in the context
 */
static void
xml_read_style_region (XmlParseContext *ctxt, xmlNodePtr tree)
{
	GnmStyle *style;
	GnmRange range;

	style = xml_read_style_region_ex (ctxt, tree, &range);

	if (style != NULL) {
		if (ctxt->version >= GNM_XML_V6)
			sheet_style_set_range (ctxt->sheet, &range, style);
		else
			sheet_style_apply_range (ctxt->sheet, &range, style);
	}
}

/**
 * xml_cell_set_array_expr : Utility routine to parse an expression
 *     and store it as an array.
 *
 * @cell : The upper left hand corner of the array.
 * @text : The text to parse.
 * @rows : The number of rows.
 * @cols : The number of columns.
 */
static void
xml_cell_set_array_expr (XmlParseContext *ctxt,
			 GnmCell *cell, char const *text,
			 int const rows, int const cols)
{
	GnmParsePos pp;
	GnmExprTop const *texpr =
		gnm_expr_parse_str (text,
				    parse_pos_init_cell (&pp, cell),
				    GNM_EXPR_PARSE_DEFAULT,
				    ctxt->convs, NULL);

	g_return_if_fail (texpr != NULL);
	gnm_cell_set_array_formula (cell->base.sheet,
				cell->pos.col, cell->pos.row,
				cell->pos.col + cols-1, cell->pos.row + rows-1,
				texpr);
}

/**
 * xml_not_used_old_array_spec : See if the string corresponds to
 *     a pre-0.53 style array expression.
 *     If it is the upper left corner	 - assign it.
 *     If it is a member of the an array - ignore it the corner will assign it.
 *     If it is not a member of an array return TRUE.
 */
static gboolean
xml_not_used_old_array_spec (XmlParseContext *ctxt,
			     GnmCell *cell, char *content)
{
	int rows, cols, row, col;

#if 0
	/* This is the syntax we are trying to parse */
	g_string_append_printf (str, "{%s}(%d,%d)[%d][%d]", expr_text,
		array.rows, array.cols, array.y, array.x);
#endif
	char *end, *expr_end, *ptr;

	if (content[0] != '=' || content[1] != '{')
		return TRUE;

	expr_end = strrchr (content, '}');
	if (expr_end == NULL || expr_end[1] != '(')
		return TRUE;

	rows = strtol (ptr = expr_end + 2, &end, 10);
	if (end == ptr || *end != ',')
		return TRUE;
	cols = strtol (ptr = end + 1, &end, 10);
	if (end == ptr || end[0] != ')' || end[1] != '[')
		return TRUE;
	row = strtol (ptr = (end + 2), &end, 10);
	if (end == ptr || end[0] != ']' || end[1] != '[')
		return TRUE;
	col = strtol (ptr = (end + 2), &end, 10);
	if (end == ptr || end[0] != ']' || end[1] != '\0')
		return TRUE;

	if (row == 0 && col == 0) {
		*expr_end = '\0';
		xml_cell_set_array_expr (ctxt, cell, content + 2, rows, cols);
	}

	return FALSE;
}

/*
 * Create a Cell equivalent to the XML subtree of doc.
 */
static GnmCell *
xml_read_cell (XmlParseContext *ctxt, xmlNodePtr tree)
{
	GnmCell *cell;
	xmlNodePtr child;
	int col, row;
	int array_cols, array_rows, shared_expr_index = -1;
	xmlChar *content = NULL;
	int  style_idx;
	gboolean style_read = FALSE;
	gboolean is_post_52_array = FALSE;
	gboolean is_new_cell = TRUE;
	gboolean is_value = FALSE;
	GnmValueType value_type = VALUE_EMPTY; /* Make compiler shut up */
	GOFormat *value_fmt = NULL;

	if (strcmp (tree->name, "Cell")) {
		g_printerr ("xml_read_cell: invalid element type %s, 'Cell' expected`\n",
			    tree->name);
		return NULL;
	}
	xml_node_get_int (tree, "Col", &col);
	xml_node_get_int (tree, "Row", &row);

	cell = sheet_cell_get (ctxt->sheet, col, row);
	if ((is_new_cell = (cell == NULL)))
		cell = sheet_cell_create (ctxt->sheet, col, row);
	if (cell == NULL)
		return NULL;

	if (ctxt->version < GNM_XML_V3) {
		/*
		 * This style code is a gross anachronism that slugs performance
		 * in the common case this data won't exist. In the long term all
		 * files will make the 0.41 - 0.42 transition and this can go.
		 * Newer file format includes an index pointer to the Style
		 * Old format includes the Style online
		 */
		if (xml_node_get_int (tree, "Style", &style_idx)) {
			GnmStyle *style;

			style_read = TRUE;
			style = g_hash_table_lookup (ctxt->style_table,
						     GINT_TO_POINTER (style_idx));
			if (style) {
				gnm_style_ref (style);
				sheet_style_set_pos (ctxt->sheet, col, row,
						     style);
			} /* else reading a newer version with style_idx == 0 */
		}
	} else {
		/* Is this a post 0.52 shared expression */
		if (!xml_node_get_int (tree, "ExprID", &shared_expr_index))
			shared_expr_index = -1;

		/* Is this a post 0.57 formatted value */
		if (ctxt->version >= GNM_XML_V4) {
			int tmp;
			is_post_52_array =
				xml_node_get_int (tree, "Rows", &array_rows) &&
				xml_node_get_int (tree, "Cols", &array_cols);
			if (xml_node_get_int (tree, "ValueType", &tmp)) {
				xmlChar *fmt;

				value_type = tmp;
				is_value = TRUE;

				fmt = xmlGetProp (tree, CC2XML ("ValueFormat"));
				if (fmt != NULL) {
					value_fmt = go_format_new_from_XL (CXML2C (fmt));
					xmlFree (fmt);
				}
			}
		}
	}

	if (ctxt->version < GNM_XML_V10)
		for (child = tree->xmlChildrenNode; child != NULL ; child = child->next) {
			if (xmlIsBlankNode (child))
				continue;
			/*
			 * This style code is a gross anachronism that slugs performance
			 * in the common case this data won't exist. In the long term all
			 * files will make the 0.41 - 0.42 transition and this can go.
			 * This is even older backwards compatibility than 0.41 - 0.42
			 */
			if (!style_read && !strcmp (child->name, "Style")) {
				GnmStyle *style = xml_read_style (ctxt, child, FALSE);
				if (style)
					sheet_style_set_pos (ctxt->sheet, col, row, style);
			/* This is a pre version 1.0.3 file */
			} else if (!strcmp (child->name, "Content")) {
				content = xml_node_get_cstr (child, NULL);

				/* Is this a post 0.52 array */
				if (ctxt->version == GNM_XML_V3) {
					is_post_52_array =
					    xml_node_get_int (child, "Rows", &array_rows) &&
					    xml_node_get_int (child, "Cols", &array_cols);
				}
			} else if (!strcmp (child->name, "Comment")) {
				xmlChar *comment = xmlNodeGetContent (child);
				cell_set_comment (cell->base.sheet,
						  &cell->pos, NULL, comment);
				xmlFree (comment);
			}
		}

	/* As of 1.0.3 we are back to storing the cell content directly as the content in cell
	 * rather than creating piles and piles of useless nodes.
	 */
	if (content == NULL) {
		/* in libxml1 <foo/> would return NULL
		 * in libxml2 <foo/> would return ""
		 */
		if (tree->xmlChildrenNode != NULL)
			content = xmlNodeGetContent (tree);

		/* Early versions had newlines at the end of their content */
		if (ctxt->version <= GNM_XML_V1 && content != NULL) {
			char *tmp = strchr (XML2C (content), '\n');
			if (tmp != NULL)
				*tmp = '\0';
		}
	}

	if (content != NULL) {
		if (is_post_52_array) {
			g_return_val_if_fail (content[0] == '=', NULL);

			xml_cell_set_array_expr (ctxt, cell, CXML2C (content + 1),
						 array_rows, array_cols);
		} else if (ctxt->version >= GNM_XML_V3 ||
			   xml_not_used_old_array_spec (ctxt, cell, XML2C (content))) {
			if (is_value)
				gnm_cell_set_value (cell,
					value_new_from_string (value_type,
							       CXML2C (content),
							       value_fmt, FALSE));
			else {
				/* gnm_cell_set_text would probably handle this.
				 * BUT, be extra careful just incase a sheet
				 * appears that defines format text on a cell
				 * with a formula.  Try REALLY REALLY hard
				 * to parse it.  No need to worry about
				 * accidentally parsing something because
				 * support for Text formats did not happen
				 * until after ValueType was added.
				 */
				GnmParsePos pos;
				GnmExprTop const *texpr = NULL;
				char const *expr_start = gnm_expr_char_start_p (CXML2C (content));
				if (NULL != expr_start && *expr_start) {
					texpr = gnm_expr_parse_str (expr_start,
								    parse_pos_init_cell (&pos, cell),
								    GNM_EXPR_PARSE_DEFAULT,
								    ctxt->convs, NULL);
				}
				if (texpr != NULL) {
					gnm_cell_set_expr (cell, texpr);
					gnm_expr_top_unref (texpr);
				} else
					gnm_cell_set_text (cell, CXML2C (content));
			}
		}

		if (shared_expr_index > 0) {
			if (shared_expr_index == (int)ctxt->shared_exprs->len + 1) {
				if (!gnm_cell_has_expr (cell)) {
					g_warning ("XML-IO: Shared expression with no expession? id = %d\ncontent ='%s'",
						   shared_expr_index, CXML2C (content));
					gnm_cell_set_expr
						(cell,
						 gnm_expr_top_new_constant
						 (value_dup (cell->value)));
				}
				g_ptr_array_add (ctxt->shared_exprs,
						 (gpointer)cell->base.texpr);
			} else {
				g_warning ("XML-IO: Duplicate or invalid shared expression: %d",
					   shared_expr_index);
			}
		}
		xmlFree (content);
	} else if (shared_expr_index > 0) {
		if (shared_expr_index <= (int)ctxt->shared_exprs->len + 1) {
			GnmExprTop *texpr =
				g_ptr_array_index (ctxt->shared_exprs,
						   shared_expr_index - 1);
			gnm_cell_set_expr (cell, texpr);
		} else {
			g_warning ("XML-IO: Missing shared expression");
		}
	} else if (is_new_cell)
		/*
		 * Only set to empty if this is a new cell.
		 * If it was created by a previous array
		 * we do not want to erase it.
		 */
		gnm_cell_set_value (cell, value_new_empty ());

	go_format_unref (value_fmt);
	return cell;
}

static void
xml_read_sheet_layout (XmlParseContext *ctxt, xmlNodePtr tree)
{
	SheetView *sv = sheet_get_view (ctxt->sheet, ctxt->wb_view);
	xmlNodePtr child;
	GnmCellPos tmp, frozen_tl, unfrozen_tl;

	tree = e_xml_get_child_by_name (tree, CC2XML ("SheetLayout"));
	if (tree == NULL)
		return;

	/* The top left cell in pane[0] */
	if (xml_node_get_cellpos (tree, "TopLeft", &tmp))
		sv_set_initial_top_left (sv, tmp.col, tmp.row);

	child = e_xml_get_child_by_name (tree, CC2XML ("FreezePanes"));
	if (child != NULL &&
	    xml_node_get_cellpos (child, "FrozenTopLeft", &frozen_tl) &&
	    xml_node_get_cellpos (child, "UnfrozenTopLeft", &unfrozen_tl))
		sv_freeze_panes (sv, &frozen_tl, &unfrozen_tl);
}

static char const *filter_cond_name[] = { "eq", "gt", "lt", "gte", "lte", "ne" };

/*
 * WARNING WARNING WARING
 * Value and ValueType are _reversed !!!
 */
static struct { char const *op, *valtype, *val; } filter_expr_attrs[] = {
	{ "Op0", "Value0", "ValueType0" },
	{ "Op1", "Value1", "ValueType1" }
};

static GnmFilterCondition *
xml_read_filter_expr (XmlParseContext *ctxt, xmlNode *field)
{
	GnmValueType value_type;
	GnmFilterOp op[2];
	GnmValue *v[2];
	int i, j;
	xmlChar *tmp;

	for (i = 0 ; i < 2 ; i++) {
		/* get the operator */
		tmp = xml_node_get_cstr (field, filter_expr_attrs[i].op);
		if (tmp == NULL)
			break;
		for (j = G_N_ELEMENTS (filter_cond_name); j-- ; )
			if (!g_ascii_strcasecmp (tmp, filter_cond_name[j]))
				break;
		xmlFree (tmp);
		if (j < 0)
			break;
		op[i] = j;

		/* get value */
		if (!xml_node_get_int (field, filter_expr_attrs[i].valtype, &j))
			break;
		value_type = j;

		tmp = xml_node_get_cstr (field, filter_expr_attrs[i].val);
		if (tmp == NULL)
			break;
		v[i] = value_new_from_string (value_type,
					      CXML2C (tmp),
					      NULL,
					      FALSE);
		xmlFree (tmp);
	}

	if (i == 0)
		return NULL;
	if (i == 1)
		return gnm_filter_condition_new_single (op[0], v[0]);

	if (i == 2) {
		gboolean is_and = TRUE;
		xml_node_get_bool (field, "IsAnd", &is_and);
		return gnm_filter_condition_new_double (
			op[0], v[0], is_and, op[1], v[1]);
	}

	return NULL;
}

static void
xml_read_filter_field (XmlParseContext *ctxt, xmlNode *field, GnmFilter *filter)
{
	GnmFilterCondition *cond = NULL;
	char *type;
	int i;

	if (!xml_node_get_int (field, "Index", &i))
		return;

	type = xml_node_get_cstr (field, "Type");
	if (type == NULL)
		return;

	if (!g_ascii_strcasecmp (type, "expr"))
		cond = xml_read_filter_expr (ctxt, field);
	else if (!g_ascii_strcasecmp (type, "blanks"))
		cond = gnm_filter_condition_new_single (
				  GNM_FILTER_OP_BLANKS, NULL);
	else if (!g_ascii_strcasecmp (type, "nonblanks"))
		cond = gnm_filter_condition_new_single (
				  GNM_FILTER_OP_NON_BLANKS, NULL);
	else if (!g_ascii_strcasecmp (type, "bucket")) {
		gboolean top, items;
		double count;

		if (xml_node_get_bool (field, CC2XML ("Top"), &top) &&
		    xml_node_get_bool (field, CC2XML ("Items"), &items) &&
		    xml_node_get_double (field, CC2XML ("Count"), &count))
			cond = gnm_filter_condition_new_bucket (
					top, items, count);
	}
	xmlFree (type);

	if (cond != NULL)
		gnm_filter_set_condition (filter, i, cond, FALSE);
}

static void
xml_read_sheet_filters (XmlParseContext *ctxt, xmlNode const *container)
{
	xmlNode *filter_node, *field;
	GnmRange	 r;
	xmlChar   *area;
	GnmFilter *filter;

	container = e_xml_get_child_by_name (container, CC2XML ("Filters"));
	if (container == NULL)
		return;

	for (filter_node = container->xmlChildrenNode; filter_node != NULL; filter_node = filter_node->next) {
		if (xmlIsBlankNode (filter_node))
			continue;
		area = xml_node_get_cstr (filter_node, "Area");
		if (area == NULL)
			continue;
		if (range_parse (&r, CXML2C (area))) {
			filter = gnm_filter_new (ctxt->sheet, &r);
			for (field = filter_node->xmlChildrenNode; field != NULL; field = field->next)
				if (!xmlIsBlankNode (field))
					xml_read_filter_field (ctxt, field, filter);
		}
		xmlFree (area);
	}
}

static void
xml_read_solver (XmlParseContext *ctxt, xmlNodePtr tree)
{
	SolverConstraint *c;
	xmlNodePtr       child, ptr;
	int              col, row, ptype;
	xmlChar          *s;
	Sheet *sheet = ctxt->sheet;
	SolverParameters *param = sheet->solver_parameters;

	tree = e_xml_get_child_by_name (tree, CC2XML ("Solver"));
	if (tree == NULL)
		return;

	if (xml_node_get_int (tree, "TargetCol", &col) && col >= 0 &&
	    xml_node_get_int (tree, "TargetRow", &row) && row >= 0)
		param->target_cell = sheet_cell_fetch (sheet, col, row);

	if (xml_node_get_int (tree, "ProblemType", &ptype))
		param->problem_type = (SolverProblemType)ptype;

	s = xml_node_get_cstr (tree, "Inputs");
	g_free (param->input_entry_str);
	param->input_entry_str = g_strdup ((const gchar *)s);
	xmlFree (s);

	param->constraints = NULL;
	/* Handle both formats.
	 * Pre 1.4 we would nest the constraints (I suspect this was unintentional)
	 * After 1.4 we stored them serially. */
	for (ptr = tree->xmlChildrenNode; ptr != NULL ; ptr = ptr->next) {
		if (xmlIsBlankNode (ptr) ||
		    ptr->name == NULL || strcmp (ptr->name, "Constr"))
			continue;
		child = ptr;
		do {
			int type;

			c = g_new (SolverConstraint, 1);
			xml_node_get_int (child, "Lcol", &c->lhs.col);
			xml_node_get_int (child, "Lrow", &c->lhs.row);
			xml_node_get_int (child, "Rcol", &c->rhs.col);
			xml_node_get_int (child, "Rrow", &c->rhs.row);
			xml_node_get_int (child, "Cols", &c->cols);
			xml_node_get_int (child, "Rows", &c->rows);
			xml_node_get_int (child, "Type", &type);
			switch (type) {
			case 1:
				c->type = SolverLE;
				break;
			case 2:
				c->type = SolverGE;
				break;
			case 4:
				c->type = SolverEQ;
				break;
			case 8:
				c->type = SolverINT;
				break;
			case 16:
				c->type = SolverBOOL;
				break;
			default:
				c->type = SolverLE;
				break;
			}
#ifdef GNM_ENABLE_SOLVER
			c->str = write_constraint_str (c->lhs.col, c->lhs.row,
						       c->rhs.col, c->rhs.row,
						       c->type, c->cols, c->rows);
#endif
			param->constraints = g_slist_append (param->constraints, c);
			child = e_xml_get_child_by_name (child, CC2XML ("Constr"));
		} while (child != NULL);
	}

	/* The options of the Solver. */
	xml_node_get_int (tree, "MaxTime", &(param->options.max_time_sec));
	xml_node_get_int (tree, "MaxIter", &(param->options.max_iter));
	xml_node_get_bool (tree, "NonNeg",
			  &(param->options.assume_non_negative));
	xml_node_get_bool (tree, "Discr", &(param->options.assume_discrete));
	xml_node_get_bool (tree, "AutoScale",
			  &(param->options.automatic_scaling));
	xml_node_get_bool (tree, "ShowIter",
			  &(param->options.show_iter_results));
	xml_node_get_bool (tree, "AnswerR", &(param->options.answer_report));
	xml_node_get_bool (tree, "SensitivityR",
			  &(param->options.sensitivity_report));
	xml_node_get_bool (tree, "LimitsR", &(param->options.limits_report));
	xml_node_get_bool (tree, "PerformR",
			  &(param->options.performance_report));
	xml_node_get_bool (tree, "ProgramR", &(param->options.program_report));
}

static void
xml_read_scenarios (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	Sheet      *sheet = ctxt->sheet;

	tree = e_xml_get_child_by_name (tree, CC2XML ("Scenarios"));
	if (tree == NULL)
		return;

	child = e_xml_get_child_by_name (tree, CC2XML ("Scenario"));
	while (child != NULL) {
		scenario_t *s;
		xmlChar    *str;
		int        rows, cols, i;
		GnmValue      *range;

		s = g_new0 (scenario_t, 1);

		/* Scenario: name. */
		str = xml_node_get_cstr (child, "Name");
		s->name = g_strdup ((const gchar *)str);
		xmlFree (str);

		/* Scenario: comment. */
		str = xml_node_get_cstr (child, "Comment");
		s->comment = g_strdup ((const gchar *)str);
		xmlFree (str);

		/* Scenario: changing cells in a string form. */
		str = xml_node_get_cstr (child, "CellsStr");
		s->cell_sel_str = g_strdup ((const gchar *)str);
		range = value_new_cellrange_str (sheet, str);
		if (range) {
			GnmValueRange *vrange = (GnmValueRange *) range;

			s->range.start.col = vrange->cell.a.col;
			s->range.start.row = vrange->cell.a.row;
			s->range.end.col   = vrange->cell.b.col;
			s->range.end.row   = vrange->cell.b.row;
			value_release (range);
		}
		xmlFree (str);

		/* Scenario: values. */
		rows = s->range.end.row - s->range.start.row + 1;
		cols = s->range.end.col - s->range.start.col + 1;
		s->changing_cells = g_new (GnmValue *, rows * cols);
		for (i = 0; i < cols * rows; i++) {
			GString *name;

			name = g_string_new (NULL);
			g_string_append_printf (name, "V%d", i);
			s->changing_cells [i] = xml_node_get_value (child,
								    name->str);
			g_string_free (name, TRUE);
		}

		sheet->scenarios = g_list_append (sheet->scenarios, s);
		child = e_xml_get_child_by_name (child, CC2XML ("Scenario"));
	}
}

static SheetObject *
xml_read_sheet_object (XmlParseContext const *ctxt, xmlNodePtr tree)
{
	char *tmp;
	int tmp_int;
	GObject *obj;
	SheetObject *so;
	SheetObjectClass *klass;

	/* Old crufty IO */
	if (!strcmp (tree->name, "Rectangle"))
		so = g_object_new (GNM_SO_FILLED_TYPE, NULL);
	else if (!strcmp (tree->name, "Ellipse"))
		so = g_object_new (GNM_SO_FILLED_TYPE, "is-oval", TRUE, NULL);
	else if (!strcmp (tree->name, "Line"))
		so = g_object_new (GNM_SO_LINE_TYPE, "is-arrow", TRUE, NULL);
	else if (!strcmp (tree->name, "Arrow"))
		so = g_object_new (GNM_SO_LINE_TYPE, NULL);

	/* Class renamed between 1.0.x and 1.2.x */
	else if (!strcmp (tree->name, "GnmGraph"))
		so = sheet_object_graph_new (NULL);

	/* Class renamed in 1.2.2 */
	else if (!strcmp (tree->name, "CellComment"))
		so = g_object_new (cell_comment_get_type (), NULL);

	/* Class renamed in 1.3.91 */
	else if (!strcmp (tree->name, "SheetObjectGraphic"))
		so = g_object_new (GNM_SO_LINE_TYPE, NULL);
	else if (!strcmp (tree->name, "SheetObjectFilled"))
		so = g_object_new (GNM_SO_FILLED_TYPE, NULL);
	else if (!strcmp (tree->name, "SheetObjectText"))
		so = g_object_new (GNM_SO_FILLED_TYPE, NULL);

	else {
		GType type = g_type_from_name ((gchar *)tree->name);
		if (type == 0) {
			char *str = g_strdup_printf (_("Unsupported object type '%s'"),
						     tree->name);
			gnm_io_warning_unsupported_feature (ctxt->io_context, str);
			g_free (str);
			return NULL;
		}

		obj = g_object_new (type, NULL);
		if (obj == NULL)
			return NULL;

		so = SHEET_OBJECT (obj);
	}

	klass = SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS(so));

	if (klass->read_xml_dom &&
	    (klass->read_xml_dom) (so, tree->name, ctxt, tree)) {
		g_object_unref (G_OBJECT (so));
		return NULL;
	}

	tmp = (char *) xmlGetProp (tree, (xmlChar *)"ObjectBound");
	if (tmp != NULL) {
		GnmRange r;
		if (range_parse (&r, tmp)) {
			/* Patch problems introduced in some 1.7.x versions that stored
			 * comments in merged cells with the full rectangle of the merged cell
			 * rather than just the top left corner */
			if (G_OBJECT_TYPE (so) == CELL_COMMENT_TYPE)
				r.end = r.start;
			so->anchor.cell_bound = r;
		}
		xmlFree (tmp);
	}

	tmp =  (char *) xmlGetProp (tree, (xmlChar *)"ObjectOffset");
	if (tmp != NULL) {
		sscanf (tmp, "%g %g %g %g",
			so->anchor.offset +0, so->anchor.offset +1,
			so->anchor.offset +2, so->anchor.offset +3);
		xmlFree (tmp);
	}

#if 0
	/* Deprecated in 1.7.7 */
	tmp = (char *) xmlGetProp (tree, (xmlChar *)"ObjectAnchorType");
	if (tmp != NULL) {
		int i[4], count;
		sscanf (tmp, "%d %d %d %d", i+0, i+1, i+2, i+3);

		for (count = 4; count-- > 0 ; )
			so->anchor.type[count] = i[count];
		xmlFree (tmp);
	}
#endif

	if (xml_node_get_int (tree, "Direction", &tmp_int))
		so->anchor.base.direction = tmp_int;
	else
		so->anchor.base.direction = GOD_ANCHOR_DIR_UNKNOWN;

	/* Do not assign to a sheet when extracting a cell region */
	if (NULL != ctxt->sheet) {
		sheet_object_set_sheet (so, ctxt->sheet);
		g_object_unref (G_OBJECT (so));
	}
	return so;
}

static void
xml_read_merged_regions (XmlParseContext const *ctxt, xmlNodePtr sheet)
{
	xmlNodePtr container, region;

	container = e_xml_get_child_by_name (sheet, CC2XML ("MergedRegions"));
	if (container == NULL)
		return;

	for (region = container->xmlChildrenNode; region; region = region->next)
		if (!xmlIsBlankNode (region)) {
			xmlChar *content = xml_node_get_cstr (region, NULL);
			GnmRange r;
			if (content != NULL) {
				if (range_parse (&r, CXML2C (content)))
					gnm_sheet_merge_add (ctxt->sheet, &r, FALSE, NULL);
				xmlFree (content);
			}
		}
}

static void
xml_read_styles (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	xmlNodePtr regions;

	child = e_xml_get_child_by_name (tree, CC2XML ("Styles"));
	if (child == NULL)
		return;

	for (regions = child->xmlChildrenNode; regions != NULL; regions = regions->next) {
		if (!xmlIsBlankNode (regions))
			xml_read_style_region (ctxt, regions);
		count_io_progress_update (ctxt->io_context, 1);
	}
}

static void
xml_read_colrow_info (XmlParseContext *ctxt, xmlNodePtr tree,
		      gboolean is_cols)
{
	ColRowInfo *cri;
	double size_pts;
	int pos, val;

	if (!xml_node_get_int (tree, "No", &pos) ||
	    NULL == (cri = sheet_colrow_fetch (ctxt->sheet, pos, is_cols)))
		return;

	if (xml_node_get_int (tree, "HardSize", &val))
		cri->hard_size = val;
	if (xml_node_get_double (tree, "Unit", &size_pts)) {
		if (is_cols)
			sheet_col_set_size_pts (ctxt->sheet,
				pos, size_pts, cri->hard_size);
		else
			sheet_row_set_size_pts (ctxt->sheet,
				pos, size_pts, cri->hard_size);
	}

	if (xml_node_get_int (tree, "Hidden", &val) && val)
		cri->visible = FALSE;
	if (xml_node_get_int (tree, "Collapsed", &val) && val)
		cri->is_collapsed = TRUE;
	if (xml_node_get_int (tree, "OutlineLevel", &val) && val > 0)
		cri->outline_level = val;

	if (xml_node_get_int (tree, "Count", &val)) {
		/* resize flags are already set only need to copy the sizes */
		for ( ; --val > 0 ; )
			colrow_copy (sheet_colrow_fetch (ctxt->sheet, ++pos, is_cols), cri);
	}
	if (is_cols) {
		if (ctxt->sheet->cols.max_outline_level < cri->outline_level)
			ctxt->sheet->cols.max_outline_level = cri->outline_level;
	} else {
		if (ctxt->sheet->rows.max_outline_level < cri->outline_level)
			ctxt->sheet->rows.max_outline_level = cri->outline_level;
	}
}

static void
xml_read_cols_info (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr cols, col;
	double tmp;

	cols = e_xml_get_child_by_name (tree, CC2XML ("Cols"));
	if (cols == NULL)
		return;

	if (xml_node_get_double (cols, "DefaultSizePts", &tmp))
		sheet_col_set_default_size_pts (ctxt->sheet, tmp);

	for (col = cols->xmlChildrenNode; col; col = col->next)
		if (!xmlIsBlankNode (col))
			xml_read_colrow_info (ctxt, col, TRUE);
}

static void
xml_read_rows_info (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr rows, row;
	double tmp;

	rows = e_xml_get_child_by_name (tree, CC2XML ("Rows"));
	if (rows == NULL)
		return;

	if (xml_node_get_double (rows, "DefaultSizePts", &tmp))
		sheet_row_set_default_size_pts (ctxt->sheet, tmp);

	for (row = rows->xmlChildrenNode; row; row = row->next)
		if (!xmlIsBlankNode (row))
			xml_read_colrow_info (ctxt, row, FALSE);
}

static void
xml_read_cell_styles (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr styles, child;
	GnmStyle *style;
	int style_idx;

	ctxt->style_table = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) gnm_style_unref);

	child = e_xml_get_child_by_name (tree, CC2XML ("CellStyles"));
	if (child == NULL)
		return;

	for (styles = child->xmlChildrenNode; styles; styles = styles->next) {
		if (!xmlIsBlankNode (styles) &&
		    xml_node_get_int (styles, "No", &style_idx)) {
			style = xml_read_style (ctxt, styles, FALSE);
			g_hash_table_insert (
				ctxt->style_table,
				GINT_TO_POINTER (style_idx),
				style);
		}
	}
}

/*
 * Create a Sheet equivalent to the XML subtree of doc.
 */
static Sheet *
xml_sheet_read (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	/* xmlNodePtr styles; */
	Sheet *sheet = NULL;
	double zoom_factor;
	gboolean tmp;
	xmlChar *val;
	int tmpi;
	GnmColor *c;

	if (strcmp (tree->name, "Sheet")){
		g_printerr ("xml_sheet_read: invalid element type %s, 'Sheet' expected\n",
			    tree->name);
	}

	/*
	 * Get the name of the sheet.  If it does exist, use the existing
	 * name, otherwise create a sheet (ie, for the case of only reading
	 * a new sheet).
	 */
	val = xml_node_get_cstr (e_xml_get_child_by_name (tree, CC2XML ("Name")), NULL);
	if (val == NULL)
		return NULL;

	sheet = workbook_sheet_by_name (ctxt->wb, CXML2C (val));
	if (sheet == NULL)
		sheet = sheet_new (ctxt->wb, CXML2C (val));
	xmlFree (val);
	if (sheet == NULL)
		return NULL;

	ctxt->sheet = sheet;

	if (!xml_node_get_bool (tree, "DisplayFormulas", &(sheet->display_formulas)))
		sheet->display_formulas = FALSE;
	if (!xml_node_get_bool (tree, "HideZero", &(sheet->hide_zero)))
		sheet->hide_zero = FALSE;
	if (!xml_node_get_bool (tree, "HideGrid", &(sheet->hide_grid)))
		sheet->hide_grid = FALSE;
	if (!xml_node_get_bool (tree, "HideColHeader", &(sheet->hide_col_header)))
		sheet->hide_col_header = FALSE;
	if (!xml_node_get_bool (tree, "HideRowHeader", &(sheet->hide_row_header)))
		sheet->hide_row_header = FALSE;
	if (xml_node_get_bool (tree, "DisplayOutlines", &tmp))
		g_object_set (sheet, "display-outlines", tmp, NULL);
	if (xml_node_get_bool (tree, "OutlineSymbolsBelow", &tmp))
		g_object_set (sheet, "display-outlines-below", tmp, NULL);
	if (xml_node_get_bool (tree, "OutlineSymbolsRight", &tmp))
		g_object_set (sheet, "display-outlines-right", tmp, NULL);
	if (xml_node_get_bool (tree, "RTL_Layout", &tmp))
		g_object_set (sheet, "text-is-rtl", tmp, NULL);
	if (xml_node_get_bool (tree, "Protected", &tmp))
		g_object_set (sheet, "protected", tmp, NULL);

	if (NULL != (val = xml_node_get_cstr (tree, CC2XML ("ExprConvention")))) {
		GnmConventions const *convs = gnm_conventions_default;
		if (0 == strcmp (val, "gnumeric:R1C1"))
			convs = gnm_conventions_xls_r1c1;
		g_object_set (sheet, "conventions", convs, NULL);
		xmlFree (val);
	}
	if (xml_node_get_enum (tree, "Visibility", GNM_SHEET_VISIBILITY_TYPE, &tmpi))
		g_object_set (sheet, "visibility", tmpi, NULL);
	sheet->tab_color = xml_node_get_color (tree, "TabColor");
	sheet->tab_text_color = xml_node_get_color (tree, "TabTextColor");
	if (NULL != (c = xml_node_get_color (tree, "GridColor")))
		sheet_style_set_auto_pattern_color (sheet, c);

	xml_node_get_double (e_xml_get_child_by_name (tree, CC2XML ("Zoom")), NULL,
			     &zoom_factor);

	xml_read_print_info (ctxt, tree);
	xml_read_styles (ctxt, tree);
	xml_read_cell_styles (ctxt, tree);
	xml_read_cols_info (ctxt, tree);
	xml_read_rows_info (ctxt, tree);
	xml_read_merged_regions (ctxt, tree);
	xml_read_sheet_filters (ctxt, tree);
	xml_read_selection_info (ctxt, tree);

	xml_read_names (ctxt, tree, NULL, sheet);

	child = e_xml_get_child_by_name (tree, CC2XML ("Objects"));
	if (child != NULL) {
		xmlNodePtr object = child->xmlChildrenNode;
		for (; object != NULL ; object = object->next)
			if (!xmlIsBlankNode (object))
				xml_read_sheet_object (ctxt, object);
	}

	child = e_xml_get_child_by_name (tree, CC2XML ("Cells"));
	if (child != NULL) {
		xmlNodePtr cell;

		for (cell = child->xmlChildrenNode; cell != NULL; cell = cell->next) {
			if (!xmlIsBlankNode (cell))
				xml_read_cell (ctxt, cell);
			count_io_progress_update (ctxt->io_context, 1);
		}
	}

	xml_read_solver (ctxt, tree);
	xml_read_scenarios (ctxt, tree);
	xml_read_sheet_layout (ctxt, tree);

	g_hash_table_destroy (ctxt->style_table);

	/* Init ColRowInfo's size_pixels and force a full respan */
	g_object_set (sheet, "zoom-factor", zoom_factor, NULL);
	sheet_flag_recompute_spans (sheet);

	return sheet;
}

/****************************************************************************/

static void
xml_read_clipboard_cell (XmlParseContext *ctxt, xmlNodePtr tree,
			 GnmCellRegion *cr, Sheet *sheet)
{
	int tmp, col, row;
	GnmCellCopy *cc;
	xmlNode  *child;
	xmlChar	 *content;
	int array_cols, array_rows, shared_expr_index = -1;
	gboolean is_post_52_array = FALSE;
	gboolean is_value = FALSE;
	GnmValueType value_type = VALUE_EMPTY; /* Make compiler shut up */
	GOFormat *value_fmt = NULL;
	GnmParsePos pp;

	g_return_if_fail (0 == strcmp (tree->name, "Cell"));

	cc = gnm_cell_copy_new (cr,
		(xml_node_get_int (tree, "Col", &col) ? col - cr->base.col : 0),
		(xml_node_get_int (tree, "Row", &row) ? row - cr->base.row : 0));
	parse_pos_init (&pp, NULL, sheet, col, row);

	/* Is this a post 0.52 shared expression */
	if (!xml_node_get_int (tree, "ExprID", &shared_expr_index))
		shared_expr_index = -1;

	is_post_52_array =
		xml_node_get_int (tree, "Rows", &array_rows) &&
		xml_node_get_int (tree, "Cols", &array_cols);
	if (xml_node_get_int (tree, "ValueType", &tmp)) {
		xmlChar *fmt;

		value_type = tmp;
		is_value = TRUE;

		fmt = xmlGetProp (tree, CC2XML ("ValueFormat"));
		if (fmt != NULL) {
			value_fmt = go_format_new_from_XL (CXML2C (fmt));
			xmlFree (fmt);
		}
	}

	child = e_xml_get_child_by_name (tree, CC2XML ("Content"));
	content = xml_node_get_cstr ((child != NULL) ? child : tree, NULL);
	if (content != NULL) {
		if (is_post_52_array) {
			g_return_if_fail (content[0] == '=');

			cc->texpr =
				gnm_expr_parse_str (CXML2C (content), &pp,
						    GNM_EXPR_PARSE_DEFAULT,
						    ctxt->convs, NULL);

			g_return_if_fail (cc->texpr != NULL);
#warning TODO : arrays
		} else if (is_value)
			cc->val = value_new_from_string (value_type,
				CXML2C (content), value_fmt, FALSE);
		else {
			parse_text_value_or_expr (&pp, CXML2C (content),
				&cc->val, &cc->texpr, value_fmt, cr->date_conv);
		}

		if (shared_expr_index > 0) {
			if (shared_expr_index == (int)ctxt->shared_exprs->len + 1) {
				if (NULL == cc->texpr) {
					/* The parse failed, but we know it is
					 * an expression.  this can happen
					 * until we get proper interworkbook
					 * linkages.  Force the content into
					 * being an expression.
					 */
					cc->texpr =
						gnm_expr_top_new_constant
						(value_new_string
						 (gnm_expr_char_start_p
						  (CXML2C (content))));
					if (cc->val) {
						value_release (cc->val);
						cc->val = NULL;
					}
				}
				g_ptr_array_add (ctxt->shared_exprs,
						 (gpointer)cc->texpr);
			} else {
				g_warning ("XML-IO: Duplicate or invalid shared expression: %d",
					   shared_expr_index);
			}
		}
		xmlFree (content);
	} else if (shared_expr_index > 0) {
		if (shared_expr_index <= (int)ctxt->shared_exprs->len + 1) {
			cc->texpr = g_ptr_array_index (ctxt->shared_exprs,
						       shared_expr_index - 1);
			gnm_expr_top_ref (cc->texpr);
		} else {
			g_warning ("XML-IO: Missing shared expression");
		}
	}
	go_format_unref (value_fmt);
}

/**
 * xml_clipboard_read :
 * @wbc : where to report errors.
 * @buffer : the buffer to parse.
 * @length : the size of the buffer.
 *
 * Attempt to parse the data in @buffer assuming it is in Gnumeric
 * ClipboardRange format.
 *
 * returns a GnmCellRegion on success or NULL on failure.
 */
GnmCellRegion *
xml_cellregion_read (WorkbookControl *wbc, Sheet *sheet, const char *buffer, int length)
{
	XmlParseContext *ctxt;
	xmlNode		*l, *clipboard;
	xmlDoc		*doc;
	GnmCellRegion   *cr = NULL;
	GnmLocale       *locale;
	int dummy;
	xmlChar *dateconvstr;

	g_return_val_if_fail (buffer != NULL, NULL);

	locale = gnm_push_C_locale ();

	doc = xmlParseDoc (CC2XML (buffer));

	if (doc == NULL) {
		go_cmd_context_error_import (GO_CMD_CONTEXT (wbc),
			_("Unparsable xml in clipboard"));
		goto err;
	}
	clipboard = doc->xmlRootNode;
	if (clipboard == NULL || strcmp (clipboard->name, "ClipboardRange")) {
		xmlFreeDoc (doc);
		go_cmd_context_error_import (GO_CMD_CONTEXT (wbc),
			_("Clipboard is in unknown format"));
		goto err;
	}

	/* ctxt->sheet must == NULL or copying objects will break */
	ctxt = xml_parse_ctx_new (doc, NULL, NULL);
	cr = cellregion_new (NULL);

	xml_node_get_int (clipboard, "Cols", &cr->cols);
	xml_node_get_int (clipboard, "Rows", &cr->rows);
	xml_node_get_int (clipboard, "BaseCol", &cr->base.col);
	xml_node_get_int (clipboard, "BaseRow", &cr->base.row);
	/* if it exists it is TRUE */
	cr->not_as_contents = xml_node_get_int (clipboard, "NotAsContent", &dummy);

	dateconvstr = xml_node_get_cstr (clipboard, "DateConvention");
	if (!dateconvstr)
		dateconvstr = xml_node_get_cstr (clipboard, "gnm:DateConvention");
	cr->date_conv = go_date_conv_from_str (dateconvstr
					       ? CXML2C (dateconvstr)
					       : "Lotus:1900");
	if (dateconvstr)
		xmlFree (dateconvstr);

	l = e_xml_get_child_by_name (clipboard, CC2XML ("Styles"));
	if (l != NULL)
		for (l = l->xmlChildrenNode; l != NULL ; l = l->next)
			if (!xmlIsBlankNode (l)) {
				GnmStyleRegion *sr = g_new (GnmStyleRegion, 1);
				sr->style = xml_read_style_region_ex (ctxt, l, &sr->range);
				cr->styles = g_slist_prepend (cr->styles, sr);
			}

	l = e_xml_get_child_by_name (clipboard, CC2XML ("MergedRegions"));
	if (l != NULL)
		for (l = l->xmlChildrenNode; l != NULL ; l = l->next)
			if (!xmlIsBlankNode (l)) {
				GnmRange r;
				xmlChar *content = (char *)xmlNodeGetContent (l);
				if (range_parse (&r, CXML2C (content)))
					cr->merged = g_slist_prepend (cr->merged,
								      range_dup (&r));
				xmlFree (content);
			}

	l = e_xml_get_child_by_name (clipboard, CC2XML ("Cells"));
	if (l != NULL)
		for (l = l->xmlChildrenNode; l != NULL ; l = l->next)
			if (!xmlIsBlankNode (l))
				xml_read_clipboard_cell (ctxt, l, cr, sheet);

	l = e_xml_get_child_by_name (clipboard, CC2XML ("Objects"));
	if (l != NULL)
		for (l = l->xmlChildrenNode; l != NULL ; l = l->next)
			if (!xmlIsBlankNode (l))
				cr->objects = g_slist_prepend (cr->objects,
							       xml_read_sheet_object (ctxt, l));

	xml_parse_ctx_destroy (ctxt);
	xmlFreeDoc (doc);

 err:
	gnm_pop_C_locale (locale);

	return cr;
}

/*****************************************************************************/

/* These will be searched IN ORDER, so add new versions at the top */
static const struct {
	char const * const id;
	GnumericXMLVersion const version;
} GnumericVersions [] = {
	{ "http://www.gnumeric.org/v10.dtd", GNM_XML_V10 },	/* 1.0.3 */
	{ "http://www.gnumeric.org/v9.dtd", GNM_XML_V9 },	/* 0.73 */
	{ "http://www.gnumeric.org/v8.dtd", GNM_XML_V8 },	/* 0.71 */
	{ "http://www.gnome.org/gnumeric/v7", GNM_XML_V7 },	/* 0.66 */
	{ "http://www.gnome.org/gnumeric/v6", GNM_XML_V6 },	/* 0.62 */
	{ "http://www.gnome.org/gnumeric/v5", GNM_XML_V5 },
	{ "http://www.gnome.org/gnumeric/v4", GNM_XML_V4 },
	{ "http://www.gnome.org/gnumeric/v3", GNM_XML_V3 },
	{ "http://www.gnome.org/gnumeric/v2", GNM_XML_V2 },
	{ "http://www.gnome.org/gnumeric/", GNM_XML_V1 },
	{ NULL }
};

static xmlNsPtr
xml_check_version (xmlDocPtr doc, GnumericXMLVersion *version)
{
	xmlNsPtr gmr;
	int i;

	if (doc == NULL || doc->xmlRootNode == NULL)
		return NULL;

	/* Do a bit of checking, get the namespaces, and check the top elem.  */
	if (doc->xmlRootNode->name == NULL || strcmp (doc->xmlRootNode->name, "Workbook"))
		return NULL;

	for (i = 0 ; GnumericVersions [i].id != NULL ; ++i ) {
		gmr = xmlSearchNsByHref (doc, doc->xmlRootNode, CC2XML (GnumericVersions[i].id));
		if (gmr != NULL) {
			*version = GnumericVersions [i].version;
			return gmr;
		}
	}
	return NULL;
}

static void
xml_sheet_create (XmlParseContext *ctxt, xmlNodePtr node)
{
	if (strcmp (node->name, "Sheet")) {
		g_printerr ("xml_sheet_create: invalid element type %s, 'Sheet' expected\n",
			    node->name);
	} else {
		xmlChar *name = xml_node_get_cstr (
			e_xml_get_child_by_name (node, CC2XML ("Name")), NULL);

		if (name == NULL) {
			char *tmp = workbook_sheet_get_free_name (ctxt->wb,
					_("Sheet"), TRUE, TRUE);
			name = xmlStrdup (CC2XML (tmp));
			g_free (tmp);
		}

		g_return_if_fail (name != NULL);

		workbook_sheet_attach (ctxt->wb,
				       sheet_new (ctxt->wb, CXML2C (name)));
		xmlFree (name);
	}
}

static gint
xml_get_n_children (xmlNodePtr tree)
{
	gint n = 0;
	xmlNodePtr node;

	for (node = tree->xmlChildrenNode; node != NULL; node = node->next)
		n++;

	return n;
}

static gint
xml_read_sheet_n_elements (xmlNodePtr tree)
{
	gint n = 0;
	xmlNodePtr node;

	node = e_xml_get_child_by_name (tree, CC2XML ("Styles"));
	if (node != NULL) {
		n += xml_get_n_children (node);
	}
	node = e_xml_get_child_by_name (tree, CC2XML ("Cells"));
	if (node != NULL) {
		n += xml_get_n_children (node);
	}

	return n;
}

static gint
xml_read_workbook_n_elements (xmlNodePtr tree)
{
	xmlNodePtr node;
	gint n = 0;

	for (node = tree->xmlChildrenNode; node != NULL; node = node->next) {
		if (node->name != NULL && strcmp (node->name, "Sheet") == 0) {
			n += xml_read_sheet_n_elements (node);
		}
	}

	return n;
}

/*
 * Create a Workbook equivalent to the XML subtree of doc.
 */
static gboolean
xml_workbook_read (IOContext *context,
		   XmlParseContext *ctxt, xmlNodePtr tree)
{
	Sheet     *sheet;
	GnmLocale *locale;
	xmlChar   *str;
	xmlNodePtr child, c;

	if (strcmp (tree->name, "Workbook")){
		g_warning ("xml_workbook_read: invalid element type %s, 'Workbook' expected`\n",
			   tree->name);
		return FALSE;
	}

	locale = gnm_push_C_locale ();

	child = e_xml_get_child_by_name (tree, CC2XML ("MetaData"));
	if (child)
		dom_read_meta_data (ctxt, child, go_doc_get_meta_data (GO_DOC (ctxt->wb)));

        /* DEPRECATED, moved to Calculation and expanded in 1.7.11
	 * Valid value == 1904, anything else == Lotus:1900  */
	child = e_xml_get_child_by_name (tree, CC2XML ("DateConvention"));
	if (child != NULL) {
		int convention;
		if (xml_node_get_int (child, NULL, &convention) && convention == 1904)
			workbook_set_1904 (ctxt->wb, TRUE);
	}

	child = e_xml_get_child_by_name (tree, CC2XML ("Geometry"));
	if (child) {
		int width, height;

		if (xml_node_get_int (child, "Width", &width) &&
		    xml_node_get_int (child, "Height", &height))
			wb_view_preferred_size (ctxt->wb_view, width, height);
	}

/*	child = xml_search_child (tree, "Style");
	if (child != NULL)
	xml_read_style (ctxt, child, &wb->style, FALSE);*/

	child = e_xml_get_child_by_name (tree, CC2XML ("Sheets"));
	if (child == NULL)
		return FALSE;

	io_progress_message (context, _("Processing file..."));
	io_progress_range_push (context, 0.5, 1.0);
	count_io_progress_set (context, xml_read_workbook_n_elements (child),
			       N_ELEMENTS_BETWEEN_UPDATES);
	ctxt->io_context = context;

	/*
	 * Pass 1: Create all the sheets, to make sure
	 * all of the references to forward sheets are properly
	 * handled
	 */
	for (c = child->xmlChildrenNode; c != NULL ; c = c->next)
		if (!xmlIsBlankNode (c))
			xml_sheet_create (ctxt, c);

	/*
	 * Now read names which can have inter-sheet references
	 * to these sheet titles
	 */
	xml_read_names (ctxt, tree, ctxt->wb, NULL);

	/*
	 * Pass 2: read the contents
	 */
	for (c = child->xmlChildrenNode; c != NULL ; c = c->next)
		if (!xmlIsBlankNode (c))
			sheet = xml_sheet_read (ctxt, c);

	io_progress_unset (context);
	io_progress_range_pop (context);

	child = e_xml_get_child_by_name (tree, CC2XML ("Attributes"));
	if (child && ctxt->version >= GNM_XML_V5)
		xml_read_wbv_attributes (ctxt, child);

	child = e_xml_get_child_by_name (tree, CC2XML ("UIData"));
	if (child) {
		int sheet_index = 0;
		if (xml_node_get_int (child, "SelectedTab", &sheet_index)) {
			Sheet *sheet = workbook_sheet_by_index (ctxt->wb,
								sheet_index);
			if (sheet)
				wb_view_sheet_focus (ctxt->wb_view, sheet);
		}
	}

	child = e_xml_get_child_by_name (tree, CC2XML ("Calculation"));
	if (child != NULL) {
		gboolean b;
		int	 i;
		double	 d;

		if (xml_node_get_bool (child, "ManualRecalc", &b))
			workbook_set_recalcmode (ctxt->wb, !b);
		if (xml_node_get_bool   (child, "EnableIteration", &b))
			workbook_iteration_enabled (ctxt->wb, b);
		if (xml_node_get_int    (child, "MaxIterations", &i))
			workbook_iteration_max_number (ctxt->wb, i);
		if (xml_node_get_double (child, "IterationTolerance", &d))
			workbook_iteration_tolerance (ctxt->wb, d);
		if (NULL != (str = xml_node_get_cstr (child, "DateConvention"))) {
			workbook_set_1904 (ctxt->wb,
				strcmp (str, "Apple:1904") == 0);
			xmlFree (str);
		}
	}

	gnm_pop_C_locale (locale);

	workbook_queue_all_recalc (ctxt->wb);

	return TRUE;
}

static GsfInput *
maybe_gunzip (GsfInput *input)
{
	GsfInput *gzip = gsf_input_gzip_new (input, NULL);
	if (gzip) {
		g_object_unref (input);
		return gzip;
	} else {
		gsf_input_seek (input, 0, G_SEEK_SET);
		return input;
	}
}

static GsfInput *
maybe_convert (GsfInput *input, gboolean quiet)
{
	static char const *noencheader = "<?xml version=\"1.0\"?>";
	static char const *encheader = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
	const size_t nelen = strlen (noencheader);
	const size_t elen = strlen (encheader);
	guint8 const *buf;
	gsf_off_t input_size;
	GString the_buffer, *buffer = &the_buffer;
	guint ui;
	char *converted;
	char const *encoding;
	gboolean ok;
	gboolean any_numbered = FALSE;

	input_size = gsf_input_remaining (input);

	buf = gsf_input_read (input, nelen, NULL);
	if (!buf ||
	    strncmp (noencheader, (const char *)buf, nelen) != 0 ||
	    input_size >= (gsf_off_t)(G_MAXINT - elen))
		return input;

	input_size -= nelen;

	the_buffer.len = 0;
	the_buffer.allocated_len = input_size + elen + 1;
	the_buffer.str = g_try_malloc (the_buffer.allocated_len);
	if (!the_buffer.str)
		return input;

	g_string_append (buffer, encheader);
	ok = gsf_input_read (input, input_size, (guint8 *)buffer->str + elen) != NULL;
	gsf_input_seek (input, 0, G_SEEK_SET);
	if (!ok) {
		g_free (buffer->str);
		return input;
	}
	buffer->len = input_size + elen;
	buffer->str[buffer->len] = 0;

	for (ui = 0; ui < buffer->len; ui++) {
		if (buffer->str[ui] == '&' &&
		    buffer->str[ui + 1] == '#' &&
		    g_ascii_isdigit (buffer->str[ui + 2])) {
			guint start = ui;
			guint c = 0;
			ui += 2;
			while (g_ascii_isdigit (buffer->str[ui])) {
				c = c * 10 + (buffer->str[ui] - '0');
				ui++;
			}
			if (buffer->str[ui] == ';' && c >= 128 && c <= 255) {
				buffer->str[start] = c;
				g_string_erase (buffer, start + 1, ui - start);
				ui = start;
			}
			any_numbered = TRUE;
		}
	}

	encoding = go_guess_encoding (buffer->str, buffer->len, NULL, &converted);
	if (encoding && !any_numbered &&
	    converted && strcmp (buffer->str, converted) == 0)
		quiet = TRUE;

	g_free (buffer->str);

	if (encoding) {
		g_object_unref (input);
		if (!quiet)
			g_warning ("Converted xml document with no explicit encoding from transliterated %s to UTF-8.",
				   encoding);
		return gsf_input_memory_new ((void *)converted, strlen (converted), TRUE);
	} else {
		if (!quiet)
			g_warning ("Failed to convert xml document with no explicit encoding to UTF-8.");
		return input;
	}
}

/* We parse and do some limited validation of the XML file, if this passes,
 * then we return TRUE
 */

typedef enum {
	XML_PROBE_STATE_PROBING,
	XML_PROBE_STATE_ERR,
	XML_PROBE_STATE_SUCCESS
} GnmXMLProbeState;

static void
xml_probe_start_element (GnmXMLProbeState *state, xmlChar const *name, xmlChar const **atts)
{
	int len = strlen (name);

	*state = XML_PROBE_STATE_ERR;
	if (len < 8)
		return;
	if (strcmp (name+len-8, "Workbook"))
		return;
	/* Do we want to get fancy and check namespace ? */
	*state = XML_PROBE_STATE_SUCCESS;
}

static void
xml_probe_problem (GnmXMLProbeState *state, char const *msg, ...)
{
	*state = XML_PROBE_STATE_ERR;
}

static IOContext *io_context = NULL;
static void
xml_dom_read_warning (gpointer state, char const *fmt, ...)
{
	va_list args;
	va_start (args, fmt);
	if (gnumeric_io_warning_occurred (io_context))
		gnumeric_io_error_push (io_context,
			error_info_new_vprintf (GO_ERROR, fmt, args));
	else
		gnm_io_warning_varargs (io_context, fmt, args);
	va_end (args);
}

static void
xml_dom_read_error (gpointer state, char const *fmt, ...)
{
	ErrorInfo *ei;
	va_list args;
	va_start (args, fmt);
	ei = error_info_new_vprintf (GO_ERROR, fmt, args);
	va_end (args);

	if (gnumeric_io_error_occurred (io_context))
		gnumeric_io_error_push (io_context, ei);
	else
		gnumeric_io_error_info_set (io_context, ei);
}


static xmlSAXHandler xml_sax_prober;
static gboolean
xml_probe (GOFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
{
	xmlParserCtxt *parse_context;
	GnmXMLProbeState is_gnumeric_xml = XML_PROBE_STATE_PROBING;
	char const *buf;

	if (pl == FILE_PROBE_FILE_NAME) {
		char const *name = gsf_input_name (input);
		int len;

		if (name == NULL)
			return FALSE;

		len = strlen (name);
		if (len >= 7 && !g_ascii_strcasecmp (name+len-7, ".xml.gz"))
			return TRUE;

		name = gsf_extension_pointer (name);

		return (name != NULL &&
			(g_ascii_strcasecmp (name, "gnumeric") == 0 ||
			 g_ascii_strcasecmp (name, "xml") == 0));
	}

/* probe by content */
	if (gsf_input_seek (input, 0, G_SEEK_SET))
		return FALSE;

	g_object_ref (input);
	input = maybe_gunzip (input);
	input = maybe_convert (input, TRUE);
	gsf_input_seek (input, 0, G_SEEK_SET);

	buf = gsf_input_read (input, 4, NULL);
	if (buf == NULL)
		goto unref_input;
	parse_context = xmlCreatePushParserCtxt (&xml_sax_prober, &is_gnumeric_xml,
		(char *)buf, 4, gsf_input_name (input));
	if (parse_context == NULL)
		goto unref_input;

	do {
		buf = gsf_input_read (input, 1, NULL);
		if (buf != NULL)
			xmlParseChunk (parse_context, (char *)buf, 1, 0);
		else
			is_gnumeric_xml = XML_PROBE_STATE_ERR;
	} while (is_gnumeric_xml == XML_PROBE_STATE_PROBING);

	xmlFreeParserCtxt (parse_context);

unref_input:
	g_object_unref (input);

	return is_gnumeric_xml == XML_PROBE_STATE_SUCCESS;
}

/*
 * Open an XML file and read a Workbook
 * One parse the XML file, getting a tree, then analyze the tree to build
 * the actual in-memory structure.
 */
static void
gnumeric_xml_read_workbook (GOFileOpener const *fo,
                            IOContext *context,
                            gpointer wb_view,
                            GsfInput *input)
{
	xmlParserCtxtPtr pctxt;
	xmlDocPtr res = NULL;
	xmlNsPtr gmr;
	XmlParseContext *ctxt;
	GnumericXMLVersion    version;
	guint8 const *buf;
	gsf_off_t size;
	gsf_off_t len;

	g_return_if_fail (input != NULL);

	if (gsf_input_seek (input, 0, G_SEEK_SET))
		return;

	io_progress_message (context, _("Reading file..."));
	io_progress_range_push (context, 0.0, 0.5);

	g_object_ref (input);
	input = maybe_gunzip (input);
	input = maybe_convert (input, FALSE);
	gsf_input_seek (input, 0, G_SEEK_SET);

	value_io_progress_set (context, gsf_input_size (input), 0);

	buf = gsf_input_read (input, 4, NULL);
	size = gsf_input_remaining (input);
	if (buf) {
		pctxt = xmlCreatePushParserCtxt (NULL, NULL,
			(char *)buf, 4, gsf_input_name (input));
		/* ick global, no easy way to add user data to stock DOM parser
		 * and override the warnings */
		io_context = context;
		pctxt->sax->warning    = (warningSAXFunc) xml_dom_read_warning;
		pctxt->sax->error      = (errorSAXFunc) xml_dom_read_warning;
		pctxt->sax->fatalError = (fatalErrorSAXFunc) xml_dom_read_error;

		for (; size > 0 ; size -= len) {
			len = XML_INPUT_BUFFER_SIZE;
			if (len > size)
				len =  size;
			buf = gsf_input_read (input, len, NULL);
			if (buf == NULL)
				break;
			xmlParseChunk (pctxt, (char *)buf, len, 0);
			value_io_progress_update (context, gsf_input_tell (input));
		}
		xmlParseChunk (pctxt, (char *)buf, 0, 1);
		res = pctxt->myDoc;
		io_context = NULL;
		xmlFreeParserCtxt (pctxt);
	}

	g_object_unref (input);
	io_progress_unset (context);
	io_progress_range_pop (context);

	/* Do a bit of checking, get the namespaces, and check the top elem. */
	gmr = xml_check_version (res, &version);
	if (gmr == NULL) {
		if (res != NULL)
			xmlFreeDoc (res);
		go_cmd_context_error_import (GO_CMD_CONTEXT (context),
			_("The file is not a Gnumeric Workbook file"));
		return;
	}

	/* Parse the file */
	ctxt = xml_parse_ctx_new (res, gmr, wb_view);
	ctxt->version = version;
	xml_workbook_read (context, ctxt, res->xmlRootNode);
	workbook_set_saveinfo (wb_view_get_workbook (ctxt->wb_view),
		FILE_FL_AUTO, go_file_saver_for_id ("Gnumeric_XmlIO:sax"));

	xml_parse_ctx_destroy (ctxt);
	xmlFreeDoc (res);
}

void
xml_init (void)
{
	GOFileSaver *saver;
	GSList *suffixes = go_slist_create (g_strdup ("gnumeric"), g_strdup ("xml"), NULL);
	GSList *mimes = go_slist_create (g_strdup ("application/x-gnumeric"), NULL);
	xml_sax_prober.comment    = NULL;
	xml_sax_prober.warning    = NULL;
	xml_sax_prober.error      = (errorSAXFunc) xml_probe_problem;
	xml_sax_prober.fatalError = (fatalErrorSAXFunc) xml_probe_problem;
	xml_sax_prober.startElement = (startElementSAXFunc) xml_probe_start_element;
#warning REMOVE for 2.0
	go_file_opener_register (go_file_opener_new (
		"Gnumeric_XmlIO:dom",
		_("Gnumeric XML (*.gnumeric) Old slow importer"),
		suffixes, mimes,
		xml_probe, gnumeric_xml_read_workbook), 40);

	go_file_opener_register (go_file_opener_new (
		"Gnumeric_XmlIO:sax",
		_("Gnumeric XML (*.gnumeric)"),
		suffixes, mimes,
		xml_probe, gnm_xml_file_open), 50);

	saver = go_file_saver_new ("Gnumeric_XmlIO:sax",
				   "gnumeric",
				   _("Gnumeric XML (*.gnumeric)"),
				   FILE_FL_AUTO, gnm_xml_file_save);
	/*
	 * The following avoids a hard dep on goffice trunk.  When we
	 * otherwise require that, just do the g_object_set.
	 */
	{
		GParamSpec *pspec = g_object_class_find_property
			(G_OBJECT_GET_CLASS (saver), "mime-type");
		if (pspec)
			g_object_set (G_OBJECT (saver),
				      "mime-type", "application/x-gnumeric",
				      NULL);
	}

	go_file_saver_register_as_default (saver, 50);
}

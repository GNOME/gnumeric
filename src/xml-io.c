/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * xml-io.c: save/read gnumeric workbooks using gnumeric-1.0 style xml.
 *
 * Authors:
 *   Daniel Veillard <Daniel.Veillard@w3.org>
 *   Miguel de Icaza <miguel@gnu.org>
 *   Jody Goldberg <jody@gnome.org>
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 */
#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnumeric.h"
#include "xml-io.h"
#include "xml-io-version.h"

#include "style-color.h"
#include "style-border.h"
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
#include "file.h"
#include "expr.h"
#include "expr-impl.h"
#include "expr-name.h"
#include "cell.h"
#include "value.h"
#include "validation.h"
#include "sheet-merge.h"
#include "sheet-filter.h"
#include "io-context.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook-priv.h" /* Workbook::names */
#include "selection.h"
#include "clipboard.h"
#include "format.h"
#include "ranges.h"
#include "file.h"
#include "str.h"
#include "hlink.h"
#include "gutils.h"
#include "plugin-util.h"
#include "gnumeric-gconf.h"

#include <goffice/utils/go-locale.h>
#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <gsf/gsf-input-gzip.h>
#include <gsf/gsf-output.h>
#include <gsf/gsf-output-gzip.h>
#include <gsf/gsf-utils.h>
#include <gsf/gsf-input-memory.h>

#include <locale.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <limits.h>

/* Precision to use when saving point measures. */
#define POINT_SIZE_PRECISION 4

/* FIXME - tune the values below */
#define XML_INPUT_BUFFER_SIZE      4096
#define N_ELEMENTS_BETWEEN_UPDATES 20

#define CC2XML(s) ((const xmlChar *)(s))
#define C2XML(s) ((xmlChar *)(s))
#define CXML2C(s) ((const char *)(s))
#define XML2C(s) ((char *)(s))

/* ------------------------------------------------------------------------- */

static GnmExprConventions *
xml_io_conventions (void)
{
	GnmExprConventions *res = gnm_expr_conventions_new ();

	res->decimal_sep_dot = TRUE;
	res->ref_parser = gnm_1_0_rangeref_parse;
	res->range_sep_colon = TRUE;
	res->sheet_sep_exclamation = TRUE;
	res->dots_in_names = TRUE;
	res->output_sheet_name_sep = "!";
	res->output_argument_sep = ",";
	res->output_array_col_sep = ",";
	res->output_translated = FALSE;
	res->unknown_function_handler = gnm_func_placeholder_factory;

	return res;
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
	ctxt->version      = GNUM_XML_V9;
	ctxt->doc          = doc;
	ctxt->ns           = ns;
	ctxt->expr_map     = g_hash_table_new (g_direct_hash, g_direct_equal);
	ctxt->shared_exprs = g_ptr_array_new ();
	ctxt->wb_view      = wb_view;
	ctxt->wb	   = (wb_view != NULL) ? wb_view_workbook (wb_view) : NULL;
	ctxt->exprconv     = xml_io_conventions ();

	return ctxt;
}

void
xml_parse_ctx_destroy (XmlParseContext *ctxt)
{
	g_return_if_fail (ctxt != NULL);

	g_hash_table_destroy (ctxt->expr_map);
	g_ptr_array_free (ctxt->shared_exprs, TRUE);
	gnm_expr_conventions_free (ctxt->exprconv);

	g_free (ctxt);
}

/*************************************************************************/

xmlNode *
e_xml_get_child_by_name (xmlNode const *parent, char const *child_name)
{
	xmlNode *child;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (child_name != NULL, NULL);

	for (child = parent->xmlChildrenNode; child != NULL; child = child->next) {
		if (xmlStrcmp (child->name, child_name) == 0) {
			return child;
		}
	}
	return NULL;
}

xmlNode *
e_xml_get_child_by_name_no_lang (xmlNode const *parent, char const *name)
{
	xmlNodePtr node;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (node = parent->xmlChildrenNode; node != NULL; node = node->next) {
		xmlChar *lang;

		if (node->name == NULL || strcmp (node->name, name) != 0) {
			continue;
		}
		lang = xmlGetProp (node, "xml:lang");
		if (lang == NULL) {
			return node;
		}
		xmlFree (lang);
	}

	return NULL;
}


xmlNode *
e_xml_get_child_by_name_by_lang (const xmlNode *parent, const gchar *name)
{
	xmlNodePtr   best_node = NULL, node;
	gint         best_lang_score = INT_MAX;
	GList const *lang_list = go_locale_languages ();

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (node = parent->xmlChildrenNode; node != NULL; node = node->next) {
		xmlChar *lang;

		if (node->name == NULL || strcmp (node->name, name) != 0)
			continue;

		lang = xmlGetProp (node, "xml:lang");
		if (lang != NULL) {
			const GList *l;
			gint i;

			for (l = lang_list, i = 0;
			     l != NULL && i < best_lang_score;
			     l = l->next, i++) {
				if (strcmp ((gchar *) l->data, lang) == 0) {
					best_node = node;
					best_lang_score = i;
				}
			}
		} else if (best_node == NULL)
			best_node = node;

		xmlFree (lang);
		if (best_lang_score == 0) 
			return best_node;
	}

	return best_node;
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
		c->color.red, c->color.green, c->color.blue);
}

void
gnm_xml_out_add_cellpos (GsfXMLOut *o, char const *id, GnmCellPos const *p)
{
	g_return_if_fail (p != NULL);
	gsf_xml_out_add_cstr_unchecked (o, id, cellpos_as_string (p));
}

/*****************************************************************************/
/* Get an xmlChar * value for a node carried as an attibute
 * result must be xmlFree
 */
xmlChar *
xml_node_get_cstr (xmlNodePtr node, char const *name)
{
	if (name != NULL)
		return xmlGetProp (node, CC2XML (name));
	/* in libxml1 <foo/> would return NULL
	 * in libxml2 <foo/> would return ""
	 */
	if (node->xmlChildrenNode != NULL)
		return xmlNodeGetContent (node);
	return NULL;
}
void
xml_node_set_cstr (xmlNodePtr node, char const *name, char const *val)
{
	if (name)
		xmlSetProp (node, CC2XML (name), CC2XML (val));
	else
		xmlNodeSetContent (node, CC2XML (val));
}

gboolean
xml_node_get_bool (xmlNodePtr node, char const *name, gboolean *val)
{
	xmlChar *buf = xml_node_get_cstr (node, name);
	if (buf == NULL)
		return FALSE;

	*val = (!strcmp (buf, "1")
		|| 0 == g_ascii_strcasecmp (buf, "true"));
	g_free (buf);
	return TRUE;
}

void
xml_node_set_bool (xmlNodePtr node, char const *name, gboolean val)
{
	xml_node_set_cstr (node, name, val ? "true" : "false");
}

gboolean
xml_node_get_int (xmlNodePtr node, char const *name, int *val)
{
	xmlChar *buf;
	char *end;

	buf = xml_node_get_cstr (node, name);
	if (buf == NULL)
		return FALSE;

	errno = 0; /* strto(ld) sets errno, but does not clear it.  */
	*val = strtol (CXML2C (buf), &end, 10);
	xmlFree (buf);

	/* FIXME: it is, strictly speaking, not valid to use buf here.  */
	return (CXML2C (buf) != end) && (errno != ERANGE);
}

void
xml_node_set_int (xmlNodePtr node, char const *name, int val)
{
	char str[4 * sizeof (int)];
	sprintf (str, "%d", val);
	xml_node_set_cstr (node, name, str);
}

gboolean
xml_node_get_double (xmlNodePtr node, char const *name, double *val)
{
	xmlChar *buf;
	char *end;

	buf = xml_node_get_cstr (node, name);
	if (buf == NULL)
		return FALSE;

	errno = 0; /* strto(ld) sets errno, but does not clear it.  */
	*val = strtod (CXML2C (buf), &end);
	xmlFree (buf);

	/* FIXME: it is, strictly speaking, now valid to use buf here.  */
	return (CXML2C (buf) != end) && (errno != ERANGE);
}

void
xml_node_set_double (xmlNodePtr node, char const *name, double val,
		     int precision)
{
	char str[101 + DBL_DIG];

	if (precision < 0 || precision > DBL_DIG)
		precision = DBL_DIG;

	if (fabs (val) < 1e9 && fabs (val) > 1e-5)
		snprintf (str, 100 + DBL_DIG, "%.*g", precision, val);
	else
		snprintf (str, 100 + DBL_DIG, "%f", val);

	xml_node_set_cstr (node, name, str);
}

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
	type = (GnmValueType) atoi (str);
	vstr = g_strrstr (str, ":") + 1;

	value = value_new_from_string (type, vstr, NULL, FALSE);
	xmlFree (str);

	return value;
}

static void
xml_node_set_value (xmlNodePtr node, char const *name, GnmValue const *value)
{
        GString *str;

	/* Set value type. */
	str = g_string_new (NULL);
	g_string_append_printf (str, "%d:", value->type);

	/* Set value. */
        value_get_as_gstring (value, str, gnm_expr_conventions_default);
	xml_node_set_cstr (node, name, str->str);
	g_string_free (str, FALSE);
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

gboolean
xml_node_get_gocolor (xmlNodePtr node, char const *name, GOColor *res)
{
	xmlChar *color;
	int r, g, b;

	color = xmlGetProp (node, CC2XML (name));
	if (color == NULL)
		return FALSE;
	if (sscanf (CXML2C (color), "%X:%X:%X", &r, &g, &b) == 3) {
		r >>= 8;
		g >>= 8;
		b >>= 8;
		*res = RGBA_TO_UINT (r,g,b,0xff);
		xmlFree (color);
		return TRUE;
	}
	xmlFree (color);
	return FALSE;
}

void
xml_node_set_gocolor (xmlNodePtr node, char const *name, GOColor val)
{
	char str[4 * sizeof (val)];
	GdkColor tmp;
	go_color_to_gdk (val, &tmp);
	sprintf (str, "%X:%X:%X", tmp.red, tmp.green, tmp.blue);
	xml_node_set_cstr (node, name, str);
}

void
xml_node_set_color (xmlNodePtr node, char const *name, GnmColor const *val)
{
	char str[4 * sizeof (val->color)];
	sprintf (str, "%X:%X:%X", val->color.red, val->color.green,
		 val->color.blue);
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
xml_node_set_cellpos (xmlNodePtr node, char const *name, GnmCellPos const *val)
{
	xml_node_set_cstr (node, name, cellpos_as_string (val));
}

/*
 * Set a double value for a node with POINT_SIZE_PRECISION digits precision.
 */
static void
xml_node_set_points (xmlNodePtr node, char const *name, double val)
{
	xml_node_set_double (node, name, val, POINT_SIZE_PRECISION);
}

static void
xml_node_set_print_unit (xmlNodePtr node, char const *name,
			 PrintUnit const *pu)
{
	xmlNodePtr child = xmlNewChild (node, NULL, CC2XML (name), NULL);
	xml_node_set_points (child, "Points", pu->points);
	xml_node_set_cstr (child, "PrefUnit", pu->desired_display->abbr);
}

static void
xml_node_set_print_margins (xmlNodePtr node, char const *name,
			    double points)
{
	xmlNodePtr  child;

	if (name == NULL)
		return;

	child = xmlNewChild (node, NULL, CC2XML (name), NULL);
	xml_node_set_points (child, "Points", points);
	xml_node_set_cstr (child, "PrefUnit", "Pt");
}

static void
xml_node_get_print_unit (xmlNodePtr node, PrintUnit * const pu)
{
	gchar       *txt;

	g_return_if_fail (pu != NULL);
	g_return_if_fail (node != NULL);

	xml_node_get_double (node, "Points", &pu->points);
	txt = (gchar *)xmlGetProp  (node, CC2XML ("PrefUnit"));
	if (txt) {
		pu->desired_display = unit_name_to_unit (txt);
		xmlFree (txt);
	}
}

static void
xml_node_get_print_margin (xmlNodePtr node, double *points)
{
	g_return_if_fail (node != NULL);

	xml_node_get_double (node, "Points", points);
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
xml_node_set_range (xmlNodePtr tree, GnmRange const *r)
{
	g_return_if_fail (range_is_sane (r));

	xml_node_set_int (tree, "startCol", r->start.col);
	xml_node_set_int (tree, "startRow", r->start.row);
	xml_node_set_int (tree, "endCol",   r->end.col);
	xml_node_set_int (tree, "endRow",   r->end.row);
}

static void
xml_read_selection_info (XmlParseContext *ctxt, xmlNodePtr tree)
{
	GnmRange r;
	GnmCellPos pos;
	SheetView *sv = sheet_get_view (ctxt->sheet, ctxt->wb_view);
	xmlNodePtr sel, selections = e_xml_get_child_by_name (tree, CC2XML ("Selections"));

	if (selections == NULL)
		return;

	sv_selection_reset (sv);
	for (sel = selections->xmlChildrenNode; sel; sel = sel->next)
		if (!xmlIsBlankNode (sel) && xml_node_get_range (sel, &r))
			sv_selection_add_range (sv,
						r.start.col, r.start.row,
						r.start.col, r.start.row,
						r.end.col, r.end.row);

	if (xml_node_get_int (selections, "CursorCol", &pos.col) &&
	    xml_node_get_int (selections, "CursorRow", &pos.row))
		sv_set_edit_pos (sv, &pos);
}

static void
xml_write_selection_info (XmlParseContext *ctxt, Sheet const *sheet,
			  xmlNodePtr tree)
{
	GList *ptr, *copy;
	SheetView *sv = sheet_get_view (sheet, ctxt->wb_view);

	tree = xmlNewChild (tree, ctxt->ns,
		CC2XML ("Selections"), NULL);

	/* Insert the selections in REVERSE order */
	copy = g_list_copy (sv->selections);
	ptr = g_list_reverse (copy);
	for (; ptr != NULL ; ptr = ptr->next) {
		GnmRange const *r = ptr->data;
		xmlNodePtr child = xmlNewChild (tree, ctxt->ns,
						CC2XML ("Selection"),
						NULL);
		xml_node_set_range (child, r);
	}
	g_list_free (copy);

	xml_node_set_int (tree, "CursorCol", sv->edit_pos_real.col);
	xml_node_set_int (tree, "CursorRow", sv->edit_pos_real.row);
}

/*
 * Create an XML subtree of doc equivalent to the given GnmBorder.
 */
static char const *StyleSideNames[6] =
{
 	"Top",
 	"Bottom",
 	"Left",
 	"Right",
	"Diagonal",
	"Rev-Diagonal"
};

static xmlNodePtr
xml_write_style_border (XmlParseContext *ctxt,
			const GnmStyle *style)
{
	xmlNodePtr cur;
	xmlNodePtr side;
	int        i;

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
		GnmBorder const *border;
		if (mstyle_is_element_set (style, i) &&
		    NULL != (border = mstyle_get_border (style, i))) {
			break;
		}
	}
	if (i > MSTYLE_BORDER_DIAGONAL)
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns,
			     CC2XML ("StyleBorder"), NULL);

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
		GnmBorder const *border;
		if (mstyle_is_element_set (style, i) &&
		    NULL != (border = mstyle_get_border (style, i))) {
			StyleBorderType t = border->line_type;
			GnmColor *col   = border->color;
 			side = xmlNewChild (cur, ctxt->ns,
					    CC2XML (StyleSideNames [i - MSTYLE_BORDER_TOP]),
 					    NULL);
			xml_node_set_int (side, "Style", t);
			if (t != STYLE_BORDER_NONE)
				xml_node_set_color (side, "Color", col);
 		}
	}
	return cur;
}

/*
 * Create a GnmBorder equivalent to the XML subtree of doc.
 */
static void
xml_read_style_border (XmlParseContext *ctxt, xmlNodePtr tree, GnmStyle *mstyle)
{
	xmlNodePtr side;
	int        i;

	if (strcmp (tree->name, "StyleBorder")){
		fprintf (stderr,
			 "xml_read_style_border: invalid element type %s, "
			 "'StyleBorder' expected`\n", tree->name);
	}

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
 		if ((side = e_xml_get_child_by_name (tree,
					      CC2XML (StyleSideNames [i - MSTYLE_BORDER_TOP]))) != NULL) {
			int		 t;
			GnmColor      *color = NULL;
			GnmBorder    *border;
			xml_node_get_int (side, "Style", &t);
			if (t != STYLE_BORDER_NONE)
				color = xml_node_get_color (side, "Color");
			border = style_border_fetch ((StyleBorderType)t, color,
						     style_border_get_orientation (i));
			mstyle_set_border (mstyle, i, border);
 		}
	}
}

/*
 * Create an XML subtree of doc equivalent to the given Style.
 */
xmlNodePtr
xml_write_style (XmlParseContext *ctxt,
		 GnmStyle *style)
{
	xmlNodePtr  cur, child;
	xmlChar    *tstr;
	GnmHLink   const *link;
	GnmValidation const *v;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Style"),
			     NULL);

	if (mstyle_is_element_set (style, MSTYLE_ALIGN_H))
		xml_node_set_int (cur, "HAlign", mstyle_get_align_h (style));
	if (mstyle_is_element_set (style, MSTYLE_ALIGN_V))
		xml_node_set_int (cur, "VAlign", mstyle_get_align_v (style));
	if (mstyle_is_element_set (style, MSTYLE_WRAP_TEXT))
		xml_node_set_int (cur, "WrapText",
				  mstyle_get_wrap_text (style));
	if (mstyle_is_element_set (style, MSTYLE_SHRINK_TO_FIT))
		xml_node_set_int (cur, "ShrinkToFit",
				  mstyle_get_shrink_to_fit (style));
	if (mstyle_is_element_set (style, MSTYLE_ROTATION))
		xml_node_set_int (cur, "Rotation", mstyle_get_rotation (style));
	if (mstyle_is_element_set (style, MSTYLE_PATTERN))
		xml_node_set_int (cur, "Shade", mstyle_get_pattern (style));
	if (mstyle_is_element_set (style, MSTYLE_INDENT))
		xml_node_set_int (cur, "Indent", mstyle_get_indent (style));
	if (mstyle_is_element_set (style, MSTYLE_CONTENT_LOCKED))
		xml_node_set_int (cur, "Locked",
				  mstyle_get_content_locked (style));
	if (mstyle_is_element_set (style, MSTYLE_CONTENT_HIDDEN))
		xml_node_set_int (cur, "Hidden",
				  mstyle_get_content_hidden (style));

	if (mstyle_is_element_set (style, MSTYLE_COLOR_FORE))
		xml_node_set_color (cur, "Fore",
				    mstyle_get_color (style, MSTYLE_COLOR_FORE));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_BACK))
		xml_node_set_color (cur, "Back",
				    mstyle_get_color (style, MSTYLE_COLOR_BACK));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_PATTERN))
		xml_node_set_color (cur, "PatternColor",
				    mstyle_get_color (style, MSTYLE_COLOR_PATTERN));
	if (mstyle_is_element_set (style, MSTYLE_FORMAT)) {
		char *fmt = style_format_as_XL (mstyle_get_format (style), FALSE);
		xml_node_set_cstr (cur, "Format", fmt);
		g_free (fmt);
	}

	if (mstyle_is_element_set (style, MSTYLE_FONT_NAME) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_SIZE) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_BOLD) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_ITALIC) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_UNDERLINE) ||
	    mstyle_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH)) {
		char const *fontname;

		if (mstyle_is_element_set (style, MSTYLE_FONT_NAME))
			fontname = mstyle_get_font_name (style);
		else /* backwards compatibility */
			fontname = "Helvetica";

		tstr = xmlEncodeEntitiesReentrant (ctxt->doc,
						   CC2XML (fontname));
		child = xmlNewChild (cur, ctxt->ns, CC2XML ("Font"),
				     tstr);
		if (tstr) xmlFree (tstr);

		if (mstyle_is_element_set (style, MSTYLE_FONT_SIZE))
			xml_node_set_points (child, "Unit",
					      mstyle_get_font_size (style));
		if (mstyle_is_element_set (style, MSTYLE_FONT_BOLD))
			xml_node_set_int (child, "Bold",
					   mstyle_get_font_bold (style));
		if (mstyle_is_element_set (style, MSTYLE_FONT_ITALIC))
			xml_node_set_int (child, "Italic",
					   mstyle_get_font_italic (style));
		if (mstyle_is_element_set (style, MSTYLE_FONT_UNDERLINE))
			xml_node_set_int (child, "Underline",
					   (int)mstyle_get_font_uline (style));
		if (mstyle_is_element_set (style, MSTYLE_FONT_STRIKETHROUGH))
			xml_node_set_int (child, "StrikeThrough",
					   mstyle_get_font_strike (style));
	}

	if ((link = mstyle_get_hlink (style)) != NULL) {
		child = xmlNewChild (cur, ctxt->ns,
			CC2XML ("HyperLink"), NULL);
		xml_node_set_cstr (child, "type",
			g_type_name (G_OBJECT_TYPE (link)));
		xml_node_set_cstr (child, "target",
			(const char *)gnm_hlink_get_target (link));

		if (gnm_hlink_get_tip (link) != NULL)
			xml_node_set_cstr (child, "tip",
				(const char *)gnm_hlink_get_tip (link));
	}

	v = mstyle_get_validation (style);
	if (v != NULL) {
		GnmParsePos    pp;
		char	   *tmp;

		child = xmlNewChild (cur, ctxt->ns,
				     CC2XML ("Validation"), NULL);
		xml_node_set_int (child, "Style", v->style);
		xml_node_set_int (child, "Type", v->type);
		switch (v->type) {
		case VALIDATION_TYPE_AS_INT :
		case VALIDATION_TYPE_AS_NUMBER :
		case VALIDATION_TYPE_AS_DATE :
		case VALIDATION_TYPE_AS_TIME :
		case VALIDATION_TYPE_TEXT_LENGTH :
			xml_node_set_int (child, "Operator", v->op);
			break;
		default :
			break;
		}

		xml_node_set_bool (child, "AllowBlank", v->allow_blank);
		xml_node_set_bool (child, "UseDropdown", v->use_dropdown);

		if (v->title != NULL && v->title->str[0] != '\0')
			xml_node_set_cstr (child, "Title", v->title->str);
		if (v->msg != NULL && v->msg->str[0] != '\0')
			xml_node_set_cstr (child, "Message", v->msg->str);

		parse_pos_init_sheet (&pp, ctxt->sheet);
		if (v->expr[0] != NULL &&
		    (tmp = gnm_expr_as_string (v->expr[0], &pp, ctxt->exprconv)) != NULL) {
			xmlNewChild (child, child->ns, CC2XML ("Expression0"), CC2XML (tmp));
			g_free (tmp);
		}
		if (v->expr[1] != NULL &&
		    (tmp = gnm_expr_as_string (v->expr[1], &pp, ctxt->exprconv)) != NULL) {
			xmlNewChild (child, child->ns, CC2XML ("Expression1"), CC2XML (tmp));
			g_free (tmp);
		}
	}

	child = xml_write_style_border (ctxt, style);
	if (child)
		xmlAddChild (cur, child);

	return cur;
}

typedef struct {
	xmlNode *names;
	XmlParseContext *ctxt;
} XMLWriteNames;

static void
cb_xml_write_name (gpointer key, GnmNamedExpr *nexpr, XMLWriteNames *user)
{
	xmlNodePtr  nameNode;
	xmlChar *txt;
	char *expr_str;

	g_return_if_fail (nexpr != NULL);

	nameNode = xmlNewChild (user->names, user->ctxt->ns, CC2XML ("Name"), NULL);
	txt = xmlEncodeEntitiesReentrant (user->ctxt->doc, CC2XML (nexpr->name->str));
	xmlNewChild (nameNode, user->ctxt->ns, CC2XML ("name"), txt);
	if (txt)
		xmlFree (txt);

	expr_str = expr_name_as_string (nexpr, NULL, user->ctxt->exprconv);
	txt = xmlEncodeEntitiesReentrant (user->ctxt->doc, CC2XML (expr_str));
	xmlNewChild (nameNode, user->ctxt->ns, CC2XML ("value"), txt);
	if (txt)
		xmlFree (txt);
	g_free (expr_str);

	xmlNewChild (nameNode, user->ctxt->ns, CC2XML ("position"),
		CC2XML (cellpos_as_string (&nexpr->pos.eval)));
}

static xmlNodePtr
xml_write_names (XmlParseContext *ctxt, GnmNamedExprCollection *scope)
{
	XMLWriteNames user;

	if (scope == NULL)
		return NULL;

	user.ctxt  = ctxt;
	user.names = xmlNewDocNode (ctxt->doc, ctxt->ns,
				    CC2XML ("Names"), NULL);
	g_hash_table_foreach (scope->names, (GHFunc) cb_xml_write_name, &user);
	return user.names;
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
	GnmExpr const *expr;

	if (name == NULL)
		return;

	for (name = name->xmlChildrenNode; name ; name = name->next) {
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
		expr = gnm_expr_parse_str (CXML2C (expr_str), &pp,
					   GNM_EXPR_PARSE_DEFAULT,
					   ctxt->exprconv, &perr);
		if (exp != NULL) {
			char *err = NULL;
			expr_name_add (&pp, CXML2C (name_str), expr, &err, TRUE);
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

static xmlNodePtr
xml_write_summary (XmlParseContext *ctxt, SummaryInfo *summary_info)
{
	GList *items, *m;
	xmlChar *tstr;
	xmlNodePtr cur;

	if (!summary_info)
		return NULL;

	m = items = summary_info_as_list (summary_info);

	if (!items)
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Summary"), NULL);

	while (items) {
		xmlNodePtr   tmp;
		SummaryItem *sit = items->data;
		if (sit) {
			xmlChar *text;

			tmp = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Item"), NULL);
			tstr = xmlEncodeEntitiesReentrant (ctxt->doc, CC2XML (sit->name));
			xmlNewChild (tmp, ctxt->ns, CC2XML ("name"), tstr);
			if (tstr) xmlFree (tstr);

			if (sit->type == SUMMARY_INT) {
				text = C2XML (g_strdup_printf ("%d", sit->v.i));
				tstr = xmlEncodeEntitiesReentrant (ctxt->doc, text);
				xmlNewChild (tmp, ctxt->ns, CC2XML ("val-int"), tstr);
				if (tstr) xmlFree (tstr);
			} else {
				text = C2XML (summary_item_as_text (sit));
				tstr = xmlEncodeEntitiesReentrant (ctxt->doc, text);
				xmlNewChild (tmp, ctxt->ns, CC2XML ("val-string"), tstr);
				if (tstr) xmlFree (tstr);
			}
			g_free (text);
			xmlAddChild (cur, tmp);
		}
		items = g_list_next (items);
	}
	g_list_free (m);
	return cur;
}

static void
xml_read_summary (XmlParseContext *ctxt, xmlNodePtr tree, SummaryInfo *summary_info)
{
	xmlNodePtr child;

	g_return_if_fail (ctxt != NULL);
	g_return_if_fail (tree != NULL);
	g_return_if_fail (summary_info != NULL);

	for (child = tree->xmlChildrenNode; child != NULL ; child = child->next)
		if (!xmlIsBlankNode (child) && child->name && !strcmp (child->name, "Item")) {
			xmlNodePtr bits;
			xmlChar *name = NULL;

			for (bits = child->xmlChildrenNode; bits != NULL ; bits = bits->next) {
				SummaryItem *sit = NULL;

				if (xmlIsBlankNode (bits))
					continue;

				if (!strcmp (bits->name, "name")) {
					name = xml_node_get_cstr (bits, NULL);
				} else {
					xmlChar *txt;
					g_return_if_fail (name);

					txt = xml_node_get_cstr (bits, NULL);
					if (txt != NULL){
						if (!strcmp (bits->name, "val-string"))
							sit = summary_item_new_string (CXML2C (name),
										       CXML2C (txt),
										       TRUE);
						else if (!strcmp (bits->name, "val-int"))
							sit = summary_item_new_int (CXML2C (name),
										    atoi (CXML2C (txt)));

						if (sit)
							summary_info_add (summary_info, sit);
						xmlFree (txt);
					}
				}
			}
			if (name) {
				xmlFree (name);
				name = NULL;
			}
		}
}

static void
xml_node_set_print_hf (xmlNodePtr node, char const *name,
		       PrintHF const *hf)
{
	xmlNodePtr  child;

	if (hf == NULL || name == NULL)
		return;

	child = xmlNewChild (node, NULL, CC2XML (name), NULL);
	xml_node_set_cstr (child, "Left", hf->left_format);
	xml_node_set_cstr (child, "Middle", hf->middle_format);
	xml_node_set_cstr (child, "Right", hf->right_format);
}

static void
xml_node_get_print_hf (xmlNodePtr node, PrintHF *hf)
{
	xmlChar *txt;

	g_return_if_fail (hf != NULL);
	g_return_if_fail (node != NULL);

	txt = xmlGetProp (node, CC2XML ("Left"));
	if (txt) {
		if (hf->left_format)
			g_free (hf->left_format);
		hf->left_format = g_strdup ((gchar *)txt);
		xmlFree (txt);
	}

	txt = xmlGetProp (node, CC2XML ("Middle"));
	if (txt) {
		if (hf->middle_format)
			g_free (hf->middle_format);
		hf->middle_format = g_strdup ((gchar *)txt);
		xmlFree (txt);
	}

	txt = xmlGetProp (node, CC2XML ("Right"));
	if (txt) {
		if (hf->right_format)
			g_free (hf->right_format);
		hf->right_format = g_strdup ((gchar *)txt);
		xmlFree (txt);
	}
}

static void
xml_write_attribute (xmlNode *parent, char const *name, char const *value)
{
	xmlNodePtr attr = xmlNewChild (parent, parent->ns, CC2XML ("Attribute"), NULL);

	/* backwards compatibility with 1.0.x which uses gtk-1.2 GTK_TYPE_BOOLEAN */
	xmlNewChild (attr, attr->ns, CC2XML ("type"), CC2XML ("4"));
	xmlNewChild (attr, attr->ns, CC2XML ("name"), CC2XML (name));
	xmlNewChild (attr, attr->ns, CC2XML ("value"), CC2XML (value));
}

static xmlNodePtr
xml_write_wbv_attributes (XmlParseContext *ctxt)
{
	xmlNodePtr attributes = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Attributes"), NULL);
	xml_write_attribute (attributes, "WorkbookView::show_horizontal_scrollbar",
		ctxt->wb_view->show_horizontal_scrollbar ? "TRUE" : "FALSE");
	xml_write_attribute (attributes, "WorkbookView::show_vertical_scrollbar",
		ctxt->wb_view->show_vertical_scrollbar ? "TRUE" : "FALSE");
	xml_write_attribute (attributes, "WorkbookView::show_notebook_tabs",
		ctxt->wb_view->show_notebook_tabs ? "TRUE" : "FALSE");
	xml_write_attribute (attributes, "WorkbookView::do_auto_completion",
		ctxt->wb_view->do_auto_completion ? "TRUE" : "FALSE");
	xml_write_attribute (attributes, "WorkbookView::is_protected",
		ctxt->wb_view->is_protected ? "TRUE" : "FALSE");
	return attributes;
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
	wb_view_prefs_update (ctxt->wb_view);
}

static xmlNodePtr
xml_write_print_repeat_range (XmlParseContext *ctxt,
			      char const *name,
			      PrintRepeatRange *range)
{
	xmlNodePtr cur;
	char const *s;

	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (range != NULL, NULL);

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML (name), NULL);
	s = (range->use) ? range_name (&range->range) : "";
	xml_node_set_cstr  (cur, "value", s);

	return cur;
}

static xmlNodePtr
xml_write_print_info (XmlParseContext *ctxt, PrintInformation *pi)
{
	xmlNodePtr cur, child;
	double header = 0, footer = 0, left = 0, right = 0;

	g_return_val_if_fail (pi != NULL, NULL);

	print_info_get_margins (pi, &header, &footer, &left, &right);
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("PrintInformation"), NULL);

	child = xmlNewChild (cur, ctxt->ns, CC2XML ("Margins"), NULL);
	xml_node_set_print_unit (child, "top",    &pi->margins.top);
	xml_node_set_print_unit (child, "bottom", &pi->margins.bottom);
	xml_node_set_print_margins (child, "left", left);
	xml_node_set_print_margins (child, "right", right);
	xml_node_set_print_margins (child, "header", header);
	xml_node_set_print_margins (child, "footer", footer);

	child = xmlNewChild (cur, ctxt->ns, CC2XML ("Scale"), NULL);
	if (pi->scaling.type == PERCENTAGE) {
		xml_node_set_cstr  (child, "type", "percentage");
		xml_node_set_double  (child, "percentage", pi->scaling.percentage.x, -1);
	} else {
		xml_node_set_cstr  (child, "type", "size_fit");
		xml_node_set_double  (child, "cols", pi->scaling.dim.cols, -1);
		xml_node_set_double  (child, "rows", pi->scaling.dim.rows, -1);
	}

	child = xmlNewChild (cur, ctxt->ns, CC2XML ("vcenter"), NULL);
	xml_node_set_int  (child, "value", pi->center_vertically);
	child = xmlNewChild (cur, ctxt->ns, CC2XML ("hcenter"), NULL);
	xml_node_set_int  (child, "value", pi->center_horizontally);

	child = xmlNewChild (cur, ctxt->ns, CC2XML ("grid"), NULL);
	xml_node_set_int  (child, "value",    pi->print_grid_lines);
	child = xmlNewChild (cur, ctxt->ns, CC2XML ("even_if_only_styles"), NULL);
	xml_node_set_int  (child, "value",    pi->print_even_if_only_styles);
	child = xmlNewChild (cur, ctxt->ns, CC2XML ("monochrome"), NULL);
	xml_node_set_int  (child, "value",    pi->print_black_and_white);
	child = xmlNewChild (cur, ctxt->ns, CC2XML ("draft"), NULL);
	xml_node_set_int  (child, "value",    pi->print_as_draft);
	child = xmlNewChild (cur, ctxt->ns, CC2XML ("titles"), NULL);
	xml_node_set_int  (child, "value",    pi->print_titles);

	child = xml_write_print_repeat_range (ctxt, "repeat_top", &pi->repeat_top);
	xmlAddChild (cur, child);

	child = xml_write_print_repeat_range (ctxt, "repeat_left", &pi->repeat_left);
	xmlAddChild (cur, child);

	xmlNewChild (cur, ctxt->ns, CC2XML ("order"),
		     CC2XML ((pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT)
		     ? "d_then_r" : "r_then_d"));
	xmlNewChild (cur, ctxt->ns, CC2XML ("orientation"),
		     CC2XML ((print_info_get_orientation (pi) 
			      == PRINT_ORIENT_VERTICAL)
		     ? "portrait" : "landscape"));

	xml_node_set_print_hf (cur, "Header", pi->header);
	xml_node_set_print_hf (cur, "Footer", pi->footer);
	if (NULL != print_info_get_paper (pi))
		xmlNewChild (cur, ctxt->ns, CC2XML ("paper"),
			CC2XML (print_info_get_paper (pi)));

	return cur;
}

/*
 * Earlier versions of Gnumeric confused top margin with header, bottom margin
 * with footer (see comment at top of print.c). We fix this by making sure
 * that top > header and bottom > footer.
 */
#define DSWAP(a,b) { double tmp; tmp = a; a = b; b = tmp; }
static void
xml_print_info_fix_margins (PrintInformation *pi)
{
	double header = 0, footer = 0, left = 0, right = 0;
	gboolean switched = FALSE;

	print_info_get_margins (pi, &header, &footer, &left, &right);

	if (pi->margins.top.points < header) {
		switched = TRUE;
		DSWAP (pi->margins.top.points, header);
	}
	if (pi->margins.bottom.points < footer) {
		switched = TRUE;
		DSWAP (pi->margins.bottom.points, footer);
	}
	if (switched)
		print_info_set_margins (pi, header, footer, left, right);
}

static void
xml_read_print_margins (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	PrintInformation *pi;
	double header = 0, footer = 0, left = 0, right = 0;

	g_return_if_fail (ctxt != NULL);
	g_return_if_fail (tree != NULL);
	g_return_if_fail (IS_SHEET (ctxt->sheet));

	pi = ctxt->sheet->print_info;

	g_return_if_fail (pi != NULL);

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("top"))))
		xml_node_get_print_unit (child, &pi->margins.top);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("bottom"))))
		xml_node_get_print_unit (child, &pi->margins.bottom);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("left"))))
		xml_node_get_print_margin (child, &left);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("right"))))
		xml_node_get_print_margin (child, &right);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("header"))))
		xml_node_get_print_margin (child, &header);
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("footer"))))
		xml_node_get_print_margin (child, &footer);

	print_info_set_margins (pi, header, footer, left, right);
	xml_print_info_fix_margins (pi);
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
	if (ctxt->version > GNUM_XML_V4 &&
	    (child = e_xml_get_child_by_name (tree, CC2XML (name)))) {
		xmlChar *s = xml_node_get_cstr (child, "value");

		if (s) {
			GnmRange r;
			if (parse_range (CXML2C (s), &r)) {
				range->range = r;
				range->use   = TRUE;
			}
			xmlFree (s);
		}
	}
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
				pi->scaling.type = PERCENTAGE;
				if (xml_node_get_double (child, "percentage", &tmp))
					pi->scaling.percentage.x =
						pi->scaling.percentage.y = tmp;
			} else {
				int cols, rows;
				pi->scaling.type = SIZE_FIT;
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
		pi->print_comments        = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, CC2XML ("titles")))) {
		xml_node_get_int  (child, "value",  &b);
		pi->print_titles          = (b == 1);
	}

	xml_read_print_repeat_range (ctxt, tree, "repeat_top",
				     &pi->repeat_top);
	xml_read_print_repeat_range (ctxt, tree, "repeat_left",
				     &pi->repeat_left);

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("order")))) {
		xmlChar *txt = xmlNodeGetContent (child);
		if (!strcmp (CXML2C (txt), "d_then_r"))
			pi->print_order = PRINT_ORDER_DOWN_THEN_RIGHT;
		else
			pi->print_order = PRINT_ORDER_RIGHT_THEN_DOWN;
		xmlFree (txt);
	}

	if ((child = e_xml_get_child_by_name (tree, CC2XML ("orientation")))) {
		xmlChar *txt = xmlNodeGetContent (child);
		if (!strcmp (CXML2C (txt), "portrait"))
			print_info_set_orientation (pi, PRINT_ORIENT_VERTICAL);
		else
			print_info_set_orientation (pi, PRINT_ORIENT_HORIZONTAL);
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
 * @mstyle: the style to setup to this font.
 * @fontname: an X11-like font name.
 *
 * Tries to guess the fontname, the weight and italization parameters
 * and setup mstyle
 *
 * Returns: A valid style font.
 */
static void
style_font_read_from_x11 (GnmStyle *mstyle, char const *fontname)
{
	char const *c;

	c = font_component (fontname, 2);
	if (strncmp (c, "bold", 4) == 0)
		mstyle_set_font_bold (mstyle, TRUE);

	c = font_component (fontname, 3);
	if (strncmp (c, "o", 1) == 0)
		mstyle_set_font_italic (mstyle, TRUE);

	if (strncmp (c, "i", 1) == 0)
		mstyle_set_font_italic (mstyle, TRUE);
}

/*
 * Create a Style equivalent to the XML subtree of doc.
 */
GnmStyle *
xml_read_style (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	xmlChar *prop;
	int val;
	GnmColor *c;
	GnmStyle     *mstyle;

	mstyle = (ctxt->version >= GNUM_XML_V6 ||
		  ctxt->version <= GNUM_XML_V2)
		? mstyle_new_default ()
		: mstyle_new ();

	if (strcmp (tree->name, "Style")) {
		fprintf (stderr,
			 "xml_read_style: invalid element type %s, 'Style' expected\n",
			 tree->name);
	}

	if (xml_node_get_int (tree, "HAlign", &val))
		mstyle_set_align_h (mstyle, val);

	if (ctxt->version >= GNUM_XML_V6) {
		if (xml_node_get_int (tree, "WrapText", &val))
			mstyle_set_wrap_text (mstyle, val);
		if (xml_node_get_int (tree, "ShrinkToFit", &val))
			mstyle_set_shrink_to_fit (mstyle, val);
	} else if (xml_node_get_int (tree, "Fit", &val))
		mstyle_set_wrap_text (mstyle, val);

	if (xml_node_get_int (tree, "Locked", &val))
		mstyle_set_content_locked (mstyle, val);
	if (xml_node_get_int (tree, "Hidden", &val))
		mstyle_set_content_hidden (mstyle, val);

	if (xml_node_get_int (tree, "VAlign", &val))
		mstyle_set_align_v (mstyle, val);

	if (xml_node_get_int (tree, "Rotation", &val))
		mstyle_set_rotation (mstyle, val);

	if (xml_node_get_int (tree, "Shade", &val))
		mstyle_set_pattern (mstyle, val);

	if (xml_node_get_int (tree, "Indent", &val))
		mstyle_set_indent (mstyle, val);

	if ((c = xml_node_get_color (tree, "Fore")) != NULL)
		mstyle_set_color (mstyle, MSTYLE_COLOR_FORE, c);

	if ((c = xml_node_get_color (tree, "Back")) != NULL)
		mstyle_set_color (mstyle, MSTYLE_COLOR_BACK, c);

	if ((c = xml_node_get_color (tree, "PatternColor")) != NULL)
		mstyle_set_color (mstyle, MSTYLE_COLOR_PATTERN, c);

	prop = xmlGetProp (tree, CC2XML ("Format"));
	if (prop != NULL) {
		mstyle_set_format_text (mstyle, CXML2C (prop));
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
				mstyle_set_font_size (mstyle, size_pts);

			if (xml_node_get_int (child, "Bold", &t))
				mstyle_set_font_bold (mstyle, t);

			if (xml_node_get_int (child, "Italic", &t))
				mstyle_set_font_italic (mstyle, t);

			if (xml_node_get_int (child, "Underline", &t))
				mstyle_set_font_uline (mstyle, (StyleUnderlineType)t);

			if (xml_node_get_int (child, "StrikeThrough", &t))
				mstyle_set_font_strike (mstyle, t ? TRUE : FALSE);

			font = xml_node_get_cstr (child, NULL);
			if (font) {
				if (*font == '-')
					style_font_read_from_x11 (mstyle, CXML2C (font));
				else
					mstyle_set_font_name (mstyle, CXML2C (font));
				xmlFree (font);
			}

		} else if (!strcmp (child->name, "StyleBorder")) {
			xml_read_style_border (ctxt, child, mstyle);
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
				mstyle_set_hlink (mstyle, link);
				xmlFree (target);
			}
			xmlFree (type);
		} else if (!strcmp (child->name, "Validation")) {
			int dummy;
			ValidationStyle style;
			ValidationType type;
			ValidationOp op = VALIDATION_OP_NONE;
			GnmParsePos     pp;
			xmlNode *e_node;
			xmlChar *title, *msg;
			gboolean allow_blank, use_dropdown;
			GnmExpr const *expr0 = NULL, *expr1 = NULL;

			xml_node_get_int (child, "Style", &dummy);
			style = dummy;
			xml_node_get_int (child, "Type", &dummy);
			type = dummy;
			if (xml_node_get_int (child, "Operator", &dummy))
				op = dummy;

			if (!xml_node_get_bool (child, "AllowBlank", &(allow_blank)))
				allow_blank = FALSE;
			if (!xml_node_get_bool (child, "UseDropdown", &(use_dropdown)))
				use_dropdown = FALSE;

			title = xml_node_get_cstr (child, "Title");
			msg = xml_node_get_cstr (child, "Message");

			parse_pos_init_sheet (&pp, ctxt->sheet);
			e_node = e_xml_get_child_by_name (child, CC2XML ("Expression0"));
			if (e_node != NULL) {
				xmlChar *content = xml_node_get_cstr (e_node, NULL);
				if (content != NULL) {
					expr0 = gnm_expr_parse_str (CXML2C (content), &pp,
								    GNM_EXPR_PARSE_DEFAULT,
								    ctxt->exprconv, NULL);
					xmlFree (content);
				}
			}
			e_node = e_xml_get_child_by_name (child, CC2XML ("Expression1"));
			if (e_node != NULL) {
				xmlChar *content = xml_node_get_cstr (e_node, NULL);
				if (content != NULL) {
					expr1 = gnm_expr_parse_str (CXML2C (content), &pp,
								    GNM_EXPR_PARSE_DEFAULT,
								    ctxt->exprconv, NULL);
					xmlFree (content);
				}
			}

			mstyle_set_validation (mstyle,
				validation_new (style, type, op, CXML2C (title),
					CXML2C (msg), expr0, expr1, allow_blank,
					use_dropdown));

			xmlFree (msg);
			xmlFree (title);
		} else {
			fprintf (stderr, "xml_read_style: unknown type '%s'\n",
				 child->name);
		}
	}

	return mstyle;
}

/*
 * Create an XML subtree of doc equivalent to the given GnmStyleRegion.
 */
static xmlNodePtr
xml_write_style_region (XmlParseContext *ctxt, GnmStyleRegion const *region)
{
	xmlNodePtr cur, child;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("StyleRegion"), NULL);
	xml_node_set_range (cur, &region->range);

	if (region->style != NULL) {
		child = xml_write_style (ctxt, region->style);
		if (child)
			xmlAddChild (cur, child);
	}
	return cur;
}

/*
 * Create a GnmStyleRegion equivalent to the XML subtree of doc.
 * Return an mstyle and a range in the @range parameter
 */
static GnmStyle*
xml_read_style_region_ex (XmlParseContext *ctxt, xmlNodePtr tree, GnmRange *range)
{
	xmlNodePtr child;
	GnmStyle    *style = NULL;

	if (strcmp (tree->name, "StyleRegion")){
		fprintf (stderr,
			 "xml_read_style_region_ex: invalid element type %s, 'StyleRegion' expected`\n",
			 tree->name);
		return NULL;
	}
	xml_node_get_range (tree, range);

	child = e_xml_get_child_by_name (tree, CC2XML ("Style"));
	if (child)
		style = xml_read_style (ctxt, child);

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
		if (ctxt->version >= GNUM_XML_V6)
			sheet_style_set_range (ctxt->sheet, &range, style);
		else
			sheet_style_apply_range (ctxt->sheet, &range, style);
	}
}

/*
 * Create an XML subtree of doc equivalent to the given ColRowInfo.
 */
typedef struct
{
	gboolean is_column;
	xmlNodePtr container;
	XmlParseContext *ctxt;

	ColRowInfo *previous;
	int rle_count;
} closure_write_colrow;

static gboolean
xml_write_colrow_info (ColRowInfo *info, void *user_data)
{
	closure_write_colrow * closure = user_data;
	xmlNodePtr cur;
	ColRowInfo const *prev = closure->previous;

	closure->rle_count++;
	if (colrow_equal (prev, info))
		return FALSE;

	if (prev != NULL) {
		if (closure->is_column)
			cur = xmlNewDocNode (closure->ctxt->doc,
					     closure->ctxt->ns, CC2XML ("ColInfo"), NULL);
		else
			cur = xmlNewDocNode (closure->ctxt->doc,
					     closure->ctxt->ns, CC2XML ("RowInfo"), NULL);

		if (cur != NULL) {
			xml_node_set_int (cur, "No", prev->pos);
			xml_node_set_points (cur, "Unit", prev->size_pts);
			xml_node_set_int (cur, "MarginA", prev->margin_a);
			xml_node_set_int (cur, "MarginB", prev->margin_b);
			if (prev->hard_size)
				xml_node_set_int (cur, "HardSize", TRUE);
			if (!prev->visible)
				xml_node_set_int (cur, "Hidden", TRUE);
			if (prev->is_collapsed)
				xml_node_set_int (cur, "Collapsed", TRUE);
			if (prev->outline_level > 0)
				xml_node_set_int (cur, "OutlineLevel", prev->outline_level);

			if (closure->rle_count > 1)
				xml_node_set_int (cur, "Count", closure->rle_count);
			xmlAddChild (closure->container, cur);
		}
	}
	closure->rle_count = 0;
	closure->previous = info;

	return FALSE;
}

/*
 * Create an XML subtree of doc equivalent to the given Cell.
 * set col and row to be the absolute coordinates
 */
static xmlNodePtr
xml_write_cell_and_position (XmlParseContext *ctxt, GnmCell const *cell, GnmParsePos const *pp)
{
	xmlNodePtr cellNode;
	xmlChar *tstr;
	GnmExprArray const *ar;
	gboolean write_contents = TRUE;
	gboolean const is_shared_expr =
	    (cell_has_expr (cell) && gnm_expr_is_shared (cell->base.expression));

	cellNode = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Cell"), NULL);
	xml_node_set_int (cellNode, "Col", pp->eval.col);
	xml_node_set_int (cellNode, "Row", pp->eval.row);

	/* Only the top left corner of an array needs to be saved (>= 0.53) */
	if (NULL != (ar = cell_is_array (cell)) && (ar->y != 0 || ar->x != 0))
		return cellNode;

	/* As of version 0.53 we save the ID of shared expressions */
	if (is_shared_expr) {
		gconstpointer const expr = cell->base.expression;
		gpointer id = g_hash_table_lookup (ctxt->expr_map, expr);

		if (id == NULL) {
			id = GINT_TO_POINTER (g_hash_table_size (ctxt->expr_map) + 1);
			g_hash_table_insert (ctxt->expr_map, (gpointer)expr, id);
		} else if (ar == NULL)
			write_contents = FALSE;

		xml_node_set_int (cellNode, "ExprID", GPOINTER_TO_INT (id));
	}

	if (write_contents) {
		GString *str = g_string_sized_new (1000);
		if (cell_has_expr (cell)) {
			g_string_append_c (str, '=');
			gnm_expr_as_gstring (str, cell->base.expression, pp, ctxt->exprconv);
		} else
			value_get_as_gstring (cell->value, str, ctxt->exprconv);
		tstr = xmlEncodeEntitiesReentrant (ctxt->doc, CC2XML (str->str));
		g_string_free (str, TRUE);

		xmlNodeSetContent (cellNode, CC2XML (tstr));
		if (tstr)
			xmlFree (tstr);

		if (!cell_has_expr (cell)) {
			g_return_val_if_fail (cell->value != NULL, cellNode);

			xml_node_set_int (cellNode, "ValueType",
				cell->value->type);

			if (VALUE_FMT (cell->value) != NULL) {
				char *fmt = style_format_as_XL (VALUE_FMT (cell->value), FALSE);
				xmlSetProp (cellNode, CC2XML ("ValueFormat"), CC2XML (fmt));
				g_free (fmt);
			}
		}
	}

	/* As of version 0.53 we save the size of the array as attributes */
	/* As of version 0.57 the attributes are in the Cell not the Content */
	if (ar != NULL) {
	        xml_node_set_int (cellNode, "Rows", ar->rows);
	        xml_node_set_int (cellNode, "Cols", ar->cols);
	}

	return cellNode;
}

/*
 * Create an XML subtree of doc equivalent to the given Cell.
 */
static xmlNodePtr
xml_write_cell (XmlParseContext *ctxt, GnmCell const *cell)
{
	GnmParsePos pp;
	return xml_write_cell_and_position (ctxt, cell,
		parse_pos_init_cell (&pp, cell));
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
	GnmExpr const *expr =
		gnm_expr_parse_str (text,
				    parse_pos_init_cell (&pp, cell),
				    GNM_EXPR_PARSE_DEFAULT,
				    ctxt->exprconv, NULL);

	g_return_if_fail (expr != NULL);
	cell_set_array_formula (cell->base.sheet,
				cell->pos.col, cell->pos.row,
				cell->pos.col + cols-1, cell->pos.row + rows-1,
				expr);
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
	GnmFormat *value_fmt = NULL;

	if (strcmp (tree->name, "Cell")) {
		fprintf (stderr,
		 "xml_read_cell: invalid element type %s, 'Cell' expected`\n",
			 tree->name);
		return NULL;
	}
	xml_node_get_int (tree, "Col", &col);
	xml_node_get_int (tree, "Row", &row);

	cell = sheet_cell_get (ctxt->sheet, col, row);
	if ((is_new_cell = (cell == NULL)))
		cell = sheet_cell_new (ctxt->sheet, col, row);
	if (cell == NULL)
		return NULL;

	if (ctxt->version < GNUM_XML_V3) {
		/*
		 * This style code is a gross anachronism that slugs performance
		 * in the common case this data won't exist. In the long term all
		 * files will make the 0.41 - 0.42 transition and this can go.
		 * Newer file format includes an index pointer to the Style
		 * Old format includes the Style online
		 */
		if (xml_node_get_int (tree, "Style", &style_idx)) {
			GnmStyle *mstyle;

			style_read = TRUE;
			mstyle = g_hash_table_lookup (ctxt->style_table,
						      GINT_TO_POINTER (style_idx));
			if (mstyle) {
				mstyle_ref (mstyle);
				sheet_style_set_pos (ctxt->sheet, col, row,
						     mstyle);
			} /* else reading a newer version with style_idx == 0 */
		}
	} else {
		/* Is this a post 0.52 shared expression */
		if (!xml_node_get_int (tree, "ExprID", &shared_expr_index))
			shared_expr_index = -1;

		/* Is this a post 0.57 formatted value */
		if (ctxt->version >= GNUM_XML_V4) {
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
					value_fmt = style_format_new_XL (CXML2C (fmt), FALSE);
					xmlFree (fmt);
				}
			}
		}
	}

	if (ctxt->version < GNUM_XML_V10)
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
				GnmStyle *mstyle = xml_read_style (ctxt, child);
				if (mstyle)
					sheet_style_set_pos (ctxt->sheet, col, row, mstyle);
			/* This is a pre version 1.0.3 file */
			} else if (!strcmp (child->name, "Content")) {
				content = xml_node_get_cstr (child, NULL);

				/* Is this a post 0.52 array */
				if (ctxt->version == GNUM_XML_V3) {
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
		if (ctxt->version <= GNUM_XML_V1 && content != NULL) {
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
		} else if (ctxt->version >= GNUM_XML_V3 ||
			   xml_not_used_old_array_spec (ctxt, cell, XML2C (content))) {
			if (is_value)
				cell_set_value (cell,
					value_new_from_string (value_type,
							       CXML2C (content),
							       value_fmt, FALSE));
			else {
				/* cell_set_text would probably handle this.
				 * BUT, be extra careful just incase a sheet
				 * appears that defines format text on a cell
				 * with a formula.  Try REALLY REALLY hard
				 * to parse it.  No need to worry about
				 * accidentally parsing something because
				 * support for Text formats did not happen
				 * until after ValueType was added.
				 */
				GnmParsePos pos;
				GnmExpr const *expr = NULL;
				char const *expr_start = gnm_expr_char_start_p (CXML2C (content));
				if (NULL != expr_start && *expr_start)
					expr = gnm_expr_parse_str (expr_start,
								   parse_pos_init_cell (&pos, cell),
								   GNM_EXPR_PARSE_DEFAULT,
								   ctxt->exprconv, NULL);
				if (expr != NULL) {
					cell_set_expr (cell, expr);
					gnm_expr_unref (expr);
				} else
					cell_set_text (cell, CXML2C (content));
			}
		}

		if (shared_expr_index > 0) {
			if (shared_expr_index == (int)ctxt->shared_exprs->len + 1) {
				if (!cell_has_expr (cell)) {
					g_warning ("XML-IO: Shared expression with no expession? id = %d\ncontent ='%s'",
						   shared_expr_index, CXML2C (content));
					cell_set_expr (cell,
						gnm_expr_new_constant (value_dup (cell->value)));
				}
				g_ptr_array_add (ctxt->shared_exprs,
						 (gpointer) cell->base.expression);
			} else {
				g_warning ("XML-IO: Duplicate or invalid shared expression: %d",
					   shared_expr_index);
			}
		}
		xmlFree (content);
	} else if (shared_expr_index > 0) {
		if (shared_expr_index <= (int)ctxt->shared_exprs->len + 1) {
			GnmExpr *expr = g_ptr_array_index (ctxt->shared_exprs,
							    shared_expr_index - 1);
			cell_set_expr (cell, expr);
		} else {
			g_warning ("XML-IO: Missing shared expression");
		}
	} else if (is_new_cell)
		/*
		 * Only set to empty if this is a new cell.
		 * If it was created by a previous array
		 * we do not want to erase it.
		 */
		cell_set_value (cell, value_new_empty ());

	style_format_unref (value_fmt);
	return cell;
}

static void
xml_write_merged_regions (XmlParseContext const *ctxt,
			  xmlNodePtr sheet, GSList *ptr)
{
	xmlNodePtr container;

	if (ptr == NULL)
		return;

	container = xmlNewChild (sheet, ctxt->ns, CC2XML ("MergedRegions"), NULL);
	for (; ptr != NULL ; ptr = ptr->next) {
		GnmRange const * const range = ptr->data;
		xmlNewChild (container, ctxt->ns, CC2XML ("Merge"), CC2XML (range_name (range)));
	}
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

static void
xml_write_sheet_layout (XmlParseContext *ctxt, xmlNodePtr tree, Sheet const *sheet)
{
	SheetView const *sv = sheet_get_view (sheet, ctxt->wb_view);

	tree = xmlNewChild (tree, ctxt->ns, CC2XML ("SheetLayout"), NULL);

	xml_node_set_cellpos (tree, "TopLeft", &sv->initial_top_left);
	if (sv_is_frozen (sv)) {
		xmlNodePtr freeze = xmlNewChild (tree, ctxt->ns, CC2XML ("FreezePanes"), NULL);
		xml_node_set_cellpos (freeze, "FrozenTopLeft", &sv->frozen_top_left);
		xml_node_set_cellpos (freeze, "UnfrozenTopLeft",
				      &sv->unfrozen_top_left);
	}
}

static char const *filter_cond_name[] = { "eq", "gt", "lt", "gte", "lte", "ne" };
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
xml_write_filter_expr (XmlParseContext *ctxt, xmlNode *field,
		       GnmFilterCondition const *cond, unsigned i)
{
	GString *text = g_string_new (NULL);

	xml_node_set_cstr (field, filter_expr_attrs[i].op,
			   filter_cond_name [cond->op[i]]);
	xml_node_set_int (field, filter_expr_attrs[i].valtype,
			  cond->value[i]->type);

	value_get_as_gstring (cond->value[i], text, ctxt->exprconv);
	xml_node_set_cstr (field, filter_expr_attrs[i].val, CC2XML (text->str));
	g_string_free (text, TRUE);
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
		int count;

		if (xml_node_get_bool (field, CC2XML ("Top"), &top) &&
		    xml_node_get_bool (field, CC2XML ("Items"), &items) &&
		    xml_node_get_int (field, CC2XML ("Count"), &count))
			cond = gnm_filter_condition_new_bucket (
					top, items, count);
	}
	xmlFree (type);

	if (cond != NULL)
		gnm_filter_set_condition (filter, i, cond, FALSE);
}

static void
xml_write_filter_field (XmlParseContext *ctxt, xmlNode *filter,
			GnmFilterCondition const *cond, unsigned i)
{
	xmlNode *field = xmlNewChild (filter, ctxt->ns,
				      CC2XML ("Field"), NULL);
	xml_node_set_int (field, "Index", i);

	switch (GNM_FILTER_OP_TYPE_MASK & cond->op[0]) {
	case 0: xml_node_set_cstr (field, "Type", "expr");
		xml_write_filter_expr (ctxt, field, cond, 0);
		if (cond->op[1] != GNM_FILTER_UNUSED) {
			xml_write_filter_expr (ctxt, field, cond, 1);
			xml_node_set_bool (field, "IsAnd", cond->is_and);
		}
		break;
	case GNM_FILTER_OP_BLANKS:
		xml_node_set_cstr (field, "Type", "blanks");
		break;
	case GNM_FILTER_OP_NON_BLANKS:
		xml_node_set_cstr (field, "Type", "nonblanks");
		break;
	case GNM_FILTER_OP_TOP_N:
		xml_node_set_cstr (field, "Type", "bucket");
		xml_node_set_bool (field, "top",
			cond->op[0] & 1 ? TRUE : FALSE);
		xml_node_set_bool (field, "items",
			cond->op[0] & 2 ? TRUE : FALSE);
		xml_node_set_int (field, "count", cond->count);
		break;
	};
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
		if (parse_range (CXML2C (area), &r)) {
			filter = gnm_filter_new (ctxt->sheet, &r);
			for (field = filter_node->xmlChildrenNode; field != NULL; field = field->next)
				if (!xmlIsBlankNode (field))
					xml_read_filter_field (ctxt, field, filter);
		}
		xmlFree (area);
	}
}

static void
xml_write_sheet_filters (XmlParseContext *ctxt, xmlNode *container,
			 Sheet const *sheet)
{
	GSList *ptr;
	GnmFilter *filter;
	GnmFilterCondition const *cond;
	xmlNode *filter_node;
	unsigned i;

	if (sheet->filters == NULL)
		return;

	container = xmlNewChild (container, ctxt->ns, CC2XML ("Filters"), NULL);
	for (ptr = sheet->filters; ptr != NULL ; ptr = ptr->next) {
		filter = ptr->data;
		filter_node = xmlNewChild (container, ctxt->ns,
				   CC2XML ("Filter"), NULL);

		if (filter_node != NULL) {
			xml_node_set_cstr (filter_node, "Area",
				range_name (&filter->r));
			for (i = filter->fields->len ; i-- > 0 ; ) {
				cond = gnm_filter_get_condition (filter, i);
				if (cond != NULL &&
				    cond->op[0] != GNM_FILTER_UNUSED)
					xml_write_filter_field (ctxt, filter_node, cond, i);
			}
		}
	}
}

static xmlNodePtr
xml_write_styles (XmlParseContext *ctxt, GnmStyleList *styles)
{
	GnmStyleList *ptr;
	xmlNodePtr cur;

	if (!styles)
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Styles"),
			     NULL);
	for (ptr = styles; ptr; ptr = ptr->next) {
		GnmStyleRegion *sr = ptr->data;

		xmlAddChild (cur, xml_write_style_region (ctxt, sr));
	}
	style_list_free (styles);

	return cur;
}

static void
xml_read_solver (XmlParseContext *ctxt, xmlNodePtr tree)
{
#ifdef ENABLE_SOLVER
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
			c->str = write_constraint_str (c->lhs.col, c->lhs.row,
						       c->rhs.col, c->rhs.row,
						       c->type, c->cols, c->rows);

			param->constraints = g_slist_append (param->constraints, c);
			child = e_xml_get_child_by_name (child, CC2XML ("Constr"));
		} while (child != NULL);
	}

	/* The options of the Solver. */
	xml_node_get_int (tree, "MaxTime", &(param->options.max_time_sec));
	xml_node_get_int (tree, "MaxIter", &(param->options.max_iter));
	xml_node_get_int (tree, "NonNeg",
			  &(param->options.assume_non_negative));
	xml_node_get_int (tree, "Discr", &(param->options.assume_discrete));
	xml_node_get_int (tree, "AutoScale",
			  &(param->options.automatic_scaling));
	xml_node_get_int (tree, "ShowIter",
			  &(param->options.show_iter_results));
	xml_node_get_int (tree, "AnswerR", &(param->options.answer_report));
	xml_node_get_int (tree, "SensitivityR",
			  &(param->options.sensitivity_report));
	xml_node_get_int (tree, "LimitsR", &(param->options.limits_report));
	xml_node_get_int (tree, "PerformR",
			  &(param->options.performance_report));
	xml_node_get_int (tree, "ProgramR", &(param->options.program_report));
#endif
}

static xmlNodePtr
xml_write_solver (XmlParseContext *ctxt, SolverParameters const *param)
{
	xmlNodePtr       cur = NULL;
	xmlNodePtr       constr;
	xmlNodePtr       prev = NULL;
	GSList           *constraints;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns,
			     CC2XML ("Solver"), NULL);

	if (param->target_cell != NULL) {
	        xml_node_set_int (cur, "TargetCol",
				   param->target_cell->pos.col);
	        xml_node_set_int (cur, "TargetRow",
				   param->target_cell->pos.row);
	} else {
	        xml_node_set_int (cur, "TargetCol", -1);
	        xml_node_set_int (cur, "TargetRow", -1);
	}

	xml_node_set_int (cur, "ProblemType", param->problem_type);
	xml_node_set_cstr (cur, "Inputs", param->input_entry_str);

	constraints = param->constraints;
	while (constraints) {
	        const SolverConstraint *c =
			(const SolverConstraint *)constraints->data;

		constr = xmlNewDocNode (ctxt->doc, ctxt->ns,
					CC2XML ("Constr"), NULL);
		xml_node_set_int (constr, "Lcol", c->lhs.col);
		xml_node_set_int (constr, "Lrow", c->lhs.row);
		xml_node_set_int (constr, "Rcol", c->rhs.col);
		xml_node_set_int (constr, "Rrow", c->rhs.row);
		xml_node_set_int (constr, "Cols", c->cols);
		xml_node_set_int (constr, "Rows", c->rows);

		switch (c->type) {
		case SolverLE:
		        xml_node_set_int (constr, "Type", 1);
			break;
		case SolverGE:
		        xml_node_set_int (constr, "Type", 2);
			break;
		case SolverEQ:
		        xml_node_set_int (constr, "Type", 4);
			break;
		case SolverINT:
		        xml_node_set_int (constr, "Type", 8);
			break;
		case SolverBOOL:
		        xml_node_set_int (constr, "Type", 16);
			break;
		default:
		        xml_node_set_int (constr, "Type", 0);
			break;
		}

		if (!prev)
		        xmlAddChild (cur, constr);
		else
		        xmlAddChild (prev, constr);

		prev = constr;
		constraints = constraints->next;
	}

	/* The options of the Solver. */
	xml_node_set_int (cur, "MaxTime", param->options.max_time_sec);
	xml_node_set_int (cur, "MaxIter", param->options.max_iter);
	xml_node_set_int (cur, "NonNeg",
			  param->options.assume_non_negative);
	xml_node_set_int (cur, "Discr", param->options.assume_discrete);
	xml_node_set_int (cur, "AutoScale", param->options.automatic_scaling);
	xml_node_set_int (cur, "ShowIter", param->options.show_iter_results);
	xml_node_set_int (cur, "AnswerR", param->options.answer_report);
	xml_node_set_int (cur, "SensitivityR",
			  param->options.sensitivity_report);
	xml_node_set_int (cur, "LimitsR", param->options.limits_report);
	xml_node_set_int (cur, "PerformR", param->options.performance_report);
	xml_node_set_int (cur, "ProgramR", param->options.program_report);
	return cur;
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
		range = global_range_parse (sheet, str);
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
			g_string_free (name, FALSE);
		}

		sheet->scenarios = g_list_append (sheet->scenarios, s);
		child = e_xml_get_child_by_name (child, CC2XML ("Scenario"));
	}
}

static xmlNodePtr
xml_write_scenarios (XmlParseContext *ctxt, GList const *scenarios)
{
	xmlNodePtr cur;
	xmlNodePtr scen;
	xmlNodePtr prev = NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns,
			     CC2XML ("Scenarios"), NULL);

	while (scenarios) {
	        const scenario_t *s =
			(const scenario_t *)scenarios->data;
		GString  *name;
		int      i, cols, rows;

		scen = xmlNewDocNode (ctxt->doc, ctxt->ns,
				      CC2XML ("Scenario"), NULL);

		/* Scenario: name. */
		xml_node_set_cstr (scen, "Name", s->name);

		/* Scenario: comment. */
		xml_node_set_cstr (scen, "Comment", s->comment);

		/* Scenario: changing cells in a string form.  In a string
		 * form so that we can in the future allow it to contain
		 * multiple ranges without modifing the file format.*/
		xml_node_set_cstr (scen, "CellsStr", s->cell_sel_str);

		/* Scenario: values. */
		rows = s->range.end.row - s->range.start.row + 1;
		cols = s->range.end.col - s->range.start.col + 1;
		for (i = 0; i < cols * rows; i++) {
			name = g_string_new (NULL);
			g_string_append_printf (name, "V%d", i);
			xml_node_set_value (scen, name->str,
					    s->changing_cells [i]);
			g_string_free (name, FALSE);
		}

		if (!prev)
		        xmlAddChild (cur, scen);
		else
		        xmlAddChild (prev, scen);

		prev = scen;
		scenarios = scenarios->next;
	}

	return cur;
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
		so = g_object_new (GNM_SO_FILLED_TYPE, "is_oval", TRUE, NULL);
	else if (!strcmp (tree->name, "Line"))
		so = g_object_new (GNM_SO_LINE_TYPE, "is_arrow", TRUE, NULL);
	else if (!strcmp (tree->name, "Arrow"))
		so = g_object_new (GNM_SO_LINE_TYPE, NULL);

	/* Class renamed between 1.0.x and 1.2.x */
	else if (!strcmp (tree->name, "GnmGraph"))
		so = sheet_object_graph_new (FALSE);

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
		if (parse_range (tmp, &r))
			so->anchor.cell_bound = r;
		xmlFree (tmp);
	}

	tmp =  (char *) xmlGetProp (tree, (xmlChar *)"ObjectOffset");
	if (tmp != NULL) {
		sscanf (tmp, "%g %g %g %g",
			so->anchor.offset +0, so->anchor.offset +1,
			so->anchor.offset +2, so->anchor.offset +3);
		xmlFree (tmp);
	}

	tmp = (char *) xmlGetProp (tree, (xmlChar *)"ObjectAnchorType");
	if (tmp != NULL) {
		int i[4], count;
		sscanf (tmp, "%d %d %d %d", i+0, i+1, i+2, i+3);

		for (count = 4; count-- > 0 ; )
			so->anchor.type[count] = i[count];
		xmlFree (tmp);
	}

	if (xml_node_get_int (tree, "Direction", &tmp_int))
		so->anchor.direction = tmp_int;
	else
		so->anchor.direction = SO_DIR_UNKNOWN;

	/* Do not assign to a sheet when extracting a cell region */
	if (NULL != ctxt->sheet) {
		sheet_object_set_sheet (so, ctxt->sheet);
		g_object_unref (G_OBJECT (so));
	}
	return so;
}

static xmlNodePtr
xml_write_sheet_object (XmlParseContext const *ctxt, SheetObject const *so)
{
	xmlNodePtr tree;
	char buffer[4*(DBL_DIG+10)];
	char const *type_name;
	SheetObjectClass *klass = SHEET_OBJECT_CLASS (G_OBJECT_GET_CLASS(so));

	g_return_val_if_fail (klass != NULL, NULL);

	if (klass->write_xml_dom == NULL)
		return NULL;

	/* A hook so that things can sometimes change names */
	type_name = klass->xml_export_name;
	if (type_name == NULL)
		type_name = G_OBJECT_TYPE_NAME (so);
	tree = xmlNewDocNode (ctxt->doc, ctxt->ns, (xmlChar *)type_name, NULL);

	if (tree == NULL)
		return NULL;

	if (klass->write_xml_dom (so, ctxt, tree)) {
		xmlUnlinkNode (tree);
		xmlFreeNode (tree);
		return NULL;
	}

	xml_node_set_cstr (tree, "ObjectBound", range_name (&so->anchor.cell_bound));
	snprintf (buffer, sizeof (buffer), "%.*g %.*g %.*g %.*g",
		  DBL_DIG, so->anchor.offset [0], DBL_DIG, so->anchor.offset [1],
		  DBL_DIG, so->anchor.offset [2], DBL_DIG, so->anchor.offset [3]);
	xml_node_set_cstr (tree, "ObjectOffset", buffer);
	snprintf (buffer, sizeof (buffer), "%d %d %d %d",
		  so->anchor.type [0], so->anchor.type [1],
		  so->anchor.type [2], so->anchor.type [3]);
	xml_node_set_cstr (tree, "ObjectAnchorType", buffer);
	xml_node_set_int (tree, "Direction", so->anchor.direction);

	return tree;
}

/*
 *
 */

static int
natural_order_cmp (const void *a, const void *b)
{
	GnmCell const *ca = *(GnmCell const **)a ;
	GnmCell const *cb = *(GnmCell const **)b ;
	int diff = (ca->pos.row - cb->pos.row);

	if (diff != 0)
		return diff;
	return ca->pos.col - cb->pos.col;
}

static void
copy_hash_table_to_ptr_array (gpointer key, GnmCell *cell, gpointer user_data)
{
	if (!cell_is_empty (cell) || cell->base.expression != NULL)
		g_ptr_array_add (user_data, cell) ;
}

/*
 * Create an XML subtree of doc equivalent to the given Sheet.
 */
static xmlNodePtr
xml_sheet_write (XmlParseContext *ctxt, Sheet const *sheet)
{
	xmlNodePtr sheetNode;
	xmlNodePtr child;
	xmlNodePtr rows;
	xmlNodePtr cols;
	xmlNodePtr cells;
	xmlNodePtr printinfo;
	xmlNodePtr styles;
	xmlNodePtr solver;
	xmlNodePtr scenarios;
	xmlChar *tstr;

	/* General information about the Sheet */
	sheetNode = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Sheet"), NULL);
	if (sheetNode == NULL)
		return NULL;
	xml_node_set_bool (sheetNode, "DisplayFormulas", sheet->display_formulas);
	xml_node_set_bool (sheetNode, "HideZero", sheet->hide_zero);
	xml_node_set_bool (sheetNode, "HideGrid", sheet->hide_grid);
	xml_node_set_bool (sheetNode, "HideColHeader", sheet->hide_col_header);
	xml_node_set_bool (sheetNode, "HideRowHeader", sheet->hide_row_header);
	xml_node_set_bool (sheetNode, "DisplayOutlines", sheet->display_outlines);
	xml_node_set_bool (sheetNode, "OutlineSymbolsBelow", sheet->outline_symbols_below);
	xml_node_set_bool (sheetNode, "OutlineSymbolsRight", sheet->outline_symbols_right);

	if (sheet->tab_color != NULL)
		xml_node_set_color (sheetNode, "TabColor", sheet->tab_color);
	if (sheet->tab_text_color != NULL)
		xml_node_set_color (sheetNode, "TabTextColor", sheet->tab_text_color);

	tstr = xmlEncodeEntitiesReentrant (ctxt->doc, CC2XML (sheet->name_unquoted));
	xmlNewChild (sheetNode, ctxt->ns, CC2XML ("Name"),  tstr);
	if (tstr)
		xmlFree (tstr);
	{
		char str[4 * sizeof (int) + DBL_DIG + 50];
		sprintf (str, "%d", sheet->cols.max_used);
		xmlNewChild (sheetNode, ctxt->ns, CC2XML ("MaxCol"), CC2XML (str));
		sprintf (str, "%d", sheet->rows.max_used);
		xmlNewChild (sheetNode, ctxt->ns, CC2XML ("MaxRow"), CC2XML (str));
		sprintf (str, "%f", sheet->last_zoom_factor_used);
		xmlNewChild (sheetNode, ctxt->ns, CC2XML ("Zoom"), CC2XML (str));
	}

	child = xml_write_names (ctxt, sheet->names);
	if (child)
		xmlAddChild (sheetNode, child);

	/*
	 * Print Information
	 */
	printinfo = xml_write_print_info (ctxt, sheet->print_info);
	if (printinfo)
		xmlAddChild (sheetNode, printinfo);

	/*
	 * Styles
	 */
	styles = xml_write_styles (ctxt, sheet_style_get_list (sheet, NULL));
	if (styles)
		xmlAddChild (sheetNode, styles);

	/*
	 * Cols informations.
	 */
	cols = xmlNewChild (sheetNode, ctxt->ns, CC2XML ("Cols"), NULL);
	xml_node_set_points (cols, "DefaultSizePts",
			      sheet_col_get_default_size_pts (sheet));
	{
		closure_write_colrow closure;
		closure.is_column = TRUE;
		closure.container = cols;
		closure.ctxt = ctxt;
		closure.previous = NULL;
		closure.rle_count = 0;
		colrow_foreach (&sheet->cols,
				0, SHEET_MAX_COLS-1,
				&xml_write_colrow_info, &closure);
		xml_write_colrow_info (NULL, &closure); /* flush */
	}

	/*
	 * Rows informations.
	 */
	rows = xmlNewChild (sheetNode, ctxt->ns, CC2XML ("Rows"), NULL);
	xml_node_set_points (rows, "DefaultSizePts",
			      sheet_row_get_default_size_pts (sheet));
	{
		closure_write_colrow closure;
		closure.is_column = FALSE;
		closure.container = rows;
		closure.ctxt = ctxt;
		closure.previous = NULL;
		closure.rle_count = 0;
		colrow_foreach (&sheet->rows,
				0, SHEET_MAX_ROWS-1,
				&xml_write_colrow_info, &closure);
		xml_write_colrow_info (NULL, &closure); /* flush */
	}

	/* Save the current selection */
	xml_write_selection_info (ctxt, sheet, sheetNode);

	/* Objects */
	if (sheet->sheet_objects != NULL) {
		xmlNodePtr objects = xmlNewChild (sheetNode, ctxt->ns,
						  CC2XML ("Objects"), NULL);
		GList *l = sheet->sheet_objects;
		while (l) {
			child = xml_write_sheet_object (ctxt, l->data);
			if (child)
				xmlAddChild (objects, child);
			l = l->next;
		}
	}

	/* save cells in natural order */
	cells = xmlNewChild (sheetNode, ctxt->ns, CC2XML ("Cells"), NULL);
	{
		size_t i;
		GPtrArray *natural = g_ptr_array_new ();
		g_hash_table_foreach (sheet->cell_hash,
				      (GHFunc) copy_hash_table_to_ptr_array, natural);
		qsort (&g_ptr_array_index (natural, 0),
		       natural->len,
		       sizeof (gpointer),
		       natural_order_cmp);
		for (i = 0; i < natural->len; i++) {
			child = xml_write_cell (ctxt, g_ptr_array_index (natural, i));
			xmlAddChild (cells, child);
		}
		g_ptr_array_free (natural, TRUE);
	}

	xml_write_merged_regions (ctxt, sheetNode, sheet->list_merged);
	xml_write_sheet_layout (ctxt, sheetNode, sheet);
	xml_write_sheet_filters (ctxt, sheetNode, sheet);

	solver = xml_write_solver (ctxt, sheet->solver_parameters);
	if (solver)
		xmlAddChild (sheetNode, solver);

	scenarios = xml_write_scenarios (ctxt, sheet->scenarios);
	if (scenarios)
		xmlAddChild (sheetNode, scenarios);

	return sheetNode;
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
				if (parse_range (CXML2C (content), &r))
					sheet_merge_add (ctxt->sheet, &r, FALSE, NULL);
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

/*
 * Create a ColRowInfo equivalent to the XML subtree of doc.
 */
static int
xml_read_colrow_info (XmlParseContext *ctxt, xmlNodePtr tree,
		      ColRowInfo *info, double *size_pts)
{
	int val, count;

	info->size_pts = -1;
	xml_node_get_int (tree, "No", &info->pos);
	xml_node_get_double (tree, "Unit", size_pts);
	if (xml_node_get_int (tree, "MarginA", &val))
		info->margin_a = val;
	if (xml_node_get_int (tree, "MarginB", &val))
		info->margin_b = val;
	if (xml_node_get_int (tree, "HardSize", &val))
		info->hard_size = val;
	if (xml_node_get_int (tree, "Hidden", &val) && val)
		info->visible = FALSE;
	if (xml_node_get_int (tree, "Collapsed", &val) && val)
		info->is_collapsed = TRUE;
	if (xml_node_get_int (tree, "OutlineLevel", &val) && val > 0)
		info->outline_level = val;
	if (xml_node_get_int (tree, "Count", &count))
		return count;
	return 1;
}

static void
xml_read_cols_info (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr cols, col;
	double size_pts, tmp;
	ColRowInfo *info;
	int count, pos;
	Sheet *sheet = ctxt->sheet;

	cols = e_xml_get_child_by_name (tree, CC2XML ("Cols"));
	if (cols == NULL)
		return;

	if (xml_node_get_double (cols, "DefaultSizePts", &tmp))
		sheet_col_set_default_size_pts (sheet, tmp);

	for (col = cols->xmlChildrenNode; col; col = col->next)
		if (!xmlIsBlankNode (col)) {
			info = sheet_col_new (sheet);
			count = xml_read_colrow_info (ctxt, col, info, &size_pts);
			sheet_col_add (sheet, info);
			sheet_col_set_size_pts (ctxt->sheet, info->pos, size_pts, info->hard_size);

			/* resize flags are already set only need to copy the sizes */
			for (pos = info->pos ; --count > 0 ; )
				colrow_copy (sheet_col_fetch (ctxt->sheet, ++pos), info);
		}
}

static void
xml_read_rows_info (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr rows, row;
	double size_pts, tmp;
	ColRowInfo *info;
	int count, pos;
	Sheet *sheet = ctxt->sheet;

	rows = e_xml_get_child_by_name (tree, CC2XML ("Rows"));
	if (rows == NULL)
		return;

	if (xml_node_get_double (rows, "DefaultSizePts", &tmp))
		sheet_row_set_default_size_pts (sheet, tmp);

	for (row = rows->xmlChildrenNode; row; row = row->next)
		if (!xmlIsBlankNode (row)) {
			info = sheet_row_new (sheet);
			count = xml_read_colrow_info (ctxt, row, info, &size_pts);
			sheet_row_add (sheet, info);
			sheet_row_set_size_pts (ctxt->sheet, info->pos, size_pts, info->hard_size);

			/* resize flags are already set only need to copy the sizes */
			for (pos = info->pos ; --count > 0 ; )
				colrow_copy (sheet_row_fetch (ctxt->sheet, ++pos), info);
		}
}

static void
xml_read_cell_styles (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr styles, child;
	GnmStyle *mstyle;
	int style_idx;

	ctxt->style_table = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						   NULL, (GDestroyNotify) mstyle_unref);

	child = e_xml_get_child_by_name (tree, CC2XML ("CellStyles"));
	if (child == NULL)
		return;

	for (styles = child->xmlChildrenNode; styles; styles = styles->next) {
		if (!xmlIsBlankNode (styles) &&
		    xml_node_get_int (styles, "No", &style_idx)) {
			mstyle = xml_read_style (ctxt, styles);
			g_hash_table_insert (
				ctxt->style_table,
				GINT_TO_POINTER (style_idx),
				mstyle);
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
	xmlChar *val;

	if (strcmp (tree->name, "Sheet")){
		fprintf (stderr,
			 "xml_sheet_read: invalid element type %s, 'Sheet' expected\n",
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
	if (!xml_node_get_bool (tree, "DisplayOutlines", &(sheet->display_outlines)))
		sheet->display_outlines = TRUE;
	if (!xml_node_get_bool (tree, "OutlineSymbolsBelow", &(sheet->outline_symbols_below)))
		sheet->outline_symbols_below = TRUE;
	if (!xml_node_get_bool (tree, "OutlineSymbolsRight", &(sheet->outline_symbols_right)))
		sheet->outline_symbols_right = TRUE;
	sheet->tab_color = xml_node_get_color (tree, "TabColor");
	sheet->tab_text_color = xml_node_get_color (tree, "TabTextColor");

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
	sheet_flag_recompute_spans (sheet);
	sheet_set_zoom_factor (sheet, zoom_factor, FALSE, FALSE);

	return sheet;
}

/****************************************************************************/

static CellCopy *
cell_copy_new (void)
{
	GnmCell *cell;
	CellCopy *cc;

	cell = cell_new ();
	cell->pos.col = -1;
	cell->pos.row = -1;
	cell->value   = value_new_empty ();

	cc          = g_new (CellCopy, 1);
	cc->type    = CELL_COPY_TYPE_CELL;
	cc->u.cell  = cell;

	return cc;
}

static void
xml_read_cell_copy (XmlParseContext *ctxt, xmlNodePtr tree,
		    GnmCellRegion *cr, Sheet *sheet)
{
	int tmp;
	CellCopy *cc;
	GnmCell  *cell;
	xmlNode  *child;
	xmlChar	 *content;
	int array_cols, array_rows, shared_expr_index = -1;
	gboolean is_post_52_array = FALSE;
	gboolean is_value = FALSE;
	GnmValueType value_type = VALUE_EMPTY; /* Make compiler shut up */
	GnmFormat *value_fmt = NULL;
	GnmParsePos pp;

	if (strcmp (tree->name, "Cell")) {
		fprintf (stderr,
		 "xml_read_cell_copy: invalid element type %s, 'Cell' expected`\n",
			 tree->name);
		return;
	}

	cc = cell_copy_new ();
	cell = cc->u.cell;
	xml_node_get_int (tree, "Col", &cell->pos.col);
	xml_node_get_int (tree, "Row", &cell->pos.row);
	cc->col_offset = cell->pos.col - cr->base.col;
	cc->row_offset = cell->pos.row - cr->base.row;
	parse_pos_init (&pp, NULL, sheet, cell->pos.col, cell->pos.row);

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
			value_fmt = style_format_new_XL (CXML2C (fmt), FALSE);
			xmlFree (fmt);
		}
	}

	child = e_xml_get_child_by_name (tree, CC2XML ("Content"));
	content = xml_node_get_cstr ((child != NULL) ? child : tree, NULL);
	if (content != NULL) {
		if (is_post_52_array) {
			GnmExpr const *expr;

			g_return_if_fail (content[0] == '=');

			expr = gnm_expr_parse_str (CXML2C (content), &pp,
						   GNM_EXPR_PARSE_DEFAULT,
						   ctxt->exprconv, NULL);

			g_return_if_fail (expr != NULL);
#warning TODO : arrays
		} else if (is_value)
			cell->value = value_new_from_string (value_type,
							     CXML2C (content),
							     value_fmt,
							     FALSE);
		else {
			GnmValue *val;
			GnmExpr const *expr;
			GnmDateConventions const *date_conv =
				ctxt->wb ? workbook_date_conv (ctxt->wb) : NULL;

			parse_text_value_or_expr (&pp,
						  CXML2C (content),
						  &val, &expr, value_fmt,
						  date_conv);

			if (val != NULL) {	/* String was a value */
				value_release (cell->value);
				cell->value = val;
			} else		/* String was an expression */
				cell->base.expression = expr;
		}

		if (shared_expr_index > 0) {
			if (shared_expr_index == (int)ctxt->shared_exprs->len + 1) {
				if (!cell_has_expr (cell)) {
					/* The parse failed, but we know it is
					 * an expression.  this can happen
					 * until we get proper interworkbook
					 * linkages.  Force the content into
					 * being an expression.
					 */
					cell->base.expression = gnm_expr_new_constant (
						value_new_string (
							  gnm_expr_char_start_p (CXML2C (content))));
					value_release (cell->value);
					cell->value = value_new_empty ();
				}
				g_ptr_array_add (ctxt->shared_exprs,
						 (gpointer) cell->base.expression);
			} else {
				g_warning ("XML-IO: Duplicate or invalid shared expression: %d",
					   shared_expr_index);
			}
		}
		xmlFree (content);
	} else if (shared_expr_index > 0) {
		if (shared_expr_index <= (int)ctxt->shared_exprs->len + 1) {
			GnmExpr *expr = g_ptr_array_index (ctxt->shared_exprs,
							    shared_expr_index - 1);
			gnm_expr_ref (expr);
			cell->base.expression = expr;
		} else {
			g_warning ("XML-IO: Missing shared expression");
		}
	}
	style_format_unref (value_fmt);

	cr->content = g_slist_prepend (cr->content, cc);
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
xml_cellregion_read (WorkbookControl *wbc, Sheet *sheet, guchar *buffer, int length)
{
	XmlParseContext *ctxt;
	xmlNode	   *l, *clipboard;
	xmlDoc	   *doc;
	GnmCellRegion *cr;
	int dummy;

	g_return_val_if_fail (buffer != NULL, NULL);

	doc = xmlParseDoc (buffer);
	if (doc == NULL) {
		gnm_cmd_context_error_import (GNM_CMD_CONTEXT (wbc),
			_("Unparsable xml in clipboard"));
		return NULL;
	}
	clipboard = doc->xmlRootNode;
	if (clipboard == NULL || strcmp (clipboard->name, "ClipboardRange")) {
		xmlFreeDoc (doc);
		gnm_cmd_context_error_import (GNM_CMD_CONTEXT (wbc),
			_("Clipboard is in unknown format"));
		return NULL;
	}

	/* ctxt->sheet must == NULL or copying objects will break */
	ctxt = xml_parse_ctx_new (doc, NULL, NULL);
	cr = cellregion_new (NULL);

	xml_node_get_int (clipboard, "Cols", &cr->cols);
	xml_node_get_int (clipboard, "Rows", &cr->rows);
	xml_node_get_int (clipboard, "BaseCol", &cr->base.col);
	xml_node_get_int (clipboard, "BaseRow", &cr->base.row);
	/* if it exists it is TRUE */
	cr->not_as_content = xml_node_get_int (clipboard, "NotAsContent", &dummy);

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
				if (parse_range (CXML2C (content), &r))
					cr->merged = g_slist_prepend (cr->merged,
								      range_dup (&r));
				xmlFree (content);
			}

	l = e_xml_get_child_by_name (clipboard, CC2XML ("Cells"));
	if (l != NULL)
		for (l = l->xmlChildrenNode; l != NULL ; l = l->next)
			if (!xmlIsBlankNode (l))
				xml_read_cell_copy (ctxt, l, cr, sheet);

	l = e_xml_get_child_by_name (clipboard, CC2XML ("Objects"));
	if (l != NULL)
		for (l = l->xmlChildrenNode; l != NULL ; l = l->next)
			if (!xmlIsBlankNode (l))
				cr->objects = g_slist_prepend (cr->objects,
							       xml_read_sheet_object (ctxt, l));

	xml_parse_ctx_destroy (ctxt);
	xmlFreeDoc (doc);

	return cr;
}

/**
 * xml_cellregion_write :
 * @wbc : where to report errors.
 * @cr  : the content to store.
 * @size: store the size of the buffer here.
 *
 * Caller is responsible for xmlFree-ing the result.
 * returns NULL on error
 **/
xmlChar *
xml_cellregion_write (WorkbookControl *wbc, GnmCellRegion *cr, int *size)
{
	XmlParseContext *ctxt;
	xmlNode   *clipboard, *container = NULL;
	GSList    *ptr;
	GnmStyleList *s_ptr;
	CellCopyList *c_ptr;
	xmlChar	  *buffer;
	GnmParsePos   pp;

	g_return_val_if_fail (cr != NULL, NULL);
	g_return_val_if_fail (IS_SHEET (cr->origin_sheet), NULL);

	/* Create the tree */
	ctxt = xml_parse_ctx_new (xmlNewDoc (CC2XML ("1.0")), NULL, NULL), NULL;
	clipboard = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("ClipboardRange"), NULL);
	xml_node_set_int (clipboard, "Cols", cr->cols);
	xml_node_set_int (clipboard, "Rows", cr->rows);
	xml_node_set_int (clipboard, "BaseCol", cr->base.col);
	xml_node_set_int (clipboard, "BaseRow", cr->base.row);
	if (cr->not_as_content)
		xml_node_set_int (clipboard, "NotAsContent", 1);

	/* styles */
	if (cr->styles != NULL)
		container = xmlNewChild (clipboard, clipboard->ns, CC2XML ("Styles"), NULL);
	for (s_ptr = cr->styles ; s_ptr != NULL ; s_ptr = s_ptr->next) {
		GnmStyleRegion const *sr = s_ptr->data;
		xmlAddChild (container, xml_write_style_region (ctxt, sr));
	}

	/* merges */
	if (cr->merged != NULL)
		container = xmlNewChild (clipboard, clipboard->ns, CC2XML ("MergedRegions"), NULL);
	for (ptr = cr->merged ; ptr != NULL ; ptr = ptr->next) {
		GnmRange const *m_range = ptr->data;
		xmlNewChild (container, container->ns, CC2XML ("Merge"),
			     CC2XML (range_name (m_range)));
	}

	/* NOTE SNEAKY : ensure that sheet names have explicit workbooks */
	pp.wb = NULL;
	pp.sheet = cr->origin_sheet;

	/* cells */
	if (cr->content != NULL)
		container = xmlNewChild (clipboard, clipboard->ns, CC2XML ("Cells"), NULL);
	for (c_ptr = cr->content; c_ptr != NULL ; c_ptr = c_ptr->next) {
		CellCopy const *c_copy = c_ptr->data;

		g_return_val_if_fail (c_copy->type == CELL_COPY_TYPE_CELL, NULL);

		pp.eval.col = cr->base.col + c_copy->col_offset,
		pp.eval.row = cr->base.row + c_copy->row_offset;
		xmlAddChild (container,
			xml_write_cell_and_position (ctxt, c_copy->u.cell, &pp));
	}

	if (cr->objects != NULL)
		container = xmlNewChild (clipboard, clipboard->ns, CC2XML ("Objects"), NULL);
	for (c_ptr = cr->objects; c_ptr != NULL ; c_ptr = c_ptr->next) 
		xmlAddChild (container, 
			xml_write_sheet_object (ctxt, c_ptr->data));

	ctxt->doc->xmlRootNode = clipboard;
	xmlIndentTreeOutput = TRUE;
	xmlDocDumpFormatMemoryEnc (ctxt->doc, &buffer, size, "UTF-8", TRUE);
	xmlFreeDoc (ctxt->doc);
	xml_parse_ctx_destroy (ctxt);

	return buffer;
}

/*****************************************************************************/

/* These will be searched IN ORDER, so add new versions at the top */
static const struct {
	char const * const id;
	GnumericXMLVersion const version;
} GnumericVersions [] = {
	{ "http://www.gnumeric.org/v10.dtd", GNUM_XML_V10 },	/* 1.0.3 */
	{ "http://www.gnumeric.org/v9.dtd", GNUM_XML_V9 },	/* 0.73 */
	{ "http://www.gnumeric.org/v8.dtd", GNUM_XML_V8 },	/* 0.71 */
	{ "http://www.gnome.org/gnumeric/v7", GNUM_XML_V7 },	/* 0.66 */
	{ "http://www.gnome.org/gnumeric/v6", GNUM_XML_V6 },	/* 0.62 */
	{ "http://www.gnome.org/gnumeric/v5", GNUM_XML_V5 },
	{ "http://www.gnome.org/gnumeric/v4", GNUM_XML_V4 },
	{ "http://www.gnome.org/gnumeric/v3", GNUM_XML_V3 },
	{ "http://www.gnome.org/gnumeric/v2", GNUM_XML_V2 },
	{ "http://www.gnome.org/gnumeric/", GNUM_XML_V1 },
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

/*
 * Create an XML subtree of doc equivalent to the given Workbook.
 */
static xmlNodePtr
xml_workbook_write (XmlParseContext *ctxt)
{
	xmlNodePtr cur;
	xmlNodePtr child;
	GList *sheets, *sheets0;
	char *old_num_locale, *old_monetary_locale;
	Workbook *wb = wb_view_workbook (ctxt->wb_view);

	/*
	 * General informations about the Sheet.
	 */
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Workbook"), NULL);	/* the Workbook name !!! */
	if (cur == NULL)
		return NULL;
	if (ctxt->ns == NULL) {
		/* GnumericVersions[0] is always the first item and
		 * the most recent version, see table above. Keep the table
		 * ordered this way!
		 */
		ctxt->ns = xmlNewNs (cur, CC2XML (GnumericVersions[0].id), CC2XML ("gmr"));
		xmlSetNs(cur, ctxt->ns);

		xmlNewNsProp (cur,
			xmlNewNs (cur, CC2XML ("http://www.w3.org/2001/XMLSchema-instance"), CC2XML ("xsi")),
			CC2XML ("schemaLocation"),
			CC2XML ("http://www.gnumeric.org/v8.xsd"));
	}
	child = xmlNewChild (cur, ctxt->ns, CC2XML ("Version"), NULL);
	xml_node_set_int (child, "Epoch", GNM_VERSION_EPOCH);
	xml_node_set_int (child, "Major", GNM_VERSION_MAJOR);
	xml_node_set_int (child, "Minor", GNM_VERSION_MINOR);
	xml_node_set_cstr (child, "Full", GNUMERIC_VERSION);

	old_num_locale = g_strdup (gnm_setlocale (LC_NUMERIC, NULL));
	gnm_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnm_setlocale (LC_MONETARY, NULL));
	gnm_setlocale (LC_MONETARY, "C");
	gnm_set_untranslated_bools ();

	child = xml_write_wbv_attributes (ctxt);
	if (child)
		xmlAddChild (cur, child);

	child = xml_write_summary (ctxt, workbook_metadata (wb));
	if (child)
		xmlAddChild (cur, child);

	{
		GnmDateConventions const *conv = workbook_date_conv (wb);
		if (conv->use_1904)
			xmlNewChild (cur, ctxt->ns, CC2XML ("DateConvention"), "1904");
	}

	/* The sheet name index is required for the xml_sax
	 * importer to work correctly. We don't use it for
	 * the dom loader! These must be written BEFORE
	 * the named expressions.
	 */
	child = xmlNewChild (cur, ctxt->ns, CC2XML ("SheetNameIndex"), NULL);
	sheets0 = sheets = workbook_sheets (wb);
	while (sheets) {
		xmlChar *tstr;
		Sheet *sheet = sheets->data;

		tstr = xmlEncodeEntitiesReentrant (ctxt->doc, CC2XML (sheet->name_unquoted));
		xmlNewChild (child, ctxt->ns, CC2XML ("SheetName"),  tstr);
		if (tstr)
			xmlFree (tstr);

		sheets = g_list_next (sheets);
	}
	xmlAddChild (cur, child);

	child = xml_write_names (ctxt, wb->names);
	if (child)
		xmlAddChild (cur, child);

/*	child = xml_write_style (ctxt, &wb->style, -1);
	if (child)
	xmlAddChild (cur, child);*/

	child = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("Geometry"), NULL);
	xml_node_set_int (child, "Width", ctxt->wb_view->preferred_width);
	xml_node_set_int (child, "Height", ctxt->wb_view->preferred_height);
	xmlAddChild (cur, child);

	/* sheet content */
	child = xmlNewChild (cur, ctxt->ns, CC2XML ("Sheets"), NULL);
	for (sheets = sheets0; sheets ; sheets = sheets->next)
		xmlAddChild (child, xml_sheet_write (ctxt, sheets->data));
	g_list_free (sheets0);

	child = xmlNewDocNode (ctxt->doc, ctxt->ns, CC2XML ("UIData"), NULL);
	xml_node_set_int (child, "SelectedTab", wb_view_cur_sheet (ctxt->wb_view)->index_in_wb);
	xmlAddChild (cur, child);

	child = xmlNewChild (cur, ctxt->ns, CC2XML ("Calculation"), NULL);
	xml_node_set_bool   (child, "ManualRecalc",	 !ctxt->wb->recalc_auto);
	xml_node_set_bool   (child, "EnableIteration",    ctxt->wb->iteration.enabled);
	xml_node_set_int    (child, "MaxIterations",      ctxt->wb->iteration.max_number);
	xml_node_set_double (child, "IterationTolerance", ctxt->wb->iteration.tolerance, -1);

	/* gnm_setlocale restores bools to locale translation */
	gnm_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	gnm_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	return cur;
}

static void
xml_sheet_create (XmlParseContext *ctxt, xmlNodePtr node)
{
	if (strcmp (node->name, "Sheet")) {
		fprintf (stderr,
			 "xml_sheet_create: invalid element type %s, 'Sheet' expected\n",
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
				       sheet_new (ctxt->wb, CXML2C (name)),
				       NULL);
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
	Sheet *sheet;
	xmlNodePtr child, c;
	char *old_num_locale, *old_monetary_locale;

	if (strcmp (tree->name, "Workbook")){
		fprintf (stderr,
			 "xml_workbook_read: invalid element type %s, 'Workbook' expected`\n",
			 tree->name);
		return FALSE;
	}

	old_num_locale = g_strdup (gnm_setlocale (LC_NUMERIC, NULL));
	gnm_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnm_setlocale (LC_MONETARY, NULL));
	gnm_setlocale (LC_MONETARY, "C");
	gnm_set_untranslated_bools ();

	child = e_xml_get_child_by_name (tree, CC2XML ("Summary"));
	if (child)
		xml_read_summary (ctxt, child, workbook_metadata (ctxt->wb));

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
			wb_view_preferred_size	  (ctxt->wb_view, width, height);
	}

/*	child = xml_search_child (tree, "Style");
	if (child != NULL)
	xml_read_style (ctxt, child, &wb->style);*/

	child = e_xml_get_child_by_name (tree, CC2XML ("Sheets"));
	if (child == NULL)
		return FALSE;

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

	child = e_xml_get_child_by_name (tree, CC2XML ("Sheets"));

	/*
	 * Pass 2: read the contents
	 */
	io_progress_message (context, _("Processing file..."));
	io_progress_range_push (context, 0.5, 1.0);
	count_io_progress_set (context, xml_read_workbook_n_elements (child),
	                       N_ELEMENTS_BETWEEN_UPDATES);
	ctxt->io_context = context;
	for (c = child->xmlChildrenNode; c != NULL ; c = c->next)
		if (!xmlIsBlankNode (c))
			sheet = xml_sheet_read (ctxt, c);

	io_progress_unset (context);
	io_progress_range_pop (context);

	child = e_xml_get_child_by_name (tree, CC2XML ("Attributes"));
	if (child && ctxt->version >= GNUM_XML_V5)
		xml_read_wbv_attributes (ctxt, child);

	child = e_xml_get_child_by_name (tree, CC2XML ("UIData"));
	if (child) {
		int sheet_index = 0;
		if (xml_node_get_int (child, "SelectedTab", &sheet_index))
			wb_view_sheet_focus (ctxt->wb_view,
				workbook_sheet_by_index (ctxt->wb, sheet_index));
	}

	child = e_xml_get_child_by_name (tree, CC2XML ("Calculation"));
	if (child != NULL) {
		gboolean b;
		int 	 i;
		double	 d;

		if (xml_node_get_bool (child, "ManualRecalc", &b))
			workbook_autorecalc_enable (ctxt->wb, !b);
		if (xml_node_get_bool   (child, "EnableIteration", &b))
			workbook_iteration_enabled (ctxt->wb, b);
		if (xml_node_get_int    (child, "MaxIterations", &i))
			workbook_iteration_max_number (ctxt->wb, i);
		if (xml_node_get_double (child, "IterationTolerance", &d))
			workbook_iteration_tolerance (ctxt->wb, d);
	}

	/* gnm_setlocale restores bools to locale translation */
	gnm_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	gnm_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

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
	guint8 const *buf;
	gsf_off_t input_size;
	GString *buffer;
	guint ui;
	GsfInput *old_input = input;

	buf = gsf_input_read (input, strlen (noencheader), NULL);
	if (!buf || strncmp (noencheader, buf, strlen (noencheader)) != 0)
		return input;

	input_size = gsf_input_remaining (input);
	buffer = g_string_sized_new (input_size + strlen (encheader));
	g_string_append (buffer, encheader);
	if (!gsf_input_read (input, input_size, buffer->str + strlen (encheader))) {
		g_string_free (buffer, TRUE);
		return input;
	}
	buffer->len = input_size + strlen (encheader);
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
		}
	}

	if (g_get_charset (NULL)) {
		input = gsf_input_memory_new (buffer->str, buffer->len, TRUE);
		g_string_free (buffer, FALSE);
		if (!quiet)
			g_warning ("Converted xml document with no encoding from pseudo-UTF-8 to UTF-8.");
	} else {
		gsize bytes_written;
		char *converted =
			g_locale_to_utf8 (buffer->str, buffer->len,
					  NULL, &bytes_written, NULL);
		g_string_free (buffer, TRUE);
		if (!converted) {
			gsf_input_seek (input, 0, G_SEEK_SET);
			if (!quiet)
				g_warning ("Failed to convert xml document with no encoding from locale to UTF-8.");
			return input;
		}

		input = gsf_input_memory_new (converted, bytes_written, TRUE);
		if (!quiet)
			g_warning ("Converted xml document with no encoding from locale to UTF-8.");
	}

	g_object_unref (old_input);
	return input;
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
			error_info_new_vprintf (GNM_ERROR, fmt, args));
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
	ei = error_info_new_vprintf (GNM_ERROR, fmt, args);
	va_end (args);

	if (gnumeric_io_error_occurred (io_context))
		gnumeric_io_error_push (io_context, ei);
	else
		gnumeric_io_error_info_set (io_context, ei);
}


static xmlSAXHandler xml_sax_prober;
static gboolean
xml_probe (GnmFileOpener const *fo, GsfInput *input, FileProbeLevel pl)
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
gnumeric_xml_read_workbook (GnmFileOpener const *fo,
                            IOContext *context,
                            WorkbookView *wb_view,
                            GsfInput *input)
{
	xmlParserCtxtPtr pctxt;
	xmlDocPtr res = NULL;
	xmlNsPtr gmr;
	XmlParseContext *ctxt;
	GnumericXMLVersion    version;
	guint8 const *buf;
	gsf_off_t size;
	size_t len;

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
		gnm_cmd_context_error_import (GNM_CMD_CONTEXT (context),
			_("The file is not a Gnumeric Workbook file"));
		return;
	}

	/* Parse the file */
	ctxt = xml_parse_ctx_new (res, gmr, wb_view);
	ctxt->version = version;
	xml_workbook_read (context, ctxt, res->xmlRootNode);
	workbook_set_saveinfo (wb_view_workbook (ctxt->wb_view),
		FILE_FL_AUTO, gnm_file_saver_for_id ("Gnumeric_xml_sax:xml_sax"));

	xml_parse_ctx_destroy (ctxt);
	xmlFreeDoc (res);
}

/*
 * Save a Workbook in an XML file
 * One build an in-memory XML tree and save it to a file.
 */
static void
gnumeric_xml_write_workbook (GnmFileSaver const *fs,
                             IOContext *context,
                             WorkbookView const *wb_view,
                             GsfOutput *output)
{
	xmlDocPtr xml;
	XmlParseContext *ctxt;
	char const *extension;
	GsfOutput *gzout = NULL;
	const char *filename;

	g_return_if_fail (wb_view != NULL);
	g_return_if_fail (GSF_IS_OUTPUT (output));

	xml = xmlNewDoc (CC2XML ("1.0"));
	if (xml == NULL) {
		gnm_cmd_context_error_export (GNM_CMD_CONTEXT (context),
			_("Failure saving file"));
		return;
	}

	/* we share context with import const_cast is safe */
	ctxt = xml_parse_ctx_new (xml, NULL, (WorkbookView *)wb_view);
	ctxt->io_context = context;
	xml->xmlRootNode = xml_workbook_write (ctxt);
	xml_parse_ctx_destroy (ctxt);

	/* If the suffix is .xml disable compression */
	filename = gsf_output_name (output);
	extension = filename ? gsf_extension_pointer (filename) : NULL;
	if (extension == NULL ||
	    g_ascii_strcasecmp (extension, "xml") != 0 ||
	    gnm_app_prefs->xml_compression_level != 0) {
		gzout  = gsf_output_gzip_new (output, NULL);
		output = gzout;
	}
	xmlIndentTreeOutput = TRUE;
	if (gsf_xmlDocFormatDump (output, xml, "UTF-8", TRUE) < 0)
		gnm_cmd_context_error_export (GNM_CMD_CONTEXT (context),
				     "Error saving XML");
	if (gzout) {
		gsf_output_close (gzout);
		g_object_unref (gzout);
	}

	xmlFreeDoc (xml);
}

void
xml_init (void)
{
	GSList *suffixes = gnm_slist_create (g_strdup ("gnumeric"), g_strdup ("xml"), NULL);
	GSList *mimes = gnm_slist_create (g_strdup ("application/x-gnumeric"), NULL);
	xml_sax_prober.comment    = NULL;
	xml_sax_prober.warning    = NULL;
	xml_sax_prober.error      = (errorSAXFunc) xml_probe_problem;
	xml_sax_prober.fatalError = (fatalErrorSAXFunc) xml_probe_problem;
	xml_sax_prober.startElement = (startElementSAXFunc) xml_probe_start_element;
	gnm_file_opener_register (gnm_file_opener_new (
		"Gnumeric_XmlIO:gnum_xml",
		_("Gnumeric XML (*.gnumeric)"),
		suffixes, mimes,
		xml_probe, gnumeric_xml_read_workbook), 50);
	gnm_file_saver_register_as_default (gnm_file_saver_new (
		"Gnumeric_XmlIO:gnum_xml", "gnumeric",
		_("Gnumeric XML (*.gnumeric) original slow exporter"),
		FILE_FL_AUTO, gnumeric_xml_write_workbook), 30);
}

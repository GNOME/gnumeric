/* vim: set sw=8: */
/*
 * xml-io.c: save/read gnumeric Sheets using an XML encoding.
 *
 * Authors:
 *   Daniel Veillard <Daniel.Veillard@w3.org>
 *   Miguel de Icaza <miguel@gnu.org>
 *   Jody Goldberg <jody@gnome.org>
 */
#include <config.h>
#include "xml-io.h"
#include "style-color.h"
#include "style-border.h"
#include "style.h"
#include "sheet.h"
#include "sheet-style.h"
#include "sheet-object.h"
#include "sheet-object-cell-comment.h"
#include "str.h"
#include "print-info.h"
#include "file.h"
#include "expr.h"
#include "expr-name.h"
#include "cell.h"
#include "value.h"
#include "validation.h"
#include "sheet-merge.h"
#include "io-context.h"
#include "command-context.h"
#include "workbook-control.h"
#include "workbook-view.h"
#include "workbook.h"
#include "selection.h"
#include "clipboard.h"
#include "format.h"
#include "ranges.h"
#include "file.h"
#include "str.h"
#include "plugin-util.h"

#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <zlib.h>
#include <string.h>
#include <gal/util/e-xml-utils.h>
#include <gal/widgets/e-colors.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-util.h>
#include <libgnome/gnome-config.h>
#include <locale.h>
#include <math.h>
#include <limits.h>
#ifdef ENABLE_BONOBO
#include <bonobo/bonobo-exception.h>
#endif

/* Precision to use when saving point measures. */
#define POINT_SIZE_PRECISION 3

/* FIXME - tune the values below */
#define XML_INPUT_BUFFER_SIZE      4096
#define N_ELEMENTS_BETWEEN_UPDATES 20

/* ------------------------------------------------------------------------- */

static GnumFileOpener *xml_opener = NULL;
static GnumFileSaver  *xml_saver = NULL;

GnumFileOpener *
gnumeric_xml_get_opener (void)
{
	return xml_opener;
}

GnumFileSaver *
gnumeric_xml_get_saver (void)
{
	return xml_saver;
}

/* ------------------------------------------------------------------------- */

XmlParseContext *
xml_parse_ctx_new_full (xmlDocPtr             doc,
			xmlNsPtr              ns,
			GnumericXMLVersion    version,
			XmlSheetObjectReadFn  read_fn,
			XmlSheetObjectWriteFn write_fn,
			gpointer              user_data)
{
	XmlParseContext *ctxt = g_new0 (XmlParseContext, 1);

	ctxt->version      = version;
	ctxt->doc          = doc;
	ctxt->ns           = ns;
	ctxt->expr_map     = g_hash_table_new (g_direct_hash, g_direct_equal);
	ctxt->shared_exprs = g_ptr_array_new ();

	ctxt->write_fn     = write_fn;
	ctxt->read_fn      = read_fn;
	ctxt->user_data    = user_data;

	return ctxt;
}

XmlParseContext *
xml_parse_ctx_new (xmlDocPtr doc,
		   xmlNsPtr  ns)
{
	return xml_parse_ctx_new_full (
		doc, ns, GNUM_XML_LATEST, NULL, NULL, NULL);
}

void
xml_parse_ctx_destroy (XmlParseContext *ctxt)
{
	g_return_if_fail (ctxt != NULL);

	g_hash_table_destroy (ctxt->expr_map);
	g_ptr_array_free (ctxt->shared_exprs, TRUE);

	g_free (ctxt);
}

/* ------------------------------------------------------------------------- */

/*
 * Internal stuff: xml helper functions.
 */

static void
xml_arg_set (GtkArg *arg, const gchar *string)
{
	switch (arg->type) {
	case GTK_TYPE_CHAR:
		GTK_VALUE_CHAR (*arg) = string[0];
		break;
	case GTK_TYPE_UCHAR:
		GTK_VALUE_UCHAR (*arg) = string[0];
		break;
	case GTK_TYPE_BOOL:
		if (!strcmp (string, "TRUE"))
			GTK_VALUE_BOOL (*arg) = TRUE;
		else
			GTK_VALUE_BOOL (*arg) = FALSE;
		break;
	case GTK_TYPE_INT:
		GTK_VALUE_INT (*arg) = atoi (string);
		break;
	case GTK_TYPE_UINT:
		GTK_VALUE_UINT (*arg) = atoi (string);
		break;
	case GTK_TYPE_LONG:
		GTK_VALUE_LONG (*arg) = atol (string);
		break;
	case GTK_TYPE_ULONG:
		GTK_VALUE_ULONG (*arg) = atol (string);
		break;
	case GTK_TYPE_FLOAT:
		GTK_VALUE_FLOAT (*arg) = atof (string);
		break;
	case GTK_TYPE_DOUBLE:
		GTK_VALUE_DOUBLE (*arg) = atof (string);
		break;
	case GTK_TYPE_STRING:
		GTK_VALUE_STRING (*arg) = g_strdup (string);
		break;
	}
}

static char *
xml_arg_get (GtkArg *arg)
{
	switch (arg->type) {
	case GTK_TYPE_CHAR:
		return g_strdup (&GTK_VALUE_CHAR (*arg));
	case GTK_TYPE_UCHAR:
		return g_strdup (&GTK_VALUE_UCHAR (*arg));
	case GTK_TYPE_BOOL:
		if (GTK_VALUE_BOOL (*arg))
			return g_strdup ("TRUE");
		else
			return g_strdup ("FALSE");
	case GTK_TYPE_INT:
		return g_strdup_printf("%i", GTK_VALUE_INT (*arg));
	case GTK_TYPE_UINT:
		return g_strdup_printf("%u", GTK_VALUE_UINT (*arg));
	case GTK_TYPE_LONG:
		return g_strdup_printf("%li", GTK_VALUE_LONG (*arg));
	case GTK_TYPE_ULONG:
		return g_strdup_printf("%lu", GTK_VALUE_ULONG (*arg));
	case GTK_TYPE_FLOAT:
		return g_strdup_printf("%f", GTK_VALUE_FLOAT (*arg));
	case GTK_TYPE_DOUBLE:
		return g_strdup_printf("%f", GTK_VALUE_DOUBLE (*arg));
	case GTK_TYPE_STRING:
		return g_strdup (GTK_VALUE_STRING (*arg));
	}

	return NULL;
}

/* Get an xmlChar * value for a node carried as an attibute
 * result must be xmlFree
 */
xmlChar *
xml_node_get_cstr (xmlNodePtr node, char const *name)
{
	return name ? xmlGetProp (node, name) : xmlNodeGetContent (node);
}
void
xml_node_set_cstr (xmlNodePtr node, char const *name, char const *val)
{
	if (name)
		xmlSetProp (node, name, val);
	else
		xmlNodeSetContent (node, val);
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
	*val = strtol (buf, &end, 10);
	xmlFree (buf);

	return ((char *)buf != end) && (errno != ERANGE);
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
	*val = strtod (buf, &end);
	xmlFree (buf);

	return ((char *)buf != end) && (errno != ERANGE);
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

StyleColor *
xml_node_get_color (xmlNodePtr node, char const *name)
{
	StyleColor *res = NULL;
	xmlChar *color;
	int red, green, blue;

	color = xmlGetProp (node, name);
	if (color == NULL)
		return 0;
	if (sscanf (color, "%X:%X:%X", &red, &green, &blue) == 3)
		res = style_color_new (red, green, blue);
	xmlFree (color);
	return res;
}
void
xml_node_set_color (xmlNodePtr node, char const *name, StyleColor const *val)
{
	char str[4 * sizeof (val->color)];
	sprintf (str, "%X:%X:%X", val->color.red, val->color.green, val->color.blue);
	xml_node_set_cstr (node, name, str);
}

static gboolean
xml_node_get_cellpos (xmlNodePtr node, char const *name, CellPos *val)
{
	xmlChar *buf;
	int dummy;
	gboolean res;

	buf = xml_node_get_cstr (node, name);
	if (val == NULL)
		return FALSE;
	res = parse_cell_name (buf, &val->col, &val->row, TRUE, &dummy);
	xmlFree (buf);
	return res;
}

static void
xml_node_set_cellpos (xmlNodePtr node, char const *name, CellPos const *val)
{
	xml_node_set_cstr (node, name, cell_pos_name (val));
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
	xmlNodePtr  child;
	char       *txt = "points";
	char       *tstr;

	if (pu == NULL || name == NULL)
		return;

	switch (pu->desired_display) {
	case UNIT_POINTS:
		txt = "points";
		break;
	case UNIT_MILLIMETER:
		txt = "mm";
		break;
	case UNIT_CENTIMETER:
		txt = "cm";
		break;
	case UNIT_INCH:
		txt = "in";
		break;
	}

	child = xmlNewChild (node, NULL, name, NULL);

	xml_node_set_points (child, "Points", pu->points);

	tstr = xmlEncodeEntitiesReentrant (node->doc, txt);
	xml_node_set_cstr (child, "PrefUnit", tstr);
	if (tstr) xmlFree (tstr);
}

static void
xml_node_get_print_unit (xmlNodePtr node, PrintUnit * const pu)
{
	char       *txt;

	g_return_if_fail (pu != NULL);
	g_return_if_fail (node != NULL);

	xml_node_get_double (node, "Points", &pu->points);
	txt = xmlGetProp  (node, "PrefUnit");
	if (txt) {
		if (!g_strcasecmp (txt, "points"))
			pu->desired_display = UNIT_POINTS;
		else if (!strcmp (txt, "mm"))
			pu->desired_display = UNIT_MILLIMETER;
		else if (!strcmp (txt, "cm"))
			pu->desired_display = UNIT_CENTIMETER;
		else if (!strcmp (txt, "in"))
			pu->desired_display = UNIT_INCH;
		xmlFree (txt);
	}
}

static gboolean
xml_node_get_range (xmlNodePtr tree, Range *r)
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
xml_node_set_range (xmlNodePtr tree, Range const *r)
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
	Range r;
	int row, col;
	Sheet *sheet = ctxt->sheet;
	xmlNodePtr sel, selections = e_xml_get_child_by_name (tree, "Selections");

	if (selections == NULL)
		return;

	sheet_selection_reset (sheet);
	for (sel = selections->xmlChildrenNode; sel; sel = sel->next)
		if (xml_node_get_range (sel, &r))
			sheet_selection_add_range (sheet,
						   r.start.col, r.start.row,
						   r.start.col, r.start.row,
						   r.end.col, r.end.row);

	if (xml_node_get_int (selections, "CursorCol", &col) &&
	    xml_node_get_int (selections, "CursorRow", &row))
		sheet_set_edit_pos (sheet, col, row);
}

static void
xml_write_selection_info (XmlParseContext *ctxt, Sheet const *sheet, xmlNodePtr tree)
{
	GList *ptr, *copy;
	tree = xmlNewChild (tree, ctxt->ns, "Selections", NULL);

	/* Insert the selections in REVERSE order */
	copy = g_list_copy (sheet->selections);
	ptr = g_list_reverse (copy);
	for (; ptr != NULL ; ptr = ptr->next) {
		Range const *r = ptr->data;
		xmlNodePtr child = xmlNewChild (tree, ctxt->ns, "Selection", NULL);
		xml_node_set_range (child, r);
	}
	g_list_free (copy);

	xml_node_set_int (tree, "CursorCol", sheet->edit_pos_real.col);
	xml_node_set_int (tree, "CursorRow", sheet->edit_pos_real.row);
}

/*
 * Create an XML subtree of doc equivalent to the given StyleBorder.
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
			const MStyle *style)
{
	xmlNodePtr cur;
	xmlNodePtr side;
	int        i;

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
		StyleBorder const *border;
		if (mstyle_is_element_set (style, i) &&
		    NULL != (border = mstyle_get_border (style, i))) {
			break;
		}
	}
	if (i > MSTYLE_BORDER_DIAGONAL)
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "StyleBorder", NULL);

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
		StyleBorder const *border;
		if (mstyle_is_element_set (style, i) &&
		    NULL != (border = mstyle_get_border (style, i))) {
			StyleBorderType t = border->line_type;
			StyleColor *col   = border->color;
 			side = xmlNewChild (cur, ctxt->ns,
					    StyleSideNames [i - MSTYLE_BORDER_TOP],
 					    NULL);
			xml_node_set_int (side, "Style", t);
			if (t != STYLE_BORDER_NONE)
				xml_node_set_color (side, "Color", col);
 		}
	}
	return cur;
}

/*
 * Create a StyleBorder equivalent to the XML subtree of doc.
 */
static void
xml_read_style_border (XmlParseContext *ctxt, xmlNodePtr tree, MStyle *mstyle)
{
	xmlNodePtr side;
	int        i;

	if (strcmp (tree->name, "StyleBorder")){
		fprintf (stderr,
			 "xml_read_style_border: invalid element type %s, 'StyleBorder' expected`\n",
			 tree->name);
	}

	for (i = MSTYLE_BORDER_TOP; i <= MSTYLE_BORDER_DIAGONAL; i++) {
 		if ((side = e_xml_get_child_by_name (tree,
					      StyleSideNames [i - MSTYLE_BORDER_TOP])) != NULL) {
			int		 t;
			StyleColor      *color = NULL;
			StyleBorder    *border;
			xml_node_get_int (side, "Style", &t);
			if (t != STYLE_BORDER_NONE)
				color = xml_node_get_color (side, "Color");
			border = style_border_fetch ((StyleBorderType)t, color,
						     style_border_get_orientation (i));
			mstyle_set_border (mstyle, i, border);
 		}
	}
}

static xmlNodePtr
xml_write_style_condition_chain (XmlParseContext *ctxt, StyleCondition *sc)
{
	xmlNodePtr cur;
	StyleCondition *sci;
	
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "StyleConditionChain", NULL);
	for (sci = sc; sci != NULL; sci = sci->next) {
		xmlNodePtr parent, ptr;

		parent = xmlNewChild (cur, ctxt->ns, "StyleCondition", NULL);
		xml_node_set_int (parent, "Type", sci->type);
		xml_node_set_int (parent, "NextOperator", sci->next_op);
		switch (sci->type) {
		case SCT_EXPR :
			ptr = xmlNewChild (parent, ctxt->ns, "Expr", NULL);
			xml_node_set_int (ptr, "Operator", sci->u.expr.op);

			if (sci->u.expr.dep.expression) {
				Sheet *sheet = sci->u.expr.dep.sheet;
				ParsePos pos, *pp;
				char *val, *tstr;
			
				pp = parse_pos_init (&pos, sheet->workbook, sheet, 0, 0);
				val = expr_tree_as_string (sci->u.expr.dep.expression, pp);
				
				tstr = xmlEncodeEntitiesReentrant (ctxt->doc, val);
				xml_node_set_cstr (ptr, "Expression", val);
				if (tstr) xmlFree (tstr);
				g_free (val);
			}
			break;
		case SCT_CONSTRAINT :
			ptr = xmlNewChild (parent, ctxt->ns, "Constraint", NULL);
			xml_node_set_int (ptr, "Value", sci->u.constraint);
			break;
		case SCT_FLAGS :
			ptr = xmlNewChild (parent, ctxt->ns, "Flags", NULL);
			xml_node_set_int (ptr, "Value", sci->u.flags);
			break;
		default :
			g_warning ("Unknown StyleCondition Type");
		}
	}
	
	return cur;
}

static StyleCondition *
xml_read_style_condition_chain (XmlParseContext *ctxt, xmlNodePtr tree)
{
	StyleCondition *scf = NULL;
	StyleCondition *scl = NULL;
	xmlNodePtr parent;
	
	if (strcmp (tree->name, "StyleConditionChain")){
		fprintf (stderr,
			 "xml_read_style_condition: invalid element type %s, 'StyleConditionChain' expected`\n",
			 tree->name);
	}
	
	parent = tree->xmlChildrenNode;
	while (parent) {
		StyleConditionBool next_op = 0;
		StyleConditionType type = 0;
		StyleCondition *sc = NULL;
		xmlNodePtr child = parent->xmlChildrenNode;
		
		if (!parent->name || strcmp (parent->name, "StyleCondition"))
			fprintf (stderr,
				 "xml_read_style_condition: invalid element type %s, 'StyleCondition' expected`\n",
				 parent->name);

		xml_node_get_int (parent, "Type", (int*) &type);
		xml_node_get_int (parent, "NextOperator", (int*) &next_op);
		
		switch (type) {
		case SCT_EXPR :
			if (child && child->name && !strcmp (child->name, "Expr")) {
				ExprTree *expr = NULL;
				ParsePos pos, *pp;
				xmlChar *s;
				int op;
			
				xml_node_get_int (child, "Operator", &op);
				s = xml_node_get_cstr (child, "Expression");

				if (s) {
					pp = parse_pos_init (&pos, ctxt->wb,
							     ctxt->sheet, 0, 0);
					if ((expr = expr_parse_str_simple (s, pp)) == NULL)
						fprintf (stderr, "xml_read_style_condition: empty/invalid expression. condition damaged!\n");
					xmlFree (s);
				} else
					g_warning ("StyleConditionExpression without Expression!");
			
				sc = style_condition_new_expr (ctxt->sheet, op, expr);
				expr_tree_unref (expr);
			} else
				fprintf (stderr,
					 "xml_read_style_condition: invalid element type %s, 'Expr' expected`\n",
					 child->name);
			break;
		case SCT_CONSTRAINT :
			if (child && child->name && !strcmp (child->name, "Constraint")) {
				StyleConditionConstraint constraint;
				
				xml_node_get_int (child, "Value", (int *) &constraint);
				sc = style_condition_new_constraint (constraint);
			} else
				fprintf (stderr,
					 "xml_read_style_condition: invalid element type %s, 'Constraint' expected`\n",
					 child->name);
			break;
		case SCT_FLAGS :
			if (child && child->name && !strcmp (child->name, "Flags")) {
				StyleConditionFlags flags;
				
				xml_node_get_int (child, "Value", (int *) &flags);
				sc = style_condition_new_flags (flags);
			} else
				fprintf (stderr,
					 "xml_read_style_condition: invalid element type %s, 'Flags' expected`\n",
					 child->name);
			break;
		default :
			g_warning ("Unknown StyleCondition type");
		}

		if (!sc) {
			g_warning ("Broken StyleConditionChain!");
			break;
		}
		
		if (scl)
			style_condition_chain (scl, next_op, sc);
		else
			scf = sc;
		scl = sc;

		parent = parent->next;
	}
	
	return scf;
}

/*
 * Create an XML subtree of doc equivalent to the given Style.
 */
xmlNodePtr
xml_write_style (XmlParseContext *ctxt,
		 MStyle *style)
{
	xmlNodePtr  cur, child;
	char       *tstr;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Style", NULL);

	if (mstyle_is_element_set (style, MSTYLE_ALIGN_H))
		xml_node_set_int (cur, "HAlign", mstyle_get_align_h (style));
	if (mstyle_is_element_set (style, MSTYLE_ALIGN_V))
		xml_node_set_int (cur, "VAlign", mstyle_get_align_v (style));
	if (mstyle_is_element_set (style, MSTYLE_WRAP_TEXT))
		xml_node_set_int (cur, "WrapText", mstyle_get_wrap_text (style));
	if (mstyle_is_element_set (style, MSTYLE_ORIENTATION))
		xml_node_set_int (cur, "Orient", mstyle_get_orientation (style));
	if (mstyle_is_element_set (style, MSTYLE_PATTERN))
		xml_node_set_int (cur, "Shade", mstyle_get_pattern (style));
	if (mstyle_is_element_set (style, MSTYLE_INDENT))
		xml_node_set_int (cur, "Indent", mstyle_get_indent (style));
	if (mstyle_is_element_set (style, MSTYLE_CONTENT_LOCKED))
		xml_node_set_int (cur, "Locked", mstyle_get_content_locked (style));
	if (mstyle_is_element_set (style, MSTYLE_CONTENT_HIDDEN))
		xml_node_set_int (cur, "Hidden", mstyle_get_content_hidden (style));

	if (mstyle_is_element_set (style, MSTYLE_COLOR_FORE))
		xml_node_set_color (cur, "Fore", mstyle_get_color (style, MSTYLE_COLOR_FORE));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_BACK))
		xml_node_set_color (cur, "Back", mstyle_get_color (style, MSTYLE_COLOR_BACK));
	if (mstyle_is_element_set (style, MSTYLE_COLOR_PATTERN))
		xml_node_set_color (cur, "PatternColor", mstyle_get_color (style, MSTYLE_COLOR_PATTERN));
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

		tstr = xmlEncodeEntitiesReentrant (ctxt->doc, fontname);
		child = xmlNewChild (cur, ctxt->ns, "Font", tstr);
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

	if (mstyle_is_element_set (style, MSTYLE_VALIDATION)) {
		Validation *v = mstyle_get_validation (style);
		xmlNodePtr xsc;
		
		child = xmlNewChild (cur, ctxt->ns, "Validation", NULL);
		xml_node_set_int (child, "Style", v->vs);

		if (v->title) {
			tstr = xmlEncodeEntitiesReentrant (ctxt->doc, v->title->str);
			xml_node_set_cstr (child, "Title", tstr);
			if (tstr) xmlFree (tstr);
		}

		if (v->msg) {
			tstr = xmlEncodeEntitiesReentrant (ctxt->doc, v->msg->str);
			xml_node_set_cstr (child, "Message", tstr);
			if (tstr) xmlFree (tstr);
		}
		
		xsc = xml_write_style_condition_chain (ctxt, v->sc);
		if (xsc)
			xmlAddChild (child, xsc);
	}

	child = xml_write_style_border (ctxt, style);
	if (child)
		xmlAddChild (cur, child);

	return cur;
}

static xmlNodePtr
xml_write_names (XmlParseContext *ctxt, GList *names)
{
	char *txt, *expr_str;
	xmlNodePtr  namesContainer, nameNode;
	NamedExpression const *nexpr;

	namesContainer = xmlNewDocNode (ctxt->doc, ctxt->ns, "Names", NULL);

	for (; names != NULL ; names = names->next) {
		nexpr = names->data;

		g_return_val_if_fail (nexpr != NULL, NULL);

		nameNode = xmlNewChild (namesContainer, ctxt->ns, "Name", NULL);

		txt = xmlEncodeEntitiesReentrant (ctxt->doc, nexpr->name->str);
		xmlNewChild (nameNode, ctxt->ns, "name", txt);
		if (txt) xmlFree (txt);

		expr_str = expr_name_as_string (nexpr, NULL);
		txt = xmlEncodeEntitiesReentrant (ctxt->doc, expr_str);
		xmlNewChild (nameNode, ctxt->ns, "value", txt);
		if (txt) xmlFree (txt);
		g_free (expr_str);

		xmlNewChild (nameNode, ctxt->ns, "position",
			cell_pos_name (&nexpr->pos.eval));
	}

	return namesContainer;
}

static void
xml_read_names (XmlParseContext *ctxt, xmlNodePtr tree,
		Workbook *wb, Sheet *sheet)
{
	xmlNode *id;
	xmlNode *expr;
	xmlNode *position;
	xmlNode *name = e_xml_get_child_by_name (tree, "Names");
	xmlChar *name_str, *expr_str;

	if (name == NULL)
		return;

	for (name = name->xmlChildrenNode; name ; name = name->next) {
		ParseError  perr;
		ParsePos    pp;

		if (name->name == NULL || strcmp (name->name, "Name"))
			continue;

		id = e_xml_get_child_by_name (name, "name");
		expr = e_xml_get_child_by_name (name, "value");
		position = e_xml_get_child_by_name (name, "position");

		g_return_if_fail (id != NULL && expr != NULL);

		name_str = xmlNodeGetContent (id);
		expr_str = xmlNodeGetContent (expr);
		g_return_if_fail (name_str != NULL && expr_str != NULL);

		parse_pos_init (&pp, wb, sheet, 0, 0);
		if (position != NULL) {
			char *pos_txt = xmlNodeGetContent (position);
			if (pos_txt != NULL) {
				CellRef tmp;
				char const *res = cellref_a1_get (&tmp, pos_txt, &pp.eval);
				if (res != NULL && *res == '\0') {
					pp.eval.col = tmp.col;
					pp.eval.row = tmp.row;
				}
				xmlFree (pos_txt);
			}
		}

		parse_error_init (&perr);
		if (!expr_name_create (&pp, name_str, expr_str, &perr))
			g_warning (perr.message);
		parse_error_free (&perr);

		xmlFree (name_str);
		xmlFree (expr_str);
	}
}

static xmlNodePtr
xml_write_summary (XmlParseContext *ctxt, SummaryInfo *summary_info)
{
	GList *items, *m;
	char *tstr;
	xmlNodePtr cur;

	if (!summary_info)
		return NULL;

	m = items = summary_info_as_list (summary_info);

	if (!items)
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Summary", NULL);

	while (items) {
		xmlNodePtr   tmp;
		SummaryItem *sit = items->data;
		if (sit) {
			char *text;

			tmp = xmlNewDocNode (ctxt->doc, ctxt->ns, "Item", NULL);
			tstr = xmlEncodeEntitiesReentrant (ctxt->doc, sit->name);
			xmlNewChild (tmp, ctxt->ns, "name", tstr);
			if (tstr) xmlFree (tstr);

			if (sit->type == SUMMARY_INT) {
				text = g_strdup_printf ("%d", sit->v.i);
				tstr = xmlEncodeEntitiesReentrant (ctxt->doc, text);
				xmlNewChild (tmp, ctxt->ns, "val-int", tstr);
				if (tstr) xmlFree (tstr);
			} else {
				text = summary_item_as_text (sit);
				tstr = xmlEncodeEntitiesReentrant (ctxt->doc, text);
				xmlNewChild (tmp, ctxt->ns, "val-string", tstr);
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

	child = tree->xmlChildrenNode;
	while (child) {
		char *name = NULL;

		if (child->name && !strcmp (child->name, "Item")) {
			xmlNodePtr bits;

			bits = child->xmlChildrenNode;
			while (bits) {
				SummaryItem *sit = NULL;

				if (!strcmp (bits->name, "name")) {
					name = xmlNodeGetContent (bits);
				} else {
					char *txt;
					g_return_if_fail (name);

					txt = xmlNodeGetContent (bits);
					if (txt != NULL){
						if (!strcmp (bits->name, "val-string"))
							sit = summary_item_new_string (name, txt);
						else if (!strcmp (bits->name, "val-int"))
							sit = summary_item_new_int (name, atoi (txt));

						if (sit)
							summary_info_add (summary_info, sit);
						xmlFree (txt);
					}
				}
				bits = bits->next;
			}
		}
		if (name){
			xmlFree (name);
			name = NULL;
		}
		child = child->next;
	}
}

static void
xml_node_set_print_hf (xmlNodePtr node, char const *name,
		       PrintHF const *hf)
{
	xmlNodePtr  child;

	if (hf == NULL || name == NULL)
		return;

	child = xmlNewChild (node, NULL, name, NULL);
	xml_node_set_cstr (child, "Left", hf->left_format);
	xml_node_set_cstr (child, "Middle", hf->middle_format);
	xml_node_set_cstr (child, "Right", hf->right_format);
}

static void
xml_node_get_print_hf (xmlNodePtr node, PrintHF *hf)
{
	char *txt;

	g_return_if_fail (hf != NULL);
	g_return_if_fail (node != NULL);

	txt = xmlGetProp (node, "Left");
	if (txt) {
		if (hf->left_format)
			g_free (hf->left_format);
		hf->left_format = g_strdup (txt);
		xmlFree (txt);
	}

	txt = xmlGetProp (node, "Middle");
	if (txt) {
		if (hf->middle_format)
			g_free (hf->middle_format);
		hf->middle_format = g_strdup (txt);
		xmlFree (txt);
	}

	txt = xmlGetProp (node, "Right");
	if (txt) {
		if (hf->right_format)
			g_free (hf->right_format);
		hf->right_format = g_strdup (txt);
		xmlFree (txt);
	}
}

static void
xml_write_attribute (XmlParseContext *ctxt, xmlNodePtr attr, GtkArg *arg)
{
	xmlChar *tstr;
	gchar *str;

	switch (arg->type) {
	case GTK_TYPE_CHAR:
	case GTK_TYPE_UCHAR:
	case GTK_TYPE_BOOL:
	case GTK_TYPE_INT:
	case GTK_TYPE_UINT:
	case GTK_TYPE_LONG:
	case GTK_TYPE_ULONG:
	case GTK_TYPE_FLOAT:
	case GTK_TYPE_DOUBLE:
	case GTK_TYPE_STRING:
		str = xml_arg_get (arg);
		tstr = xmlEncodeEntitiesReentrant (ctxt->doc, str);
		xmlNewChild (attr, ctxt->ns, "value", tstr);
		if (tstr) {
			xmlFree (tstr);
		}
		g_free (str);
		break;
	}
}

static xmlNodePtr
xml_write_attributes (XmlParseContext *ctxt, guint n_args, GtkArg *args)
{
	xmlNodePtr attributes;
	guint i;

	attributes = xmlNewDocNode (ctxt->doc, ctxt->ns, "Attributes", NULL);

	for (i = 0; i < n_args; args++, i++) {
		xmlNodePtr type, attr = xmlNewChild (attributes, ctxt->ns, "Attribute", NULL);
		xmlChar *tstr = xmlEncodeEntitiesReentrant (ctxt->doc, args->name);

		xmlNewChild (attr, ctxt->ns, "name", tstr);
		if (tstr)
			xmlFree (tstr);

		type = xmlNewChild (attr, ctxt->ns, "type", NULL);
		xml_node_set_int (type, NULL, args->type);

		xml_write_attribute (ctxt, attr, args);
	}

	return attributes;
}

static void
xml_free_arg_list (GList *list)
{
	while (list) {
		GtkArg *arg = list->data;
		if (arg) {
			g_free (arg->name);
			gtk_arg_free (arg, FALSE);
		}
		list = list->next;
	}
}

static void
xml_read_attribute (XmlParseContext *ctxt, xmlNodePtr attr, GtkArg *arg)
{
	xmlNodePtr val;
	char *value;

	switch (arg->type) {
	case GTK_TYPE_CHAR:
	case GTK_TYPE_UCHAR:
	case GTK_TYPE_BOOL:
	case GTK_TYPE_INT:
	case GTK_TYPE_UINT:
	case GTK_TYPE_LONG:
	case GTK_TYPE_ULONG:
	case GTK_TYPE_FLOAT:
	case GTK_TYPE_DOUBLE:
	case GTK_TYPE_STRING:
		val = e_xml_get_child_by_name (attr, "value");
		if (val) {
			value = xmlNodeGetContent (val);
			xml_arg_set (arg, value);

			if (value){
				xmlFree (value);
			}
		}
		break;
	}
}

static void
xml_read_attributes (XmlParseContext *ctxt, xmlNodePtr tree, GList **list)
{
	xmlNodePtr child, subchild;
	GtkArg *arg;

	g_return_if_fail (ctxt != NULL);
	g_return_if_fail (tree != NULL);

	child = tree->xmlChildrenNode;
	while (child) {
		char *name = NULL;
		int type = 0;

		if (child->name && !strcmp (child->name, "Attribute")) {

			subchild = e_xml_get_child_by_name (child, "name");
			if (subchild) {
				name = xmlNodeGetContent (subchild);
			}

			xml_node_get_int (child, "type", &type);

			if (name && type) {
				arg = gtk_arg_new (type);
				arg->name = g_strdup (name);
				xml_read_attribute (ctxt, child, arg);

				*list = g_list_prepend (*list, arg);
			}
		}
		if (name){
			xmlFree (name);
			name = NULL;
		}
		child = child->next;
	}
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

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, name, NULL);
	s = (range->use) ? range_name (&range->range) : "";
	xml_node_set_cstr  (cur, "value", s);

	return cur;
}

static xmlNodePtr
xml_write_print_info (XmlParseContext *ctxt, PrintInformation *pi)
{
	xmlNodePtr cur, child;

	g_return_val_if_fail (pi != NULL, NULL);

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "PrintInformation", NULL);

	child = xmlNewChild (cur, ctxt->ns, "Margins", NULL);
	xml_node_set_print_unit (child, "top",    &pi->margins.top);
	xml_node_set_print_unit (child, "bottom", &pi->margins.bottom);
	xml_node_set_print_unit (child, "left",   &pi->margins.left);
	xml_node_set_print_unit (child, "right",  &pi->margins.right);
	xml_node_set_print_unit (child, "header", &pi->margins.header);
	xml_node_set_print_unit (child, "footer", &pi->margins.footer);

	child = xmlNewChild (cur, ctxt->ns, "Scale", NULL);
	if (pi->scaling.type == PERCENTAGE) {
		xml_node_set_cstr  (child, "type", "percentage");
		xml_node_set_double  (child, "percentage", pi->scaling.percentage, -1);
	} else {
		xml_node_set_cstr  (child, "type", "size_fit");
		xml_node_set_double  (child, "cols", pi->scaling.dim.cols, -1);
		xml_node_set_double  (child, "rows", pi->scaling.dim.rows, -1);
	}

	child = xmlNewChild (cur, ctxt->ns, "vcenter", NULL);
	xml_node_set_int  (child, "value", pi->center_vertically);
	child = xmlNewChild (cur, ctxt->ns, "hcenter", NULL);
	xml_node_set_int  (child, "value", pi->center_horizontally);

	child = xmlNewChild (cur, ctxt->ns, "grid", NULL);
	xml_node_set_int  (child, "value",    pi->print_grid_lines);
	child = xmlNewChild (cur, ctxt->ns, "even_if_only_styles", NULL);
	xml_node_set_int  (child, "value",    pi->print_even_if_only_styles);
	child = xmlNewChild (cur, ctxt->ns, "monochrome", NULL);
	xml_node_set_int  (child, "value",    pi->print_black_and_white);
	child = xmlNewChild (cur, ctxt->ns, "draft", NULL);
	xml_node_set_int  (child, "value",    pi->print_as_draft);
	child = xmlNewChild (cur, ctxt->ns, "titles", NULL);
	xml_node_set_int  (child, "value",    pi->print_titles);

	child = xml_write_print_repeat_range (ctxt, "repeat_top", &pi->repeat_top);
	xmlAddChild (cur, child);

	child = xml_write_print_repeat_range (ctxt, "repeat_left", &pi->repeat_left);
	xmlAddChild (cur, child);

	xmlNewChild (cur, ctxt->ns, "order",
		     (pi->print_order == PRINT_ORDER_DOWN_THEN_RIGHT)
		     ? "d_then_r" : "r_then_d");
	xmlNewChild (cur, ctxt->ns, "orientation",
		     (pi->orientation == PRINT_ORIENT_VERTICAL)
		     ? "portrait" : "landscape");

	xml_node_set_print_hf (cur, "Header", pi->header);
	xml_node_set_print_hf (cur, "Footer", pi->footer);

	xmlNewChild (cur, ctxt->ns, "paper", gnome_paper_name (pi->paper));

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
	if (pi->margins.top.points < pi->margins.header.points)
		DSWAP (pi->margins.top.points, pi->margins.header.points);
	if (pi->margins.bottom.points < pi->margins.footer.points)
		DSWAP (pi->margins.bottom.points, pi->margins.footer.points);

}

static void
xml_read_print_margins (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	PrintInformation *pi;

	g_return_if_fail (ctxt != NULL);
	g_return_if_fail (tree != NULL);
	g_return_if_fail (IS_SHEET (ctxt->sheet));

	pi = ctxt->sheet->print_info;

	g_return_if_fail (pi != NULL);

	if ((child = e_xml_get_child_by_name (tree, "top")))
		xml_node_get_print_unit (child, &pi->margins.top);
	if ((child = e_xml_get_child_by_name (tree, "bottom")))
		xml_node_get_print_unit (child, &pi->margins.bottom);
	if ((child = e_xml_get_child_by_name (tree, "left")))
		xml_node_get_print_unit (child, &pi->margins.left);
	if ((child = e_xml_get_child_by_name (tree, "right")))
		xml_node_get_print_unit (child, &pi->margins.right);
	if ((child = e_xml_get_child_by_name (tree, "header")))
		xml_node_get_print_unit (child, &pi->margins.header);
	if ((child = e_xml_get_child_by_name (tree, "footer")))
		xml_node_get_print_unit (child, &pi->margins.footer);
	xml_print_info_fix_margins (pi);
}

static void
xml_read_print_repeat_range (XmlParseContext *ctxt, xmlNodePtr tree, char *name, PrintRepeatRange *range)
{
	xmlNodePtr child;

	g_return_if_fail (ctxt != NULL);
	g_return_if_fail (tree != NULL);
	g_return_if_fail (name != NULL);
	g_return_if_fail (range != NULL);

	range->use = FALSE;
	if (ctxt->version > GNUM_XML_V4 &&
	    (child = e_xml_get_child_by_name (tree, name))) {
		xmlChar *s = xml_node_get_cstr (child, "value");

		if (s) {
			Range r;
			if (parse_range (s,
					 &r.start.col, &r.start.row,
					 &r.end.col, &r.end.row)) {
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

	tree = e_xml_get_child_by_name (tree, "PrintInformation");
	if (tree == NULL)
		return;

	if ((child = e_xml_get_child_by_name (tree, "Margins"))) {
		xml_read_print_margins (ctxt, child);
	}
	if ((child = e_xml_get_child_by_name (tree, "Scale"))) {
		xmlChar *type = xml_node_get_cstr  (child, "type");
		if (type != NULL) {
			if (!strcmp (type, "percentage")) {
				double tmp;
				pi->scaling.type = PERCENTAGE;
				if (xml_node_get_double (child, "percentage", &tmp))
					pi->scaling.percentage = tmp;
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
	if ((child = e_xml_get_child_by_name (tree, "vcenter"))) {
		xml_node_get_int  (child, "value", &b);
		pi->center_vertically   = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, "hcenter"))) {
		xml_node_get_int  (child, "value", &b);
		pi->center_horizontally = (b == 1);
	}

	if ((child = e_xml_get_child_by_name (tree, "grid"))) {
		xml_node_get_int  (child, "value",    &b);
		pi->print_grid_lines  = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, "even_if_only_styles"))) {
		xml_node_get_int  (child, "value",    &b);
		pi->print_even_if_only_styles  = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, "monochrome"))) {
		xml_node_get_int  (child, "value", &b);
		pi->print_black_and_white = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, "draft"))) {
		xml_node_get_int  (child, "value",   &b);
		pi->print_as_draft        = (b == 1);
	}
	if ((child = e_xml_get_child_by_name (tree, "titles"))) {
		xml_node_get_int  (child, "value",  &b);
		pi->print_titles          = (b == 1);
	}

	xml_read_print_repeat_range (ctxt, tree, "repeat_top",
				     &pi->repeat_top);
	xml_read_print_repeat_range (ctxt, tree, "repeat_left",
				     &pi->repeat_left);

	if ((child = e_xml_get_child_by_name (tree, "order"))) {
		char *txt;
		txt = xmlNodeGetContent (child);
		if (!strcmp (txt, "d_then_r"))
			pi->print_order = PRINT_ORDER_DOWN_THEN_RIGHT;
		else
			pi->print_order = PRINT_ORDER_RIGHT_THEN_DOWN;
		xmlFree (txt);
	}

	if ((child = e_xml_get_child_by_name (tree, "orientation"))) {
		char *txt;
		txt = xmlNodeGetContent (child);
		if (!strcmp (txt, "portrait"))
			pi->orientation = PRINT_ORIENT_VERTICAL;
		else
			pi->orientation = PRINT_ORIENT_HORIZONTAL;
		xmlFree (txt);
	}

	if ((child = e_xml_get_child_by_name (tree, "Header")))
		xml_node_get_print_hf (child, pi->header);
	if ((child = e_xml_get_child_by_name (tree, "Footer")))
		xml_node_get_print_hf (child, pi->footer);

	if ((child = e_xml_get_child_by_name (tree, "paper"))) {
		char *txt = xmlNodeGetContent (child);
		pi->paper = gnome_paper_with_name (txt);
		xmlFree (txt);
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
style_font_read_from_x11 (MStyle *mstyle, char const *fontname)
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
MStyle *
xml_read_style (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	char *prop;
	int val;
	StyleColor *c;
	MStyle     *mstyle;

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
	} else if (xml_node_get_int (tree, "Fit", &val))
		mstyle_set_wrap_text (mstyle, val);

	if (xml_node_get_int (tree, "Locked", &val))
		mstyle_set_content_locked (mstyle, val);
	if (xml_node_get_int (tree, "Hidden", &val))
		mstyle_set_content_hidden (mstyle, val);

	if (xml_node_get_int (tree, "VAlign", &val))
		mstyle_set_align_v (mstyle, val);

	if (xml_node_get_int (tree, "Orient", &val))
		mstyle_set_orientation (mstyle, val);

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

	prop = xmlGetProp (tree, "Format");
	if (prop != NULL) {
		mstyle_set_format_text (mstyle, prop);
		xmlFree (prop);
	}

	child = tree->xmlChildrenNode;
	while (child != NULL) {
		if (!strcmp (child->name, "Font")) {
			char *font;
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

			font = xmlNodeGetContent (child);
			if (font) {
				if (*font == '-')
					style_font_read_from_x11 (mstyle, font);
				else
					mstyle_set_font_name (mstyle, font);
				xmlFree (font);
			}

		} else if (!strcmp (child->name, "StyleBorder")) {
			xml_read_style_border (ctxt, child, mstyle);
		} else if (!strcmp (child->name, "Validation")) {
			StyleCondition *sc;
			ValidationStyle vs;
			Validation *v;
			xmlNodePtr ptr;
			xmlChar *title;
			xmlChar *msg;
			
			xml_node_get_int (child, "Style", (int *) &vs);

			if (vs != VALIDATION_STYLE_NONE) {
				title = xml_node_get_cstr (child, "Title");
				if (strlen (title) < 1) {
					xmlFree (title);
					title = NULL;
				}
				msg = xml_node_get_cstr (child, "Message");
				if (strlen (msg) < 1) {
					xmlFree (msg);
					msg = NULL;
				}
			} else {
				title = NULL;
				msg = NULL;
			}

      			if ((ptr = e_xml_get_child_by_name (child, "StyleConditionChain")) != NULL)
				sc = xml_read_style_condition_chain (ctxt, ptr);
			else
				sc = NULL;

			v = validation_new (vs, title, msg, sc);
			mstyle_set_validation (mstyle, v);
			
			xmlFree (msg);
			xmlFree (title);
		} else {
			fprintf (stderr, "xml_read_style: unknown type '%s'\n",
				 child->name);
		}
		child = child->next;
	}

	return mstyle;
}

/*
 * Create an XML subtree of doc equivalent to the given StyleRegion.
 */
static xmlNodePtr
xml_write_style_region (XmlParseContext *ctxt, StyleRegion *region)
{
	xmlNodePtr cur, child;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "StyleRegion", NULL);
	xml_node_set_range (cur, &region->range);

	if (region->style != NULL) {
		child = xml_write_style (ctxt, region->style);
		if (child)
			xmlAddChild (cur, child);
	}
	return cur;
}

/*
 * Create a StyleRegion equivalent to the XML subtree of doc.
 * Return an mstyle and a range in the @range parameter
 */
static MStyle*
xml_read_style_region_ex (XmlParseContext *ctxt, xmlNodePtr tree, Range *range)
{
	xmlNodePtr child;
	MStyle    *style = NULL;

	if (strcmp (tree->name, "StyleRegion")){
		fprintf (stderr,
			 "xml_read_style_region_ex: invalid element type %s, 'StyleRegion' expected`\n",
			 tree->name);
		return NULL;
	}
	xml_node_get_range (tree, range);
	child = tree->xmlChildrenNode;

	if (child)
		style = xml_read_style (ctxt, child);

	return style;
}

/*
 * Create a StyleRegion equivalent to the XML subtree of doc.
 * Return nothing, attach it directly to the sheet in the context
 */
static void
xml_read_style_region (XmlParseContext *ctxt, xmlNodePtr tree)
{
	MStyle *style;
	Range range;

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
					     closure->ctxt->ns, "ColInfo", NULL);
		else
			cur = xmlNewDocNode (closure->ctxt->doc,
					     closure->ctxt->ns, "RowInfo", NULL);

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
xml_write_cell_and_position (XmlParseContext *ctxt, Cell *cell, int col, int row)
{
	xmlNodePtr cur, child = NULL;
	char *text, *tstr;
	ExprArray const *ar;
	gboolean write_contents = TRUE;
	gboolean const is_shared_expr =
	    (cell_has_expr (cell) && expr_tree_is_shared (cell->base.expression));

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Cell", NULL);
	xml_node_set_int (cur, "Col", col);
	xml_node_set_int (cur, "Row", row);

	/* Only the top left corner of an array needs to be saved (>= 0.53) */
	if (NULL != (ar = cell_is_array (cell)) && (ar->y != 0 || ar->x != 0))
		return cur;

	/* As of version 0.53 we save the ID of shared expressions */
	if (is_shared_expr) {
		gpointer const expr = cell->base.expression;
		gpointer id = g_hash_table_lookup (ctxt->expr_map, expr);

		if (id == NULL) {
			id = GINT_TO_POINTER (g_hash_table_size (ctxt->expr_map) + 1);
			g_hash_table_insert (ctxt->expr_map, expr, id);
		} else if (ar == NULL)
			write_contents = FALSE;

		xml_node_set_int (cur, "ExprID", GPOINTER_TO_INT (id));
	}

	if (write_contents) {
		if (cell_has_expr (cell)) {
			char *tmp;
			ParsePos pp;

			tmp = expr_tree_as_string (cell->base.expression,
				parse_pos_init_cell (&pp, cell));
			text = g_strconcat ("=", tmp, NULL);
			g_free (tmp);
		} else
			text = value_get_as_string (cell->value);

		tstr = xmlEncodeEntitiesReentrant (ctxt->doc, text);

		/* FIXME : Remove this useless node.  set the content directly */
		child = xmlNewChild (cur, ctxt->ns, "Content", tstr);
		if (tstr)
			xmlFree (tstr);
		g_free (text);

		if (!cell_has_expr (cell)) {
			xml_node_set_int (cur, "ValueType",
					   cell->value->type);
			if (cell->format) {
				char *fmt = style_format_as_XL (cell->format, FALSE);
				xmlSetProp (cur, "ValueFormat", fmt);
				g_free (fmt);
			}
		}
	}

	/* As of version 0.53 we save the size of the array as attributes */
	/* As of version 0.57 the attributes are in the Cell not the Content */
	if (ar != NULL) {
	        xml_node_set_int (cur, "Rows", ar->rows);
	        xml_node_set_int (cur, "Cols", ar->cols);
	}

	return cur;
}

/*
 * Create an XML subtree of doc equivalent to the given Cell.
 */
static xmlNodePtr
xml_write_cell (XmlParseContext *ctxt, Cell *cell)
{
	return xml_write_cell_and_position (ctxt, cell, cell->pos.col, cell->pos.row);
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
xml_cell_set_array_expr (Cell *cell, char const *text,
			 int const rows, int const cols)
{
	ParsePos pp;
	ExprTree *expr;

	expr = expr_parse_str_simple (text, parse_pos_init_cell (&pp, cell));

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
xml_not_used_old_array_spec (Cell *cell, char *content)
{
	int rows, cols, row, col;

#if 0
	/* This is the syntax we are trying to parse */
	g_string_sprintfa (str, "{%s}(%d,%d)[%d][%d]", expr_text,
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
		xml_cell_set_array_expr (cell, content + 2, rows, cols);
	}

	return FALSE;
}

/*
 * Create a Cell equivalent to the XML subtree of doc.
 */
static Cell *
xml_read_cell (XmlParseContext *ctxt, xmlNodePtr tree)
{
	Cell *ret;
	xmlNodePtr child;
	int col, row;
	int array_cols, array_rows, shared_expr_index = -1;
	char *content = NULL;
	char *comment = NULL;
	int  style_idx;
	gboolean style_read = FALSE;
	gboolean is_post_52_array = FALSE;
	gboolean is_new_cell = TRUE;
	gboolean is_value = FALSE;
	ValueType value_type = VALUE_EMPTY; /* Make compiler shut up */
	StyleFormat *value_fmt = NULL;

	if (strcmp (tree->name, "Cell")) {
		fprintf (stderr,
		 "xml_read_cell: invalid element type %s, 'Cell' expected`\n",
			 tree->name);
		return NULL;
	}
	xml_node_get_int (tree, "Col", &col);
	xml_node_get_int (tree, "Row", &row);

	ret = sheet_cell_get (ctxt->sheet, col, row);
	if ((is_new_cell = (ret == NULL)))
		ret = sheet_cell_new (ctxt->sheet, col, row);
	if (ret == NULL)
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
			MStyle *mstyle;

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
				char *fmt;

				value_type = tmp;
				is_value = TRUE;

				fmt = xmlGetProp (tree, "ValueFormat");
				if (fmt != NULL) {
					value_fmt = style_format_new_XL (fmt, FALSE);
					xmlFree (fmt);
				}
			}
		}
	}

	child = tree->xmlChildrenNode;
	while (child != NULL) {
		/*
		 * This style code is a gross anachronism that slugs performance
		 * in the common case this data won't exist. In the long term all
		 * files will make the 0.41 - 0.42 transition and this can go.
		 * This is even older backwards compatibility than 0.41 - 0.42
		 */
		if (!strcmp (child->name, "Style")) {
			if (!style_read) {
				MStyle *mstyle;
				mstyle = xml_read_style (ctxt, child);
				if (mstyle)
					sheet_style_set_pos (ctxt->sheet, col, row,
							     mstyle);
			}
		} else if (!strcmp (child->name, "Content")) {
			content = xmlNodeGetContent (child);

			/* Is this a post 0.52 array */
			if (ctxt->version == GNUM_XML_V3) {
				is_post_52_array =
				    xml_node_get_int (child, "Rows", &array_rows) &&
				    xml_node_get_int (child, "Cols", &array_cols);
			}
		} else if (!strcmp (child->name, "Comment")) {
			comment = xmlNodeGetContent (child);
 			if (comment) {
 				cell_set_comment (ret->base.sheet,
					&ret->pos, NULL, comment);
 				xmlFree (comment);
			}
 		}
		child = child->next;
	}
	if (content == NULL) {
		content = xmlNodeGetContent (tree);
		/* Early versions had newlines at the end of their content */
		if (ctxt->version <= GNUM_XML_V1) {
			char *tmp = strchr (content, '\n');
			if (tmp != NULL)
				*tmp = '\0';
		}
	}

	if (content != NULL) {
		if (is_post_52_array) {
			g_return_val_if_fail (content[0] == '=', NULL);

			xml_cell_set_array_expr (ret, content + 1,
						 array_rows, array_cols);
		} else if (ctxt->version >= GNUM_XML_V3 ||
			   xml_not_used_old_array_spec (ret, content)) {
			if (is_value) {
				Value *v = value_new_from_string (value_type, content);
				cell_set_value (ret, v, value_fmt);
			} else
				cell_set_text (ret, content);
		}

		if (shared_expr_index > 0) {
			if (shared_expr_index == (int)ctxt->shared_exprs->len + 1) {
				if (cell_has_expr (ret))
					g_ptr_array_add (ctxt->shared_exprs,
							 ret->base.expression);
				else
					g_warning ("XML-IO: Shared expression with no expession?");
			} else {
				g_warning ("XML-IO: Duplicate or invalid shared expression: %d",
					   shared_expr_index);
			}
		}
		xmlFree (content);
	} else if (shared_expr_index > 0) {
		if (shared_expr_index <= (int)ctxt->shared_exprs->len + 1) {
			ExprTree *expr = g_ptr_array_index (ctxt->shared_exprs,
							    shared_expr_index - 1);
			cell_set_expr (ret, expr, NULL);
		} else {
			g_warning ("XML-IO: Missing shared expression");
		}
	} else if (is_new_cell)
		/*
		 * Only set to empty if this is a new cell.
		 * If it was created by a previous array
		 * we do not want to erase it.
		 */
		cell_set_value (ret, value_new_empty (), NULL);

	return ret;
}

/*
 * Create a CellCopy equivalent to the XML subtree of doc.
 *
 */
static CellCopy *
xml_read_cell_copy (XmlParseContext *ctxt, xmlNodePtr tree)
{
	CellCopy *ret;
	xmlNodePtr child;

	if (strcmp (tree->name, "Cell")) {
		fprintf (stderr,
		 "xml_read_cell_copy: invalid element type %s, 'Cell' expected`\n",
			 tree->name);
		return NULL;
	}

	ret           = g_new (CellCopy, 1);
	ret->type     = CELL_COPY_TYPE_TEXT;
	ret->u.text   = NULL;

	xml_node_get_int (tree, "Col", &ret->col_offset);
	xml_node_get_int (tree, "Row", &ret->row_offset);

	for (child = tree->xmlChildrenNode; child != NULL ; child = child->next)
		if (!strcmp (child->name, "Content"))
			ret->u.text = xmlNodeGetContent (child);

	if (ret->u.text == NULL)
		ret->u.text = xmlNodeGetContent (tree);

	/*
	 * Here we see again that the text was malloc'ed and thus should be xmlFreed and
	 * not g_free 'd
	 */
	if (ret->u.text != NULL) {
		char *temp = g_strdup (ret->u.text);

		xmlFree (ret->u.text);
		ret->u.text = temp;
	}

	return ret;
}

/*
 * Create an XML subtree equivalent to the given cell and add it to the parent
 */
static void
xml_write_cell_to (gpointer key, gpointer value, gpointer data)
{
	XmlParseContext *ctxt = (XmlParseContext *) data;
	xmlNodePtr cur;

	cur = xml_write_cell (ctxt, (Cell *) value);
	xmlAddChild (ctxt->parent, cur);
}

static void
xml_write_merged_regions (XmlParseContext const *ctxt,
			  xmlNodePtr sheet, GSList *ptr)
{
	xmlNodePtr container;

	if (ptr == NULL)
		return;

	container = xmlNewChild (sheet, ctxt->ns, "MergedRegions", NULL);
	for (; ptr != NULL ; ptr = ptr->next) {
		Range const * const range = ptr->data;
		xmlNewChild (container, ctxt->ns, "Merge", range_name (range));
	}
}

static void
xml_read_sheet_layout (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	CellPos tmp, frozen_tl, unfrozen_tl;

	tree = e_xml_get_child_by_name (tree, "SheetLayout");
	if (tree == NULL)
	    return;

	/* The top left cell in pane[0] */
	if (xml_node_get_cellpos (tree, "TopLeft", &tmp))
		sheet_set_initial_top_left (ctxt->sheet, tmp.col, tmp.row);

	child = e_xml_get_child_by_name (tree, "FreezePanes");
	if (child != NULL &&
	    xml_node_get_cellpos (child, "FrozenTopLeft", &frozen_tl) &&
	    xml_node_get_cellpos (child, "UnfrozenTopLeft", &unfrozen_tl))
		sheet_freeze_panes (ctxt->sheet, &frozen_tl, &unfrozen_tl);
}

static void
xml_write_sheet_layout (XmlParseContext *ctxt, xmlNodePtr tree, Sheet const *sheet)
{
	tree = xmlNewChild (tree, ctxt->ns, "SheetLayout", NULL);

	xml_node_set_cellpos (tree, "TopLeft", &sheet->initial_top_left);
	if (sheet_is_frozen (sheet)) {
		xmlNodePtr freeze = xmlNewChild (tree, ctxt->ns, "FreezePanes", NULL);
		xml_node_set_cellpos (freeze, "FrozenTopLeft", &sheet->frozen_top_left);
		xml_node_set_cellpos (freeze, "UnfrozenTopLeft", &sheet->unfrozen_top_left);
	}
}

static xmlNodePtr
xml_write_styles (XmlParseContext *ctxt, StyleList *styles)
{
	StyleList *ptr;
	xmlNodePtr cur;

	if (!styles)
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Styles", NULL);
	for (ptr = styles; ptr; ptr = ptr->next) {
		StyleRegion *sr = ptr->data;

		xmlAddChild (cur, xml_write_style_region (ctxt, sr));
	}
	style_list_free (styles);

	return cur;
}

static void
xml_read_solver (XmlParseContext *ctxt, xmlNodePtr tree)
{
	SolverConstraint *c;
	xmlNodePtr       child;
	int              col, row;
	xmlChar          *s;
	Sheet *sheet = ctxt->sheet;
	SolverParameters *param = &sheet->solver_parameters;

	tree = e_xml_get_child_by_name (tree, "Solver");
	if (tree == NULL)
		return;

	xml_node_get_int (tree, "TargetCol", &col);
	xml_node_get_int (tree, "TargetRow", &row);
	if (col >= 0 && row >= 0)
	        param->target_cell = sheet_cell_fetch (sheet, col, row);

	xml_node_get_int (tree, "ProblemType", (int *) &param->problem_type);
	s = xml_node_get_cstr (tree, "Inputs");
	g_free (param->input_entry_str);
	param->input_entry_str = g_strdup (s);
	xmlFree (s);

	child = e_xml_get_child_by_name (tree, "Constr");
	param->constraints = NULL;
	while (child != NULL) {
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
		        c->type = "<=";
			break;
		case 2:
		        c->type = ">=";
			break;
		case 4:
		        c->type = "=";
			break;
		case 8:
		        c->type = "Int";
			break;
		case 16:
		        c->type = "Bool";
			break;
		default:
		        c->type = "<=";
			break;
		}
		c->str = write_constraint_str (c->lhs.col, c->lhs.row,
					       c->rhs.col, c->rhs.row,
					       c->type, c->cols, c->rows);

		param->constraints = g_slist_append (param->constraints, c);
		child = e_xml_get_child_by_name (child, "Constr");
	}
}

static xmlNodePtr
xml_write_solver (XmlParseContext *ctxt, SolverParameters const *param)
{
	xmlNodePtr       cur;
	xmlNodePtr       constr;
	xmlNodePtr       prev = NULL;
	SolverConstraint *c;
	GSList           *constraints;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Solver", NULL);

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
	        c = (SolverConstraint *) constraints->data;

		constr = xmlNewDocNode (ctxt->doc, ctxt->ns, "Constr", NULL);
		xml_node_set_int (constr, "Lcol", c->lhs.col);
		xml_node_set_int (constr, "Lrow", c->lhs.row);
		xml_node_set_int (constr, "Rcol", c->rhs.col);
		xml_node_set_int (constr, "Rrow", c->rhs.row);
		xml_node_set_int (constr, "Cols", c->cols);
		xml_node_set_int (constr, "Rows", c->rows);

		if (strcmp (c->type, "<=") == 0)
		        xml_node_set_int (constr, "Type", 1);
		else if (strcmp (c->type, ">=") == 0)
		        xml_node_set_int (constr, "Type", 2);
		else if (strcmp (c->type, "=") == 0)
		        xml_node_set_int (constr, "Type", 4);
		else if (strcmp (c->type, "Int") == 0)
		        xml_node_set_int (constr, "Type", 8);
		else if (strcmp (c->type, "Bool") == 0)
		        xml_node_set_int (constr, "Type", 16);
		else
		        xml_node_set_int (constr, "Type", 0);

		if (!prev)
		        xmlAddChild (cur, constr);
		else
		        xmlAddChild (prev, constr);

		prev = constr;
		constraints = constraints->next;
	}

	return cur;
}

/*
 *
 */

static int
natural_order_cmp (const void *a, const void *b)
{
	const Cell *ca = *(const Cell **)a ;
	const Cell *cb = *(const Cell **)b ;
	int diff = (ca->pos.row - cb->pos.row);

	if (diff != 0)
		return diff;
	return ca->pos.col - cb->pos.col;
}

static void
copy_hash_table_to_ptr_array (gpointer key, gpointer value, gpointer user_data)
{
	g_ptr_array_add (user_data,value) ;
}



/*
 * Create an XML subtree of doc equivalent to the given Sheet.
 */
static xmlNodePtr
xml_sheet_write (XmlParseContext *ctxt, Sheet const *sheet)
{
	xmlNodePtr cur;
	xmlNodePtr child;
	xmlNodePtr rows;
	xmlNodePtr cols;
	xmlNodePtr cells;
	xmlNodePtr printinfo;
	xmlNodePtr styles;
	xmlNodePtr solver;
	char *tstr;

	/* General information about the Sheet */
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Sheet", NULL);
	if (cur == NULL)
		return NULL;
	e_xml_set_bool_prop_by_name (cur, "DisplayFormulas",
				     sheet->display_formulas);
	e_xml_set_bool_prop_by_name (cur, "HideZero",
				     sheet->hide_zero);
	e_xml_set_bool_prop_by_name (cur, "HideGrid",
				     sheet->hide_grid);
	e_xml_set_bool_prop_by_name (cur, "HideColHeader",
				     sheet->hide_col_header);
	e_xml_set_bool_prop_by_name (cur, "HideRowHeader",
				     sheet->hide_row_header);
	e_xml_set_bool_prop_by_name (cur, "DisplayOutlines",
				     sheet->display_outlines);
	e_xml_set_bool_prop_by_name (cur, "OutlineSymbolsBelow",
				     sheet->outline_symbols_below);
	e_xml_set_bool_prop_by_name (cur, "OutlineSymbolsRight",
				     sheet->outline_symbols_right);

	tstr = xmlEncodeEntitiesReentrant (ctxt->doc, sheet->name_unquoted);
	xmlNewChild (cur, ctxt->ns, "Name",  tstr);
	if (tstr) xmlFree (tstr); {
		char str[4 * sizeof (int) + DBL_DIG + 50];
		sprintf (str, "%d", sheet->cols.max_used);
		xmlNewChild (cur, ctxt->ns, "MaxCol", str);
		sprintf (str, "%d", sheet->rows.max_used);
		xmlNewChild (cur, ctxt->ns, "MaxRow", str);
		sprintf (str, "%f", sheet->last_zoom_factor_used);
		xmlNewChild (cur, ctxt->ns, "Zoom", str);
	}

	child = xml_write_names (ctxt, sheet->names);
	if (child)
		xmlAddChild (cur, child);

	/*
	 * Print Information
	 */
	printinfo = xml_write_print_info (ctxt, sheet->print_info);
	if (printinfo)
		xmlAddChild (cur, printinfo);

	/*
	 * Styles
	 */
	styles = xml_write_styles (ctxt, sheet_style_get_list (sheet, NULL));
	if (styles)
		xmlAddChild (cur, styles);

	/*
	 * Cols informations.
	 */
	cols = xmlNewChild (cur, ctxt->ns, "Cols", NULL);
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
	rows = xmlNewChild (cur, ctxt->ns, "Rows", NULL);
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
	xml_write_selection_info (ctxt, sheet, cur);

	/* Objects */
	if (sheet->sheet_objects != NULL) {
		xmlNodePtr objects = xmlNewChild (cur, ctxt->ns,
						  "Objects", NULL);
		GList *l = sheet->sheet_objects;
		while (l) {
			child = sheet_object_write_xml (l->data, ctxt);
			if (child)
				xmlAddChild (objects, child);
			l = l->next;
		}
	}

	/*
	 * Cells informations
	 */
	cells = xmlNewChild (cur, ctxt->ns, "Cells", NULL);
	ctxt->parent = cells;
	/* g_hash_table_foreach (sheet->cell_hash, xml_write_cell_to, ctxt); */
	/* save cells in natural order */
	{
		gint i ,n ;
		n = g_hash_table_size (sheet->cell_hash);
		if (n > 0) {
			GPtrArray *natural = g_ptr_array_new ();
			g_hash_table_foreach (sheet->cell_hash, copy_hash_table_to_ptr_array, natural);
			qsort (&g_ptr_array_index (natural, 0),
			       n,
			       sizeof (gpointer),
			       natural_order_cmp);
			for (i = 0; i < n; i++)
				xml_write_cell_to (NULL, g_ptr_array_index (natural, i), ctxt);
			g_ptr_array_free (natural,TRUE);
		}
	}

	xml_write_merged_regions (ctxt, cur, sheet->list_merged);
	xml_write_sheet_layout (ctxt, cur, sheet);

	solver = xml_write_solver (ctxt, &sheet->solver_parameters);
	if (solver)
		xmlAddChild (cur, solver);

	return cur;
}

static xmlNodePtr
xml_write_selection_clipboard (XmlParseContext *ctxt, Sheet *sheet)
{
	xmlNodePtr cur;
	xmlNodePtr styles;
	xmlNodePtr cells;
	xmlNodePtr cell_xml;
	GSList *range_list, *ptr;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "ClipboardRange", NULL);
	if (cur == NULL)
		return NULL;

	/* Write styles */
	range_list = selection_get_ranges (sheet, FALSE);
	for (ptr = range_list; ptr != NULL ; ptr = ptr->next) {
		Range const *range = ptr->data;
		styles = xml_write_styles (ctxt,
			sheet_style_get_list (sheet, range));
		if (styles)
			xmlAddChild (cur, styles);
	}

	/* Write selected regions */
	cells = xmlNewChild (cur, ctxt->ns, "Cells", NULL);
	ctxt->parent = cells;

	for (ptr = range_list; ptr != NULL ; ptr = ptr->next) {
		int row, col;
		Range *range = ptr->data;

		for (row = range->start.row; row <= range->end.row; row++) {

			for (col = range->start.col; col <= range->end.col; col++) {
				Cell *cell = sheet_cell_get (sheet, col, row);

				if (cell) {

					cell_xml = xml_write_cell_and_position (ctxt, cell, col - range->start.col, row - range->start.row);
					xmlAddChild (ctxt->parent, cell_xml);
				}
			}
		}

		/* NOTE : The list is being invalidated */
		g_free (range);
	}
	g_slist_free (range_list);

	return cur;
}

static void
xml_read_merged_regions (XmlParseContext const *ctxt, xmlNodePtr sheet)
{
	xmlNodePtr container, region;

	container = e_xml_get_child_by_name (sheet, "MergedRegions");
	if (container == NULL)
		return;

	for (region = container->xmlChildrenNode; region; region = region->next) {
		char *content = xmlNodeGetContent (region);
		Range r;
		if (content != NULL) {
			if (parse_range (content,
					 &r.start.col, &r.start.row,
					 &r.end.col, &r.end.row))
				sheet_merge_add (NULL, ctxt->sheet, &r, FALSE);
			xmlFree (content);
		}
	}
}

static void
xml_read_styles (XmlParseContext *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	xmlNodePtr regions;

	child = e_xml_get_child_by_name (tree, "Styles");
	if (child == NULL)
		return;

	for (regions = child->xmlChildrenNode; regions != NULL; regions = regions->next) {
		xml_read_style_region (ctxt, regions);
		count_io_progress_update (ctxt->io_context, 1);
	}
}

/*
 * Read styles and add them to a cellregion
 */
static void
xml_read_styles_ex (XmlParseContext *ctxt, xmlNodePtr tree, CellRegion *cr)
{
	xmlNodePtr child;
	xmlNodePtr regions;

	child = e_xml_get_child_by_name (tree, "Styles");
	if (child == NULL)
		return;

	for (regions = child->xmlChildrenNode; regions; regions = regions->next) {
		StyleRegion *region = g_new0 (StyleRegion, 1);

		region->style = xml_read_style_region_ex (ctxt, regions, &region->range);

		cr->styles = g_list_prepend (cr->styles, region);
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
		info->margin_a = val;
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
	double tmp;
	Sheet *sheet = ctxt->sheet;

	cols = e_xml_get_child_by_name (tree, "Cols");
	if (cols == NULL)
		return;

	if (xml_node_get_double (cols, "DefaultSizePts", &tmp))
		sheet_col_set_default_size_pts (sheet, tmp);

	for (col = cols->xmlChildrenNode; col; col = col->next) {
		double size_pts;
		ColRowInfo *info;
		int count, pos;

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
	double tmp;
	Sheet *sheet = ctxt->sheet;

	rows = e_xml_get_child_by_name (tree, "Rows");
	if (rows == NULL)
		return;

	if (xml_node_get_double (rows, "DefaultSizePts", &tmp))
		sheet_row_set_default_size_pts (sheet, tmp);

	for (row = rows->xmlChildrenNode; row; row = row->next){
		double size_pts;
		ColRowInfo *info;
		int count, pos;

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

	ctxt->style_table = g_hash_table_new (g_direct_hash, g_direct_equal);

	child = e_xml_get_child_by_name (tree, "CellStyles");
	if (child == NULL)
		return;

	for (styles = child->xmlChildrenNode; styles; styles = styles->next) {
		MStyle *mstyle;
		int style_idx;

		if (xml_node_get_int (styles, "No", &style_idx)) {
			mstyle = xml_read_style (ctxt, styles);
			g_hash_table_insert (
				ctxt->style_table,
				GINT_TO_POINTER (style_idx),
				mstyle);
		}
	}
}
static void
destroy_style (gpointer key, gpointer value, gpointer data)
{
	mstyle_unref (value);
}

static void
xml_dispose_read_cell_styles (XmlParseContext *ctxt)
{
	g_hash_table_foreach (ctxt->style_table, destroy_style, NULL);
	g_hash_table_destroy (ctxt->style_table);
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
	char *val;

	if (strcmp (tree->name, "Sheet")){
		fprintf (stderr,
			 "xml_sheet_read: invalid element type %s, 'Sheet' expected\n",
			 tree->name);
	}
	child = tree->xmlChildrenNode;

	/*
	 * Get the name of the sheet.  If it does exist, use the existing
	 * name, otherwise create a sheet (ie, for the case of only reading
	 * a new sheet).
	 */
	val = xml_node_get_cstr (e_xml_get_child_by_name (tree, "Name"), NULL);
	if (val == NULL)
		return NULL;

	sheet = workbook_sheet_by_name (ctxt->wb, val);
	if (sheet == NULL)
		sheet = sheet_new (ctxt->wb, val);
	xmlFree (val);
	if (sheet == NULL)
		return NULL;

	ctxt->sheet = sheet;

	sheet->display_formulas = e_xml_get_bool_prop_by_name_with_default (tree,
		"DisplayFormulas",	FALSE);
	sheet->hide_zero = e_xml_get_bool_prop_by_name_with_default (tree,
		"HideZero",		FALSE);
	sheet->hide_grid = e_xml_get_bool_prop_by_name_with_default (tree,
		"HideGrid",		FALSE);
	sheet->hide_col_header = e_xml_get_bool_prop_by_name_with_default (tree,
		"HideColHeader",	FALSE);
	sheet->hide_row_header = e_xml_get_bool_prop_by_name_with_default (tree,
		"HideRowHeader",	FALSE);
	sheet->display_outlines = e_xml_get_bool_prop_by_name_with_default (tree,
		"DisplayOutlines",	TRUE);
	sheet->outline_symbols_below = e_xml_get_bool_prop_by_name_with_default (tree,
		"OutlineSymbolsBelow",	TRUE);
	sheet->outline_symbols_right = e_xml_get_bool_prop_by_name_with_default (tree,
		"OutlineSymbolsRight",	TRUE);

	xml_node_get_double (e_xml_get_child_by_name (tree, "Zoom"), NULL,
			     &zoom_factor);

	xml_read_print_info (ctxt, tree);
	xml_read_styles (ctxt, tree);
	xml_read_cell_styles (ctxt, tree);
	xml_read_cols_info (ctxt, tree);
	xml_read_rows_info (ctxt, tree);
	xml_read_merged_regions (ctxt, tree);
	xml_read_selection_info (ctxt, tree);

	xml_read_names (ctxt, child, NULL, sheet);

	child = e_xml_get_child_by_name (tree, "Objects");
	if (child != NULL) {
		xmlNodePtr object = child->xmlChildrenNode;
		for (; object != NULL ; object = object->next)
			sheet_object_read_xml (ctxt, object);
	}

	child = e_xml_get_child_by_name (tree, "Cells");
	if (child != NULL) {
		xmlNodePtr cell;

		for (cell = child->xmlChildrenNode; cell != NULL ; cell = cell->next) {
			xml_read_cell (ctxt, cell);
			count_io_progress_update (ctxt->io_context, 1);
		}
	}

	xml_read_solver (ctxt, tree);
	xml_read_sheet_layout (ctxt, tree);

	xml_dispose_read_cell_styles (ctxt);

	/* Init ColRowInfo's size_pixels and force a full respan */
	sheet_flag_recompute_spans (sheet);
	sheet_set_zoom_factor (sheet, zoom_factor, FALSE, FALSE);

	return sheet;
}

/*
 * Reads the tree data into the sheet and pastes cells relative to topcol and toprow.
 */
static CellRegion *
xml_read_selection_clipboard (XmlParseContext *ctxt, xmlNodePtr tree)
{
	CellRegion *cr;
	xmlNodePtr child;
	xmlNodePtr cells;

	if (strcmp (tree->name, "ClipboardRange")){
		fprintf (stderr,
			 "xml_read_selection_clipboard: invalid element type %s, 'ClipboardRange' expected\n",
			 tree->name);
	}
	child = tree->xmlChildrenNode;

	ctxt->sheet = NULL;

	cr = cellregion_new (NULL);

	/*
	 * This nicely puts all the styles into the cr->styles list
	 */
	xml_read_styles_ex (ctxt, tree, cr);
	xml_read_cell_styles (ctxt, tree);

	/*
	 * Read each cell and add them to the celllist
	 * also keep track of the highest column and row numbers
	 * passed
	 */
	child = e_xml_get_child_by_name (tree, "Cells");
	if (child != NULL) {
		for (cells = child->xmlChildrenNode; cells != NULL ; cells = cells->next) {
			CellCopy *cc = xml_read_cell_copy (ctxt, cells);
			if (cc) {
				if (cr->cols < cc->col_offset + 1)
					cr->cols = cc->col_offset + 1;
				if (cr->rows  < cc->row_offset + 1)
					cr->rows  = cc->row_offset + 1;

				cr->content = g_list_prepend (cr->content, cc);
			}
		}
	}

	xml_dispose_read_cell_styles (ctxt);

	return cr;
}

/* These will be searched IN ORDER, so add new versions at the top */
static const struct {
	char const * const id;
	GnumericXMLVersion const version;
} GnumericVersions [] = {
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

xmlNsPtr
xml_check_version (xmlDocPtr doc, GnumericXMLVersion *version)
{
	xmlNsPtr gmr;
	int i;

	/* Do a bit of checking, get the namespaces, and check the top elem.  */
	if (doc->xmlRootNode->name == NULL || strcmp (doc->xmlRootNode->name, "Workbook"))
		return NULL;

	for (i = 0 ; GnumericVersions [i].id != NULL ; ++i ) {
		gmr = xmlSearchNsByHref (doc, doc->xmlRootNode, GnumericVersions [i].id);
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
xmlNodePtr
xml_workbook_write (XmlParseContext *ctxt, WorkbookView *wb_view)
{
	xmlNodePtr cur;
	xmlNodePtr child;
	GtkArg *args;
	guint n_args;
	GList *sheets, *sheets0;
	char *old_num_locale, *old_monetary_locale, *old_msg_locale;
	Workbook *wb = wb_view_workbook (wb_view);

	/*
	 * General informations about the Sheet.
	 */
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Workbook", NULL);	/* the Workbook name !!! */
	if (cur == NULL)
		return NULL;
	if (ctxt->ns == NULL) {
		/* GnumericVersions[0] is always the first item and
		 * the most recent version, see table above. Keep the table
		 * ordered this way!
		 */
		ctxt->ns = xmlNewNs (cur, GnumericVersions[0].id, "gmr");
		xmlSetNs(cur, ctxt->ns);

		xmlNewNsProp (cur,
			xmlNewNs (cur, "http://www.w3.org/2001/XMLSchema-instance", "xsi"),
			"schemaLocation",
			"http://www.gnumeric.org/v8.xsd");
	}

	old_num_locale = g_strdup (gnumeric_setlocale (LC_NUMERIC, NULL));
	gnumeric_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnumeric_setlocale (LC_MONETARY, NULL));
	gnumeric_setlocale (LC_MONETARY, "C");
	old_msg_locale = g_strdup (textdomain (NULL));
	textdomain ("C");

	args = wb_view_get_attributev (wb_view, &n_args);
	child = xml_write_attributes (ctxt, n_args, args);
	if (child)
		xmlAddChild (cur, child);
	g_free (args);

	child = xml_write_summary (ctxt, wb->summary_info);
	if (child)
		xmlAddChild (cur, child);

	/* The sheet name index is required for the xml_sax
	 * importer to work correctly. We don't use it for
	 * the dom loader! These must be written BEFORE
	 * the named expressions.
	 */
	child = xmlNewChild (cur, ctxt->ns, "SheetNameIndex", NULL);
	sheets0 = sheets = workbook_sheets (wb);
	while (sheets) {
		char *tstr;
		Sheet *sheet = sheets->data;

		tstr = xmlEncodeEntitiesReentrant (ctxt->doc, sheet->name_unquoted);
		xmlNewChild (child, ctxt->ns, "SheetName",  tstr);
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

	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "Geometry", NULL);
	xml_node_set_int (child, "Width", wb_view->preferred_width);
	xml_node_set_int (child, "Height", wb_view->preferred_height);
	xmlAddChild (cur, child);

	/*
	 * Cells informations
	 */
	child = xmlNewChild (cur, ctxt->ns, "Sheets", NULL);
	ctxt->parent = child;

	sheets = sheets0;
	while (sheets) {
		xmlNodePtr cur, parent;
		Sheet *sheet = sheets->data;

		parent = ctxt->parent;
		cur = xml_sheet_write (ctxt, sheet);
		ctxt->parent = parent;
		xmlAddChild (parent, cur);

		sheets = g_list_next (sheets);
	}
	g_list_free (sheets0);

	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "UIData", NULL);
	xml_node_set_int (child, "SelectedTab", workbook_sheet_index_get (wb,
		wb_view_cur_sheet (wb_view)));
	xmlAddChild (cur, child);

	textdomain (old_msg_locale);
	gnumeric_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	g_free (old_msg_locale);
	gnumeric_setlocale (LC_NUMERIC, old_num_locale);
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
			e_xml_get_child_by_name (node, "Name"), NULL);

		if (name == NULL) {
			char *tmp = workbook_sheet_get_free_name (ctxt->wb,
					_("Sheet"), TRUE, TRUE);
			name = xmlStrdup (tmp);
			g_free (tmp);
		}

		g_return_if_fail (name != NULL);

		workbook_sheet_attach (ctxt->wb, sheet_new (ctxt->wb, name),
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

	node = e_xml_get_child_by_name (tree, "Styles");
	if (node != NULL) {
		n += xml_get_n_children (node);
	}
	node = e_xml_get_child_by_name (tree, "Cells");
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
gboolean
xml_workbook_read (IOContext *context, WorkbookView *wb_view,
		   XmlParseContext *ctxt, xmlNodePtr tree)
{
	Sheet *sheet;
	xmlNodePtr child, c;
	char *old_num_locale, *old_monetary_locale, *old_msg_locale;
	GList *list = NULL;
	Workbook *wb = wb_view_workbook (wb_view);

	if (strcmp (tree->name, "Workbook")){
		fprintf (stderr,
			 "xml_workbook_read: invalid element type %s, 'Workbook' expected`\n",
			 tree->name);
		return FALSE;
	}
	ctxt->wb = wb;

	old_num_locale = g_strdup (gnumeric_setlocale (LC_NUMERIC, NULL));
	gnumeric_setlocale (LC_NUMERIC, "C");
	old_monetary_locale = g_strdup (gnumeric_setlocale (LC_MONETARY, NULL));
	gnumeric_setlocale (LC_MONETARY, "C");
	old_msg_locale = g_strdup (textdomain (NULL));
	textdomain ("C");

	child = e_xml_get_child_by_name (tree, "Summary");
	if (child)
		xml_read_summary (ctxt, child, wb->summary_info);

	child = e_xml_get_child_by_name (tree, "Geometry");
	if (child) {
		int width, height;

		xml_node_get_int (child, "Width", &width);
		xml_node_get_int (child, "Height", &height);
		wb_view_preferred_size	  (wb_view, width, height);
	}

/*	child = xml_search_child (tree, "Style");
	if (child != NULL)
	xml_read_style (ctxt, child, &wb->style);*/

	child = e_xml_get_child_by_name (tree, "Sheets");
	if (child == NULL)
		return FALSE;

	/*
	 * Pass 1: Create all the sheets, to make sure
	 * all of the references to forward sheets are properly
	 * handled
	 */
	for (c = child->xmlChildrenNode; c != NULL ; c = c->next)
		xml_sheet_create (ctxt, c);

	/*
	 * Now read names which can have inter-sheet references
	 * to these sheet titles
	 */
	xml_read_names (ctxt, tree, wb, NULL);

	child = e_xml_get_child_by_name (tree, "Sheets");

	/*
	 * Pass 2: read the contents
	 */
	io_progress_message (context, _("Processing file..."));
	io_progress_range_push (context, 0.5, 1.0);
	count_io_progress_set (context, xml_read_workbook_n_elements (child),
	                       N_ELEMENTS_BETWEEN_UPDATES);
	ctxt->io_context = context;
	c = child->xmlChildrenNode;
	while (c != NULL) {
		sheet = xml_sheet_read (ctxt, c);
		c = c->next;
	}
	io_progress_unset (context);
	io_progress_range_pop (context);

	child = e_xml_get_child_by_name (tree, "Attributes");
	if (child && ctxt->version >= GNUM_XML_V5) {
		xml_read_attributes (ctxt, child, &list);
		wb_view_set_attribute_list (wb_view, list);
		xml_free_arg_list (list);
		g_list_free (list);
	}

	child = e_xml_get_child_by_name (tree, "UIData");
	if (child) {
		int sheet_index = 0;
		if (xml_node_get_int (child, "SelectedTab", &sheet_index))
			wb_view_sheet_focus (wb_view,
				workbook_sheet_by_index (ctxt->wb, sheet_index));
	}

	textdomain (old_msg_locale);
	gnumeric_setlocale (LC_MONETARY, old_monetary_locale);
	g_free (old_monetary_locale);
	g_free (old_msg_locale);
	gnumeric_setlocale (LC_NUMERIC, old_num_locale);
	g_free (old_num_locale);

	workbook_queue_all_recalc (wb);

	return TRUE;
}

/*
 * We parse and do some limited validation of the XML file, if this
 * passes, then we return TRUE
 */
static gboolean
xml_probe (GnumFileOpener const *fo, const gchar *filename, FileProbeLevel pl)
{
	int ret;
	xmlDocPtr res = NULL;
	xmlNsPtr gmr;
	xmlParserCtxtPtr ctxt;
	xmlSAXHandler silent, *old;
	GnumericXMLVersion version;

	if (pl == FILE_PROBE_FILE_NAME) {
		char const *extension = g_extension_pointer (filename);

		return (extension != NULL &&
		        (g_strcasecmp (extension, "gnumeric") == 0 ||
			 g_strcasecmp (extension, "xml.gz") == 0 ||
			 g_strcasecmp (extension, "xml") == 0));
	}

	/*
	 * Do a silent call to the XML parser.
	 */
	ctxt = xmlCreateFileParserCtxt (filename);
	if (ctxt == NULL)
		return FALSE;

	memcpy (&silent, ctxt->sax, sizeof (silent));
	old = ctxt->sax;
	ctxt->sax = &silent;

	xmlParseDocument (ctxt);

	ret = ctxt->wellFormed;
	res = ctxt->myDoc;
	ctxt->sax = old;
	xmlFreeParserCtxt (ctxt);

	/*
	 * This is not well formed.
	 */
	if (!ret) {
		xmlFreeDoc (res);
	        return FALSE;
	}

	if (res->xmlRootNode == NULL) {
		xmlFreeDoc (res);
		return FALSE;
	}

	gmr = xml_check_version (res, &version);
	xmlFreeDoc (res);
	return gmr != NULL;
}

static void
gnumeric_xml_set_compression (xmlDoc *doc, int compression)
{
	gboolean ok = TRUE;

	if (compression < 0) {
		gnome_config_push_prefix ("Gnumeric/XML_DOM/");
		compression = gnome_config_get_int_with_default ("compressionLevel=9", &ok);
		gnome_config_pop_prefix ();
	}

	if (compression >= 0)
		xmlSetDocCompressMode (doc, compression);
}

/*
 * Save a Sheet in Gnumerix XML clipboard format to a @buffer and return
 * the size of the data in @size.  Caller is responsible for xmlFree-ing
 * @buffer.
 *
 * returns 0 in case of success, -1 otherwise.
 */
int
gnumeric_xml_write_selection_clipboard (WorkbookControl *wbc, Sheet *sheet,
					xmlChar **buffer, int *size)
{
	xmlDocPtr xml;
	XmlParseContext *ctxt;

	g_return_val_if_fail (IS_SHEET (sheet), -1);

	/*
	 * Create the tree
	 */
	xml = xmlNewDoc ("1.0");
	if (xml == NULL) {
		gnumeric_error_save (COMMAND_CONTEXT (wbc), "");
		return -1;
	}

	ctxt = xml_parse_ctx_new (xml, NULL);
	xml->xmlRootNode = xml_write_selection_clipboard (ctxt, sheet);
	xml_parse_ctx_destroy (ctxt);

	gnumeric_xml_set_compression (xml, -1);
	xmlDocDumpMemory (xml, buffer, size);
	xmlFreeDoc (xml);

	return 0;
}

/*
 * Parse the gnumeric XML clipboard data in @buffer into a cellregion @cr
 * Cellregion @cr MUST be NULL (it will put a newly allocated cellregion in @cr)
 *
 * returns 0 on success and -1 otherwise.
 */
int
gnumeric_xml_read_selection_clipboard (WorkbookControl *wbc, CellRegion **cr,
				       xmlChar *buffer)
{
	xmlDocPtr doc;
	XmlParseContext *ctxt;

	g_return_val_if_fail (*cr == NULL, -1);
	g_return_val_if_fail (buffer != NULL, -1);

	/*
	 * Load the buffer into an XML tree.
	 */
	doc = xmlParseDoc (buffer);
	if (doc == NULL) {
		gnumeric_error_read (COMMAND_CONTEXT (wbc), "");
		return -1;
	}
	if (doc->xmlRootNode == NULL) {
		xmlFreeDoc (doc);
		gnumeric_error_read (COMMAND_CONTEXT (wbc),
			_("Invalid xml clipboard data. Tree is empty ?"));
		return -1;
	}

	ctxt = xml_parse_ctx_new (doc, NULL);
	*cr = xml_read_selection_clipboard (ctxt, doc->xmlRootNode);
	xml_parse_ctx_destroy (ctxt);

	xmlFreeDoc (doc);

	return 0;
}

/*
 * Open an XML file and read a Workbook
 * One parse the XML file, getting a tree, then analyze the tree to build
 * the actual in-memory structure.
 */
void
gnumeric_xml_read_workbook (GnumFileOpener const *fo,
                            IOContext *context,
                            WorkbookView *wbv,
                            const gchar *filename)
{
	gint fd;
	gzFile *f;
	struct stat sbuf;
	gint file_size;
	ErrorInfo *open_error;
	gchar buffer[XML_INPUT_BUFFER_SIZE];
	gint bytes;
	xmlParserCtxtPtr pctxt;
	xmlDocPtr res;
	xmlNsPtr gmr;
	XmlParseContext *ctxt;
	GnumericXMLVersion    version;

	g_return_if_fail (filename != NULL);

	/*
	 * Load the file into an XML tree.
	 */
	fd = gnumeric_open_error_info (filename, O_RDONLY, &open_error);
	if (fd < 0) {
		gnumeric_io_error_info_set (context, open_error);
		return;
	}
	if (fstat (fd, &sbuf) == 0) {
		file_size = sbuf.st_size;
	} else {
		gnumeric_io_error_info_set (context, error_info_new_from_errno ());
		gnumeric_io_error_push (context, error_info_new_str (
		                        _("Cannot get file size.")));
		close (fd);
		return;
	}
	f = gzdopen (fd, "r");
	if (f == NULL) {
		close (fd);
		gnumeric_io_error_info_set (context, error_info_new_str (
		_("Not enough memory to create zlib decompression state.")));
		return;
	}
	io_progress_message (context, _("Reading file..."));
	io_progress_range_push (context, 0.0, 0.5);
	value_io_progress_set (context, file_size, 0);
	bytes = gzread (f, buffer, 4);
	pctxt = xmlCreatePushParserCtxt (NULL, NULL, buffer, bytes, filename);
	while ((bytes = gzread (f, buffer, XML_INPUT_BUFFER_SIZE)) > 0) {
		xmlParseChunk (pctxt, buffer, bytes, 0);
		value_io_progress_update (context, lseek (fd, 0, SEEK_CUR));
	}
	xmlParseChunk (pctxt, buffer, 0, 1);
	res = pctxt->myDoc;
	xmlFreeParserCtxt (pctxt);
	gzclose (f);
	io_progress_unset (context);
	io_progress_range_pop (context);

	if (res == NULL) {
		gnumeric_io_error_read (context, "");
		return;
	}
	if (res->xmlRootNode == NULL) {
		xmlFreeDoc (res);
		gnumeric_io_error_read (context,
			_("Invalid xml file. Tree is empty ?"));
		return;
	}

	/* Do a bit of checking, get the namespaces, and check the top elem. */
	gmr = xml_check_version (res, &version);
	if (gmr == NULL) {
		xmlFreeDoc (res);
		gnumeric_io_error_read (context,
			_("Is not an Gnumeric Workbook file"));
		return;
	}

	/* Parse the file */
	ctxt = xml_parse_ctx_new (res, gmr);
	ctxt->version = version;
	xml_workbook_read (context, wbv, ctxt, res->xmlRootNode);
	workbook_set_saveinfo (wb_view_workbook (wbv), filename, FILE_FL_AUTO,
	                       gnumeric_xml_get_saver ());
	xml_parse_ctx_destroy (ctxt);
	xmlFreeDoc (res);
}

/*
 * Save a Workbook in an XML file
 * One build an in-memory XML tree and save it to a file.
 */
void
gnumeric_xml_write_workbook (GnumFileSaver const *fs,
                             IOContext *context,
                             WorkbookView *wb_view,
                             const gchar *filename)
{
	xmlDocPtr xml;
	XmlParseContext *ctxt;
	char const *extension;
	int compression;

	g_return_if_fail (wb_view != NULL);
	g_return_if_fail (filename != NULL);

	xml = xmlNewDoc ("1.0");
	if (xml == NULL) {
		gnumeric_io_error_save (context, "");
		return;
	}
	ctxt = xml_parse_ctx_new (xml, NULL);
	xml->xmlRootNode = xml_workbook_write (ctxt, wb_view);
	xml_parse_ctx_destroy (ctxt);

	/* If the suffix is .xml disable compression */
	extension = g_extension_pointer (filename);
	compression =
		(extension != NULL && g_strcasecmp (extension, "xml") == 0)
		? 0 : -1;

	gnumeric_xml_set_compression (xml, compression);
	if (xmlSaveFile (filename, xml) < 0)
		gnumeric_io_error_save (context, g_strerror (errno));

	xmlFreeDoc (xml);
}

void
xml_init (void)
{
	gchar *desc = _("Gnumeric XML file format");

	xml_opener = gnum_file_opener_new (
	             "Gnumeric_XmlIO:gnum_xml", desc,
	             xml_probe, gnumeric_xml_read_workbook);
	xml_saver = gnum_file_saver_new (
	            "Gnumeric_XmlIO:gnum_xml", "gnumeric", desc,
	            FILE_FL_AUTO, gnumeric_xml_write_workbook);
	register_file_opener (xml_opener, 50);
	register_file_saver_as_default (xml_saver, 50);
}

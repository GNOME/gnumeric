/*
 * xml-io.c: save/read gnumeric Sheets using an XML encoding.
 *
 * Authors:
 *   Daniel Veillard <Daniel.Veillard@w3.org>
 *   Miguel de Icaza <miguel@gnu.org>
 *
 * $Id$
 */

#include <config.h>
#include <stdio.h>
#include <gnome.h>
#include "gnumeric.h"
#include "gnome-xml/tree.h"
#include "gnome-xml/parser.h"
#include "color.h"
#include "sheet-object.h"
#include "sheet-object-graphic.h"
#include "xml-io.h"
#include "file.h"

/*
 * A parsing context.
 */
typedef struct {
	xmlDocPtr doc;		/* Xml document */
	xmlNsPtr ns;		/* Main name space */
	xmlNodePtr parent;	/* used only for g_hash_table_foreach callbacks */
	GHashTable *name_table;	/* to reproduce multiple refs with HREFs */
	int fontIdx;		/* for Font refs names ... */
	Sheet *sheet;		/* the associated sheet */
	Workbook *wb;		/* the associated sheet */
	GHashTable *style_table;/* to generate the styles and then the links to it */
	int style_count;        /* A style number */
	xmlNodePtr style_node;  /* The node where we insert the styles */
} parse_xml_context_t;

static Sheet      *xml_sheet_read     (parse_xml_context_t *ctxt, xmlNodePtr tree);
static xmlNodePtr  xml_sheet_write    (parse_xml_context_t *ctxt, Sheet *sheet);
static Workbook   *xml_workbook_read  (parse_xml_context_t *ctxt, xmlNodePtr tree);
static xmlNodePtr  xml_workbook_write (parse_xml_context_t *ctxt, Workbook *wb);

/*
 * Internal stuff: xml helper functions.
 */

/*
 * Get a value for a node either carried as an attibute or as
 * the content of a child.
 */
static char *
xml_value_get (xmlNodePtr node, const char *name)
{
	char *ret;
	xmlNodePtr child;

	ret = (char *) xmlGetProp (node, name);
	if (ret != NULL)
		return ret;
	child = node->childs;

	while (child != NULL) {
		if (!strcmp (child->name, name)) {
		        /*
			 * !!! Inefficient, but ...
			 */
			ret = xmlNodeGetContent(child);
			if (ret != NULL)
			    return (ret);
		}
		child = child->next;
	}

	return NULL;
}

#if 0
/*
 * Get a String value for a node either carried as an attibute or as
 * the content of a child.
 */
static String *
xml_get_value_string (xmlNodePtr node, const char *name)
{
	char *val;
	String *ret;

	val = xml_value_get(node, name);
	if (val == NULL) return(NULL);
        ret = string_get(val);
	free(val);
	return(ret);
}
#endif

/*
 * Get an integer value for a node either carried as an attibute or as
 * the content of a child.
 */
static int
xml_get_value_int (xmlNodePtr node, const char *name, int *val)
{
	char *ret;
	int i;
	int res;

	ret = xml_value_get (node, name);
	if (ret == NULL) return(0);
	res = sscanf (ret, "%d", &i);
	free(ret);
	
	if (res == 1) {
	        *val = i;
		return 1;
	}
	return 0;
}

#if 0
/*
 * Get a float value for a node either carried as an attibute or as
 * the content of a child.
 */
static int
xml_get_value_float (xmlNodePtr node, const char *name, float *val)
{
	int res;
	char *ret;
	float f;

	ret = xml_value_get (node, name);
	if (ret == NULL) return(0);
	res = sscanf (ret, "%f", &f);
	free(ret);
	
	if (res == 1) {
	        *val = f;
		return 1;
	}
	return 0;
}
#endif

/*
 * Get a double value for a node either carried as an attibute or as
 * the content of a child.
 */
static int
xml_get_value_double (xmlNodePtr node, const char *name, double *val)
{
	int res;
	char *ret;
	float f;

	ret = xml_value_get (node, name);
	if (ret == NULL) return(0);
	res = sscanf (ret, "%f", &f);
	free(ret);
	
	if (res == 1) {
	        *val = f;
		return 1;
	}
	return 0;
}

#if 0
/*
 * Get a set of coodinates for a node, carried as the content of a child.
 */
static int
xml_get_coordinate (xmlNodePtr node, const char *name,
		  double *x, double *y)
{
	int res;
	char *ret;
	float X, Y;

	ret = xml_value_get (node, name);
	if (ret == NULL) return(0);
	res = sscanf (ret, "(%f %f)", &X, &Y);
	free(ret);
	
	if (res == 2) {
		*x = X;
		*y = Y;
		return 1;
	}
	return 0;
}
#endif

/*
 * Get a pair of coodinates for a node, carried as the content of a child.
 */

static int
xml_get_coordinates (xmlNodePtr node, const char *name,
		   double *x1, double *y1, double *x2, double *y2)
{
	int res;
	char *ret;

	ret = xml_value_get (node, name);
	if (ret == NULL) return(0);
	res = sscanf (ret, "(%lf %lf)(%lf %lf)", x1, y1, x2, y2);
	free(ret);
	
	if (res == 4) 
		return 1;

	return 0;
}

#if 0
/*
 * Get a GnomeCanvasPoints for a node, carried as the content of a child.
 */
static GnomeCanvasPoints *
xml_get_gnome_canvas_points (xmlNodePtr node, const char *name)
{
	char *val;
	GnomeCanvasPoints *ret = NULL;
	int res;
	const char *ptr;
	int index = 0, i;
	float coord[20];	/* TODO: must be dynamic !!!! */

	val = xml_value_get (node, name);
	if (val == NULL) return(NULL);
	ptr = val;
	do {
		while ((*ptr) && (*ptr != '('))
			ptr++;
		if (*ptr == 0)
			break;
		res = sscanf (ptr, "(%f %f)", &coord[index], &coord[index + 1]);
		if (res != 2)
			break;
		index += 2;
		ptr++;
	} while (res > 0);
	free(val);

	if (index >= 2)
		ret = gnome_canvas_points_new (index / 2);
	if (ret == NULL)
		return NULL;
	for (i = 0; i < index; i++)
		ret->coords[i] = coord[i];
	return ret;
}
#endif

/*
 * Set a string value for a node either carried as an attibute or as
 * the content of a child.
 */
static void
xml_set_gnome_canvas_points (xmlNodePtr node, const char *name,
			 GnomeCanvasPoints *val)
{
	xmlNodePtr child;
	char *str, *base;
	int i;

	if (val == NULL)
		return;
	if ((val->num_points < 0) || (val->num_points > 5000))
		return;
	base = str = g_malloc (val->num_points * 30 * sizeof (char));
	if (str == NULL)
		return;
	for (i = 0; i < val->num_points; i++){
		str += sprintf (str, "(%f %f)", val->coords[2 * i],
				val->coords[2 * i + 1]);
	}

	child = node->childs;
	while (child != NULL){
		if (!strcmp (child->name, name)){
			xmlNodeSetContent (child, base);
			free (base);
			return;
		}
		child = child->next;
	}
	xmlNewChild (node, NULL, name, xmlEncodeEntities(node->doc, base));
	g_free (base);
}


/*
 * Set a string value for a node either carried as an attibute or as
 * the content of a child.
 */
static void
xml_set_value (xmlNodePtr node, const char *name, const char *val)
{
	const char *ret;
	xmlNodePtr child;

	ret = xmlGetProp (node, name);
	if (ret != NULL){
		xmlSetProp (node, name, val);
		return;
	}
	child = node->childs;
	while (child != NULL){
		if (!strcmp (child->name, name)){
			xmlNodeSetContent (child, val);
			return;
		}
		child = child->next;
	}
	xmlSetProp (node, name, val);
}

/*
 * Set a String value for a node either carried as an attibute or as
 * the content of a child.
 */
static void
xml_set_value_string (xmlNodePtr node, const char *name, String *val)
{
	const char *ret;
	xmlNodePtr child;

	ret = xmlGetProp (node, name);
	if (ret != NULL){
		xmlSetProp (node, name, val->str);
		return;
	}
	child = node->childs;
	while (child != NULL){
		if (!strcmp (child->name, name)){
			xmlNodeSetContent (child, val->str);
			return;
		}
		child = child->next;
	}
	xmlSetProp (node, name, val->str);
}

/*
 * Set an integer value for a node either carried as an attibute or as
 * the content of a child.
 */
static void
xml_set_value_int (xmlNodePtr node, const char *name, int val)
{
	const char *ret;
	xmlNodePtr child;
	char str[101];

	snprintf (str, 100, "%d", val);
	ret = xmlGetProp (node, name);
	if (ret != NULL){
		xmlSetProp (node, name, str);
		return;
	}
	child = node->childs;
	while (child != NULL){
		if (!strcmp (child->name, name)){
			xmlNodeSetContent (child, str);
			return;
		}
		child = child->next;
	}
	xmlSetProp (node, name, str);
}

#if 0
/*
 * Set a float value for a node either carried as an attibute or as
 * the content of a child.
 */
static void
xml_set_value_float (xmlNodePtr node, const char *name, float val)
{
	const char *ret;
	xmlNodePtr child;
	char str[101];

	snprintf (str, 100, "%f", val);
	ret = xmlGetProp (node, name);
	if (ret != NULL){
		xmlSetProp (node, name, str);
		return;
	}
	child = node->childs;
	while (child != NULL){
		if (!strcmp (child->name, name)){
			xmlNodeSetContent (child, str);
			return;
		}
		child = child->next;
	}
	xmlSetProp (node, name, str);
}
#endif

/*
 * Set a double value for a node either carried as an attibute or as
 * the content of a child.
 */
static void
xml_set_value_double (xmlNodePtr node, const char *name, double val)
{
	const char *ret;
	xmlNodePtr child;
	char str[101];

	snprintf (str, 100, "%f", (float) val);
	ret = xmlGetProp (node, name);
	if (ret != NULL){
		xmlSetProp (node, name, str);
		return;
	}
	child = node->childs;
	while (child != NULL){
		if (!strcmp (child->name, name)){
			xmlNodeSetContent (child, str);
			return;
		}
		child = child->next;
	}
	xmlSetProp (node, name, str);
}

/*
 * Search a child by name, if needed go down the tree to find it. 
 */
static xmlNodePtr
xml_search_child (xmlNodePtr node, const char *name)
{
	xmlNodePtr ret;
	xmlNodePtr child;

	child = node->childs;
	while (child != NULL){
		if (!strcmp (child->name, name))
			return child;
		child = child->next;
	}
	child = node->childs;
	while (child != NULL){
		ret = xml_search_child (child, name);
		if (ret != NULL)
			return ret;
		child = child->next;
	}
	return NULL;
}

/*
 * Get a color value for a node either carried as an attibute or as
 * the content of a child.
 *
 * TODO PBM: at parse time one doesn't have yet a widget, so we have
 *           to retrieve the default colormap, but this may be a bad
 *           option ...
 */
static int
xml_get_color_value (xmlNodePtr node, const char *name, StyleColor **color)
{
	char *ret;
	int red, green, blue;

	ret = xml_value_get (node, name);
	if (ret == NULL) return(0);
	if (sscanf (ret, "%X:%X:%X", &red, &green, &blue) == 3){
		*color = style_color_new (red, green, blue);
		free(ret);
		return 1;
	}
	return 0;
}

/*
 * Set a color value for a node either carried as an attibute or as
 * the content of a child.
 */
static void
xml_set_color_value (xmlNodePtr node, const char *name, StyleColor *val)
{
	const char *ret;
	xmlNodePtr child;
	char str[101];

	snprintf (str, 100, "%X:%X:%X", val->color.red, val->color.green, val->color.blue);
	ret = xmlGetProp (node, name);
	if (ret != NULL){
		xmlSetProp (node, name, str);
		return;
	}
	child = node->childs;
	while (child != NULL){
		if (!strcmp (child->name, name)){
			xmlNodeSetContent (child, str);
			return;
		}
		child = child->next;
	}
	xmlSetProp (node, name, str);
}

/*
 * Free a name when cleaning up the name Hash table.
 */
static void
name_free (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}

/**
 **
 ** Private functions : mapping between in-memory structure and XML tree
 **
 **/

static int
style_is_default_fore (StyleColor *color)
{
	if (!color)
		return TRUE;

	if (color->color.red == 0 && color->color.green == 0 && color->color.blue == 0)
		return TRUE;
	else
		return FALSE;
}

static int
style_is_default_back (StyleColor *color)
{
	if (!color)
		return TRUE;

	if (color->color.red == 0xffff && color->color.green == 0xffff && color->color.blue == 0xffff)
		return TRUE;
	else
		return FALSE;
}

/*
 * Create an XML subtree of doc equivalent to the given StyleBorder.
 */
static char *BorderTypes[8] =
{
	"none",
 	"thin",
 	"medium",
 	"dashed",
 	"dotted",
 	"thick",
 	"double",
	"hair"
};

static char *StyleSideNames[4] =
{
 	"Top",
 	"Bottom",
 	"Left",
 	"Right"
};

static xmlNodePtr
xml_write_style_border (parse_xml_context_t *ctxt, StyleBorder *border)
{
	xmlNodePtr cur;
	xmlNodePtr side;
	int lp;
       
	for (lp = 3; lp >= 0; lp--)
		if (border->type [lp] != BORDER_NONE)
			break;
	if (lp < 0)
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "StyleBorder", NULL);
	
	for (lp = 0; lp < 4; lp++){
 		if (border->type[lp] != BORDER_NONE){
 			side = xmlNewChild (cur, ctxt->ns, StyleSideNames [lp],
 					    BorderTypes [border->type [lp]]);
 			xml_set_color_value (side, "Color", border->color [lp]);
 		}
	}
	return cur;
}

/*
 * Create a StyleBorder equivalent to the XML subtree of doc.
 */
static StyleBorder *
xml_read_style_border (parse_xml_context_t *ctxt, xmlNodePtr tree)
{
	StyleBorder *ret;
 	StyleBorderType style [4] = { BORDER_NONE, BORDER_NONE, BORDER_NONE, BORDER_NONE };
 	StyleColor *color [4] = { NULL, NULL, NULL, NULL };
	xmlNodePtr side;
	int lp;

	if (strcmp (tree->name, "StyleBorder")){
		fprintf (stderr,
			 "xml_read_style_border: invalid element type %s, 'StyleBorder' expected`\n",
			 tree->name);
	}

 	for (lp = 0; lp < 4; lp++)
 	{
 		if ((side = xml_search_child (tree, StyleSideNames [lp])) != NULL)
 		{
 			/* FIXME: need to read the proper type */
 			style [lp] = BORDER_THICK ;
 			xml_get_color_value (side, "Color", &color [lp]);
 		}
	}
	
	ret = style_border_new (style, color);

	return NULL;
}

/*
 * Create an XML subtree of doc equivalent to the given Style.
 */
static xmlNodePtr
xml_write_style (parse_xml_context_t *ctxt, Style *style, int style_idx)
{
	xmlNodePtr cur, child;
	char str[50];
	char *name;

	if ((style->halign == 0) && (style->valign == 0) &&
	    (style->orientation == 0) && (style->format == NULL) &&
	    (style->font == NULL) && (style->border == NULL))
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Style", NULL);
	if (style_idx != -1)
		xml_set_value_int (cur, "No", style_idx);
	
	xml_set_value_int (cur, "HAlign", style->halign);
	xml_set_value_int (cur, "VAlign", style->valign);
	xml_set_value_int (cur, "Fit", style->fit_in_cell);
	xml_set_value_int (cur, "Orient", style->orientation);
	xml_set_value_int (cur, "Shade", style->pattern);

	if (!style_is_default_fore (style->fore_color))
		xml_set_color_value (cur, "Fore", style->fore_color);

	if (!style_is_default_back (style->back_color))
		xml_set_color_value (cur, "Back", style->back_color);

	if (style->format != NULL){
		xml_set_value (cur, "Format", style->format->format);
	}

	if (style->font != NULL){
		if ((name = (char *)
		     g_hash_table_lookup (ctxt->name_table, style->font)) != NULL){
			child = xmlNewChild (cur, ctxt->ns, "Font", NULL);
			sprintf (str, "#%s", name);
			xmlNewProp (child, "HREF", str);
		} else {
			child = xmlNewChild (cur, ctxt->ns, "Font", 
			           xmlEncodeEntities(ctxt->doc,
				                     style->font->font_name));
			xml_set_value_int (child, "Unit", style->font->size);
			sprintf (str, "FontDef%d", ctxt->fontIdx++);
			xmlNewProp (child, "NAME", str);
			g_hash_table_insert (ctxt->name_table, g_strdup (str), style->font);
		}

	}

	if (style->border != NULL){
		child = xml_write_style_border (ctxt, style->border);
		if (child)
			xmlAddChild (cur, child);
	}

	return cur;
}

/*
 * Create a Style equivalent to the XML subtree of doc.
 */
static Style *
xml_read_style (parse_xml_context_t *ctxt, xmlNodePtr tree, Style * ret)
{
	xmlNodePtr child;
	char *prop;
	int val;
	StyleColor *c;

	if (strcmp (tree->name, "Style")){
		fprintf (stderr,
			 "xml_read_style: invalid element type %s, 'Style' expected\n",
			 tree->name);
	}
	if (ret == NULL)
		ret = style_new_empty ();
	
	if (ret == NULL)
		return NULL;

	if (xml_get_value_int (tree, "HAlign", &val)){
		ret->halign = val;
		ret->valid_flags |= STYLE_ALIGN;
	}
	if (xml_get_value_int (tree, "Fit", &val)){
		ret->fit_in_cell = val;
		ret->valid_flags |= STYLE_ALIGN;
	}
	if (xml_get_value_int (tree, "VAlign", &val)){
		ret->valign = val;
		ret->valid_flags |= STYLE_ALIGN;
	}
	if (xml_get_value_int (tree, "Orient", &val)){
		ret->orientation = val;
		ret->valid_flags |= STYLE_ALIGN;
	}
	if (xml_get_value_int (tree, "Shade", &val)){
		ret->pattern = val;
		ret->valid_flags |= STYLE_PATTERN;
	}
	if (xml_get_color_value (tree, "Fore", &c)){
		ret->fore_color = c;
		ret->valid_flags |= STYLE_FORE_COLOR;
	}
	if (xml_get_color_value (tree, "Back", &c)){
		ret->back_color = c;
		ret->valid_flags |= STYLE_BACK_COLOR;
	}
	prop = xml_value_get (tree, "Format");
	if (prop != NULL){
		if (ret->format == NULL){
			ret->format = style_format_new ((const char *) prop);
			ret->valid_flags |= STYLE_FORMAT;
		}
		free (prop);
	}

	child = tree->childs;
	while (child != NULL){
		if (!strcmp (child->name, "Font")){
			char *v;

			v = xml_value_get (child, "NAME");
			if (v){
				int units = 14;
				char *font;

				xml_get_value_int (child, "Unit", &units);
				font = xmlNodeGetContent(child);
				if (font != NULL) {
					ret->font = style_font_new (font, units, 1.0, 0, 0);
					free(font);
				}
				if (ret->font){
					g_hash_table_insert (ctxt->name_table, g_strdup (v), ret->font);
					ret->valid_flags |= STYLE_FONT;
				}
				free (v);
			} else {
				StyleFont *font;

				v = xml_value_get (child, "HREF");
				if (v){
					font = g_hash_table_lookup (ctxt->name_table, v + 1);
					if (font){
						ret->font = font;
						style_font_ref (font);
						ret->valid_flags |= STYLE_FONT;
					}
				}
			}
		} else if (!strcmp (child->name, "StyleBorder")){
			StyleBorder *sb;

			sb = xml_read_style_border (ctxt, child);
		} else {
			fprintf (stderr, "xml_read_style: unknown type '%s'\n",
				 child->name);
		}
		child = child->next;
	}

	/* Now add defaults to any style that was not loaded */
	return ret;
}

#if 0
/*
 * Create an XML subtree of doc equivalent to the given StyleRegion.
 */
static xmlNodePtr
xml_write_style_region (parse_xml_context_t *ctxt, StyleRegion *region)
{
	xmlNodePtr cur, child;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "StyleRegion", NULL);
	xml_set_value_int (cur, "startCol", region->range.start_col);
	xml_set_value_int (cur, "endCol", region->range.end_col);
	xml_set_value_int (cur, "startRow", region->range.start_row);
	xml_set_value_int (cur, "endRow", region->range.end_row);

	if (region->style != NULL){
		child = xml_write_style (ctxt, region->style);
		if (child)
			xmlAddChild (cur, child);
	}
	return cur;
}
#endif

/*
 * Create a StyleRegion equivalent to the XML subtree of doc.
 */
static void
xml_read_style_region (parse_xml_context_t *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	Style *style = NULL;
	int start_col = 0, start_row = 0, end_col = 0, end_row = 0;

	if (strcmp (tree->name, "StyleRegion")){
		fprintf (stderr,
			 "xml_read_style_region: invalid element type %s, 'StyleRegion' expected`\n",
			 tree->name);
		return;
	}
	xml_get_value_int (tree, "startCol", &start_col);
	xml_get_value_int (tree, "startRow", &start_row);
	xml_get_value_int (tree, "endCol", &end_col);
	xml_get_value_int (tree, "endRow", &end_row);
	child = tree->childs;
	if (child != NULL)
		style = xml_read_style (ctxt, child, NULL);
	if (style != NULL)
		sheet_style_attach (ctxt->sheet, start_col, start_row, end_col,
				    end_row, style);

}

/*
 * Create an XML subtree of doc equivalent to the given ColRowInfo.
 */
static xmlNodePtr
xml_write_colrow_info (parse_xml_context_t *ctxt, ColRowInfo *info, int col)
{
	xmlNodePtr cur;

	if (col)
		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "ColInfo", NULL);
	else
		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "RowInfo", NULL);

	xml_set_value_int (cur, "No", info->pos);
	xml_set_value_double (cur, "Unit", info->units);
	xml_set_value_double (cur, "MarginA", info->margin_a_pt);
	xml_set_value_double (cur, "MarginB", info->margin_b);
	xml_set_value_int (cur, "HardSize", info->hard_size);

	return cur;
}

/*
 * Create a ColRowInfo equivalent to the XML subtree of doc.
 */
static ColRowInfo *
xml_read_colrow_info (parse_xml_context_t *ctxt, xmlNodePtr tree, ColRowInfo *ret)
{
	int col = 0;
	int val;

	if (!strcmp (tree->name, "ColInfo")){
		col = 1;
	} else if (!strcmp (tree->name, "RowInfo")){
		col = 0;
	} else {
		fprintf (stderr,
			 "xml_read_colrow_info: invalid element type %s, 'ColInfo/RowInfo' expected`\n",
			 tree->name);
		return NULL;
	}
	if (ret == NULL){
		if (col)
			ret = sheet_col_new (ctxt->sheet);
		else
			ret = sheet_row_new (ctxt->sheet);
	}
	if (ret == NULL)
		return NULL;

	xml_get_value_int (tree, "No", &ret->pos);
	xml_get_value_double (tree, "Unit", &ret->units);
	xml_get_value_double (tree, "MarginA", &ret->margin_a_pt);
	xml_get_value_double (tree, "MarginB", &ret->margin_b_pt);
	if (xml_get_value_int (tree, "HardSize", &val))
		ret->hard_size = val;

	return ret;
}

/*
 * Create an XML subtree of doc equivalent to the given Object.
 */
static xmlNodePtr
xml_write_sheet_object (parse_xml_context_t *ctxt, SheetObject *object)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (object);
	xmlNodePtr cur = NULL;

	switch (sog->type){
	case SHEET_OBJECT_RECTANGLE:{
			SheetObjectFilled *sof = SHEET_OBJECT_FILLED (object);

			cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Rectangle", NULL);
			if (sof->fill_color != NULL)
				xml_set_value_string (cur, "FillColor", sof->fill_color);
			xml_set_value_int (cur, "Pattern", sof->pattern);
			break;
		}

	case SHEET_OBJECT_ELLIPSE:{
			SheetObjectFilled *sof = SHEET_OBJECT_FILLED (object);

			cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Ellipse", NULL);
			if (sof->fill_color != NULL)
				xml_set_value_string (cur, "FillColor", sof->fill_color);
			xml_set_value_int (cur, "Pattern", sof->pattern);
			break;
		}

	case SHEET_OBJECT_ARROW:
		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Arrow", NULL);
		break;

	case SHEET_OBJECT_LINE:
		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Line", NULL);
		break;
	}
	if (cur == NULL)
		return NULL;
	
	xml_set_gnome_canvas_points (cur, "Points", object->bbox_points);
	xml_set_value_int (cur, "Width", sog->width);
	xml_set_value_string (cur, "Color", sog->color);

	return cur;
}

/*
 * Create a Object equivalent to the XML subtree of doc.
 */
static SheetObject *
xml_read_sheet_object (parse_xml_context_t *ctxt, xmlNodePtr tree)
{
	SheetObject *ret;
	SheetObjectFilled *sof;
	char *color;
	char *fill_color;
	int type;
	double x1, y1, x2, y2;
	int width = 1;
	int pattern;

	if (!strcmp (tree->name, "Rectangle")){
		type = SHEET_OBJECT_RECTANGLE;
	} else if (!strcmp (tree->name, "Ellipse")){
		type = SHEET_OBJECT_ELLIPSE;
	} else if (!strcmp (tree->name, "Arrow")){
		type = SHEET_OBJECT_ARROW;
	} else if (!strcmp (tree->name, "Line")){
		type = SHEET_OBJECT_LINE;
	} else {
		fprintf (stderr,
			 "xml_read_sheet_object: invalid element type %s, 'Rectangle/Ellipse ...' expected`\n",
			 tree->name);
		return NULL;
	}
	
	color = (char *) xml_value_get (tree, "Color");
	xml_get_coordinates (tree, "Points", &x1, &y1, &x2, &y2);
	xml_get_value_int (tree, "Width", &width);
	if ((type == SHEET_OBJECT_RECTANGLE) ||
	    (type == SHEET_OBJECT_ELLIPSE)){
		fill_color = (char *) xml_value_get (tree, "FillColor");
		xml_get_value_int (tree, "Pattern", &pattern);
		ret = sheet_object_create_filled (
			ctxt->sheet, type,
			x1, y1, x2, y2, fill_color, color, width);
		if (ret != NULL){
			sof = SHEET_OBJECT_FILLED (ret);
			sof->pattern = pattern;
		}
	} else {
		ret = sheet_object_create_line (
			ctxt->sheet, type,
			x1, y1, x2, y2, color, width);
	}
	sheet_object_realize (ret);
	return ret;
}

/*
 * Create an XML subtree of doc equivalent to the given Cell.
 */
static xmlNodePtr
xml_write_cell (parse_xml_context_t *ctxt, Cell *cell)
{
	xmlNodePtr cur;
	char *text;
	int style_id;

	style_id = GPOINTER_TO_INT (g_hash_table_lookup (ctxt->style_table, cell->style));
	g_assert (style_id != 0);
	
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Cell", NULL);
	xml_set_value_int (cur, "Col", cell->col->pos);
	xml_set_value_int (cur, "Row", cell->row->pos);
	xml_set_value_int (cur, "Style", style_id);

	text = cell_get_content (cell);
	xmlNewChild (cur, ctxt->ns, "Content",
		     xmlEncodeEntities(ctxt->doc, text));
	g_free (text);

	return cur;
}

/*
 * Create a Cell equivalent to the XML subtree of doc.
 */
static Cell *
xml_read_cell (parse_xml_context_t *ctxt, xmlNodePtr tree)
{
	Style *style;
	Cell *ret;
	xmlNodePtr childs;
	int row = 0, col = 0;
	char *content = NULL;

	if (strcmp (tree->name, "Cell")){
		fprintf (stderr,
		 "xml_read_cell: invalid element type %s, 'Cell' expected`\n",
			 tree->name);
		return NULL;
	}
	xml_get_value_int (tree, "Col", &col);
	xml_get_value_int (tree, "Row", &row);

	cell_deep_freeze_redraws ();

	ret = sheet_cell_get (ctxt->sheet, col, row);
	if (ret == NULL)
		ret = sheet_cell_new (ctxt->sheet, col, row);
	if (ret == NULL)
		return NULL;

	childs = tree->childs;
	while (childs != NULL) {
	        if (!strcmp (childs->name, "Style")) {
		        style = xml_read_style (ctxt, childs, NULL);
			if (style){
				style_merge_to (style, ret->style);
				style_destroy (ret->style);
				ret->style = style;
			}
		}
	        if (!strcmp (childs->name, "Content"))
		        content = xmlNodeGetContent(childs);
		childs = childs->next;
	}
	if (content == NULL)
		content = xmlNodeGetContent(tree);
	if (content != NULL) {
		char *p = content + strlen (content);

		while (p > content){
			p--;
			if (*p != ' ' && *p != '\n')
				break;
			*p = 0;
		}

		cell_set_text_simple (ret, content);
		free (content);
	} else
		cell_set_text_simple (ret, "");

	cell_deep_thaw_redraws ();

	return ret;
}

/*
 * Create an XML subtree equivalent to the given cell and add it to the parent
 */
static void
xml_write_cell_to (gpointer key, gpointer value, gpointer data)
{
	parse_xml_context_t *ctxt = (parse_xml_context_t *) data;
	xmlNodePtr cur;

	cur = xml_write_cell (ctxt, (Cell *) value);
	xmlAddChild (ctxt->parent, cur);
}

static void
add_style (gpointer key, gpointer value, gpointer data)
{
	parse_xml_context_t *ctxt = data;
	Cell *cell = (Cell *) value;
	xmlNodePtr child;
	
	if (g_hash_table_lookup (ctxt->style_table, cell->style))
		return;

	child = xml_write_style (ctxt, cell->style, ctxt->style_count);
	xmlAddChild (ctxt->style_node, child);
		     
	g_hash_table_insert (ctxt->style_table, cell->style, GINT_TO_POINTER (ctxt->style_count));
	ctxt->style_count++;
	
}

static void
xml_cell_styles_init (parse_xml_context_t *ctxt, xmlNodePtr cur, Sheet *sheet)
{
	ctxt->style_node = xmlNewChild (cur, ctxt->ns, "CellStyles", NULL);

	ctxt->style_table = g_hash_table_new (style_hash, style_compare);
	ctxt->style_count = 1;

	ctxt->style_node = cur;
	g_hash_table_foreach (sheet->cell_hash, add_style, ctxt);
}

static void
xml_cell_styles_shutdown (parse_xml_context_t *ctxt)
{
	g_hash_table_destroy (ctxt->style_table);
}

/*
 * Create an XML subtree of doc equivalent to the given Sheet.
 */
static xmlNodePtr
xml_sheet_write (parse_xml_context_t *ctxt, Sheet *sheet)
{
	xmlNodePtr cur;
	xmlNodePtr child;
	xmlNodePtr rows;
	xmlNodePtr cols;
	xmlNodePtr cells;
	xmlNodePtr objects;
	GList *l;
	char str[50];

	/*
	 * General informations about the Sheet.
	 */
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Sheet", NULL);
	if (cur == NULL)
		return NULL;
	xmlNewChild (cur, ctxt->ns, "Name", 
	             xmlEncodeEntities(ctxt->doc, sheet->name));
	sprintf (str, "%d", sheet->max_col_used);
	xmlNewChild (cur, ctxt->ns, "MaxCol", str);
	sprintf (str, "%d", sheet->max_row_used);
	xmlNewChild (cur, ctxt->ns, "MaxRow", str);
	sprintf (str, "%f", sheet->last_zoom_factor_used);
	xmlNewChild (cur, ctxt->ns, "Zoom", str);

	/*
	 * Styles used by the cells on this sheet
	 */
	xml_cell_styles_init (ctxt, cur, sheet);
	
	/*
	 * Cols informations.
	 */
	cols = xmlNewChild (cur, ctxt->ns, "Cols", NULL);
	l = sheet->cols_info;
	while (l){
		child = xml_write_colrow_info (ctxt, l->data, 1);
		if (child)
			xmlAddChild (cols, child);
		l = l->next;
	}

	/*
	 * Rows informations.
	 */
	rows = xmlNewChild (cur, ctxt->ns, "Rows", NULL);
	l = sheet->rows_info;
	while (l){
		child = xml_write_colrow_info (ctxt, l->data, 0);
		if (child)
			xmlAddChild (rows, child);
		l = l->next;
	}

	/*
	 * Style : TODO ...
	 */

	/*
	 * Objects
	 * NOTE: seems that objects == NULL while current_object != NULL
	 * is possible
	 */
	if (sheet->objects != NULL){
		objects = xmlNewChild (cur, ctxt->ns, "Objects", NULL);
		l = sheet->objects;
		while (l){
			child = xml_write_sheet_object (ctxt, l->data);
			if (child)
				xmlAddChild (objects, child);
			l = l->next;
		}
	} else if (sheet->current_object != NULL){
		objects = xmlNewChild (cur, ctxt->ns, "Objects", NULL);
		child = xml_write_sheet_object (ctxt, sheet->current_object);
		if (child)
			xmlAddChild (objects, child);
	}
	/*
	 * Cells informations
	 */
	cells = xmlNewChild (cur, ctxt->ns, "Cells", NULL);
	ctxt->parent = cells;
	g_hash_table_foreach (sheet->cell_hash, xml_write_cell_to, ctxt);
	sheet->modified = 0;

	xml_cell_styles_shutdown (ctxt);
	
	return cur;
}

/*
 * Create a Sheet equivalent to the XML subtree of doc.
 */
static Sheet *
xml_sheet_read (parse_xml_context_t *ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	xmlNodePtr rows;
	xmlNodePtr cols;
	xmlNodePtr regions;
	/* xmlNodePtr styles; */
	xmlNodePtr cells;
	xmlNodePtr objects;
	Sheet *ret = NULL;
	char *val;

	if (strcmp (tree->name, "Sheet")){
		fprintf (stderr,
			 "xml_sheet_read: invalid element type %s, 'Sheet' expected\n",
			 tree->name);
	}
	child = tree->childs;

	/*
	 * Get the name of the sheet.  If it does exist, use the existing
	 * name, otherwise create a sheet (ie, for the case of only reading
	 * a new sheet).
	 */
	val = xml_value_get (tree, "Name");
	if (val != NULL){
		ret = workbook_sheet_lookup (ctxt->wb, (const char *) val);
		if (ret == NULL)
			ret = sheet_new (ctxt->wb, (const char *) val);
		free (val);
	} 

	if (ret == NULL)
		return NULL;

	ctxt->sheet = ret;

	xml_get_value_int (tree, "MaxCol", &ret->max_col_used);
	xml_get_value_int (tree, "MaxRow", &ret->max_row_used);
	xml_get_value_double (tree, "Zoom", &ret->last_zoom_factor_used);
	child = xml_search_child (tree, "Styles");
	if (child != NULL){
		regions = child->childs;
		while (regions != NULL){
			xml_read_style_region (ctxt, regions);
			regions = regions->next;
		}
	}
	child = xml_search_child (tree, "Cols");
	if (child != NULL){
		ColRowInfo *info;

		cols = child->childs;
		while (cols != NULL){
			info = xml_read_colrow_info (ctxt, cols, NULL);
			if (info != NULL)
				sheet_col_add (ret, info);
			cols = cols->next;
		}
	}
	child = xml_search_child (tree, "Rows");
	if (child != NULL){
		ColRowInfo *info;

		rows = child->childs;
		while (rows != NULL){
			info = xml_read_colrow_info (ctxt, rows, NULL);
			if (info != NULL)
				sheet_row_add (ret, info);
			rows = rows->next;
		}
	}
	child = xml_search_child (tree, "Objects");
	if (child != NULL){
		objects = child->childs;
		while (objects != NULL){
			xml_read_sheet_object (ctxt, objects);
			objects = objects->next;
		}
	}
	child = xml_search_child (tree, "Cells");
	if (child != NULL){
		cells = child->childs;
		while (cells != NULL){
			xml_read_cell (ctxt, cells);
			cells = cells->next;
		}
	}
	/* Initialize the ColRowInfo's ->pixels data */
	sheet_set_zoom_factor (ret, ret->last_zoom_factor_used);
	return ret;
}

/*
 * Create an XML subtree of doc equivalent to the given Workbook.
 */
static xmlNodePtr
xml_workbook_write (parse_xml_context_t *ctxt, Workbook *wb)
{
	xmlNodePtr cur;
	xmlNodePtr child;
	GList *sheets;

	/*
	 * General informations about the Sheet.
	 */
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Workbook", NULL);	/* the Workbook name !!! */
	if (cur == NULL)
		return NULL;

	child = xml_write_style (ctxt, &wb->style, -1);
	if (child)
		xmlAddChild (cur, child);

	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "Geometry", NULL);
	xml_set_value_int (child, "Width", wb->toplevel->allocation.width);
	xml_set_value_int (child, "Height", wb->toplevel->allocation.height);
	xmlAddChild (cur, child);

	/*
	 * Cells informations
	 */
	child = xmlNewChild (cur, ctxt->ns, "Sheets", NULL);
	ctxt->parent = child;

	sheets = workbook_sheets (wb);
	while (sheets) {
		xmlNodePtr cur, parent;
		Sheet *sheet = sheets->data;
		
		parent = ctxt->parent;
		cur = xml_sheet_write (ctxt, sheet);
		ctxt->parent = parent;
		xmlAddChild (parent, cur);

		sheets = g_list_next (sheets);
	}
	g_list_free (sheets);
	return cur;
}

static void
xml_sheet_create (parse_xml_context_t *ctxt, xmlNodePtr tree)
{
	char *val;
	xmlNodePtr child;
	
	if (strcmp (tree->name, "Sheet")){
		fprintf (stderr,
			 "xml_sheet_create: invalid element type %s, 'Sheet' expected\n",
			 tree->name);
		return;
	}
	child = tree->childs;
	val = xml_value_get (tree, "Name");
	if (val != NULL){
		Sheet *sheet;
		
		sheet = sheet_new (ctxt->wb, (const char *) val);
		workbook_attach_sheet (ctxt->wb, sheet);
		free (val);
	}
	return;
}

/*
 * Create a Workbook equivalent to the XML subtree of doc.
 */
static Workbook *
xml_workbook_read (parse_xml_context_t *ctxt, xmlNodePtr tree)
{
	Workbook *ret;
	Sheet *sheet;
	xmlNodePtr child, c;

	if (strcmp (tree->name, "Workbook")){
		fprintf (stderr,
			 "xml_workbook_read: invalid element type %s, 'Workbook' expected`\n",
			 tree->name);
		return NULL;
	}
	ret = workbook_new ();
	ctxt->wb = ret;

	child = xml_search_child (tree, "Geometry");
	if (child){
		int width, height;

		xml_get_value_int (child, "Width", &width);
		xml_get_value_int (child, "Height", &height);
/*      gtk_widget_set_usize(ret->toplevel, width, height); */
	}
	child = xml_search_child (tree, "Style");
	if (child != NULL)
		xml_read_style (ctxt, child, &ret->style);

	child = xml_search_child (tree, "Sheets");
	if (child == NULL)
		return ret;

	/*
	 * Pass 1: Create all the sheets, to make sure
	 * all of the references to forward sheets are properly
	 * handled
	 */
	c = child->childs;
	while (c != NULL){
		xml_sheet_create (ctxt, c);
		c = c->next;
	}
	
	/*
	 * Pass 2: read the contents
	 */
	c = child->childs;
	while (c != NULL){
		sheet = xml_sheet_read (ctxt, c);
		c = c->next;
	}
	
	return ret;
}

/*
 * We parse and do some limited validation of the XML file, if this
 * passes, then we return TRUE
 */
static gboolean
xml_probe (const char *filename)
{
	xmlDocPtr res;
	xmlNsPtr gmr;

	res = xmlParseFile (filename);
	if (res == NULL)
		return FALSE;

	if (res->root == NULL) {
		xmlFreeDoc (res);
		return FALSE;
	}

	gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/");
	if (res->root->name == NULL || strcmp (res->root->name, "Workbook") || (gmr == NULL)){
		xmlFreeDoc (res);
		return FALSE;
	}
	xmlFreeDoc (res);
	return TRUE;
}

/*
 * Open an XML file and read a Sheet
 * One parse the XML file, getting a tree, then analyze the tree to build
 * the actual in-memory structure.
 */
Sheet *
gnumeric_xml_read_sheet (const char *filename)
{
	Sheet *sheet;
	xmlDocPtr res;
	xmlNsPtr gmr;
	parse_xml_context_t ctxt;

	g_return_val_if_fail (filename != NULL, NULL);

	/*
	 * Load the file into an XML tree.
	 */
	res = xmlParseFile (filename);
	if (res == NULL)
		return NULL;
	if (res->root == NULL){
		fprintf (stderr, "gnumeric_xml_read_sheet %s: tree is empty\n", filename);
		xmlFreeDoc (res);
		return NULL;
	}
	/*
	 * Do a bit of checking, get the namespaces, and check the top elem.
	 */
	gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/");
	if (strcmp (res->root->name, "Sheet") || (gmr == NULL)){
		fprintf (stderr, "gnumeric_xml_read_sheet %s: not a Sheet file\n",
			 filename);
		xmlFreeDoc (res);
		return NULL;
	}
	ctxt.doc = res;
	ctxt.ns = gmr;
	ctxt.name_table = g_hash_table_new (g_str_hash, g_str_equal);
	ctxt.fontIdx = 1;
	sheet = xml_sheet_read (&ctxt, res->root);
	g_hash_table_foreach (ctxt.name_table, name_free, NULL);
	g_hash_table_destroy (ctxt.name_table);
	
	xmlFreeDoc (res);

	sheet->modified = FALSE;
	return sheet;
}

/*
 * Save a Sheet in an XML file
 * One build an in-memory XML tree and save it to a file.
 * returns 0 in case of success, -1 otherwise.
 */
int
gnumeric_xml_write_sheet (Sheet *sheet, const char *filename)
{
	parse_xml_context_t ctxt;
	xmlDocPtr xml;
	xmlNsPtr gmr;
	GList *sheet_list;
	int ret;

	g_return_val_if_fail (sheet != NULL, -1);
	g_return_val_if_fail (IS_SHEET (sheet), -1);
	g_return_val_if_fail (filename != NULL, -1);

	/*
	 * Create the tree
	 */
	xml = xmlNewDoc ("1.0");
	if (xml == NULL){
		return -1;
	}
	gmr = xmlNewGlobalNs (xml, "http://www.gnome.org/gnumeric/", "gmr");
	ctxt.doc = xml;
	ctxt.ns = gmr;
	ctxt.fontIdx = 1;
	ctxt.name_table = g_hash_table_new (g_str_hash, g_str_equal);
	
	xml->root = xml_sheet_write (&ctxt, sheet);

	g_hash_table_foreach (ctxt.name_table, name_free, NULL);
	g_hash_table_destroy (ctxt.name_table);
	
	/*
	 * Dump it.
	 */
	xmlSetDocCompressMode (xml, 9);
	ret = xmlSaveFile (filename, xml);
	xmlFreeDoc (xml);
	sheet->modified = FALSE;
	if (ret < 0)
		return -1;
	return 0;
}

/*
 * Open an XML file and read a Workbook
 * One parse the XML file, getting a tree, then analyze the tree to build
 * the actual in-memory structure.
 */

Workbook *
gnumeric_xml_read_workbook (const char *filename)
{
	Workbook *wb;
	xmlDocPtr res;
	xmlNsPtr gmr;
	parse_xml_context_t ctxt;

	g_return_val_if_fail (filename != NULL, NULL);

	/*
	 * Load the file into an XML tree.
	 */
	res = xmlParseFile (filename);
	if (res == NULL)
		return NULL;
	if (res->root == NULL){
		fprintf (stderr, "gnumeric_xml_read_workbook %s: tree is empty\n", filename);
		xmlFreeDoc (res);
		return NULL;
	}
	/*
	 * Do a bit of checking, get the namespaces, and chech the top elem.
	 */
	gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/");
	if (strcmp (res->root->name, "Workbook") || (gmr == NULL)){
		fprintf (stderr, "gnumeric_xml_read_workbook %s: not an Workbook file\n",
			 filename);
		xmlFreeDoc (res);
		return NULL;
	}
	ctxt.doc = res;
	ctxt.ns = gmr;
	ctxt.name_table = g_hash_table_new (g_str_hash, g_str_equal);
	ctxt.fontIdx = 1;
	wb = xml_workbook_read (&ctxt, res->root);
	workbook_set_filename (wb, (char *) filename);
	workbook_recalc_all (wb);
	g_hash_table_foreach (ctxt.name_table, name_free, NULL);
	g_hash_table_destroy (ctxt.name_table);

	xmlFreeDoc (res);
	return wb;
}

/*
 * Save a Workbook in an XML file
 * One build an in-memory XML tree and save it to a file.
 * returns 0 in case of success, -1 otherwise.
 */

int
gnumeric_xml_write_workbook (Workbook *wb, const char *filename)
{
	xmlDocPtr xml;
	xmlNsPtr gmr;
	parse_xml_context_t ctxt;
	int ret;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	/*
	 * Create the tree
	 */
	xml = xmlNewDoc ("1.0");
	if (xml == NULL){
		return -1;
	}
	gmr = xmlNewGlobalNs (xml, "http://www.gnome.org/gnumeric/", "gmr");
	ctxt.doc = xml;
	ctxt.ns = gmr;
	ctxt.fontIdx = 1;
	ctxt.name_table = g_hash_table_new (g_str_hash, g_str_equal);

	xml->root = xml_workbook_write (&ctxt, wb);

	g_hash_table_foreach (ctxt.name_table, name_free, NULL);
	g_hash_table_destroy (ctxt.name_table);
	
	/*
	 * Dump it.
	 */
	xmlSetDocCompressMode (xml, 9);
	ret = xmlSaveFile (filename, xml);
	xmlFreeDoc (xml);
	if (ret < 0)
		return -1;
	return 0;
}

void
xml_init (void)
{
	char *desc = _("Old Gnumeric XML file format");
	
	file_format_register_open (50, desc, xml_probe, gnumeric_xml_read_workbook);
	file_format_register_save (".gnumeric", desc, gnumeric_xml_write_workbook);
}


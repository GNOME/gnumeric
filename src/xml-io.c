/*
 * xml-io.c: save/read gnumeric Sheets using an XML encoding.
 *
 * Daniel Veillard <Daniel.Veillard@w3.org>
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
typedef struct parseXmlContext {
	xmlDocPtr doc;		/* Xml document */
	xmlNsPtr ns;		/* Main name space */
	xmlNodePtr parent;	/* used only for g_hash_table_foreach callbacks */
	GHashTable *nameTable;	/* to reproduce multiple refs with HREFs */
	int fontIdx;		/* for Font refs names ... */
	Sheet *sheet;		/* the associated sheet */
	Workbook *wb;		/* the associated sheet */
} parseXmlContext, *parseXmlContextPtr;

static Sheet      *readXmlSheet     (parseXmlContextPtr ctxt, xmlNodePtr tree);
static xmlNodePtr  writeXmlSheet    (parseXmlContextPtr ctxt, Sheet *sheet);
static Workbook   *readXmlWorkbook  (parseXmlContextPtr ctxt, xmlNodePtr tree);
static xmlNodePtr  writeXmlWorkbook (parseXmlContextPtr ctxt, Workbook *wb);
/* static guint       ptrHash          (gconstpointer a);
   static gint        ptrCompare       (gconstpointer a, gconstpointer b); */
static void nameFree                (gpointer key, gpointer value,
				     gpointer user_data);

/*
 * Internal stuff: xml helper functions.
 */

/*
 * Get a value for a node either carried as an attibute or as
 * the content of a child.
 */
static char *
xmlGetValue (xmlNodePtr node, const char *name)
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
xmlGetStringValue (xmlNodePtr node, const char *name)
{
	char *val;
	String *ret;

	val = xmlGetValue(node, name);
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
xmlGetIntValue (xmlNodePtr node, const char *name, int *val)
{
	char *ret;
	int i;
	int res;

	ret = xmlGetValue (node, name);
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
xmlGetFloatValue (xmlNodePtr node, const char *name, float *val)
{
	int res;
	char *ret;
	float f;

	ret = xmlGetValue (node, name);
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
xmlGetDoubleValue (xmlNodePtr node, const char *name, double *val)
{
	int res;
	char *ret;

	ret = xmlGetValue (node, name);
	if (ret == NULL) return(0);
	res = sscanf (ret, "%lf", val);
	free(ret);
	
	return (res == 1);
}

#if 0
/*
 * Get a set of coodinates for a node, carried as the content of a child.
 */
static int
xmlGetCoordinate (xmlNodePtr node, const char *name,
		  double *x, double *y)
{
	int res;
	char *ret;

	ret = xmlGetValue (node, name);
	if (ret == NULL) return(0);
	res = sscanf (ret, "(%lf %lf)", x, y);
	free(ret);
	
	return (res == 2);
}
#endif

/*
 * Get a pair of coodinates for a node, carried as the content of a child.
 */

static int
xmlGetCoordinates (xmlNodePtr node, const char *name,
		   double *x1, double *y1, double *x2, double *y2)
{
	int res;
	char *ret;

	ret = xmlGetValue (node, name);
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
xmlGetGnomeCanvasPoints (xmlNodePtr node, const char *name)
{
	char *val;
	GnomeCanvasPoints *ret = NULL;
	int res;
	const char *ptr;
	int index = 0, i;
	double coord[20];	/* TODO: must be dynamic !!!! */

	val = xmlGetValue (node, name);
	if (val == NULL) return(NULL);
	ptr = val;
	do {
		while ((*ptr) && (*ptr != '('))
			ptr++;
		if (*ptr == 0)
			break;
		res = sscanf (ptr, "(%lf %lf)", &coord[index], &coord[index + 1]);
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
xmlSetGnomeCanvasPoints (xmlNodePtr node, const char *name,
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
static void xmlSetValue (xmlNodePtr node, const char *name, const char *val)
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
static void xmlSetStringValue (xmlNodePtr node, const char *name, String *val)
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
xmlSetIntValue (xmlNodePtr node, const char *name, int val)
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
xmlSetFloatValue (xmlNodePtr node, const char *name, float val)
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

#if 0
/*
 * Set a double value for a node either carried as an attibute or as
 * the content of a child.
 */
static void
xmlSetDoubleValue (xmlNodePtr node, const char *name, double val)
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
#endif

/*
 * Search a child by name, if needed go down the tree to find it. 
 */
static xmlNodePtr xmlSearchChild (xmlNodePtr node, const char *name)
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
		ret = xmlSearchChild (child, name);
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
xmlGetColorValue (xmlNodePtr node, const char *name,
			     StyleColor **color)
{
	char *ret;
	int red, green, blue;

	ret = xmlGetValue (node, name);
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
static void xmlSetColorValue (xmlNodePtr node, const char *name,
			      StyleColor *val)
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

/**
 **
 ** Public functions : high level read and write.
 **
 **/

/*
 * Open an XML file and read a Sheet
 * One parse the XML file, getting a tree, then analyze the tree to build
 * the actual in-memory structure.
 */

Sheet *gnumericReadXmlSheet (const char *filename)
{
	Sheet *sheet;
	xmlDocPtr res;
	xmlNsPtr gmr;
	parseXmlContext ctxt;

	g_return_val_if_fail (filename != NULL, NULL);

	/*
	 * Load the file into an XML tree.
	 */
	res = xmlParseFile (filename);
	if (res == NULL)
		return NULL;
	if (res->root == NULL){
		fprintf (stderr, "gnumericReadXmlSheet %s: tree is empty\n", filename);
		xmlFreeDoc (res);
		return NULL;
	}
	/*
	 * Do a bit of checking, get the namespaces, and check the top elem.
	 */
	gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/");
	if (strcmp (res->root->name, "Sheet") || (gmr == NULL)){
		fprintf (stderr, "gnumericReadXmlSheet %s: not a Sheet file\n",
			 filename);
		xmlFreeDoc (res);
		return NULL;
	}
	ctxt.doc = res;
	ctxt.ns = gmr;
	ctxt.nameTable = g_hash_table_new (g_str_hash, g_str_equal);
	ctxt.fontIdx = 1;
	sheet = readXmlSheet (&ctxt, res->root);
	g_hash_table_foreach (ctxt.nameTable, nameFree, NULL);
	g_hash_table_destroy (ctxt.nameTable);

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
gnumericWriteXmlSheet (Sheet * sheet, const char *filename)
{
	xmlDocPtr xml;
	xmlNsPtr gmr;
	parseXmlContext ctxt;
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
	ctxt.nameTable = g_hash_table_new (g_str_hash, g_str_equal);
	ctxt.fontIdx = 1;
	xml->root = writeXmlSheet (&ctxt, sheet);
	g_hash_table_foreach (ctxt.nameTable, nameFree, NULL);
	g_hash_table_destroy (ctxt.nameTable);

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

Workbook *gnumericReadXmlWorkbook (const char *filename)
{
	Workbook *wb;
	xmlDocPtr res;
	xmlNsPtr gmr;
	parseXmlContext ctxt;

	g_return_val_if_fail (filename != NULL, NULL);

	/*
	 * Load the file into an XML tree.
	 */
	res = xmlParseFile (filename);
	if (res == NULL)
		return NULL;
	if (res->root == NULL){
		fprintf (stderr, "gnumericReadXmlWorkbook %s: tree is empty\n", filename);
		xmlFreeDoc (res);
		return NULL;
	}
	/*
	 * Do a bit of checking, get the namespaces, and chech the top elem.
	 */
	gmr = xmlSearchNsByHref (res, res->root, "http://www.gnome.org/gnumeric/");
	if (strcmp (res->root->name, "Workbook") || (gmr == NULL)){
		fprintf (stderr, "gnumericReadXmlWorkbook %s: not an Workbook file\n",
			 filename);
		xmlFreeDoc (res);
		return NULL;
	}
	ctxt.doc = res;
	ctxt.ns = gmr;
	ctxt.nameTable = g_hash_table_new (g_str_hash, g_str_equal);
	ctxt.fontIdx = 1;
	wb = readXmlWorkbook (&ctxt, res->root);
	workbook_set_filename (wb, (char *) filename);
	workbook_recalc_all (wb);
	g_hash_table_foreach (ctxt.nameTable, nameFree, NULL);
	g_hash_table_destroy (ctxt.nameTable);

	xmlFreeDoc (res);
	return wb;
}

/*
 * Save a Workbook in an XML file
 * One build an in-memory XML tree and save it to a file.
 * returns 0 in case of success, -1 otherwise.
 */

int
gnumericWriteXmlWorkbook (Workbook *wb, const char *filename)
{
	xmlDocPtr xml;
	xmlNsPtr gmr;
	parseXmlContext ctxt;
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
	ctxt.nameTable = g_hash_table_new (g_str_hash, g_str_equal);
	ctxt.fontIdx = 1;
	xml->root = writeXmlWorkbook (&ctxt, wb);
	g_hash_table_foreach (ctxt.nameTable, nameFree, NULL);
	g_hash_table_destroy (ctxt.nameTable);

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

/**
 **
 ** Private functions : mapping between in-memory structure and XML tree
 **
 **/

/*
 * Free a name when cleaning up the name Hash table.
 */
static void
nameFree (gpointer key, gpointer value, gpointer user_data)
{
	g_free (key);
}

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
writeXmlStyleBorder (parseXmlContextPtr ctxt, StyleBorder *border)
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
 			xmlSetColorValue (side, "Color", border->color [lp]);
 		}
	}
	return cur;
}

/*
 * Create a StyleBorder equivalent to the XML subtree of doc.
 */
static StyleBorder *
readXmlStyleBorder (parseXmlContextPtr ctxt, xmlNodePtr tree)
{
	StyleBorder *ret;
 	StyleBorderType style [4] = { BORDER_NONE, BORDER_NONE, BORDER_NONE, BORDER_NONE };
 	StyleColor *color [4] = { NULL, NULL, NULL, NULL };
	xmlNodePtr side;
	int lp;

	if (strcmp (tree->name, "StyleBorder")){
		fprintf (stderr,
			 "readXmlStyleBorder: invalid element type %s, 'StyleBorder' expected`\n",
			 tree->name);
	}

 	for (lp = 0; lp < 4; lp++)
 	{
 		if ((side = xmlSearchChild (tree, StyleSideNames [lp])) != NULL)
 		{
 			/* FIXME: need to read the proper type */
 			style [lp] = BORDER_THICK ;
 			xmlGetColorValue (side, "Color", &color [lp]);
 		}
	}
	
	ret = style_border_new (style, color);

	return NULL;
}

/*
 * Create an XML subtree of doc equivalent to the given Style.
 */
static xmlNodePtr
writeXmlStyle (parseXmlContextPtr ctxt, Style * style)
{
	xmlNodePtr cur, child;
	char str[50];
	char *name;

	if ((style->halign == 0) && (style->valign == 0) &&
	    (style->orientation == 0) && (style->format == NULL) &&
	    (style->font == NULL) && (style->border == NULL))
		return NULL;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Style", NULL);
	xmlSetIntValue (cur, "HAlign", style->halign);
	xmlSetIntValue (cur, "VAlign", style->valign);
	xmlSetIntValue (cur, "Fit", style->fit_in_cell);
	xmlSetIntValue (cur, "Orient", style->orientation);
	xmlSetIntValue (cur, "Shade", style->pattern);

	if (!style_is_default_fore (style->fore_color))
		xmlSetColorValue (cur, "Fore", style->fore_color);

	if (!style_is_default_back (style->back_color))
		xmlSetColorValue (cur, "Back", style->back_color);

	if (style->format != NULL){
		xmlSetValue (cur, "Format", style->format->format);
	}

	if (style->font != NULL){
		if ((name = (char *)
		     g_hash_table_lookup (ctxt->nameTable, style->font)) != NULL){
			child = xmlNewChild (cur, ctxt->ns, "Font", NULL);
			sprintf (str, "#%s", name);
			xmlNewProp (child, "HREF", str);
		} else {
			child = xmlNewChild (cur, ctxt->ns, "Font", 
			           xmlEncodeEntities(ctxt->doc,
				                     style->font->font_name));
			xmlSetIntValue (child, "Unit", style->font->units);
			sprintf (str, "FontDef%d", ctxt->fontIdx++);
			xmlNewProp (child, "NAME", str);
			g_hash_table_insert (ctxt->nameTable, g_strdup (str), style->font);
		}

	}

	if (style->border != NULL){
		child = writeXmlStyleBorder (ctxt, style->border);
		if (child)
			xmlAddChild (cur, child);
	}

	return cur;
}

/*
 * Create a Style equivalent to the XML subtree of doc.
 */
static Style *
readXmlStyle (parseXmlContextPtr ctxt, xmlNodePtr tree, Style * ret)
{
	xmlNodePtr child;
	char *prop;
	int val;
	StyleColor *c;

	if (strcmp (tree->name, "Style")){
		fprintf (stderr,
			 "readXmlStyle: invalid element type %s, 'Style' expected\n",
			 tree->name);
	}
	if (ret == NULL)
		ret = style_new_empty ();
	
	if (ret == NULL)
		return NULL;

	if (xmlGetIntValue (tree, "HAlign", &val)){
		ret->halign = val;
		ret->valid_flags |= STYLE_ALIGN;
	}
	if (xmlGetIntValue (tree, "Fit", &val)){
		ret->fit_in_cell = val;
		ret->valid_flags |= STYLE_ALIGN;
	}
	if (xmlGetIntValue (tree, "VAlign", &val)){
		ret->valign = val;
		ret->valid_flags |= STYLE_ALIGN;
	}
	if (xmlGetIntValue (tree, "Orient", &val)){
		ret->orientation = val;
		ret->valid_flags |= STYLE_ALIGN;
	}
	if (xmlGetIntValue (tree, "Shade", &val)){
		ret->pattern = val;
		ret->valid_flags |= STYLE_PATTERN;
	}
	if (xmlGetColorValue (tree, "Fore", &c)){
		ret->fore_color = c;
		ret->valid_flags |= STYLE_FORE_COLOR;
	}
	if (xmlGetColorValue (tree, "Back", &c)){
		ret->back_color = c;
		ret->valid_flags |= STYLE_BACK_COLOR;
	}
	prop = xmlGetValue (tree, "Format");
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

			v = xmlGetValue (child, "NAME");
			if (v){
				int units = 14;
				char *font;

				xmlGetIntValue (child, "Unit", &units);
				font = xmlNodeGetContent(child);
				if (font != NULL) {
					ret->font = style_font_new (font, units);
					free(font);
				}
				if (ret->font){
					g_hash_table_insert (ctxt->nameTable, g_strdup (v), ret->font);
					ret->valid_flags |= STYLE_FONT;
				}
				free (v);
			} else {
				StyleFont *font;

				v = xmlGetValue (child, "HREF");
				if (v){
					font = g_hash_table_lookup (ctxt->nameTable, v + 1);
					if (font){
						ret->font = font;
						style_font_ref (font);
						ret->valid_flags |= STYLE_FONT;
					}
				}
			}
		} else if (!strcmp (child->name, "StyleBorder")){
			StyleBorder *sb;

			sb = readXmlStyleBorder (ctxt, child);
		} else {
			fprintf (stderr, "readXmlStyle: unknown type '%s'\n",
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
writeXmlStyleRegion (parseXmlContextPtr ctxt, StyleRegion *region)
{
	xmlNodePtr cur, child;

	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "StyleRegion", NULL);
	xmlSetIntValue (cur, "startCol", region->range.start_col);
	xmlSetIntValue (cur, "endCol", region->range.end_col);
	xmlSetIntValue (cur, "startRow", region->range.start_row);
	xmlSetIntValue (cur, "endRow", region->range.end_row);

	if (region->style != NULL){
		child = writeXmlStyle (ctxt, region->style);
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
readXmlStyleRegion (parseXmlContextPtr ctxt, xmlNodePtr tree)
{
	xmlNodePtr child;
	Style *style = NULL;
	int start_col = 0, start_row = 0, end_col = 0, end_row = 0;

	if (strcmp (tree->name, "StyleRegion")){
		fprintf (stderr,
			 "readXmlStyleRegion: invalid element type %s, 'StyleRegion' expected`\n",
			 tree->name);
		return;
	}
	xmlGetIntValue (tree, "startCol", &start_col);
	xmlGetIntValue (tree, "startRow", &start_row);
	xmlGetIntValue (tree, "endCol", &end_col);
	xmlGetIntValue (tree, "endRow", &end_row);
	child = tree->childs;
	if (child != NULL)
		style = readXmlStyle (ctxt, child, NULL);
	if (style != NULL)
		sheet_style_attach (ctxt->sheet, start_col, start_row, end_col,
				    end_row, style);

}

/*
 * Create an XML subtree of doc equivalent to the given ColRowInfo.
 */
static xmlNodePtr
writeXmlColRowInfo (parseXmlContextPtr ctxt, ColRowInfo *info, int col)
{
	xmlNodePtr cur;

	if (col)
		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "ColInfo", NULL);
	else
		cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "RowInfo", NULL);

	xmlSetIntValue (cur, "No", info->pos);
	xmlSetIntValue (cur, "Unit", info->units);
	xmlSetIntValue (cur, "MarginA", info->margin_a);
	xmlSetIntValue (cur, "MarginB", info->margin_b);
	xmlSetIntValue (cur, "HardSize", info->hard_size);

	return cur;
}

/*
 * Create a ColRowInfo equivalent to the XML subtree of doc.
 */
static ColRowInfo *
readXmlColRowInfo (parseXmlContextPtr ctxt,
		   xmlNodePtr tree, ColRowInfo *ret)
{
	int col = 0;
	int val;

	if (!strcmp (tree->name, "ColInfo")){
		col = 1;
	} else if (!strcmp (tree->name, "RowInfo")){
		col = 0;
	} else {
		fprintf (stderr,
			 "readXmlColRowInfo: invalid element type %s, 'ColInfo/RowInfo' expected`\n",
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

	xmlGetIntValue (tree, "No", &ret->pos);
	xmlGetIntValue (tree, "Unit", &ret->units);
	xmlGetIntValue (tree, "MarginA", &ret->margin_a);
	xmlGetIntValue (tree, "MarginB", &ret->margin_b);
	if (xmlGetIntValue (tree, "HardSize", &val))
		ret->hard_size = val;

	return ret;
}

/*
 * Create an XML subtree of doc equivalent to the given Object.
 */
static xmlNodePtr
writeXmlObject (parseXmlContextPtr ctxt, SheetObject *object)
{
	SheetObjectGraphic *sog = SHEET_OBJECT_GRAPHIC (object);
	xmlNodePtr cur = NULL;

	switch (sog->type){
	case SHEET_OBJECT_RECTANGLE:{
			SheetObjectFilled *sof = SHEET_OBJECT_FILLED (object);

			cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Rectangle", NULL);
			if (sof->fill_color != NULL)
				xmlSetStringValue (cur, "FillColor", sof->fill_color);
			xmlSetIntValue (cur, "Pattern", sof->pattern);
			break;
		}

	case SHEET_OBJECT_ELLIPSE:{
			SheetObjectFilled *sof = SHEET_OBJECT_FILLED (object);

			cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Ellipse", NULL);
			if (sof->fill_color != NULL)
				xmlSetStringValue (cur, "FillColor", sof->fill_color);
			xmlSetIntValue (cur, "Pattern", sof->pattern);
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
	
	xmlSetGnomeCanvasPoints (cur, "Points", object->bbox_points);
	xmlSetIntValue (cur, "Width", sog->width);
	xmlSetStringValue (cur, "Color", sog->color);

	return cur;
}

/*
 * Create a Object equivalent to the XML subtree of doc.
 */
static SheetObject *
readXmlObject (parseXmlContextPtr ctxt, xmlNodePtr tree)
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
			 "readXmlObject: invalid element type %s, 'Rectangle/Ellipse ...' expected`\n",
			 tree->name);
		return NULL;
	}
	
	color = (char *) xmlGetValue (tree, "Color");
	xmlGetCoordinates (tree, "Points", &x1, &y1, &x2, &y2);
	xmlGetIntValue (tree, "Width", &width);
	if ((type == SHEET_OBJECT_RECTANGLE) ||
	    (type == SHEET_OBJECT_ELLIPSE)){
		fill_color = (char *) xmlGetValue (tree, "FillColor");
		xmlGetIntValue (tree, "Pattern", &pattern);
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
writeXmlCell (parseXmlContextPtr ctxt, Cell *cell)
{
	xmlNodePtr cur, child;
	char *text;
	
	cur = xmlNewDocNode (ctxt->doc, ctxt->ns, "Cell", NULL);
	xmlSetIntValue (cur, "Col", cell->col->pos);
	xmlSetIntValue (cur, "Row", cell->row->pos);
	child = writeXmlStyle (ctxt, cell->style);

	if (child)
		xmlAddChild (cur, child);
	text = cell_get_content (cell);
	xmlNewChild(cur, ctxt->ns, "Content",
	            xmlEncodeEntities(ctxt->doc, text));
	g_free (text);

	return cur;
}

/*
 * Create a Cell equivalent to the XML subtree of doc.
 */
static Cell *
readXmlCell (parseXmlContextPtr ctxt, xmlNodePtr tree)
{
	Style *style;
	Cell *ret;
	xmlNodePtr childs;
	int row = 0, col = 0;
	char *content = NULL;

	if (strcmp (tree->name, "Cell")){
		fprintf (stderr,
		 "readXmlCell: invalid element type %s, 'Cell' expected`\n",
			 tree->name);
		return NULL;
	}
	xmlGetIntValue (tree, "Col", &col);
	xmlGetIntValue (tree, "Row", &row);

	cell_deep_freeze_redraws ();

	ret = sheet_cell_get (ctxt->sheet, col, row);
	if (ret == NULL)
		ret = sheet_cell_new (ctxt->sheet, col, row);
	if (ret == NULL)
		return NULL;

	childs = tree->childs;
	while (childs != NULL) {
	        if (!strcmp (childs->name, "Style")) {
		        style = readXmlStyle (ctxt, childs, NULL);
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
writeXmlCellTo (gpointer key, gpointer value, gpointer data)
{
	parseXmlContextPtr ctxt = (parseXmlContextPtr) data;
	xmlNodePtr cur;

	cur = writeXmlCell (ctxt, (Cell *) value);
	xmlAddChild (ctxt->parent, cur);
}

/*
 * Create an XML subtree of doc equivalent to the given Sheet.
 */
static xmlNodePtr
writeXmlSheet (parseXmlContextPtr ctxt, Sheet *sheet)
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
	 * Cols informations.
	 */
	cols = xmlNewChild (cur, ctxt->ns, "Cols", NULL);
	l = sheet->cols_info;
	while (l){
		child = writeXmlColRowInfo (ctxt, l->data, 1);
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
		child = writeXmlColRowInfo (ctxt, l->data, 0);
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
			child = writeXmlObject (ctxt, l->data);
			if (child)
				xmlAddChild (objects, child);
			l = l->next;
		}
	} else if (sheet->current_object != NULL){
		objects = xmlNewChild (cur, ctxt->ns, "Objects", NULL);
		child = writeXmlObject (ctxt, sheet->current_object);
		if (child)
			xmlAddChild (objects, child);
	}
	/*
	 * Cells informations
	 */
	cells = xmlNewChild (cur, ctxt->ns, "Cells", NULL);
	ctxt->parent = cells;
	g_hash_table_foreach (sheet->cell_hash, writeXmlCellTo, ctxt);
	sheet->modified = 0;
	return cur;
}

/*
 * Create a Sheet equivalent to the XML subtree of doc.
 */
static Sheet *
readXmlSheet (parseXmlContextPtr ctxt, xmlNodePtr tree)
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
			 "readXmlSheet: invalid element type %s, 'Sheet' expected\n",
			 tree->name);
	}
	child = tree->childs;

	/*
	 * Get the name of the sheet.  If it does exist, use the existing
	 * name, otherwise create a sheet (ie, for the case of only reading
	 * a new sheet).
	 */
	val = xmlGetValue (tree, "Name");
	if (val != NULL){
		ret = workbook_sheet_lookup (ctxt->wb, (const char *) val);
		if (ret == NULL)
			ret = sheet_new (ctxt->wb, (const char *) val);
		free (val);
	} 

	if (ret == NULL)
		return NULL;

	ctxt->sheet = ret;

	xmlGetIntValue (tree, "MaxCol", &ret->max_col_used);
	xmlGetIntValue (tree, "MaxRow", &ret->max_row_used);
	xmlGetDoubleValue (tree, "Zoom", &ret->last_zoom_factor_used);
	child = xmlSearchChild (tree, "Styles");
	if (child != NULL){
		regions = child->childs;
		while (regions != NULL){
			readXmlStyleRegion (ctxt, regions);
			regions = regions->next;
		}
	}
	child = xmlSearchChild (tree, "Cols");
	if (child != NULL){
		ColRowInfo *info;

		cols = child->childs;
		while (cols != NULL){
			info = readXmlColRowInfo (ctxt, cols, NULL);
			if (info != NULL)
				sheet_col_add (ret, info);
			cols = cols->next;
		}
	}
	child = xmlSearchChild (tree, "Rows");
	if (child != NULL){
		ColRowInfo *info;

		rows = child->childs;
		while (rows != NULL){
			info = readXmlColRowInfo (ctxt, rows, NULL);
			if (info != NULL)
				sheet_row_add (ret, info);
			rows = rows->next;
		}
	}
	child = xmlSearchChild (tree, "Objects");
	if (child != NULL){
		objects = child->childs;
		while (objects != NULL){
			readXmlObject (ctxt, objects);
			objects = objects->next;
		}
	}
	child = xmlSearchChild (tree, "Cells");
	if (child != NULL){
		cells = child->childs;
		while (cells != NULL){
			readXmlCell (ctxt, cells);
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
writeXmlWorkbook (parseXmlContextPtr ctxt, Workbook *wb)
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

	child = writeXmlStyle (ctxt, &wb->style);
	if (child)
		xmlAddChild (cur, child);

	child = xmlNewDocNode (ctxt->doc, ctxt->ns, "Geometry", NULL);
	xmlSetIntValue (child, "Width", wb->toplevel->allocation.width);
	xmlSetIntValue (child, "Height", wb->toplevel->allocation.height);
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
		cur = writeXmlSheet (ctxt, sheet);
		ctxt->parent = parent;
		xmlAddChild (parent, cur);

		sheets = g_list_next (sheets);
	}
	g_list_free (sheets);
	return cur;
}

static void
createXmlSheet (parseXmlContextPtr ctxt, xmlNodePtr tree)
{
	char *val;
	xmlNodePtr child;
	
	if (strcmp (tree->name, "Sheet")){
		fprintf (stderr,
			 "createXmlSheet: invalid element type %s, 'Sheet' expected\n",
			 tree->name);
		return;
	}
	child = tree->childs;
	val = xmlGetValue (tree, "Name");
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
readXmlWorkbook (parseXmlContextPtr ctxt, xmlNodePtr tree)
{
	Workbook *ret;
	Sheet *sheet;
	xmlNodePtr child, c;

	if (strcmp (tree->name, "Workbook")){
		fprintf (stderr,
			 "readXmlWorkbook: invalid element type %s, 'Workbook' expected`\n",
			 tree->name);
		return NULL;
	}
	ret = workbook_new ();
	ctxt->wb = ret;

	child = xmlSearchChild (tree, "Geometry");
	if (child){
		int width, height;

		xmlGetIntValue (child, "Width", &width);
		xmlGetIntValue (child, "Height", &height);
/*      gtk_widget_set_usize(ret->toplevel, width, height); */
	}
	child = xmlSearchChild (tree, "Style");
	if (child != NULL)
		readXmlStyle (ctxt, child, &ret->style);

	child = xmlSearchChild (tree, "Sheets");
	if (child == NULL)
		return ret;

	/*
	 * Pass 1: Create all the sheets, to make sure
	 * all of the references to forward sheets are properly
	 * handled
	 */
	c = child->childs;
	while (c != NULL){
		createXmlSheet (ctxt, c);
		c = c->next;
	}
	
	/*
	 * Pass 2: read the contents
	 */
	c = child->childs;
	while (c != NULL){
		sheet = readXmlSheet (ctxt, c);
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

void
xml_init (void)
{
	char *desc = _("Gnumeric XML file format");
	
	file_format_register_open (50, desc, xml_probe, gnumericReadXmlWorkbook);
	file_format_register_save (".gnumeric", desc, gnumericWriteXmlWorkbook);
}

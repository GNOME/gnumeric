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

#include "xml-io.h"

/*
 * A parsing context.
 */
typedef struct parseXmlContext {
    xmlDocPtr  doc;        /* Xml document */
    xmlNsPtr   ns;         /* Main name space */
    xmlNodePtr parent;     /* used only for g_hash_table_foreach callbacks */
    GHashTable *nameTable; /* to reproduce multiple refs with HREFs */
    int        fontIdx;    /* for Font refs names ... */
    Sheet     *sheet;      /* the associated sheet */
    Workbook  *wb;         /* the associated sheet */
} parseXmlContext, *parseXmlContextPtr;

static Sheet      *readXmlSheet     (parseXmlContextPtr ctxt, xmlNodePtr tree);
static xmlNodePtr  writeXmlSheet    (parseXmlContextPtr ctxt, Sheet *sheet);
static Workbook   *readXmlWorkbook  (parseXmlContextPtr ctxt, xmlNodePtr tree);
static xmlNodePtr  writeXmlWorkbook (parseXmlContextPtr ctxt, Workbook *wb);
static guint       ptrHash          (gconstpointer a);
static gint        ptrCompare       (gconstpointer a, gconstpointer b);
static void        nameFree         (gpointer key, gpointer value, gpointer user_data);

/**
 ** Internal stuff: xml helper functions.
 **/

/*
 * Get a value for a node either carried as an attibute or as
 * the content of a child.
 */
static const char *xmlGetValue(xmlNodePtr node, const char *name) {
    const char *ret;
    xmlNodePtr child;

    ret = xmlGetProp(node, name);
    if (ret != NULL) return(ret);
    child = node->childs;
    while (child != NULL) {
        if ((!strcmp(child->name, name)) && (child->content != NULL))
	    return(child->content);
	child = child->next;
    }
    return(NULL);
}

/*
 * Get an integer value for a node either carried as an attibute or as
 * the content of a child.
 */
static int xmlGetIntValue(xmlNodePtr node, const char *name, int *val) {
    const char *ret;
    xmlNodePtr child;
    int i;

    ret = xmlGetProp(node, name);
    if ((ret != NULL) && (sscanf(ret, "%d", &i) == 1)) {
	*val = i;
        return(1);
    }
    child = node->childs;
    while (child != NULL) {
        if (((!strcmp(child->name, name)) && (child->content != NULL) &&
	    (sscanf(child->content, "%d", &i) == 1))) {
	    *val = i;
	    return(1);
	}
	child = child->next;
    }
    return(0);
}

/*
 * Get a float value for a node either carried as an attibute or as
 * the content of a child.
 */
static int xmlGetFloatValue(xmlNodePtr node, const char *name, float *val) {
    const char *ret;
    xmlNodePtr child;
    float f;

    ret = xmlGetProp(node, name);
    if ((ret != NULL) && (sscanf(ret, "%f", &f) == 1)) {
	*val = f;
        return(1);
    }
    child = node->childs;
    while (child != NULL) {
        if (((!strcmp(child->name, name)) && (child->content != NULL) &&
	    (sscanf(child->content, "%f", &f) == 1))) {
	    *val = f;
	    return(1);
	}
	child = child->next;
    }
    return(0);
}

/*
 * Get a double value for a node either carried as an attibute or as
 * the content of a child.
 */
static int xmlGetDoubleValue(xmlNodePtr node, const char *name, double *val) {
    const char *ret;
    xmlNodePtr child;
    float f;

    ret = xmlGetProp(node, name);
    if ((ret != NULL) && (sscanf(ret, "%f", &f) == 1)) {
	*val = f;
        return(1);
    }
    child = node->childs;
    while (child != NULL) {
        if (((!strcmp(child->name, name)) && (child->content != NULL) &&
	    (sscanf(child->content, "%f", &f) == 1))) {
	    *val = f;
	    return(1);
	}
	child = child->next;
    }
    return(0);
}

/*
 * Set a string value for a node either carried as an attibute or as
 * the content of a child.
 */
static void xmlSetValue(xmlNodePtr node, const char *name, const char *val) {
    const char *ret;
    xmlNodePtr child;

    ret = xmlGetProp(node, name);
    if (ret != NULL) {
	xmlSetProp(node, name, val);
	return;
    }
    child = node->childs;
    while (child != NULL) {
        if (!strcmp(child->name, name)) {
            xmlNodeSetContent(child, val);
	    return;
	}
	child = child->next;
    }
    xmlSetProp(node, name, val);
}

/*
 * Set an integer value for a node either carried as an attibute or as
 * the content of a child.
 */
static void xmlSetIntValue(xmlNodePtr node, const char *name, int val) {
    const char *ret;
    xmlNodePtr child;
    char str[101];

    snprintf(str, 100, "%d", val);
    ret = xmlGetProp(node, name);
    if (ret != NULL) {
	xmlSetProp(node, name, str);
	return;
    }
    child = node->childs;
    while (child != NULL) {
        if (!strcmp(child->name, name)) {
            xmlNodeSetContent(child, str);
	    return;
	}
	child = child->next;
    }
    xmlSetProp(node, name, str);
}

/*
 * Set a float value for a node either carried as an attibute or as
 * the content of a child.
 */
static void xmlSetFloatValue(xmlNodePtr node, const char *name, float val) {
    const char *ret;
    xmlNodePtr child;
    char str[101];

    snprintf(str, 100, "%f", val);
    ret = xmlGetProp(node, name);
    if (ret != NULL) {
	xmlSetProp(node, name, str);
	return;
    }
    child = node->childs;
    while (child != NULL) {
        if (!strcmp(child->name, name)) {
            xmlNodeSetContent(child, str);
	    return;
	}
	child = child->next;
    }
    xmlSetProp(node, name, str);
}

/*
 * Set a double value for a node either carried as an attibute or as
 * the content of a child.
 */
static void xmlSetDoubleValue(xmlNodePtr node, const char *name, double val) {
    const char *ret;
    xmlNodePtr child;
    char str[101];

    snprintf(str, 100, "%f", (float) val);
    ret = xmlGetProp(node, name);
    if (ret != NULL) {
	xmlSetProp(node, name, str);
	return;
    }
    child = node->childs;
    while (child != NULL) {
        if (!strcmp(child->name, name)) {
            xmlNodeSetContent(child, str);
	    return;
	}
	child = child->next;
    }
    xmlSetProp(node, name, str);
}

/*
 * Search a child by name, if needed go down the tree to find it. 
 */
static xmlNodePtr xmlSearchChild(xmlNodePtr node, const char *name) {
    xmlNodePtr ret;
    xmlNodePtr child;

    child = node->childs;
    while (child != NULL) {
        if (!strcmp(child->name, name))
	    return(child);
	child = child->next;
    }
    child = node->childs;
    while (child != NULL) {
        ret = xmlSearchChild(child, name);
	if (ret != NULL) return(ret);
	child = child->next;
    }
    return(NULL);
}

/*
 * Get a color value for a node either carried as an attibute or as
 * the content of a child.
 *
 * TODO PBM: at parse time one doesn't have yet a widget, so we have
 *           to retrieve the default colormap, but this may be a bad
 *           option ...
 */
static int xmlGetColorValue(xmlNodePtr node, const char *name,
                            GdkColor **val) {
    const char *ret;
    xmlNodePtr child;
    GdkColormap *colormap;
    GdkColor col;
    int red, green, blue;
    

    colormap = gtk_widget_get_default_colormap();
    if (colormap == NULL) {
        fprintf(stderr, "xmlGetColorValue : cannot get default_colormap\n");
        return(0);
    }

    ret = xmlGetProp(node, name);
    if ((ret != NULL) &&
        (sscanf(ret, "%X:%X:%X", &red, &green, &blue) == 3)) {
	col.red = red; col.green = green; col.blue = blue;
	*val = gdk_color_copy(&col);
	gdk_color_alloc (colormap, *val);
        return(1);
    }
    child = node->childs;
    while (child != NULL) {
        if ((!strcmp(child->name, name)) && (child->content != NULL) &&
	    (sscanf(ret, "%X:%X:%X", &red, &green, &blue) == 3)) {
	    col.red = red; col.green = green; col.blue = blue;
	    *val = gdk_color_copy(&col);
	    gdk_color_alloc (colormap, *val);
	    return(1);
	}
	child = child->next;
    }
    return(0);
}

/*
 * Set a color value for a node either carried as an attibute or as
 * the content of a child.
 */
static void xmlSetColorValue(xmlNodePtr node, const char *name,
                             GdkColor *val) {
    const char *ret;
    xmlNodePtr child;
    char str[101];

    snprintf(str, 100, "%X:%X:%X", val->red, val->green, val->blue);
    ret = xmlGetProp(node, name);
    if (ret != NULL) {
	xmlSetProp(node, name, str);
	return;
    }
    child = node->childs;
    while (child != NULL) {
        if (!strcmp(child->name, name)) {
            xmlNodeSetContent(child, str);
	    return;
	}
	child = child->next;
    }
    xmlSetProp(node, name, str);
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

Sheet *gnumericReadXmlSheet(const char *filename) {
    Sheet *sheet;
    xmlDocPtr res;
    xmlNsPtr gmr;
    parseXmlContext ctxt;

    g_return_val_if_fail (filename != NULL, NULL);
    
    /*
     * Load the file into an XML tree.
     */
    res = xmlParseFile(filename);
    if (res == NULL) return(NULL);
    if (res->root == NULL) {
        fprintf(stderr, "gnumericReadXmlSheet %s: tree is empty\n", filename);
	xmlFreeDoc(res);
	return(NULL);
    }

    /*
     * Do a bit of checking, get the namespaces, and check the top elem.
     */
    gmr = xmlSearchNsByHref(res, res->root, "http://www.gnome.org/gnumeric/");
    if (strcmp(res->root->name, "Sheet") || (gmr == NULL)) {
        fprintf(stderr, "gnumericReadXmlSheet %s: not a Sheet file\n",
	        filename);
	xmlFreeDoc(res);
        return(NULL);
    }

    ctxt.doc = res;
    ctxt.ns = gmr;
    ctxt.nameTable = g_hash_table_new(ptrHash, ptrCompare);
    ctxt.fontIdx = 1;
    sheet = readXmlSheet(&ctxt, res->root);
    g_hash_table_foreach(ctxt.nameTable, nameFree, NULL);
    g_hash_table_destroy(ctxt.nameTable);

    xmlFreeDoc(res);
    return(sheet);
}

/*
 * Save a Sheet in an XML file
 * One build an in-memory XML tree and save it to a file.
 * returns 0 in case of success, -1 otherwise.
 */

int gnumericWriteXmlSheet(Sheet *sheet, const char *filename) {
    FILE *output;
    xmlDocPtr xml;
    xmlNsPtr gmr;
    parseXmlContext ctxt;

    g_return_val_if_fail (sheet != NULL, -1);
    g_return_val_if_fail (IS_SHEET (sheet), -1);
    g_return_val_if_fail (filename != NULL, -1);
    /*
     * Open in write mode, !!! Save a bak ?
     */
    output = fopen(filename, "w");
    if (output == NULL) {
	perror("fopen");
        fprintf(stderr, "gnumericWriteXmlSheet: couldn't save to file %s\n",
	        filename);
	return(-1);
    }
    
    /*
     * Create the tree
     */
    xml = xmlNewDoc("1.0");
    if (xml == NULL) {
	fclose(output);
	return(-1);
    }
    gmr = xmlNewGlobalNs(xml, "http://www.gnome.org/gnumeric/", "gmr");
    ctxt.doc = xml;
    ctxt.ns = gmr;
    ctxt.nameTable = g_hash_table_new(ptrHash, ptrCompare);
    ctxt.fontIdx = 1;
    xml->root = writeXmlSheet(&ctxt, sheet);
    g_hash_table_foreach(ctxt.nameTable, nameFree, NULL);
    g_hash_table_destroy(ctxt.nameTable);

    /*
     * Dump it.
     */
    xmlDocDump(output, xml); /* !!! Should add a return code and check */
    xmlFreeDoc(xml);
    fclose(output);
    return(0);
}

/*
 * Open an XML file and read a Workbook
 * One parse the XML file, getting a tree, then analyze the tree to build
 * the actual in-memory structure.
 */

Workbook *gnumericReadXmlWorkbook(const char *filename) {
    Workbook *wb;
    xmlDocPtr res;
    xmlNsPtr gmr;
    parseXmlContext ctxt;

    g_return_val_if_fail (filename != NULL, NULL);
    
    /*
     * Load the file into an XML tree.
     */
    res = xmlParseFile(filename);
    if (res == NULL) return(NULL);
    if (res->root == NULL) {
        fprintf(stderr, "gnumericReadXmlWorkbook %s: tree is empty\n", filename);
	xmlFreeDoc(res);
	return(NULL);
    }

    /*
     * Do a bit of checking, get the namespaces, and chech the top elem.
     */
    gmr = xmlSearchNsByHref(res, res->root, "http://www.gnome.org/gnumeric/");
    if (strcmp(res->root->name, "Workbook") || (gmr == NULL)) {
        fprintf(stderr, "gnumericReadXmlWorkbook %s: not an Workbook file\n",
	        filename);
	xmlFreeDoc(res);
        return(NULL);
    }

    ctxt.doc = res;
    ctxt.ns = gmr;
    ctxt.nameTable = g_hash_table_new(ptrHash, ptrCompare);
    ctxt.fontIdx = 1;
    wb = readXmlWorkbook(&ctxt, res->root);
    g_hash_table_foreach(ctxt.nameTable, nameFree, NULL);
    g_hash_table_destroy(ctxt.nameTable);

    xmlFreeDoc(res);
    return(wb);
}

/*
 * Save a Workbook in an XML file
 * One build an in-memory XML tree and save it to a file.
 * returns 0 in case of success, -1 otherwise.
 */

int gnumericWriteXmlWorkbook(Workbook *wb, const char *filename) {
    FILE *output;
    xmlDocPtr xml;
    xmlNsPtr gmr;
    parseXmlContext ctxt;

    g_return_val_if_fail (wb != NULL, -1);
    g_return_val_if_fail (filename != NULL, -1);
    
    /*
     * Open in write mode, !!! Save a bak ?
     */
    output = fopen(filename, "w");
    if (output == NULL) {
	perror("fopen");
        fprintf(stderr, "gnumericWriteXmlWorkbook: couldn't save to file %s\n",
	        filename);
	return(-1);
    }
    
    /*
     * Create the tree
     */
    xml = xmlNewDoc("1.0");
    if (xml == NULL) {
	fclose(output);
	return(-1);
    }
    gmr = xmlNewGlobalNs(xml, "http://www.gnome.org/gnumeric/", "gmr");
    ctxt.doc = xml;
    ctxt.ns = gmr;
    ctxt.nameTable = g_hash_table_new(ptrHash, ptrCompare);
    ctxt.fontIdx = 1;
    xml->root = writeXmlWorkbook(&ctxt, wb);
    g_hash_table_foreach(ctxt.nameTable, nameFree, NULL);
    g_hash_table_destroy(ctxt.nameTable);

    /*
     * Dump it.
     */
    xmlDocDump(output, xml); /* !!! Should add a return code and check */
    xmlFreeDoc(xml);
    fclose(output);
    return(0);
}

/**
 **
 ** Private functions : mapping between in-memory structure and XML tree
 **
 **/ 

/*
 * Free a name when cleaning up the name Hash table.
 */
static void nameFree(gpointer key, gpointer value, gpointer user_data)
{
    /* g_free(value); */
}

/*
 * Hash functions for pointers.
 */
static guint ptrHash(gconstpointer a)
{
    guint res = (guint) a;
    res >>= 2;
    res &= 0xFFFF;
    return(res);
}

/*
 * Comparison functions for pointers.
 */
static gint ptrCompare(gconstpointer a, gconstpointer b)
{
    char *ca, *cb;

    ca = (char *) a;
    cb = (char *) b;

    if (ca != cb)
	    return 0;
    
    return 1;
}

/*
 * Create an XML subtree of doc equivalent to the given StyleBorder.
 */
static char *BorderTypes[8] = { "none", "solid", "unknown", "unknown",
				"unknown", "unknown", "unknown", "unknown"};
static xmlNodePtr writeXmlStyleBorder(parseXmlContextPtr ctxt,
                                      StyleBorder *border) {
    xmlNodePtr cur;
    xmlNodePtr side;
    
    if ((border->left == BORDER_NONE) && (border->right == BORDER_NONE) &&
        (border->left == BORDER_NONE) && (border->right == BORDER_NONE))
	return(NULL);
    cur = xmlNewNode(ctxt->ns, "StyleBorder", NULL);
    if (border->left != BORDER_NONE) {
        side = xmlNewChild(cur, ctxt->ns,"Left", BorderTypes[border->left]);
	xmlSetColorValue(side, "Color", &border->left_color);
    }
    if (border->right != BORDER_NONE) {
        side = xmlNewChild(cur, ctxt->ns,"Right", BorderTypes[border->right]);
	xmlSetColorValue(side, "Color", &border->right_color);
    }
    if (border->top != BORDER_NONE) {
        side = xmlNewChild(cur, ctxt->ns,"Top", BorderTypes[border->top]);
	xmlSetColorValue(side, "Color", &border->top_color);
    }
    if (border->bottom != BORDER_NONE) {
        side = xmlNewChild(cur, ctxt->ns,"Bottom", BorderTypes[border->bottom]);
	xmlSetColorValue(side, "Color", &border->bottom_color);
    }
    return(cur);
}

/*
 * Create a StyleBorder equivalent to the XML subtree of doc.
 */
static StyleBorder *readXmlStyleBorder(parseXmlContextPtr ctxt,
                                       xmlNodePtr tree) {
    StyleBorder *ret;
    StyleBorderType left = BORDER_NONE;
    StyleBorderType right = BORDER_NONE;
    StyleBorderType top = BORDER_NONE;
    StyleBorderType bottom = BORDER_NONE;
    GdkColor *left_color = NULL;
    GdkColor *right_color = NULL;
    GdkColor *top_color = NULL;
    GdkColor *bottom_color = NULL;
    xmlNodePtr side;

    if (strcmp(tree->name, "StyleBorder")) {
        fprintf(stderr,
    "readXmlStyleBorder: invalid element type %s, 'StyleBorder' expected`\n",
		tree->name);
    }
    if ((side = xmlSearchChild(tree, "Left")) != NULL) {
        left = BORDER_SOLID;
	xmlGetColorValue(side, "Color", &left_color);
    }
    if ((side = xmlSearchChild(tree, "Right")) != NULL) {
        right = BORDER_SOLID;
	xmlGetColorValue(side, "Color", &right_color);
    }
    if ((side = xmlSearchChild(tree, "Top")) != NULL) {
        top = BORDER_SOLID;
	xmlGetColorValue(side, "Color", &top_color);
    }
    if ((side = xmlSearchChild(tree, "Bottom")) != NULL) {
        bottom = BORDER_SOLID;
	xmlGetColorValue(side, "Color", &bottom_color);
    }

    ret = style_border_new(left, right, top, bottom,
                           left_color, right_color, top_color,bottom_color);
    return(NULL);
}

/*
 * Create an XML subtree of doc equivalent to the given Style.
 */
static xmlNodePtr writeXmlStyle(parseXmlContextPtr ctxt, Style *style) {
    xmlNodePtr cur, child;
    char str[50];
    char *name;
    
    if ((style->halign == 0) && (style->valign == 0) &&
        (style->orientation ==0) && (style->format == NULL) &&
	(style->font == NULL) && (style->border == NULL) &&
        (style->shading == NULL)) return(NULL);

    cur = xmlNewNode(ctxt->ns, "Style", NULL);
    xmlSetIntValue(cur, "HAlign", style->halign);
    xmlSetIntValue(cur, "VAlign", style->valign);
    xmlSetIntValue(cur, "Orient", style->orientation);

    if (style->format != NULL) {
	xmlSetValue(cur, "Format", style->format->format);
    }
    if (style->font != NULL) {
        if ((name = (char *) 
	     g_hash_table_lookup(ctxt->nameTable, style->font)) != NULL) {
	    child = xmlNewChild(cur, ctxt->ns, "Font", NULL);
	    sprintf(str, "#%s", name);
	    xmlNewProp(child, "HREF", str);
	} else {
	    child = xmlNewChild(cur, ctxt->ns, "Font", style->font->font_name);
	    xmlSetIntValue(child, "Unit", style->font->units);
	    sprintf(str, "FontDef%d", ctxt->fontIdx++);
	    xmlNewProp(child, "NAME", str);
	    g_hash_table_insert(ctxt->nameTable, style->font, g_strdup(str));
	}
	             
    }
    if (style->border != NULL) {
        child = writeXmlStyleBorder(ctxt, style->border);
        if (child) xmlAddChild(cur, child);
    }
    if (style->shading != NULL) {
	sprintf(str, "%d", style->shading->pattern);
        xmlNewChild(cur, ctxt->ns, "Shade", str);
    }
    return(cur);
}

/*
 * Create a Style equivalent to the XML subtree of doc.
 */
static Style *readXmlStyle(parseXmlContextPtr ctxt, xmlNodePtr tree,
                           Style *ret) {
    xmlNodePtr child;
    const char *prop;
    int val;

    if (strcmp(tree->name, "Style")) {
        fprintf(stderr,
	        "readXmlStyle: invalid element type %s, 'Style' expected`\n",
		tree->name);
    }
    if (ret == NULL) {
        ret = style_new();
    }
    if (ret == NULL) return(NULL);

    if (xmlGetIntValue(tree, "HAlign", &val)) ret->halign = val;
    if (xmlGetIntValue(tree, "VAlign", &val)) ret->valign = val;
    if (xmlGetIntValue(tree, "Orient", &val)) ret->orientation = val;

    prop = xmlGetValue(tree, "Format");
    if (prop != NULL) {
	if (ret->format == NULL)
	    ret->format = style_format_new((char *) prop);
    }

    child = tree->childs;
    while (child != NULL) {
        if (!strcmp(child->name, "Font")) {
	    /* TODO */
	} else if (!strcmp(child->name, "StyleBorder")) {
            StyleBorder *sb;

	    sb = readXmlStyleBorder(ctxt, child);
	} else if (!strcmp(child->name, "Shade")) {
	    /* TODO */
	} else {
	    fprintf(stderr, "readXmlStyle: unknown type '%s'\n",
	            child->name);
	}
        child = child->next;
    }
    
    return(NULL);
}

/*
 * Create an XML subtree of doc equivalent to the given StyleRegion.
 */
static xmlNodePtr writeXmlStyleRegion(parseXmlContextPtr ctxt,
                                      StyleRegion *region) {
    xmlNodePtr cur, child;
    
    cur = xmlNewNode(ctxt->ns, "StyleRegion", NULL);
    xmlSetIntValue(cur, "startCol", region->range.start_col);
    xmlSetIntValue(cur, "endCol", region->range.end_col);
    xmlSetIntValue(cur, "startRow", region->range.start_row);
    xmlSetIntValue(cur, "endRow", region->range.end_row);

    if (region->style != NULL) {
        child = writeXmlStyle(ctxt, region->style);
    }
    return(cur);
}

/*
 * Create a StyleRegion equivalent to the XML subtree of doc.
 */
static void readXmlStyleRegion(parseXmlContextPtr ctxt, xmlNodePtr tree) {
    xmlNodePtr child;
    Style *style = NULL;
    int    start_col = 0, start_row = 0, end_col = 0, end_row = 0;

    if (strcmp(tree->name, "StyleRegion")) {
        fprintf(stderr,
    "readXmlStyleRegion: invalid element type %s, 'StyleRegion' expected`\n",
		tree->name);
	return;
    }
    xmlGetIntValue(tree, "startCol", &start_col);
    xmlGetIntValue(tree, "startRow", &start_row);
    xmlGetIntValue(tree, "endCol", &end_col);
    xmlGetIntValue(tree, "endRow", &end_row);
    child = tree->childs;
    if (child != NULL)
        style = readXmlStyle(ctxt, child, NULL);
    if (style != NULL)
	sheet_style_attach(ctxt->sheet, start_col, start_row, end_col,
	                   end_row, style);

}

/*
 * Create an XML subtree of doc equivalent to the given ColRowInfo.
 */
static xmlNodePtr writeXmlColRowInfo(parseXmlContextPtr ctxt,
                                     ColRowInfo *info, int col) {
    xmlNodePtr cur;
    
    if (col)
	cur = xmlNewNode(ctxt->ns, "ColInfo", NULL);
    else
	cur = xmlNewNode(ctxt->ns, "RowInfo", NULL);
        
    xmlSetIntValue(cur, "No", info->pos);
    xmlSetIntValue(cur, "Unit", info->units);
    xmlSetIntValue(cur, "MarginA", info->margin_a);
    xmlSetIntValue(cur, "MarginB", info->margin_b);
    xmlSetIntValue(cur, "HardSize", info->hard_size);

    return(cur);
}

/*
 * Create a ColRowInfo equivalent to the XML subtree of doc.
 */
static ColRowInfo *readXmlColRowInfo(parseXmlContextPtr ctxt,
				 xmlNodePtr tree, ColRowInfo *ret) {
    int col = 0;
    int val;

    if (!strcmp(tree->name, "ColInfo")) {
        col = 1;
    } else if (!strcmp(tree->name, "RowInfo")) {
        col = 0;
    } else {
        fprintf(stderr,
    "readXmlColRowInfo: invalid element type %s, 'ColInfo/RowInfo' expected`\n",
		tree->name);
	return(NULL);
    }
    if (ret == NULL) {
	if (col)
	    ret = sheet_col_new(ctxt->sheet);
	else
	    ret = sheet_row_new(ctxt->sheet);
    }
    if (ret == NULL) return(NULL);

    xmlGetIntValue(tree, "No", &ret->pos);
    xmlGetIntValue(tree, "Unit", &ret->units);
    xmlGetIntValue(tree, "MarginA", &ret->margin_a);
    xmlGetIntValue(tree, "MarginB", &ret->margin_b);
    if (xmlGetIntValue(tree, "HardSize", &val)) ret->hard_size = val;

    return(ret);
}

/*
 * Create an XML subtree of doc equivalent to the given Cell.
 */
static xmlNodePtr writeXmlCell(parseXmlContextPtr ctxt, Cell *cell) {
    xmlNodePtr cur;
    
    cur = xmlNewNode(ctxt->ns, "Cell", cell->entered_text->str);
    xmlSetIntValue(cur, "Col", cell->col->pos);
    xmlSetIntValue(cur, "Row", cell->row->pos);
    return(cur);
}

/*
 * Create a Cell equivalent to the XML subtree of doc.
 */
static Cell *readXmlCell(parseXmlContextPtr ctxt, xmlNodePtr tree) {
    Cell *ret;
    int row = 0, col = 0;

    if (strcmp(tree->name, "Cell")) {
        fprintf(stderr,
	"readXmlCell: invalid element type %s, 'Cell' expected`\n",
		tree->name);
	return(NULL);
    }
    xmlGetIntValue(tree, "Col", &col);
    xmlGetIntValue(tree, "Row", &row);

    ret = sheet_cell_get(ctxt->sheet, row, col);
    if (ret == NULL)
        ret = sheet_cell_new(ctxt->sheet, row, col);
    if (ret == NULL) return(NULL);

    if (tree->content != NULL)
        cell_set_text(ret, tree->content);
    
    return(NULL);
}

/*
 * Create an XML subtree equivalent to the given cell and add it to the parent
 */
static void writeXmlCellTo(gpointer key, gpointer value, gpointer data) {
    parseXmlContextPtr ctxt = (parseXmlContextPtr) data;
    xmlNodePtr cur;

    cur = writeXmlCell(ctxt, (Cell *) value);
    xmlAddChild(ctxt->parent, cur);
}

/*
 * Create an XML subtree of doc equivalent to the given Sheet.
 */
static xmlNodePtr writeXmlSheet(parseXmlContextPtr ctxt, Sheet *sheet) {
    xmlNodePtr cur;
    xmlNodePtr child;
    xmlNodePtr rows;
    xmlNodePtr cols;
    xmlNodePtr cells;
    GList *l;
    char str[50];

    /*
     * General informations about the Sheet.
     */
    cur = xmlNewNode(ctxt->ns, "Sheet", NULL);
    if (cur == NULL) return(NULL);
    xmlNewChild(cur, ctxt->ns, "Name", sheet->name);
    sprintf(str, "%d", sheet->max_col_used);
    xmlNewChild(cur, ctxt->ns, "MaxCol", str);
    sprintf(str, "%d", sheet->max_row_used);
    xmlNewChild(cur, ctxt->ns, "MaxRow", str);
    sprintf(str, "%f", sheet->last_zoom_factor_used);
    xmlNewChild(cur, ctxt->ns, "Zoom", str);

    /*
     * Cols informations.
     */
    cols = xmlNewChild(cur, ctxt->ns, "Cols", NULL);
    l = sheet->cols_info;
    while (l) {
        child = writeXmlColRowInfo(ctxt, l->data, 1);
	if (child) xmlAddChild(cols, child);
	l = l->next;
    }

    /*
     * Rows informations.
     */
    rows = xmlNewChild(cur, ctxt->ns, "Rows", NULL);
    l = sheet->cols_info;
    while (l) {
        child = writeXmlColRowInfo(ctxt, l->data, 0);
	if (child) xmlAddChild(rows, child);
	l = l->next;
    }

    /*
     * Style : TODO ...
     */
    /*
     * Cells informations
     */
    cells = xmlNewChild(cur, ctxt->ns, "Cells", NULL);
    ctxt->parent = cells;
    g_hash_table_foreach(sheet->cell_hash, writeXmlCellTo, ctxt);
    return(cur);
}

/*
 * Create a Sheet equivalent to the XML subtree of doc.
 */
static Sheet *readXmlSheet(parseXmlContextPtr ctxt, xmlNodePtr tree) {
    xmlNodePtr child;
    xmlNodePtr rows;
    xmlNodePtr cols;
    xmlNodePtr regions;
    /* xmlNodePtr styles; */
    xmlNodePtr cells;
    Sheet *ret;
    const char *val;

    if (strcmp(tree->name, "Sheet")) {
        fprintf(stderr,
	        "readXmlSheet: invalid element type %s, 'Sheet' expected`\n",
		tree->name);
    }
    child = tree->childs;
    /*
     * Get the name to create the sheet
     */
    val = xmlGetValue(tree, "Name");
    if (val != NULL) {
	ret = sheet_new(ctxt->wb, (char *) val);
    } else {
	fprintf(stderr, "readXmlSheet: Sheet has no name\n");
	ret = sheet_new(ctxt->wb, _("NoName"));
    }

    if (ret == NULL) return(NULL);

    ctxt->sheet = ret;

    xmlGetIntValue(tree, "MaxCol", &ret->max_col_used);
    xmlGetIntValue(tree, "MaxRow", &ret->max_row_used);
    xmlGetDoubleValue(tree, "Zoom", &ret->last_zoom_factor_used);
    child = xmlSearchChild(tree, "Styles");
    if (child != NULL) {
	regions = child->childs;
	while (regions != NULL) {
	    readXmlStyleRegion(ctxt, regions);
	    regions = regions->next;
	}
    }
    child = xmlSearchChild(tree, "Cols");
    if (child != NULL) {
	ColRowInfo *info;

	cols = child->childs;
	while (cols != NULL) {
	    info = readXmlColRowInfo(ctxt, cols, NULL);
	    if (info != NULL)
		sheet_col_add(ret, info);
	    cols = cols->next;
	}
    }
    child = xmlSearchChild(tree, "Rows");
    if (child != NULL) {
	ColRowInfo *info;

	rows = child->childs;
	while (rows != NULL) {
	    info = readXmlColRowInfo(ctxt, rows, NULL);
	    if (info != NULL)
		sheet_row_add(ret, info);
	    rows = rows->next;
	}
    }
    child = xmlSearchChild(tree, "Cells");
    if (child != NULL) {
	cells = child->childs;
	while (cells != NULL) {
	    readXmlCell(ctxt, cells);
	    cells = cells->next;
	}
    }
    return(ret);
}

/*
 * Create an XML subtree equivalent to the given sheet
 * and add it to the parent
 */
static void writeXmlSheetTo(gpointer key, gpointer value, gpointer data) {
    parseXmlContextPtr ctxt = (parseXmlContextPtr) data;
    xmlNodePtr cur, parent;
    Sheet *sheet = (Sheet *) value;

    parent = ctxt->parent;
    cur = writeXmlSheet(ctxt, sheet);
    ctxt->parent = parent;
    xmlAddChild(parent, cur);
}

/*
 * Create an XML subtree of doc equivalent to the given Workbook.
 */
static xmlNodePtr writeXmlWorkbook(parseXmlContextPtr ctxt, Workbook *wb) {
    xmlNodePtr cur;
    xmlNodePtr child;

    /*
     * General informations about the Sheet.
     */
    cur = xmlNewNode(ctxt->ns, "Workbook", NULL); /* the Workbook name !!! */
    if (cur == NULL) return(NULL);

    child = writeXmlStyle(ctxt, &wb->style);
    if (child) xmlAddChild(cur, child);

    /*
     * Cells informations
     */
    child = xmlNewChild(cur, ctxt->ns, "Sheets", NULL);
    ctxt->parent = child;
    g_hash_table_foreach(wb->sheets, writeXmlSheetTo, ctxt);
    return(cur);
}

/*
 * Create a Workbook equivalent to the XML subtree of doc.
 */
static Workbook *readXmlWorkbook(parseXmlContextPtr ctxt, xmlNodePtr tree) {
    Workbook *ret;
    Sheet *sheet;
    xmlNodePtr child;

    if (strcmp(tree->name, "Workbook")) {
        fprintf(stderr,
	    "readXmlWorkbook: invalid element type %s, 'Workbook' expected`\n",
		tree->name);
	return(NULL);
    }
    ret = workbook_new ();
    ctxt->wb = ret;

    child = xmlSearchChild(tree, "Style");
    if (child != NULL)
        readXmlStyle(ctxt, child, &ret->style);

    child = xmlSearchChild(tree, "Sheets");
    if (child == NULL) return(ret);
    child = child->childs;
    while (child != NULL) {
	sheet = readXmlSheet(ctxt, child);
	if (sheet != NULL)
	    workbook_attach_sheet(ret, sheet);
        child = child->next;
    }
    return(ret);
}

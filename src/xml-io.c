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
#include "tree.h"
#include "parser.h"

#include "xml-io.h"

/*
 * A parsing context.
 */
typedef struct parseXmlContext {
    xmlDocPtr  doc;        /* Xml document */
    xmlNsPtr  ns;         /* Main name space */
    xmlNodePtr parent;     /* used only for g_hash_table_foreach callbacks */
    GHashTable *nameTable; /* to reproduce multiple refs with HREFs */
    int        fontIdx;    /* for Font refs names ... */
} parseXmlContext, *parseXmlContextPtr;

static Sheet      *readXmlSheet     (parseXmlContextPtr ctxt, xmlNodePtr tree);
static xmlNodePtr  writeXmlSheet    (parseXmlContextPtr ctxt, Sheet *sheet);
static Workbook   *readXmlWorkbook  (parseXmlContextPtr ctxt, xmlNodePtr tree);
static xmlNodePtr  writeXmlWorkbook (parseXmlContextPtr ctxt, Workbook *wb);
static guint       ptrHash          (gconstpointer a);
static gint        ptrCompare       (gconstpointer a, gconstpointer b);
static void        nameFree         (gpointer key, gpointer value, gpointer user_data);


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
     * Do a bit of checking, get the namespaces, and chech the top elem.
     */
    gmr = xmlSearchNs(res, res->root, "http://www.gnome.org/gnumeric/");
    if (strcmp(res->root->name, "Sheet") || (gmr == NULL)) {
        fprintf(stderr, "gnumericReadXmlSheet %s: not an Sheet file\n",
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
    Workbook *sheet;
    xmlDocPtr res;
    xmlNsPtr gmr;
    parseXmlContext ctxt;

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
    gmr = xmlSearchNs(res, res->root, "http://www.gnome.org/gnumeric/");
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
    sheet = readXmlWorkbook(&ctxt, res->root);
    g_hash_table_foreach(ctxt.nameTable, nameFree, NULL);
    g_hash_table_destroy(ctxt.nameTable);

    xmlFreeDoc(res);
    return(sheet);
}

/*
 * Save a Workbook in an XML file
 * One build an in-memory XML tree and save it to a file.
 * returns 0 in case of success, -1 otherwise.
 */

int gnumericWriteXmlWorkbook(Workbook *sheet, const char *filename) {
    FILE *output;
    xmlDocPtr xml;
    xmlNsPtr gmr;
    parseXmlContext ctxt;

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
    xml->root = writeXmlWorkbook(&ctxt, sheet);
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
    char str[50];
    
    if ((border->left == BORDER_NONE) && (border->right == BORDER_NONE) &&
        (border->left == BORDER_NONE) && (border->right == BORDER_NONE))
	return(NULL);
    cur = xmlNewNode(ctxt->ns, "StyleBorder", NULL);
    if (border->left != BORDER_NONE) {
        side = xmlNewChild(cur, ctxt->ns, "Left", BorderTypes[border->left]);
	sprintf(str, "%X:%X:%X", border->left_color.red, 
	        border->left_color.green, border->left_color.blue);
	xmlNewProp(side, "Color", str);
    }
    if (border->right != BORDER_NONE) {
        side = xmlNewChild(cur, ctxt->ns, "Right", BorderTypes[border->right]);
	sprintf(str, "%X:%X:%X", border->right_color.red, 
	        border->right_color.green, border->right_color.blue);
	xmlNewProp(side, "Color", str);
    }
    if (border->top != BORDER_NONE) {
        side = xmlNewChild(cur, ctxt->ns, "Top", BorderTypes[border->top]);
	sprintf(str, "%X:%X:%X", border->top_color.red, 
	        border->top_color.green, border->top_color.blue);
	xmlNewProp(side, "Color", str);
    }
    if (border->bottom != BORDER_NONE) {
        side = xmlNewChild(cur, ctxt->ns, "Bottom", BorderTypes[border->bottom]);
	sprintf(str, "%X:%X:%X", border->bottom_color.red, 
	        border->bottom_color.green, border->bottom_color.blue);
	xmlNewProp(side, "Color", str);
    }
    return(cur);
}

/*
 * Create a StyleBorder equivalent to the XML subtree of doc.
 */
static StyleBorder *readXmlStyleBorder(parseXmlContextPtr ctxt,
                                       xmlNodePtr tree) {
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
    sprintf(str, "%d", style->halign);
    xmlNewProp(cur, "HAlign", str);
    sprintf(str, "%d", style->valign);
    xmlNewProp(cur, "VAlign", str);
    sprintf(str, "%d", style->orientation);
    xmlNewProp(cur, "Orient", str);

    if (style->format != NULL) {
	xmlNewProp(cur, "Format", style->format->format);
    }
    if (style->font != NULL) {
        if ((name = (char *) 
	     g_hash_table_lookup(ctxt->nameTable, style->font)) != NULL) {
	    child = xmlNewChild(cur, ctxt->ns, "Font", NULL);
	    sprintf(str, "#%s", name);
	    xmlNewProp(child, "HREF", str);
	} else {
	    child = xmlNewChild(cur, ctxt->ns, "Font", style->font->font_name);
	    sprintf(str, "%d", style->font->units);
	    xmlNewProp(child, "Unit", str);
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
static Style *readXmlStyle(parseXmlContextPtr ctxt, xmlNodePtr tree) {
    return(NULL);
}

/*
 * Create an XML subtree of doc equivalent to the given ColRowInfo.
 */
static xmlNodePtr writeXmlColRowInfo(parseXmlContextPtr ctxt,
                                     ColRowInfo *info) {
    xmlNodePtr cur, child;
    char str[50];
    
    cur = xmlNewNode(ctxt->ns, "ColRowInfo", NULL);
    sprintf(str, "%d", info->pos);
    xmlNewProp(cur, "No", str);
    sprintf(str, "%d", info->units);
    xmlNewProp(cur, "Unit", str);
    sprintf(str, "%d", info->margin_a);
    xmlNewProp(cur, "MarginA", str);
    sprintf(str, "%d", info->margin_b);
    xmlNewProp(cur, "MarginB", str);
    if (info->style) {
	child = writeXmlStyle(ctxt, info->style);
	if (child) xmlAddChild(cur, child);
    }

    return(cur);
}

/*
 * Create a ColRowInfo equivalent to the XML subtree of doc.
 */
static ColRowInfo *readXmlColRowInfo(parseXmlContextPtr ctxt,
                                     xmlNodePtr tree) {
    return(NULL);
}

/*
 * Create an XML subtree of doc equivalent to the given Cell.
 */
static xmlNodePtr writeXmlCell(parseXmlContextPtr ctxt, Cell *cell) {
    xmlNodePtr cur;
    char str[50];
    
    cur = xmlNewNode(ctxt->ns, "Cell", cell->entered_text->str);
    sprintf(str, "%d", cell->col->pos);
    xmlNewProp(cur, "Col", str);
    sprintf(str, "%d", cell->row->pos);
    xmlNewProp(cur, "Row", str);
    return(cur);
}

/*
 * Create a Cell equivalent to the XML subtree of doc.
 */
static Cell *readXmlCell(parseXmlContextPtr ctxt, xmlNodePtr tree) {
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
    child = writeXmlStyle(ctxt, &sheet->style);
    if (child) xmlAddChild(cur, child);

    /*
     * Cols informations.
     */
    cols = xmlNewChild(cur, ctxt->ns, "Cols", NULL);
    child = writeXmlStyle(ctxt, &sheet->style);
    if (child) xmlAddChild(cols, child);
    l = sheet->cols_info;
    while (l) {
        child = writeXmlColRowInfo(ctxt, l->data);
	if (child) xmlAddChild(cols, child);
	l = l->next;
    }

    /*
     * Rows informations.
     */
    rows = xmlNewChild(cur, ctxt->ns, "Rows", NULL);
    child = writeXmlStyle(ctxt, &sheet->style);
    if (child) xmlAddChild(rows, child);
    l = sheet->cols_info;
    while (l) {
        child = writeXmlColRowInfo(ctxt, l->data);
	if (child) xmlAddChild(rows, child);
	l = l->next;
    }

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
    return(NULL);
}

/*
 * Create an XML subtree equivalent to the given cell and add it to the parent
 */
static void writeXmlSheetTo(gpointer key, gpointer value, gpointer data) {
    parseXmlContextPtr ctxt = (parseXmlContextPtr) data;
    xmlNodePtr cur;
    Sheet *sheet = (Sheet *) value;
    char *filename;
    char *p;

    cur = xmlNewNode(ctxt->ns, "Sheet", sheet->name);
    xmlAddChild(ctxt->parent, cur);

    filename = g_strdup(sheet->name);
    if (filename == NULL) {
        fprintf(stderr, "Out of memory, couln't save %s\n", sheet->name);
	return;
    }
    for (p = filename;*p;p++)
        if ((*p == ' ') || (*p == '\t') || (*p == '\n') || (*p == '\r'))
	    *p = '_';
    gnumericWriteXmlSheet(sheet, filename);
    g_free(filename);
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
    return(NULL);
}

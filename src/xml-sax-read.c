/* vim: set sw=8:
 * $Id$
 */

/*
 * xml2.c : a test harness for the sax based xml parse routines.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include "config.h"
#include "gnumeric.h"
#include "plugin.h"
#include "plugin-util.h"
#include "workbook.h"

#include <gnome-xml/tree.h>
#include <gnome-xml/parser.h>
#include <gnome-xml/parserInternals.h>

typedef struct _XML2ParseState XML2ParseState;

/*****************************************************************************/

static void
xml2UnknownAttr (XML2ParseState *state, CHAR const * const *attrs, char const *name)
{
	g_warning("Unexpected attribute '%s'='%s' for element of type %s.", name, attrs[0], attrs[1]);
}

static int
xmlParseAttrDouble (CHAR const * const *attrs, char const *name, double * res)
{
	char *end;
	double tmp;
	
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = g_strtod (attrs[1], &end);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected double, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}
static gboolean
xmlParseNDouble (CHAR const *chars, int len, double *res)
{
	char *end, *tmp = g_strndup (chars, len);
	*res = g_strtod (tmp, &end);
	g_free (tmp);
	return end == (tmp+len);
}

static int
xmlParseAttrInt (CHAR const * const *attrs, char const *name, int *res)
{
	char *end;
	int tmp;
	
	g_return_val_if_fail (attrs != NULL, FALSE);
	g_return_val_if_fail (attrs[0] != NULL, FALSE);
	g_return_val_if_fail (attrs[1] != NULL, FALSE);

	if (strcmp (attrs[0], name))
		return FALSE;

	tmp = strtol (attrs[1], &end, 10);
	if (*end) {
		g_warning ("Invalid attribute '%s', expected integer, received '%s'",
			   name, attrs[1]);
		return FALSE;
	}
	*res = tmp;
	return TRUE;
}
#if 0
static gboolean
xmlParseNInt (CHAR const *chars, int len, int *res)
{
	char *end, *tmp = g_strndup (chars, len);
	*res = strtol (tmp, &end, 10);
	g_free (tmp);
	return end == (tmp+len);
}
#endif

/*****************************************************************************/

#if 0
/*
 * Save a Workbook in an XML file
 * returns 0 in case of success, -1 otherwise.
 */
static int
xml2_write (CommandContext *context, Workbook *wb, char const *filename)
{
	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	return 0;
}
#endif

/*****************************************************************************/

typedef enum {
STATE_START,

STATE_WB,
	STATE_WB_ATTRIBUTES,
		STATE_WB_ATTRIBUTES_ELEM,
			STATE_WB_ATTRIBUTES_ELEM_NAME,
			STATE_WB_ATTRIBUTES_ELEM_TYPE,
			STATE_WB_ATTRIBUTES_ELEM_VALUE,
	STATE_WB_SUMMARY,
		STATE_WB_SUMMARY_ITEM,
			STATE_WB_SUMMARY_ITEM_NAME,
			STATE_WB_SUMMARY_ITEM_VALUE,
	STATE_WB_GEOMETRY,
	STATE_WB_SHEETS,
		STATE_SHEET,
			STATE_SHEET_NAME,	/* convert to attr */
			STATE_SHEET_MAXCOL,	/* convert to attr */
			STATE_SHEET_MAXROW,	/* convert to attr */
			STATE_SHEET_ZOOM,	/* convert to attr */
			STATE_SHEET_PRINTINFO,
				STATE_PRINT_MARGIN,
				STATE_PRINT_VCENTER,
				STATE_PRINT_HCENTER,
				STATE_PRINT_GRID,
				STATE_PRINT_MONO,
				STATE_PRINT_DRAFTS,
				STATE_PRINT_TITLES,
				STATE_PRINT_ORDER,
				STATE_PRINT_ORIENT,
				STATE_PRINT_HEADER,
				STATE_PRINT_FOOTER,
				STATE_PRINT_PAPER,
			STATE_SHEET_STYLES,
				STATE_STYLE_REGION,
					STATE_STYLE_STYLE,
						STATE_STYLE_FONT,
						STATE_STYLE_BORDER,
							STATE_BORDER_TOP,
							STATE_BORDER_BOTTOM,
							STATE_BORDER_LEFT,
							STATE_BORDER_RIGHT,
							STATE_BORDER_DIAG,
							STATE_BORDER_REV_DIAG,
			STATE_SHEET_COLS,
				STATE_COL,
			STATE_SHEET_ROWS,
				STATE_ROW,
			STATE_SHEET_SELECTIONS,
				STATE_SELECTION,
			STATE_SHEET_CELLS,
				STATE_CELL,
					STATE_CELL_CONTENT,
			STATE_SHEET_SOLVER,
	STATE_WB_VIEW,

	STATE_UNKNOWN
} xml2State;

/*
 * This is not complete yet.
 * I just pulled it out of a single saved gnumeric file.
 * However, with a bit of cleanup, we should be able to use it as the basis for a dtd.
 */
static char const * const xml2_state_names[] =
{
"START",
"gmr:Workbook",
	"gmr:Attributes",
		"gmr:Attribute",
			"gmr:name",
			"gmr:type",
			"gmr:value",
	"gmr:Summary",
		"gmr:Item",
			"gmr:name",
			"gmr:val-string",
	"gmr:Geometry",
	"gmr:Sheets",
		"gmr:Sheet",
			"gmr:Name",
			"gmr:MaxCol",
			"gmr:MaxRow",
			"gmr:Zoom",
			"gmr:PrintInformation",
				"gmr:PrintUnit",
				"gmr:vcenter",
				"gmr:hcenter",
				"gmr:grid",
				"gmr:monochrome",
				"gmr:draft",
				"gmr:titles",
				"gmr:order",
				"gmr:orientation",
				"gmr:Footer",
				"gmr:Header",
				"gmr:paper",
			"gmr:Styles",
				"gmr:StyleRegion",
					"gmr:Style",
						"gmr:Font",
						"gmr:StyleBorder",
							"gmr:Top",
							"gmr:Bottom",
							"gmr:Left",
							"gmr:Right",
							"gmr:Diagonal",
							"gmr:Rev-Diagonal",
			"gmr:Cols",
				"gmr:ColInfo",

			"gmr:Rows",
				"gmr:RowInfo",
			"gmr:Selections",
				"gmr:Selection",
			"gmr:Cells",
				"gmr:Cell",
					"gmr:Content",
			"gmr:Solver",
	"gmr:UIData",

	"Unknown",
NULL
};

struct _XML2ParseState
{
	xml2State state;
	gint	  unknown_depth;	/* handle recursive unknown tags */
	GSList	 *state_stack;

	Workbook *wb;		/* the associated workbook */
	int version;

	Sheet *sheet;
	double sheet_zoom;

	GHashTable *style_table;/* old style styles compatibility */
	GHashTable *expr_map;	/*
				 * Emitted expressions with ref count > 1
				 * When writing this is map from expr pointer -> index
				 * when reading this is a map from index -> expr pointer
				 */
};

/****************************************************************************/

static void
xml2_parse_sheet_name (XML2ParseState *state, CHAR const *chars, int len)
{
	char *tmp;
	g_return_if_fail (state->sheet == NULL);

	tmp = g_strndup (chars, len);
	if (tmp) {
		state->sheet = sheet_new (state->wb, tmp);
		g_free (tmp);
		workbook_attach_sheet (state->wb, state->sheet);
	}
}

static void
xml2_parse_sheet_zoom (XML2ParseState *state, CHAR const *chars, int len)
{
	double zoom;

	g_return_if_fail (state->sheet != NULL);

	if (xmlParseNDouble (chars, len, &zoom))
		sheet_set_zoom_factor (state->sheet, zoom);
}

static void
xml2ParseColRow (XML2ParseState *state, CHAR const **attrs, gboolean is_col)
{
	ColRowInfo *cri = NULL;
	double size = -1.;
	int dummy;

	g_return_if_fail (state->sheet != NULL);

	for (; attrs[0] && attrs[1] ; attrs += 2) {
		if (xmlParseAttrInt (attrs, "No", &dummy)) {
			g_return_if_fail (cri == NULL);

			cri = is_col
				? sheet_col_fetch (state->sheet, dummy)
				: sheet_row_fetch (state->sheet, dummy);
		} else {
			g_return_if_fail (cri != NULL);

			if (xmlParseAttrDouble (attrs, "Unit", &size)) ;
			else if (xmlParseAttrInt (attrs, "MarginA", &cri->margin_a)) ;
			else if (xmlParseAttrInt (attrs, "MarginB", &cri->margin_b)) ;
			else if (xmlParseAttrInt (attrs, "HardSize", &dummy))
				cri->hard_size = dummy;
			else if (xmlParseAttrInt (attrs, "Hidden", &dummy))
				cri->visible = !dummy;
			else
				xml2UnknownAttr (state, attrs, "ColRow");
		}
	}

	g_return_if_fail (cri != NULL && size > -1.);

	if (is_col)
		sheet_col_set_size_pts (state->sheet, cri->pos, size, cri->hard_size);
	else
		sheet_row_set_size_pts (state->sheet, cri->pos, size, cri->hard_size);
}

/****************************************************************************/

static gboolean
xml2SwitchState (XML2ParseState *state, CHAR const *name, xml2State const newState)
{
	if (strcmp (name, xml2_state_names[newState]))
		    return FALSE;

	state->state_stack = g_slist_prepend (state->state_stack, GINT_TO_POINTER (state->state));
	state->state = newState;
	return TRUE;
}

static void
xml2UnknownState (XML2ParseState *state, CHAR const *name)
{
	if (state->unknown_depth++)
		return;
	g_warning("Unexpected element '%s' in state %s.", name, xml2_state_names[state->state]);
}

/*
 * We parse and do some limited validation of the XML file, if this
 * passes, then we return TRUE
 */
static gboolean
xml2_probe (const char *filename)
{
	return TRUE;
}

static void
xml2StartDocument(XML2ParseState *state)
{
	state->state = STATE_START;
	state->unknown_depth = 0;
	state->state_stack = NULL;

	state->sheet = NULL;
	state->version = -1;
}

static void
xml2EndDocument(XML2ParseState *state)
{
	g_message ("Ending state == %s", xml2_state_names[state->state]);
	if (state->unknown_depth != 0)
		g_warning ("unknown_depth != 0 (%d)", state->unknown_depth);
}

static void
xml2StartElement(XML2ParseState *state, CHAR const *name, CHAR const **attrs)
{
	switch (state->state) {
	case STATE_START:
		if (xml2SwitchState (state, name, STATE_WB)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB :
		if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES)) {
		} else if (xml2SwitchState (state, name, STATE_WB_SUMMARY)) {
		} else if (xml2SwitchState (state, name, STATE_WB_GEOMETRY)) {
		} else if (xml2SwitchState (state, name, STATE_WB_SHEETS)) {
		} else if (xml2SwitchState (state, name, STATE_WB_VIEW)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_ATTRIBUTES :
		if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES_ELEM)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_ATTRIBUTES_ELEM :
		if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES_ELEM_NAME)) {
		} else if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES_ELEM_TYPE)) {
		} else if (xml2SwitchState (state, name, STATE_WB_ATTRIBUTES_ELEM_VALUE)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_SUMMARY :
		if (xml2SwitchState (state, name, STATE_WB_SUMMARY_ITEM)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_SUMMARY_ITEM :
		if (xml2SwitchState (state, name, STATE_WB_SUMMARY_ITEM_NAME)) {
		} else if (xml2SwitchState (state, name, STATE_WB_SUMMARY_ITEM_VALUE)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_WB_SHEETS :
		if (xml2SwitchState (state, name, STATE_SHEET)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET :
		if (xml2SwitchState (state, name, STATE_SHEET_NAME)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_MAXCOL)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_MAXROW)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_ZOOM)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_PRINTINFO)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_STYLES)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_COLS)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_ROWS)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_SELECTIONS)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_CELLS)) {
		} else if (xml2SwitchState (state, name, STATE_SHEET_SOLVER)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_PRINTINFO :
		if (xml2SwitchState (state, name, STATE_PRINT_MARGIN)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_VCENTER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_HCENTER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_GRID)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_MONO)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_DRAFTS)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_TITLES)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_ORDER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_ORIENT)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_HEADER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_FOOTER)) {
		} else if (xml2SwitchState (state, name, STATE_PRINT_PAPER)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_STYLES :
		if (xml2SwitchState (state, name, STATE_STYLE_REGION)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_STYLE_REGION :
		if (xml2SwitchState (state, name, STATE_STYLE_STYLE)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_STYLE_STYLE :
		if (xml2SwitchState (state, name, STATE_STYLE_FONT)) {
		} else if (xml2SwitchState (state, name, STATE_STYLE_BORDER)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_STYLE_BORDER :
		if (xml2SwitchState (state, name, STATE_BORDER_TOP)) {
		} else if (xml2SwitchState (state, name, STATE_BORDER_BOTTOM)) {
		} else if (xml2SwitchState (state, name, STATE_BORDER_LEFT)) {
		} else if (xml2SwitchState (state, name, STATE_BORDER_RIGHT)) {
		} else if (xml2SwitchState (state, name, STATE_BORDER_DIAG)) {
		} else if (xml2SwitchState (state, name, STATE_BORDER_REV_DIAG)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_COLS :
		if (xml2SwitchState (state, name, STATE_COL))
			xml2ParseColRow (state, attrs, TRUE);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_ROWS :
		if (xml2SwitchState (state, name, STATE_ROW))
			xml2ParseColRow (state, attrs, FALSE);
		else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_SELECTIONS :
		if (xml2SwitchState (state, name, STATE_SELECTION)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_SHEET_CELLS :
		if (xml2SwitchState (state, name, STATE_CELL)) {
		} else
			xml2UnknownState (state, name);
		break;

	case STATE_CELL :
		if (xml2SwitchState (state, name, STATE_CELL_CONTENT)) {
		} else
			xml2UnknownState (state, name);
		break;

	default :
		break;
	};

#if 0
	printf ("start '%s'\n", name);

	if (attrs == NULL)
		return;
	while (*attrs)
		puts (*(attrs++));
#endif
}

static void
xml2EndElement(XML2ParseState *state, const CHAR *name)
{
	g_return_if_fail (state->state_stack != NULL);
	g_return_if_fail (!strcmp (name, xml2_state_names[state->state]));

	switch (state->state) {
	case STATE_SHEET :
		state->sheet = NULL;
		break;

	default :
		break;
	};

	/* pop the state stack */
	state->state = GPOINTER_TO_INT (state->state_stack->data);
	state->state_stack = g_slist_remove (state->state_stack, state->state_stack->data);
}

static void
xml2Characters(XML2ParseState *state, const CHAR *chars, int len)
{
	switch (state->state) {
	case STATE_WB_ATTRIBUTES_ELEM_NAME :
	case STATE_WB_ATTRIBUTES_ELEM_TYPE :
	case STATE_WB_ATTRIBUTES_ELEM_VALUE :

	case STATE_WB_SUMMARY_ITEM_NAME :
	case STATE_WB_SUMMARY_ITEM_VALUE :
		break;

	case STATE_SHEET_NAME :
		xml2_parse_sheet_name (state, chars, len);
		break;

	case STATE_SHEET_MAXCOL :
	case STATE_SHEET_MAXROW :
		/* Ignore these */
		break;

	case STATE_SHEET_ZOOM :
		xml2_parse_sheet_zoom (state, chars, len);
		break;

	case STATE_PRINT_MARGIN :
	case STATE_PRINT_ORDER :
	case STATE_PRINT_ORIENT :
	case STATE_PRINT_PAPER :

	case STATE_STYLE_FONT :
	case STATE_CELL_CONTENT :
#if 0
		printf("char = '");
		for (i = 0; i < len; i++)
			putchar (chars[i]);
		printf("'\n");
#endif
		break;

	default :
		break;
	};
}

static xmlEntityPtr
xml2GetEntity(XML2ParseState *state, const CHAR *name)
{
	return xmlGetPredefinedEntity(name);
}

static void
xml2Warning(XML2ParseState *state, const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	g_logv("XML", G_LOG_LEVEL_WARNING, msg, args);
	va_end(args);
}

static void
xml2Error(XML2ParseState *state, const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	g_logv("XML", G_LOG_LEVEL_CRITICAL, msg, args);
	va_end(args);
}

static void
xml2FatalError(XML2ParseState *state, const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	g_logv("XML", G_LOG_LEVEL_ERROR, msg, args);
	va_end(args);
}

static xmlSAXHandler xml2SAXParser = {
	0, /* internalSubset */
	0, /* isStandalone */
	0, /* hasInternalSubset */
	0, /* hasExternalSubset */
	0, /* resolveEntity */
	(getEntitySAXFunc)xml2GetEntity, /* getEntity */
	0, /* entityDecl */
	0, /* notationDecl */
	0, /* attributeDecl */
	0, /* elementDecl */
	0, /* unparsedEntityDecl */
	0, /* setDocumentLocator */
	(startDocumentSAXFunc)xml2StartDocument, /* startDocument */
	(endDocumentSAXFunc)xml2EndDocument, /* endDocument */
	(startElementSAXFunc)xml2StartElement, /* startElement */
	(endElementSAXFunc)xml2EndElement, /* endElement */
	0, /* reference */
	(charactersSAXFunc)xml2Characters, /* characters */
	0, /* ignorableWhitespace */
	0, /* processingInstruction */
	0, /* comment */
	(warningSAXFunc)xml2Warning, /* warning */
	(errorSAXFunc)xml2Error, /* error */
	(fatalErrorSAXFunc)xml2FatalError, /* fatalError */
};


static int
xml2_read (CommandContext *context, Workbook *wb, char const *filename)
{
	xmlParserCtxtPtr ctxt;
	XML2ParseState state;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	state.wb = wb;

	/*
	 * TODO : think about pushing the data into the parser
	 * and using vfs.
	 */
	ctxt = xmlCreateFileParserCtxt (filename);
	if (!ctxt)
		return -1;
	ctxt->sax = &xml2SAXParser;
	ctxt->userData = &state;

	xmlParseDocument(ctxt);

	if (!ctxt->wellFormed)
		g_warning("document not well formed!");
	ctxt->sax = NULL;
	xmlFreeParserCtxt(ctxt);

	return 0;
}

static int
xml2_open (CommandContext *context, Workbook *wb, char const *filename)
{
	int res = xml2_read (context, wb, filename);

	if (res == 0)
		workbook_set_saveinfo (wb, filename, FILE_FL_MANUAL, NULL);

	return res;
}

static int
xml2_can_unload (PluginData *pd)
{
	return TRUE;
}

static void
xml2_cleanup_plugin (PluginData *pd)
{
	file_format_unregister_open (&xml2_probe, &xml2_open);
#if 0
	file_format_unregister_save (&xml2_write);
#endif
}

PluginInitResult
init_plugin (CommandContext *context, PluginData *pd)
{

	if (plugin_version_mismatch  (context, pd, GNUMERIC_VERSION))
		return PLUGIN_QUIET_ERROR;

	if (plugin_data_init (pd, &xml2_can_unload, &xml2_cleanup_plugin,
			      _("EXPERIMENTAL XML"),
			      _("The next generation sax based xml I/O subsystem"))) {

		/* low priority for now */
		file_format_register_open (1, 
					   _("Gnumeric (*.gnumeric) XML based file format"),
					   NULL, &xml2_open);

#if 0
		file_format_register_save (".gnumeric", 
					   _("Gnumeric (*.gnumeric) XML based file format"),
					   FILE_FL_MANUAL, &xml2_write);
#endif
		return PLUGIN_OK;
	}

	return PLUGIN_ERROR;
}

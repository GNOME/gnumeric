/* vim: set sw=8: */

/*
 * excel-xml-read.c : Read MS Excel's xml
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
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
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "xml-io-version.h"
#include "io-context.h"
#include "plugin.h"
#include "plugin-util.h"
#include "module-plugin-defs.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-merge.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "style-border.h"
#include "style-color.h"
#include "format.h"
#include "cell.h"
#include "position.h"
#include "expr.h"
#include "expr-name.h"
#include "print-info.h"
#include "validation.h"
#include "value.h"
#include "selection.h"
#include "command-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "error-info.h"

#include <gsf/gsf-libxml.h>
#include <gsf/gsf-input.h>
#include <stdlib.h>
#include <string.h>

gboolean excel_xml_file_probe (GnmFileOpener const *fo, GsfInput *input,
			       FileProbeLevel pl);
void     excel_xml_file_open (GnmFileOpener const *fo, IOContext *io_context,
			      WorkbookView *wb_view, GsfInput *input);

/*****************************************************************************/

typedef struct {
	GsfXMLIn base;

	IOContext 	*context;	/* The IOcontext managing things */
	WorkbookView	*wb_view;	/* View for the new workbook */
	Workbook	*wb;		/* The new workbook */
	Sheet		*sheet;		/* The current sheet */
} ExcelXMLReadState;

/****************************************************************************/

static void
unknown_attr (ExcelXMLReadState *state,
	      xmlChar const * const *attrs, char const *name)
{
	g_return_if_fail (attrs != NULL);

#if 0
	if (state->version == GNUM_XML_LATEST)
		gnm_io_warning (state->context,
			_("Unexpected attribute %s::%s == '%s'."),
			name, attrs[0], attrs[1]);
#endif
}

static void
xl_xml_wb (GsfXMLIn *xin, xmlChar const **attrs)
{
	ExcelXMLReadState *state = (ExcelXMLReadState *)xin;

#if 0
	for (; attrs != NULL && attrs[0] && attrs[1] ; attrs += 2)
		if (gsf_xml_in_namecmp (xin, attrs[0], "schemaLocation")) {
		} else
			unknown_attr (state, attrs, "Workbook");
#endif
}

/****************************************************************************/

enum {
	XL_NS_SS,
	XL_NS_O,
	XL_NS_XL,
	XL_NS_HTML
};

static GsfXMLInNS content_ns[] = {
	GSF_XML_IN_NS (XL_NS_SS,   "urn:schemas-microsoft-com:office:spreadsheet"),
	GSF_XML_IN_NS (XL_NS_O,    "urn:schemas-microsoft-com:office:office"),
	GSF_XML_IN_NS (XL_NS_XL,   "urn:schemas-microsoft-com:office:excel"),
	GSF_XML_IN_NS (XL_NS_HTML, "http://www.w3.org/TR/REC-html40"),
	{ NULL }
};

static GsfXMLInNode excel_xml_dtd[] = {
GSF_XML_IN_NODE_FULL (START, START, -1, NULL, FALSE, FALSE, TRUE, NULL, NULL, 0),
GSF_XML_IN_NODE_FULL (START, WORKBOOK, XL_NS_SS, "Workbook", FALSE, FALSE, TRUE, NULL, NULL, 0),
  GSF_XML_IN_NODE (WORKBOOK, DOC_PROP, XL_NS_O, "DocumentProperties", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_AUTHOR,	 XL_NS_O, "Author",     TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_LAST_AUTHOR, XL_NS_O, "LastAuthor", TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_CREATED,	 XL_NS_O, "Created",    TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_LAST_SAVED,	 XL_NS_O, "LastSaved",  TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_COMPANY,	 XL_NS_O, "Company",    TRUE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_PROP, PROP_VERSION,	 XL_NS_O, "Version",    TRUE, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, DOC_SETTINGS, XL_NS_O, "OfficeDocumentSettings", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_SETTINGS, DOC_COMPONENTS, XL_NS_O, "DownloadComponents", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (DOC_SETTINGS, DOC_COMPONENTS_LOCATION, XL_NS_O, "LocationOfComponents", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, WB_VIEW, XL_NS_XL, "ExcelWorkbook", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_HEIGHT, XL_NS_XL, "WindowHeight", TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_WIDTH,  XL_NS_XL, "WindowWidth",  TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_TOP_X,  XL_NS_XL, "WindowTopX",   TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, VIEW_TOP_Y,  XL_NS_XL, "WindowTopY",   TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, PROTECT_STRUCTURE, XL_NS_XL, "ProtectStructure",   TRUE, NULL, NULL),
    GSF_XML_IN_NODE (WB_VIEW, PROTECT_WINDOWS,   XL_NS_XL, "ProtectWindows",     TRUE, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, STYLES, XL_NS_SS, "Styles", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (STYLES, STYLE, XL_NS_SS,  "Style", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE, ALIGNMENT,  XL_NS_SS, "Alignment", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE, BORDERS,    XL_NS_SS, "Borders",   FALSE, NULL, NULL),
        GSF_XML_IN_NODE (BORDERS, BORDER, XL_NS_SS, "Border",    FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE, FONT,       XL_NS_SS, "Font",      FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE, INTERIOR,   XL_NS_SS, "Interior",  FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE, NUM_FMT,    XL_NS_SS, "NumberFormat", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (STYLE, PROTECTION, XL_NS_SS, "Protection", FALSE, NULL, NULL),
  GSF_XML_IN_NODE (WORKBOOK, NAMES, XL_NS_SS, "Names", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (NAMES, NAMED_RANGE, XL_NS_SS, "NamedRange", FALSE, NULL, NULL),

  GSF_XML_IN_NODE_FULL (WORKBOOK, WORKSHEET, XL_NS_SS, "Worksheet", FALSE, FALSE, TRUE, NULL, NULL, 0),
    GSF_XML_IN_NODE (WORKSHEET, TABLE, XL_NS_SS, "Table", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (TABLE, COLUMN, XL_NS_SS, "Column", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (TABLE, ROW, XL_NS_SS, "Row", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (ROW, CELL, XL_NS_SS, "Cell", FALSE, NULL, NULL),
          GSF_XML_IN_NODE (CELL, DATA, XL_NS_SS, "Data", TRUE, NULL, NULL),
          GSF_XML_IN_NODE (CELL, NAMED_CELL, XL_NS_SS, "NamedCell", FALSE, NULL, NULL),
    GSF_XML_IN_NODE (WORKSHEET, OPTIONS, XL_NS_XL, "WorksheetOptions", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, SELECTED, XL_NS_XL, "Selected", FALSE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PANES, XL_NS_XL, "Panes", FALSE, NULL, NULL),
        GSF_XML_IN_NODE (PANES, PANE, XL_NS_XL,  "Pane", FALSE, NULL, NULL),
          GSF_XML_IN_NODE (PANE, PANE_NUM, XL_NS_XL,  "Number", TRUE, NULL, NULL),
          GSF_XML_IN_NODE (PANE, PANE_ACTIVEROW, XL_NS_XL,  "ActiveRow", TRUE, NULL, NULL),
          GSF_XML_IN_NODE (PANE, PANE_ACTIVECOL, XL_NS_XL,  "ActiveCol", TRUE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PROT_OBJS,	XL_NS_XL, "ProtectObjects", TRUE, NULL, NULL),
      GSF_XML_IN_NODE (OPTIONS, PROT_SCENARIOS, XL_NS_XL, "ProtectScenarios", TRUE, NULL, NULL),
  { NULL }
};
static GsfXMLInDoc *doc;

void
excel_xml_file_open (GnmFileOpener const *fo, IOContext *io_context,
		     WorkbookView *wb_view, GsfInput *input)
{
	ExcelXMLReadState state;

	/* init */
	state.base.doc = doc;
	state.context = io_context;
	state.wb_view = wb_view;
	state.wb = wb_view_workbook (wb_view);
	state.sheet = NULL;

	if (!gsf_xml_in_parse (&state.base, input))
		gnumeric_io_error_string (io_context, _("XML document not well formed!"));
}

void
excel_xml_read_init (void)
{
	doc = gsf_xml_in_doc_new (excel_xml_dtd, content_ns);
}
void
excel_xml_read_cleanup (void)
{
	gsf_xml_in_doc_free (doc);
}

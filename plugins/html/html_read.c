/*
 * html_read.c
 *
 * Copyright (C) 1999, 2000 Rasca, Berlin
 * EMail: thron@gmx.de
 * Copyright (c) 2001 Andreas J. Guelzow
 * EMail: aguelzow@taliesin.ca
 * Copyright (c) 2002 Jody Goldberg
 * EMail: jody@gnome.org
 *
 * Contributors :
 *   Almer. S. Tigelaar <almer1@dds.nl>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet-style.h"
#include "style.h"
#include "style-color.h"
#include "html.h"
#include "cell.h"
#include "cellspan.h"
#include "sheet.h"
#include "sheet-merge.h"
#include "ranges.h"
#include "value.h"
#include "font.h"
#include "plugin-util.h"
#include "error-info.h"
#include "style-border.h"
#include <rendered-value.h>

#include <gsf/gsf-input.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>
#include <ctype.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>

#define HTML_BOLD	1
#define HTML_ITALIC	2
#define HTML_RIGHT	4
#define HTML_CENTER	8

#if 0
							sheet_style_set_pos (cell->base.sheet,
									     cell->pos.col, cell->pos.row,
									     mstyle);
#endif


static Sheet *
html_get_sheet (char const *name, Workbook *wb) 
{
	Sheet *sheet = NULL;

	if (name) {
		sheet = workbook_sheet_by_name (wb, name);
		if (sheet == NULL) {
			sheet = sheet_new (wb, name);
			workbook_sheet_attach (wb, sheet, NULL);
		}
	} else
		sheet = workbook_sheet_add (wb, NULL, FALSE);
	return sheet;
}

static void
html_read_content (htmlNodePtr cur, xmlBufferPtr buf, MStyle *mstyle, gboolean first, 
		   htmlDocPtr doc)
{
	htmlNodePtr ptr;

	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (ptr->type == XML_TEXT_NODE)
			htmlNodeDump (buf, doc, ptr);
		else if (ptr->type == XML_ELEMENT_NODE) {
			if (first) {
				if (xmlStrEqual (ptr->name, "i") 
				    || xmlStrEqual (ptr->name, "em"))
					mstyle_set_font_italic (mstyle, TRUE);
				if (xmlStrEqual (ptr->name, "b"))
					mstyle_set_font_bold (mstyle, TRUE);
			}
			html_read_content (ptr, buf, mstyle, first && (ptr == cur->children), doc);
		}
	}
}

static void
html_read_row (htmlNodePtr cur, htmlDocPtr doc, Sheet *sheet, int row)
{
	htmlNodePtr ptr;
	int col = -1;
	
	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (xmlStrEqual (ptr->name, "td") || xmlStrEqual (ptr->name, "th")) {
			xmlBufferPtr buf;
			xmlAttrPtr   props;
			int colspan = 1;
			int rowspan = 1;
			CellPos pos;
			MStyle *mstyle;

			/* Check whetehr we need to skip merges from above */
			pos.row = row;
			pos.col = col + 1;
			while (sheet_merge_contains_pos (sheet, &pos)) {
				col++;
				pos.col++;
			}

			/* Do we span across multiple rows or cols? */
			props = ptr->properties;
			while (props) {
				if (xmlStrEqual (props->name, "colspan") && props->children)
				    colspan = atoi (props->children->content);
				if (xmlStrEqual (props->name, "rowspan") && props->children)
				    rowspan = atoi (props->children->content);
				props = props->next;
			}
			if (colspan < 1)
				colspan = 1;
			if (rowspan < 1)
				rowspan = 1;

			/* Let's figure out the content of the cell */
			buf = xmlBufferCreate ();
			mstyle = mstyle_new_default ();
			if (xmlStrEqual (ptr->name, "th"))
				mstyle_set_font_bold (mstyle, TRUE);

			html_read_content (ptr, buf, mstyle, TRUE, doc);

			if (buf->use > 0) {
				char *name;
				Cell *cell;

				name = g_strndup (buf->content, buf->use);
				cell = sheet_cell_fetch	(sheet, col + 1, row);
				sheet_style_set_pos (sheet, col + 1, row, mstyle);
				cell_set_text (cell, name);
				g_free (name);
			}
			xmlBufferFree (buf);

			/* If necessary create the merge */
			if (colspan > 1 || rowspan > 1) {
				Range range;
				Range *r = &range;

				range_init (r, col + 1, row, col + colspan, row + rowspan - 1);
				sheet_merge_add (NULL, sheet, r, FALSE);
			}

			col += colspan;
		}
	}
}

static void
html_read_table (htmlNodePtr cur, htmlDocPtr doc, WorkbookView *wb_view)
{
	Sheet *sheet = NULL;
	Workbook *wb;
	htmlNodePtr ptr, ptr2;
	int row = -1;

	g_return_if_fail (cur != NULL);
	g_return_if_fail (wb_view != NULL);

	wb = wb_view_workbook (wb_view);
	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (ptr->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrEqual (ptr->name, "caption")) {
			xmlBufferPtr buf;
			buf = xmlBufferCreate ();
			for (ptr2 = ptr->children; ptr2 != NULL ; ptr2 = ptr2->next) {
				htmlNodeDump (buf, doc, ptr2);
			}
			if (buf->use > 0) {
				char *name;
				name = g_strndup (buf->content, buf->use);
				sheet = html_get_sheet (name, wb);
				g_free (name);
			}
			xmlBufferFree (buf);
		}
		if (xmlStrEqual (ptr->name, "tr")) {
			row++;
			if (sheet == NULL)
				sheet = html_get_sheet (NULL, wb);
			html_read_row (ptr, doc, sheet, row);
		}
	}
}


static void
html_search_for_tables (htmlNodePtr cur, htmlDocPtr doc, WorkbookView *wb_view)
{
    if (cur == NULL) {
        xmlGenericError(xmlGenericErrorContext,
		"htmlNodeDumpFormatOutput : node == NULL\n");
	return;
    }

    if (cur->type == XML_ELEMENT_NODE) {
	    if (!xmlStrEqual (cur->name, "table")) {
		    htmlNodePtr ptr;
		    for (ptr = cur->children; ptr != NULL ; ptr = ptr->next)
			    html_search_for_tables (ptr, doc, wb_view);
		    return;
	    }
	    html_read_table (cur, doc, wb_view);
    }
}

void
html_file_open (GnumFileOpener const *fo, IOContext *io_context,
		WorkbookView *wb_view, GsfInput *input)
{
	guint8 const *buf;
	int size, len;
	htmlParserCtxtPtr ctxt;
	htmlDocPtr doc = NULL;

	g_return_if_fail (input != NULL);

	size = gsf_input_size (input) - 4;
	buf = gsf_input_read (input, 4, NULL);
	if (buf != NULL) {
		ctxt = htmlCreatePushParserCtxt (NULL, NULL,
			buf, 4, gsf_input_name (input), 0);

		for (; size > 0 ; size -= len) {
			len = 4096;
			if (len > size)
				len =  size;
		       buf = gsf_input_read (input, len, NULL);
		       if (buf == NULL)
			       break;
		       htmlParseChunk (ctxt, buf, len, 0);
		}

		htmlParseChunk (ctxt, buf, 0, 1);
		doc = ctxt->myDoc;
		htmlFreeParserCtxt (ctxt);
	}

	if (doc != NULL) {
		xmlNodePtr ptr;
		for (ptr = doc->children; ptr != NULL ; ptr = ptr->next)
			html_search_for_tables (ptr, doc, wb_view);
		xmlFreeDoc (doc);
	} else
		gnumeric_io_error_info_set (io_context,
			error_info_new_str (_("Unable to parse the html.")));
}

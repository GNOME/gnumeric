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
 *   Almer S. Tigelaar <almer1@dds.nl>
 *   Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include <string.h>
#include "html.h"

#include <sheet-object-cell-comment.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-merge.h>
#include <sheet-style.h>
#include <style.h>
#include <cell.h>
#include <ranges.h>
#include <io-context.h>

#include <gsf/gsf-input.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>

typedef struct {
	Sheet *sheet;
	int   row;
} GnmHtmlTableCtxt;

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
html_append_text (GString *buf, xmlChar *text)
{
	xmlChar *p;
	
	while (*text) {
		while (g_ascii_isspace (*text))
			text++;
		if (*text) {
			for (p = text; *p && !g_ascii_isspace (*p); p++)
				;
			if (buf->len > 0)
				g_string_append_c (buf, ' ');
			g_string_append_len (buf, text, p - text);
			text = p;
		}
	}
}

static void
html_read_content (htmlNodePtr cur, GString *buf, MStyle *mstyle,
		   xmlBufferPtr a_buf, gboolean first, htmlDocPtr doc)
{
	htmlNodePtr ptr;

	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (ptr->type == XML_TEXT_NODE) {
			html_append_text (buf, ptr->content);
 		}
		else if (ptr->type == XML_ELEMENT_NODE) {
			if (first) {
				if (xmlStrEqual (ptr->name, (xmlChar *)"i")
				    || xmlStrEqual (ptr->name, (xmlChar *)"em"))
					mstyle_set_font_italic (mstyle, TRUE);
				if (xmlStrEqual (ptr->name, (xmlChar *)"b"))
					mstyle_set_font_bold (mstyle, TRUE);
			}
			if (xmlStrEqual (ptr->name, (xmlChar *)"a")) {
				xmlAttrPtr   props;
				props = ptr->properties;
				while (props) {
					if (xmlStrEqual (props->name, (xmlChar *)"href") && props->children) {
						htmlNodeDump (a_buf, doc, props->children);
						xmlBufferAdd (a_buf, (xmlChar *)"\n", -1);
					}
					props = props->next;
				}
			}
			if (xmlStrEqual (ptr->name, (xmlChar *)"img")) {
				xmlAttrPtr   props;
				props = ptr->properties;
				while (props) {
					if (xmlStrEqual (props->name, (xmlChar *)"src") && props->children) {
						htmlNodeDump (a_buf, doc, props->children);
						xmlBufferAdd (a_buf, (xmlChar *)"\n", -1);
					}
					props = props->next;
				}
			}
			html_read_content (ptr, buf, mstyle, a_buf, first, doc);
		}
		first = FALSE;
	}
}

static void
html_read_row (htmlNodePtr cur, htmlDocPtr doc, GnmHtmlTableCtxt *tc)
{
	htmlNodePtr ptr;
	int col = -1;

	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (xmlStrEqual (ptr->name, (xmlChar *)"td") || xmlStrEqual (ptr->name, (xmlChar *)"th")) {
			GString *buf;
			xmlBufferPtr a_buf;
			xmlAttrPtr   props;
			int colspan = 1;
			int rowspan = 1;
			CellPos pos;
			MStyle *mstyle;

			/* Check whether we need to skip merges from above */
			pos.row = tc->row;
			pos.col = col + 1;
			while (sheet_merge_contains_pos (tc->sheet, &pos)) {
				col++;
				pos.col++;
			}

			/* Do we span across multiple rows or cols? */
			props = ptr->properties;
			while (props) {
				if (xmlStrEqual (props->name, (xmlChar *)"colspan") && props->children)
				    colspan = atoi ((char *)props->children->content);
				if (xmlStrEqual (props->name, (xmlChar *)"rowspan") && props->children)
				    rowspan = atoi ((char *)props->children->content);
				props = props->next;
			}
			if (colspan < 1)
				colspan = 1;
			if (rowspan < 1)
				rowspan = 1;

			/* Let's figure out the content of the cell */
			buf = g_string_new (NULL);
			a_buf = xmlBufferCreate ();

			mstyle = mstyle_new_default ();
			if (xmlStrEqual (ptr->name, (xmlChar *)"th"))
				mstyle_set_font_bold (mstyle, TRUE);

			html_read_content (ptr, buf, mstyle, a_buf, TRUE, doc);

			if (buf->len > 0) {
				Cell *cell;

				cell = sheet_cell_fetch	(tc->sheet, col + 1, tc->row);
				sheet_style_set_pos (tc->sheet, col + 1, tc->row, mstyle);
				cell_set_text (cell, buf->str);
			}
			if (a_buf->use > 0) {
				char *name;
				
				name = g_strndup ((gchar *)a_buf->content, a_buf->use);
				cell_set_comment (tc->sheet, &pos, NULL, name);
				g_free (name);
			}
			g_string_free (buf, buf->len == 0);
			xmlBufferFree (a_buf);

			/* If necessary create the merge */
			if (colspan > 1 || rowspan > 1) {
				Range range;
				Range *r = &range;

				range_init (r, col + 1, tc->row, col + colspan, tc->row + rowspan - 1);
				sheet_merge_add (tc->sheet, r, FALSE, NULL);
			}

			col += colspan;
		}
	}
}

static void
html_read_rows (htmlNodePtr cur, htmlDocPtr doc, Workbook *wb,
		GnmHtmlTableCtxt *tc)
{
	htmlNodePtr ptr;

	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (ptr->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrEqual (ptr->name, (xmlChar *)"tr")) {
			tc->row++;
			if (tc->sheet == NULL)
				tc->sheet = html_get_sheet (NULL, wb);
			html_read_row (ptr, doc, tc);
		}
	}
}

static void
html_read_table (htmlNodePtr cur, htmlDocPtr doc, WorkbookView *wb_view)
{
	GnmHtmlTableCtxt tc;
	Workbook *wb;
	htmlNodePtr ptr, ptr2;

	g_return_if_fail (cur != NULL);
	g_return_if_fail (wb_view != NULL);

	tc.sheet = NULL;
	tc.row   = -1;
	wb = wb_view_workbook (wb_view);
	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (ptr->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrEqual (ptr->name, (xmlChar *)"caption")) {
			xmlBufferPtr buf;
			buf = xmlBufferCreate ();
			for (ptr2 = ptr->children; ptr2 != NULL ; ptr2 = ptr2->next) {
				htmlNodeDump (buf, doc, ptr2);
			}
			if (buf->use > 0) {
				char *name;
				name = g_strndup ((gchar *)buf->content, buf->use);
				tc.sheet = html_get_sheet (name, wb);
				g_free (name);
			}
			xmlBufferFree (buf);
		} else if (xmlStrEqual (ptr->name, (xmlChar *)"thead") ||
			   xmlStrEqual (ptr->name, (xmlChar *)"tfoot") ||
			   xmlStrEqual (ptr->name, (xmlChar *)"tbody")) {
			html_read_rows (ptr, doc, wb, &tc);
		} else if (xmlStrEqual (ptr->name, (xmlChar *)"tr")) {
			html_read_rows (cur, doc, wb, &tc);
			break;
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
	    htmlNodePtr ptr;
	    if (xmlStrEqual (cur->name, (xmlChar *)"table"))
		    html_read_table (cur, doc, wb_view);
	    for (ptr = cur->children; ptr != NULL ; ptr = ptr->next)
		    html_search_for_tables (ptr, doc, wb_view);
    }
}

void
html_file_open (GnmFileOpener const *fo, IOContext *io_context,
		WorkbookView *wb_view, GsfInput *input)
{
	guint8 const *buf;
	gsf_off_t size;
	int len, bomlen;
	htmlParserCtxtPtr ctxt;
	htmlDocPtr doc = NULL;
	xmlCharEncoding enc;

	g_return_if_fail (input != NULL);

	size = gsf_input_size (input) - 4;
	buf = gsf_input_read (input, 4, NULL);
	if (buf != NULL) {
		enc = xmlDetectCharEncoding(buf, 4);
		switch (enc) {	/* Skip byte order mark */
		case XML_CHAR_ENCODING_UCS4BE:
		case XML_CHAR_ENCODING_UCS4LE:
		case XML_CHAR_ENCODING_UCS4_2143:
		case XML_CHAR_ENCODING_UCS4_3412:
		case XML_CHAR_ENCODING_EBCDIC:
			bomlen = 4;
			break;
		case XML_CHAR_ENCODING_UTF16BE:
		case XML_CHAR_ENCODING_UTF16LE:
			bomlen = 2;
			break;
		case XML_CHAR_ENCODING_UTF8:
			if (buf[0] == 0xef)
				bomlen = 3;
			else if (buf[0] == 0x3c)
				bomlen = 4;
			break;
		default:
			bomlen = 0;
		}
		ctxt = htmlCreatePushParserCtxt (NULL, NULL,
			(char *)(buf + bomlen), 4 - bomlen,
				 gsf_input_name (input), enc);

		for (; size > 0 ; size -= len) {
			len = 4096;
			if (len > size)
				len =  size;
		       buf = gsf_input_read (input, len, NULL);
		       if (buf == NULL)
			       break;
		       htmlParseChunk (ctxt, (char *)buf, len, 0);
		}

		htmlParseChunk (ctxt, (char *)buf, 0, 1);
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

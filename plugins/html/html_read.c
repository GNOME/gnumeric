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

#define HTML_BOLD	1
#define HTML_ITALIC	2
#define HTML_RIGHT	4
#define HTML_CENTER	8

#if 0
				if (p[1] == 'i' || p[1] == 'I')
					*flags |= HTML_ITALIC;
				else if (p[1] == 'b' || p[1] == 'B')
					*flags |= HTML_BOLD;
#endif
static void
html_read_buffer (IOContext *io_context, WorkbookView *wb_view, 
		  guchar const *buf, int buf_size)
{
	Workbook *wb = wb_view_workbook (wb_view);
	Sheet *sheet;
	Cell *cell;
	int num, row, col, flags;
	guchar const *p, *str, *ptr;

	sheet = NULL;
	col = 0;
	row = -1;
	num = 0;
	for (ptr = buf; (ptr - buf) < buf_size ; ) {
quick_hack :
		/* FIXME : This is an ugly hack.  I'll patch it a bit for now
		 * but we should migrate to libxml
		 */
		if (ptr == NULL)
			continue;

		if (NULL != (p = findtag (ptr, "<TABLE"))) {
			sheet = workbook_sheet_add (wb, NULL, FALSE);
			row = -1;
			ptr = strchr (p + 6, '>');
			goto quick_hack;
		} else if (NULL != (p = findtag (ptr, "</TABLE>"))) {
			sheet = NULL;
			ptr = strchr (p + 7, '>');
			goto quick_hack;
		} else if (NULL != (p = findtag (ptr, "<TR"))) {
			row++;
			col = 0;
			ptr = strchr (p + 3, '>');
			goto quick_hack;
		} else if (NULL != (p = findtag (ptr, "<TD"))) {
			/* process table data .. */
			if (sheet) {
				p += 3;
				flags = 0;
				/* find the end of the TD tag and check for attributes */
				while (*p) {
					if (*p == '>') {
						p++;
						break;
					}
					if (*p == ' ' && p[1] != '>') {
						p++;
						if (strncasecmp (p, "align=", 6) == 0) {
							p += 6;
							if (*p == '"')
								p++;
							if (*p == '>') {
								p++;
								break;
							}
							if (strncasecmp (p, "right", 5) == 0) {
								p += 5;
								flags |= HTML_RIGHT;
							} else if (strncasecmp (p, "center", 6) == 0) {
								p += 6;
								flags |= HTML_CENTER;
							}
						}
					} else {
						p++;
					}
				}
				if (row == -1)	/* if we didn't found a TR .. */
					row = 0;
				if (*p) {
					str = html_get_string (p, &flags, &ptr);
					cell = sheet_cell_fetch (sheet, col, row);
					if (str && cell) {
						if (flags) {
							MStyle *mstyle = mstyle_new_default ();
							/*
							 * set the attributes of the cell
							 */
							if (flags & HTML_BOLD)
								mstyle_set_font_bold (mstyle, TRUE);

							if (flags & HTML_ITALIC)
								mstyle_set_font_italic (mstyle, TRUE);

							if (flags & HTML_RIGHT)
								mstyle_set_align_h (mstyle, HALIGN_CENTER);

							sheet_style_set_pos (cell->base.sheet,
									     cell->pos.col, cell->pos.row,
									     mstyle);
						}
						/* set the content of the cell */
						cell_set_text (cell, str);
					}
				}
				col++;
				goto quick_hack;
			}
		}
	}
}

static void
html_search_for_tables (xmlNodePtr cur)
{
#if 0
    const htmlElemDesc * info;

    if (cur == NULL) {
        xmlGenericError(xmlGenericErrorContext,
		"htmlNodeDumpFormatOutput : node == NULL\n");
	return;
    }
    /*
     * Special cases.
     */
    if (cur->type == XML_DTD_NODE)
	return;
    if (cur->type == XML_HTML_DOCUMENT_NODE) {
	htmlDocContentDumpOutput(buf, (xmlDocPtr) cur, encoding);
	return;
    }
    if (cur->type == HTML_TEXT_NODE) {
	if (cur->content != NULL) {
	    if (((cur->name == (const xmlChar *)xmlStringText) ||
		 (cur->name != (const xmlChar *)xmlStringTextNoenc)) &&
		((cur->parent == NULL) ||
		 (!xmlStrEqual(cur->parent->name, BAD_CAST "script")))) {
		xmlChar *buffer;

		buffer = xmlEncodeEntitiesReentrant(doc, cur->content);
		if (buffer != NULL) {
		    xmlOutputBufferWriteString(buf, (const char *)buffer);
		    xmlFree(buffer);
		}
	    } else {
		xmlOutputBufferWriteString(buf, (const char *)cur->content);
	    }
	}
	return;
    }
    /*
     * Get specific HTML info for that node.
     */
    info = htmlTagLookup(cur->name);

    xmlOutputBufferWriteString(buf, "<");
    xmlOutputBufferWriteString(buf, (const char *)cur->name);
    if (cur->properties != NULL)
        htmlAttrListDumpOutput(buf, doc, cur->properties, encoding);

    if ((info != NULL) && (info->empty)) {
        xmlOutputBufferWriteString(buf, ">");
	if ((format) && (!info->isinline) && (cur->next != NULL)) {
	    if ((cur->next->type != HTML_TEXT_NODE) &&
		(cur->next->type != HTML_ENTITY_REF_NODE) &&
		(cur->parent != NULL) &&
		(!xmlStrEqual(cur->parent->name, BAD_CAST "pre")))
		xmlOutputBufferWriteString(buf, "\n");
	}
	return;
    }
    if (((cur->type == XML_ELEMENT_NODE) || (cur->content == NULL)) &&
	(cur->children == NULL)) {
        if ((info != NULL) && (info->saveEndTag != 0) &&
	    (xmlStrcmp(BAD_CAST info->name, BAD_CAST "html")) &&
	    (xmlStrcmp(BAD_CAST info->name, BAD_CAST "body"))) {
	    xmlOutputBufferWriteString(buf, ">");
	} else {
	    xmlOutputBufferWriteString(buf, "></");
	    xmlOutputBufferWriteString(buf, (const char *)cur->name);
	    xmlOutputBufferWriteString(buf, ">");
	}
	if ((format) && (cur->next != NULL) &&
            (info != NULL) && (!info->isinline)) {
	    if ((cur->next->type != HTML_TEXT_NODE) &&
		(cur->next->type != HTML_ENTITY_REF_NODE) &&
		(cur->parent != NULL) &&
		(!xmlStrEqual(cur->parent->name, BAD_CAST "pre")))
		xmlOutputBufferWriteString(buf, "\n");
	}
	return;
    }
    xmlOutputBufferWriteString(buf, ">");
    if ((cur->type != XML_ELEMENT_NODE) &&
	(cur->content != NULL)) {
	    /*
	     * Uses the OutputBuffer property to automatically convert
	     * invalids to charrefs
	     */

            xmlOutputBufferWriteString(buf, (const char *) cur->content);
    }
    if (cur->children != NULL) {
        if ((format) && (info != NULL) && (!info->isinline) &&
	    (cur->children->type != HTML_TEXT_NODE) &&
	    (cur->children->type != HTML_ENTITY_REF_NODE) &&
	    (cur->children != cur->last) &&
	    (!xmlStrEqual(cur->name, BAD_CAST "pre")))
	    xmlOutputBufferWriteString(buf, "\n");
	htmlNodeListDumpOutput(buf, doc, cur->children, encoding, format);
        if ((format) && (info != NULL) && (!info->isinline) &&
	    (cur->last->type != HTML_TEXT_NODE) &&
	    (cur->last->type != HTML_ENTITY_REF_NODE) &&
	    (cur->children != cur->last) &&
	    (!xmlStrEqual(cur->name, BAD_CAST "pre")))
	    xmlOutputBufferWriteString(buf, "\n");
    }
    xmlOutputBufferWriteString(buf, "</");
    xmlOutputBufferWriteString(buf, (const char *)cur->name);
    xmlOutputBufferWriteString(buf, ">");
    if ((format) && (info != NULL) && (!info->isinline) &&
	(cur->next != NULL)) {
        if ((cur->next->type != HTML_TEXT_NODE) &&
	    (cur->next->type != HTML_ENTITY_REF_NODE) &&
	    (cur->parent != NULL) &&
	    (!xmlStrEqual(cur->parent->name, BAD_CAST "pre")))
	    xmlOutputBufferWriteString(buf, "\n");
    }
#endif
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
			html_search_for_tables (ptr);
		xmlFreeDoc (doc);
	} else
		gnumeric_io_error_info_set (io_context,
			error_info_new_str (_("Unable to parse the html.")));
}

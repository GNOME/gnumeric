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
#include <glib/gi18n.h>
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

#define CC2XML(s) ((const xmlChar *)(s))
#define C2XML(s) ((xmlChar *)(s))
#define CXML2C(s) ((const char *)(s))
#define XML2C(s) ((char *)(s))

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
html_append_text (GString *buf, const xmlChar *text)
{
	const xmlChar *p;
	
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
html_read_content (htmlNodePtr cur, GString *buf, GnmStyle *mstyle,
		   xmlBufferPtr a_buf, gboolean first, htmlDocPtr doc)
{
	htmlNodePtr ptr;

	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (ptr->type == XML_TEXT_NODE) {
			html_append_text (buf, ptr->content);
 		}
		else if (ptr->type == XML_ELEMENT_NODE) {
			if (first) {
				if (xmlStrEqual (ptr->name, CC2XML ("i"))
				    || xmlStrEqual (ptr->name, CC2XML ("em")))
					mstyle_set_font_italic (mstyle, TRUE);
				if (xmlStrEqual (ptr->name, CC2XML ("b")))
					mstyle_set_font_bold (mstyle, TRUE);
			}
			if (xmlStrEqual (ptr->name, CC2XML ("a"))) {
				xmlAttrPtr   props;
				props = ptr->properties;
				while (props) {
					if (xmlStrEqual (props->name, CC2XML ("href")) && props->children) {
						htmlNodeDump (a_buf, doc, props->children);
						xmlBufferAdd (a_buf, CC2XML ("\n"), -1);
					}
					props = props->next;
				}
			}
			if (xmlStrEqual (ptr->name, CC2XML ("img"))) {
				xmlAttrPtr   props;
				props = ptr->properties;
				while (props) {
					if (xmlStrEqual (props->name, CC2XML ("src")) && props->children) {
						htmlNodeDump (a_buf, doc, props->children);
						xmlBufferAdd (a_buf, CC2XML ("\n"), -1);
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
		if (xmlStrEqual (ptr->name, CC2XML ("td")) ||
		    xmlStrEqual (ptr->name, CC2XML ("th"))) {
			GString *buf;
			xmlBufferPtr a_buf;
			xmlAttrPtr   props;
			int colspan = 1;
			int rowspan = 1;
			GnmCellPos pos;
			GnmStyle *mstyle;

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
				if (xmlStrEqual (props->name, CC2XML ("colspan")) && props->children)
				    colspan = atoi (CXML2C (props->children->content));
				if (xmlStrEqual (props->name, CC2XML ("rowspan")) && props->children)
				    rowspan = atoi (CXML2C (props->children->content));
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
			if (xmlStrEqual (ptr->name, CC2XML ("th")))
				mstyle_set_font_bold (mstyle, TRUE);

			html_read_content (ptr, buf, mstyle, a_buf, TRUE, doc);

			if (buf->len > 0) {
				GnmCell *cell = sheet_cell_fetch (tc->sheet, col + 1, tc->row);
				sheet_style_set_pos (tc->sheet, col + 1, tc->row, mstyle);
				cell_set_text (cell, buf->str);
			} else
				mstyle_unref (mstyle);

			if (a_buf->use > 0) {
				char *name;
				
				name = g_strndup (CXML2C (a_buf->content), a_buf->use);
				cell_set_comment (tc->sheet, &pos, NULL, name);
				g_free (name);
			}
			g_string_free (buf, TRUE);
			xmlBufferFree (a_buf);

			/* If necessary create the merge */
			if (colspan > 1 || rowspan > 1) {
				GnmRange range;
				GnmRange *r = &range;

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
		if (xmlStrEqual (ptr->name, CC2XML ("tr"))) {
			tc->row++;
			if (tc->sheet == NULL)
				tc->sheet = html_get_sheet (NULL, wb);
			html_read_row (ptr, doc, tc);
		}
	}
}

static void
html_read_table (htmlNodePtr cur, htmlDocPtr doc, WorkbookView *wb_view, 
		 GnmHtmlTableCtxt *tc)
{
	Workbook *wb;
	htmlNodePtr ptr, ptr2;

	g_return_if_fail (cur != NULL);
	g_return_if_fail (wb_view != NULL);

	wb = wb_view_workbook (wb_view);
	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (ptr->type != XML_ELEMENT_NODE)
			continue;
		if (xmlStrEqual (ptr->name, CC2XML ("caption"))) {
			xmlBufferPtr buf;
			buf = xmlBufferCreate ();
			for (ptr2 = ptr->children; ptr2 != NULL ; ptr2 = ptr2->next) {
				htmlNodeDump (buf, doc, ptr2);
			}
			if (buf->use > 0) {
				char *name;
				name = g_strndup (CXML2C (buf->content), buf->use);
				tc->sheet = html_get_sheet (name, wb);
				g_free (name);
			}
			xmlBufferFree (buf);
		} else if (xmlStrEqual (ptr->name, CC2XML ("thead")) ||
			   xmlStrEqual (ptr->name, CC2XML ("tfoot")) ||
			   xmlStrEqual (ptr->name, CC2XML ("tbody"))) {
			html_read_rows (ptr, doc, wb, tc);
		} else if (xmlStrEqual (ptr->name, CC2XML ("tr"))) {
			html_read_rows (cur, doc, wb, tc);
			break;
		}
	}
}

/* Element types which imply that we are inside a table */
static const char *table_start_elt_types[] = {
	"caption",
	"col",
	"colgroup",
	"tbody",
	"tfoot",
	"thead",
	"tr",
	NULL
};

/* Element types which imply that we are inside a row */
static const char *row_start_elt_types[] = {
	"td",
	"th",
	NULL
};

/* Element types which occur inside tables and rows, but also outside */
static const char *cont_elt_types[] = {
	"del",
	"ins",
	NULL
};

static gboolean 
is_elt_type (htmlNodePtr ptr, const char** types)
{
	const char **p;
	gboolean ret = FALSE;

	for (p = types; *p; p++)
		if (xmlStrEqual (ptr->name, CC2XML ((*p)))) {
			ret = TRUE;
			break;
		}

	return ret;
}

static gboolean
starts_inferred_table (htmlNodePtr ptr)
{
	return ((ptr->type == XML_ELEMENT_NODE) &&
		is_elt_type (ptr, table_start_elt_types));
}

static gboolean
ends_inferred_table (htmlNodePtr ptr)
{
	return ((ptr->type == XML_ELEMENT_NODE) &&
		!(is_elt_type (ptr, table_start_elt_types) ||
		  is_elt_type (ptr, cont_elt_types)));
}

static gboolean
starts_inferred_row (htmlNodePtr ptr)
{
	return ((ptr->type == XML_ELEMENT_NODE) &&
		is_elt_type (ptr, row_start_elt_types));
}

static gboolean
ends_inferred_row (htmlNodePtr ptr)
{
	return ((ptr->type == XML_ELEMENT_NODE) &&
		!(is_elt_type (ptr, row_start_elt_types) ||
		  is_elt_type (ptr, cont_elt_types)));
}

/*
 * Handles incomplete html fragments as may occur on the clipboard,
 * e.g. a <td> without <tr> and <table> in front of it.  
 */
static void
html_search_for_tables (htmlNodePtr cur, htmlDocPtr doc,
			WorkbookView *wb_view, GnmHtmlTableCtxt *tc)
{
	htmlNodePtr ptr;

	if (cur == NULL) {
		xmlGenericError(xmlGenericErrorContext,
				"htmlNodeDumpFormatOutput : node == NULL\n");
		return;
	}
	
	if (cur->type != XML_ELEMENT_NODE)
		return;
	
	if (xmlStrEqual (cur->name, CC2XML ("table"))) {
		html_read_table (cur, doc, wb_view, tc);
	} else if (starts_inferred_table (cur) || starts_inferred_row (cur)) {
		htmlNodePtr tnode = xmlNewNode (NULL, "table");
	
		/* Link in a table node */
		xmlAddPrevSibling (cur, tnode);
		if (starts_inferred_row (cur)) {
			htmlNodePtr rnode = xmlNewNode (NULL, "tr");

			/* Link in a row node */
			xmlAddChild (tnode, rnode);
			/* Make following elements children of the row node,
			 * until we meet one which isn't legal in a row. */
			while ((ptr = tnode->next) != NULL) {
				if (ends_inferred_row (ptr))
					break;
				xmlUnlinkNode (ptr);
				xmlAddChild (rnode, ptr);
			}
		}
		/* Make following elements children of the row node,
		 * until we meet one which isn't legal in a table. */
		while ((ptr = tnode->next) != NULL) {
			if (ends_inferred_table (ptr))
				break;
			xmlUnlinkNode (ptr);
			xmlAddChild (tnode, ptr);
		}
		html_read_table (tnode, doc, wb_view, tc);
	} else {
		for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
			html_search_for_tables (ptr, doc, wb_view, tc);
			/* ptr may now have been pushed down in the tree, 
			 * if so, ptr->next is not the right pointer to 
			 * follow */
			while (ptr->parent != cur)
				ptr = ptr->parent;
		}
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
	GnmHtmlTableCtxt tc;

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
			else
				bomlen = 0;
			break;
		default:
			bomlen = 0;
		}
		ctxt = htmlCreatePushParserCtxt (NULL, NULL,
						 (const char *)(buf + bomlen),
						 4 - bomlen,
						 gsf_input_name (input), enc);

		for (; size > 0 ; size -= len) {
			len = MIN (4096, size);
			buf = gsf_input_read (input, len, NULL);
			if (buf == NULL)
				break;
			htmlParseChunk (ctxt, (const char *)buf, len, 0);
		}

		htmlParseChunk (ctxt, (const char *)buf, 0, 1);
		doc = ctxt->myDoc;
		htmlFreeParserCtxt (ctxt);
	}

	if (doc != NULL) {
		xmlNodePtr ptr;
		tc.sheet = NULL;
		tc.row   = -1;
		for (ptr = doc->children; ptr != NULL ; ptr = ptr->next)
			html_search_for_tables (ptr, doc, wb_view, &tc);
		xmlFreeDoc (doc);
	} else
		gnumeric_io_error_info_set (io_context,
			error_info_new_str (_("Unable to parse the html.")));
}

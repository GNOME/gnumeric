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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
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
#include <style-color.h>
#include <hlink.h>
#include <cell.h>
#include <ranges.h>
#include <goffice/goffice.h>

#include <gsf/gsf-input.h>
#include <libxml/HTMLparser.h>
#include <libxml/HTMLtree.h>

#define CC2XML(s) ((xmlChar const *)(s))
#define C2XML(s) ((xmlChar *)(s))
#define CXML2C(s) ((char const *)(s))
#define XML2C(s) ((char *)(s))

typedef struct {
	Sheet *sheet;
	int   row;
	WorkbookView *wb_view;
} GnmHtmlTableCtxt;

static void html_read_table (htmlNodePtr cur, htmlDocPtr doc,
			     WorkbookView *wb_view,
			     GnmHtmlTableCtxt *tc);


static Sheet *
html_get_sheet (char const *name, Workbook *wb)
{
	Sheet *sheet = NULL;

	if (name) {
		sheet = workbook_sheet_by_name (wb, name);
		if (sheet == NULL) {
			sheet = sheet_new (wb, name, GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
			workbook_sheet_attach (wb, sheet);
		}
	} else
		sheet = workbook_sheet_add (wb, -1, GNM_DEFAULT_COLS, GNM_DEFAULT_ROWS);
	return sheet;
}


/* deletes any initial whitespace */
/* thereafter, including at the end, */
/* collapses any run of whitespace to a single space. */
/* (This may or may not be what you want, e.g. <pre>...</pre>) */
/* It's up to the caller to deal with the possible final trailing space. */
static void
html_append_trim_text (GString *buf, const xmlChar *text)
{
	const xmlChar *p;
	const xmlChar *last_sp;

	while (*text) {
		// collect a run of spaces, if any
		for (last_sp = p = text;
		     *p && g_unichar_isspace (g_utf8_get_char (p));
		     p = g_utf8_next_char (p)) {
			last_sp = p;
		}
		if (buf->len == 0 ||
		    g_unichar_isspace (g_utf8_get_char (g_utf8_prev_char (buf->str + buf->len)))) {
			text = p;	      /* skip all the spaces */
		} else {
			text = last_sp;	      /* keep the last space */
		}
		if (*text) {
			// collect a run of non-spaces, if any
			for (/* keep p */;
			     *p && !g_unichar_isspace (g_utf8_get_char (p));
			     p =  g_utf8_next_char (p)) {
			}
			// here p points to either a space or EoS
			if (*p) p = g_utf8_next_char (p);
			// copy the non-spaces and one trailing space if any
			g_string_append_len (buf, text, p - text);
		}
		text = p;
	}
}

/* remove one trailing space, if it exists */
static void
html_rtrim (GString *buf)
{
	gchar* last;

	if (buf->len == 0)
		return;

	last = g_utf8_prev_char (buf->str + buf->len);
	if (g_unichar_isspace (g_utf8_get_char (last)))
		g_string_truncate(buf, last - buf->str);
}

static void
html_read_content (htmlNodePtr cur, GString *buf, GnmStyle *mstyle,
		   xmlBufferPtr a_buf, GSList **hrefs, gboolean first,
		   htmlDocPtr doc, GnmHtmlTableCtxt *tc)
{
	htmlNodePtr ptr;

	for (ptr = cur->children; ptr != NULL ; ptr = ptr->next) {
		if (ptr->type == XML_TEXT_NODE) {
			if (g_utf8_validate (ptr->content, -1, NULL))
				html_append_trim_text (buf, ptr->content);
			else
				g_string_append (buf, _("[Warning: Invalid text string has been removed.]"));
		} else if (ptr->type == XML_ELEMENT_NODE) {
			if (first) {
				if (xmlStrEqual (ptr->name, CC2XML ("i"))
				    || xmlStrEqual (ptr->name, CC2XML ("em")))
					gnm_style_set_font_italic (mstyle, TRUE);
				if (xmlStrEqual (ptr->name, CC2XML ("b")))
					gnm_style_set_font_bold (mstyle, TRUE);
			}
			if (xmlStrEqual (ptr->name, CC2XML ("a"))) {
				xmlAttrPtr   props;
				props = ptr->properties;
				while (props) {
					if (xmlStrEqual (props->name, CC2XML ("href")) && props->children) {
						*hrefs = g_slist_prepend (
							*hrefs, props->children);

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
			if (xmlStrEqual (ptr->name, CC2XML ("table"))) {
				Sheet *last_sheet = tc->sheet;
				int   last_row = tc->row;
				tc->sheet = NULL;
				tc->row   = -1;
				html_read_table (ptr, doc, tc->wb_view, tc);
				if (tc->sheet) {
					g_string_append_printf (buf, _("[see sheet %s]"), tc->sheet->name_quoted);
					xmlBufferAdd (a_buf, CC2XML (_("The original html file is\n"
								       "using nested tables.")), -1);
				}
				tc->sheet = last_sheet;
				tc->row = last_row;
			} else
				html_read_content
					(ptr, buf, mstyle, a_buf, hrefs, first, doc, tc);
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
			GSList *hrefs = NULL;
			GnmHLink *lnk = NULL;

			/* Check whether we need to skip merges from above */
			pos.row = tc->row;
			pos.col = col + 1;
			while (gnm_sheet_merge_contains_pos (tc->sheet, &pos)) {
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

			mstyle = gnm_style_new_default ();
			if (xmlStrEqual (ptr->name, CC2XML ("th")))
				gnm_style_set_font_bold (mstyle, TRUE);

			html_read_content (ptr, buf, mstyle, a_buf,
					   &hrefs, TRUE, doc, tc);
			html_rtrim(buf);

			if (g_slist_length (hrefs) >= 1 &&
			    buf->len > 0) {
				/* One hyperlink, and text to make it
				 * visible */
				char *url;
				xmlBufferPtr h_buf = xmlBufferCreate ();

				hrefs = g_slist_reverse (hrefs);
				htmlNodeDump (
					h_buf, doc, (htmlNodePtr)hrefs->data);
				url = g_strndup (
					CXML2C (h_buf->content), h_buf->use);
				if (g_str_has_prefix (url, "mailto:"))
					lnk = gnm_hlink_new (
						gnm_hlink_email_get_type (),
						tc->sheet);
				else
					lnk = gnm_hlink_new (
						gnm_hlink_url_get_type (),
						tc->sheet);
				gnm_hlink_set_target (lnk, url);
				gnm_style_set_hlink (mstyle, lnk);
				gnm_style_set_font_uline (mstyle,
							  UNDERLINE_SINGLE);
				gnm_style_set_font_color (mstyle,
							  gnm_color_new_go (GO_COLOR_BLUE));
				g_free (url);
				xmlBufferFree (h_buf);
			}
			if (g_slist_length (hrefs) > 1 || buf->len <= 0) {
				/* Multiple links,
				 * or no text to give hyperlink style,
				 * so put them in a comment */
				GSList *l;

				for (l = hrefs; l != NULL; l = l->next) {
					htmlNodeDump (a_buf, doc,
						      (htmlNodePtr)l->data);
					xmlBufferAdd (a_buf, CC2XML ("\n"),
						      -1);
				}
			}
			g_slist_free (hrefs);
			if (buf->len > 0) {
				GnmCell *cell = sheet_cell_fetch (tc->sheet, col + 1, tc->row);
				sheet_style_set_pos (tc->sheet, col + 1, tc->row, mstyle);
				sheet_cell_set_text (cell, buf->str, NULL);
			} else
				gnm_style_unref (mstyle);

			if (a_buf->use > 0) {
				char *name;

				name = g_strndup (CXML2C (a_buf->content), a_buf->use);
				cell_set_comment (tc->sheet, &pos, NULL, name, NULL);
				g_free (name);
			}
			g_string_free (buf, TRUE);
			xmlBufferFree (a_buf);

			/* If necessary create the merge */
			if (colspan > 1 || rowspan > 1) {
				GnmRange range;
				GnmRange *r = &range;

				range_init (r, col + 1, tc->row, col + colspan, tc->row + rowspan - 1);
				gnm_sheet_merge_add (tc->sheet, r, FALSE, NULL);
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

	wb = wb_view_get_workbook (wb_view);
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
static char const *table_start_elt_types[] = {
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
static char const *row_start_elt_types[] = {
	"td",
	"th",
	NULL
};

/* Element types which occur inside tables and rows, but also outside */
static char const *cont_elt_types[] = {
	"del",
	"ins",
	NULL
};

static gboolean
is_elt_type (htmlNodePtr ptr, char const ** types)
{
	char const **p;
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

	// We're looking for tables, but sometimes we get html with no
	// tables and just text.  Consider that a single-cell table.
	if (cur->type == XML_TEXT_NODE) {
		Workbook *wb = wb_view_get_workbook (wb_view);
		GnmCell *cell;
		int col = 0;

		tc->row++;
		if (tc->sheet == NULL)
			tc->sheet = html_get_sheet (NULL, wb);
		cell = sheet_cell_fetch (tc->sheet, col + 1, tc->row);
		sheet_cell_set_text (cell, CXML2C (cur->content), NULL);
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
html_file_open (G_GNUC_UNUSED GOFileOpener const *fo, GOIOContext *io_context,
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

	if (gsf_input_seek (input, 0, G_SEEK_SET))
		return;

	size = gsf_input_size (input);
	if (size >= 4) {
		size -= 4;
		buf = gsf_input_read (input, 4, NULL);
		if (buf != NULL) {
			enc = xmlDetectCharEncoding(buf, 4);
			switch (enc) {
#if LIBXML_VERSION < 20702
			/* Skip byte order mark */
			case XML_CHAR_ENCODING_UCS4BE:
			case XML_CHAR_ENCODING_UCS4LE:
			case XML_CHAR_ENCODING_UCS4_2143:
			case XML_CHAR_ENCODING_UCS4_3412:
				if (buf[0] == 0xFE || buf[1] == 0xFE || buf[2] == 0xFE || buf[3] == 0xFE)
					bomlen = 4;
				else
					bomlen = 0;
				break;
			case XML_CHAR_ENCODING_EBCDIC:
				if (buf[0] == 0xDD)
					bomlen = 4;
				else
					bomlen = 0;
				break;
			case XML_CHAR_ENCODING_UTF16BE:
			case XML_CHAR_ENCODING_UTF16LE:
				if (buf[0] == 0xFE || buf[1] == 0xFE)
					bomlen = 2;
				else
					bomlen = 0;
				break;
			case XML_CHAR_ENCODING_UTF8:
				if (buf[0] == 0xef)
					bomlen = 3;
				else
					bomlen = 0;
				break;
#endif
			case XML_CHAR_ENCODING_NONE:
				bomlen = 0;
				/* Try to detect unmarked UTF16LE
				   (Firefox Windows clipboard, drag data all platforms) */
				if ((buf[0] >= 0x20 || g_ascii_isspace(buf[0])) &&
				    buf[1] == 0 &&
				    (buf[2] >= 0x20 || g_ascii_isspace(buf[2])) &&
				    buf[3] == 0)
					enc =  XML_CHAR_ENCODING_UTF16LE;
				break;
			default:
				bomlen = 0;
			}
			ctxt = htmlCreatePushParserCtxt (
				NULL, NULL, (char const *)(buf + bomlen),
				4 - bomlen, gsf_input_name (input), enc);

			for (; size > 0 ; size -= len) {
				len = MIN (4096, size);
				buf = gsf_input_read (input, len, NULL);
				if (buf == NULL)
					break;
				htmlParseChunk (
					ctxt, (char const *)buf, len, 0);
			}

			htmlParseChunk (ctxt, (char const *)buf, 0, 1);
			doc = ctxt->myDoc;
			htmlFreeParserCtxt (ctxt);
		}
	}

	if (doc != NULL) {
		xmlNodePtr ptr;
		tc.sheet = NULL;
		tc.row   = -1;
		tc.wb_view = wb_view;
		for (ptr = doc->children; ptr != NULL ; ptr = ptr->next)
			html_search_for_tables (ptr, doc, wb_view, &tc);
		xmlFreeDoc (doc);
	} else
		go_io_error_info_set (io_context,
			go_error_info_new_str (_("Unable to parse the html.")));
}

/* Quick and dirty html probe. */
gboolean
html_file_probe (G_GNUC_UNUSED GOFileOpener const *fo, GsfInput *input,
		 G_GNUC_UNUSED GOFileProbeLevel pl)
{
	gsf_off_t size = 200;
	guint8 const* buf = gsf_input_read (input, size, NULL);
	gchar *ulstr = NULL;
	GString *ustr;
	gboolean res = FALSE;

	/* Avoid seeking in large streams - try to read, fall back if
	 * stream is too short.  (Actually, currently _size does not
	 * involve any syscalls -- MW).  */
	if (!buf) {
		size = gsf_input_size (input);
		buf = gsf_input_read (input, size, NULL);
		if (!buf)
			return res;
	}

	if (go_guess_encoding (buf, size, NULL, &ustr, NULL)) {
		ulstr = g_utf8_strdown (ustr->str, -1);
		g_string_free (ustr, TRUE);
	}

	if (!ulstr)
		return res;

	res = (strstr (ulstr, "<table") != NULL ||
	       strstr (ulstr, "<html") != NULL ||
	       strstr (ulstr, "<!doctype html") != NULL);

	g_free (ulstr);

	return res;
}

/*
 * html.c
 *
 * Copyright (C) 1999, 2000 Rasca, Berlin
 * EMail: thron@gmx.de
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

#include "config.h"
#include "io-context.h"
#include "workbook-view.h"
#include "workbook.h"
#include "sheet-style.h"
#include "style.h"
#include "style-color.h"
#include "html.h"
#include "cell.h"
#include "sheet.h"
#include "value.h"
#include "font.h"
#include "plugin-util.h"
#include "error-info.h"

#include <gnome.h>
#include <errno.h>
#include <ctype.h>

/*
 * escape special characters
 */
static int
html_fprintf (FILE *fp, const char *s)
{
	int len, i;
	const char *p;

	if (!s)
		return 0;
	len = strlen (s);
	if (!len)
		return 0;
	p = s;
	for (i = 0; i < len; i++) {
		switch (*p) {
			case '<':
				fprintf (fp, "&lt;");
				break;
			case '>':
				fprintf (fp, "&gt;");
				break;
			case '&':
				fprintf (fp, "&amp;");
				break;
			case '\'':
				fprintf (fp, "&apos;");
				break;
			case '\"':
				fprintf (fp, "&quot;");
				break;
			default:
				fprintf (fp, "%c", *p);
				break;
		}
		p++;
	}
	return len;
}

static void
html_write_cell_str (FILE *fp, Cell *cell, MStyle *mstyle)
{
	if (font_is_monospaced (mstyle))
		fprintf (fp, "<TT>");
	if (mstyle_get_font_bold (mstyle))
		fprintf (fp, "<B>");
	if (mstyle_get_font_italic (mstyle))
		fprintf (fp, "<I>");

	if (!cell_is_blank (cell)) {
		char * text = cell_get_rendered_text (cell);
		html_fprintf (fp, text);
		g_free (text);
	} else
		fprintf (fp, "<BR>");

	if (mstyle_get_font_italic (mstyle))
		fprintf (fp, "</I>");
	if (mstyle_get_font_bold (mstyle))
		fprintf (fp, "</B>");
	if (font_is_monospaced (mstyle))
		fprintf (fp, "</TT>");
}

static void
html_get_color (MStyle *mstyle, MStyleElementType t, int *r, int *g, int *b)
{
	*r = mstyle_get_color (mstyle, t)->color.red >> 8;
	*g = mstyle_get_color (mstyle, t)->color.green >> 8;
	*b = mstyle_get_color (mstyle, t)->color.blue >> 8;
}

/*
 * write a TD
 */
static void
html_write_cell32 (FILE *fp, Cell *cell, MStyle *style)
{
	unsigned int r, g, b;

	g_return_if_fail (style != NULL);

	fprintf (fp, "\t<TD");

	if (cell) {
		switch (style_default_halign (style, cell)) {
		case HALIGN_RIGHT :
			fprintf (fp, " align=right");
			break;

		case HALIGN_CENTER :
		case HALIGN_CENTER_ACROSS_SELECTION :
			fprintf (fp, " align=center");
			break;
		default :
			break;
		}
		if (mstyle_get_align_v (style) & VALIGN_TOP)
			fprintf (fp, " valign=top");
	}

	html_get_color (style, MSTYLE_COLOR_BACK, &r, &g, &b);
	if (r < 255 || g < 255 || b < 255)
		fprintf (fp, " bgcolor=\"#%02X%02X%02X\"", r, g, b);
	fprintf (fp, ">");
	html_get_color (style, MSTYLE_COLOR_FORE, &r, &g, &b);
	if (r != 0 || g != 0 || b != 0)
		fprintf (fp, "<FONT color=\"#%02X%02X%02X\">",
			 r, g, b);
	html_write_cell_str (fp, cell, style);

	if (r != 0 || g != 0 || b != 0)
		fprintf (fp, "</FONT>");

	fprintf (fp, "</TD>\n");
}

/*
 * write a TD
 */
static void
html_write_cell40 (FILE *fp, Cell *cell, MStyle *style)
{
	unsigned int r, g, b;

	g_return_if_fail (style != NULL);

	fprintf (fp, "\t<TD");

	if (cell) {
		switch (style_default_halign (style, cell)) {
		case HALIGN_RIGHT :
			fprintf (fp, " halign=right");
			break;

		case HALIGN_CENTER :
		case HALIGN_CENTER_ACROSS_SELECTION :
			fprintf (fp, " halign=center");
			break;
		default :
			break;
		}

		if (mstyle_get_align_v (style) & VALIGN_TOP)
			fprintf (fp, " valign=top");
	}

	html_get_color (style, MSTYLE_COLOR_BACK, &r, &g, &b);
	if (r < 255 || g < 255 || b < 255)
		fprintf (fp, " bgcolor=\"#%02X%02X%02X\"", r, g, b);
	fprintf (fp, ">");
	html_get_color (style, MSTYLE_COLOR_FORE, &r, &g, &b);
	if (r != 0 || g != 0 || b != 0)
		fprintf (fp, "<FONT color=\"#%02X%02X%02X\">",
			 r, g, b);

	html_write_cell_str (fp, cell, style);

	if (r != 0 || g != 0 || b != 0)
		fprintf (fp, "</FONT>");

	fprintf (fp, "</TD>\n");
}

/*
 * write every sheet of the workbook to a html 3.2 table
 *
 * FIXME: Should html quote sheet name (and everything else)
 */
void
html32_file_save (GnumFileSaver const *fs, IOContext *io_context,
                  WorkbookView *wb_view, const gchar *file_name)
{
	FILE *fp;
	GList *sheets, *ptr;
	Cell *cell;
	MStyle *style;
	int row, col;
	Workbook *wb = wb_view_workbook (wb_view);
	ErrorInfo *open_error;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (file_name != NULL);

	fp = gnumeric_fopen_error_info (file_name, "w", &open_error);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, open_error);
		return;
	}

	fprintf (fp,
"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\n"
"<HTML>\n"
"<HEAD>\n\t<TITLE>Tables</TITLE>\n"
"\t<!-- "G_PLUGIN_FOR_HTML" -->\n"
"<STYLE><!--\n"
"TT {\n"
"\tfont-family: courier;\n"
"}\n"
"TD {\n"
"\tfont-family: helvetica, sans-serif;\n"
"}\n"
"CAPTION {\n"
"\tfont-size: 14pt;\n"
"\ttext-align: left;\n"
"}\n"
"--></STYLE>\n"
"</HEAD>\n</BODY>\n");
	sheets = workbook_sheets (wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;
		Range r = sheet_get_extent (sheet, FALSE);

		fprintf (fp, "<TABLE border=1>\n");
		fprintf (fp, "<CAPTION>%s</CAPTION>\n", sheet->name_unquoted);

		for (row = r.start.row; row <= r.end.row; row++) {
			fprintf (fp, "<TR>\n");
			for (col = r.start.col; col <= r.end.col; col++) {
				cell = sheet_cell_get (sheet, col, row);
				style = sheet_style_get (sheet, col, row);

				html_write_cell32 (fp, cell, style);
			}
			fprintf (fp, "</TR>\n");
		}
		fprintf (fp, "</TABLE>\n<P>\n\n");
	}
	g_list_free (sheets);
	fprintf (fp, "<BODY>\n</HTML>\n");
	fclose (fp);
}

/*
 * write every sheet of the workbook to a html 4.0 table
 *
 * FIXME: Should html quote sheet name (and everything else)
 */
void
html40_file_save (GnumFileSaver const *fs, IOContext *io_context,
                  WorkbookView *wb_view, const gchar *file_name)
{
	FILE *fp;
	GList *sheets, *ptr;
	Cell *cell;
	MStyle *style;
	int row, col;
	Workbook *wb = wb_view_workbook (wb_view);
	ErrorInfo *open_error;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (file_name != NULL);

	fp = gnumeric_fopen_error_info (file_name, "w", &open_error);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, open_error);
		return;
	}

	fprintf (fp,
"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\"\n"
"\t\t\"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<HTML>\n"
"<HEAD>\n\t<TITLE>Tables</TITLE>\n"
"\t<!-- "G_PLUGIN_FOR_HTML" -->\n"
"<STYLE><!--\n"
"TT {\n"
"\tfont-family: courier;\n"
"}\n"
"TD {\n"
"\tfont-family: helvetica, sans-serif;\n"
"}\n"
"CAPTION {\n"
"\tfont-family: helvetica, sans-serif;\n"
"\tfont-size: 14pt;\n"
"\ttext-align: left;\n"
"}\n"
"--></STYLE>\n"
"</HEAD>\n</BODY>\n");
	sheets = workbook_sheets (wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;
		Range r = sheet_get_extent (sheet, FALSE);

		fprintf (fp, "<TABLE border=1>\n");
		fprintf (fp, "<CAPTION>%s</CAPTION>\n", sheet->name_unquoted);

		for (row = r.start.row; row <= r.end.row; row++) {
			fprintf (fp, "<TR>\n");
			for (col = r.start.col; col <= r.end.col; col++) {
				cell  = sheet_cell_get (sheet, col, row);
				style = sheet_style_get (sheet, col, row);

				html_write_cell40 (fp, cell, style);
			}
			fprintf (fp, "</TR>\n");
		}
		fprintf (fp, "</TABLE>\n<P>\n\n");
	}
	g_list_free (sheets);
	fprintf (fp, "<BODY>\n</HTML>\n");
	fclose (fp);
}

#define HTML_BOLD	1
#define HTML_ITALIC	2
#define HTML_RIGHT	4
#define HTML_CENTER	8

/*
 */
static char *
html_get_string (char const *s, int *flags, char const **last)
{
#define LINESIZE 1024
	static char buf[LINESIZE];
	char const *p;
	char *q;

	buf[0] = buf[LINESIZE-1] = '\0';
	if (!s)
		return NULL;
	q = buf;
	p = s;
	while (*p) {
		if (*p == '<') {
			if (!strncasecmp (p+1, "/td>", 4)) {
				p += 5;
				break;
			}
			if (p [2] == '>') {
				if (tolower ((unsigned char)p[1]) == 'i')
					*flags |= HTML_ITALIC;
				else if (tolower ((unsigned char)p[1]) == 'b')
					*flags |= HTML_BOLD;
			}
			p = strchr (p, '>');
			if (p == NULL)
				break;
		} else if (*p == '&') {
			if (strstr (p, "&lt;")) {
				*q++ = '<';
				p += 3;
			} else if (strstr (p, "&gt;")) {
				*q++ = '>';
				p += 3;
			} else if (strstr (p, "&amp;")) {
				*q++ = '&';
				p += 4;
			} else if (strstr (p, "&apos;")) {
				*q++ = '\'';
				p += 5;
			} else if (strstr (p, "&quot;")) {
				*q++ = '\"';
				p += 5;
			} else {
				*q++ = *p;
			}
		} else if (*p == '\n') {
			break;
		} else {
			*q++ = *p;
		}
		p++;
	}
	*last = p;
	*q = '\0';
	return buf;
}

/* quick utility to do a case insensitive search for tags */
static char const *
findtag (char const *buf, char const *tag)
{
	int n;
	g_return_val_if_fail (*tag == '<', NULL);

	n = strlen (tag);

	--buf;
	do {
		buf = strchr (buf+1, '<');
	} while (buf != NULL && strncasecmp (buf, tag, n));
	return buf;
}

/*
 * try at least to read back what we have written before..
 */
void
html32_file_open (GnumFileOpener const *fo, IOContext *io_context,
                  WorkbookView *wb_view, const char *filename)
{
	Workbook *wb = wb_view_workbook (wb_view);
	FILE *fp;
	Sheet *sheet;
	Cell *cell;
	int num, row, col, flags;
	char const *p, *str, *ptr;
	char buf[LINESIZE];
	ErrorInfo *open_error;

	g_return_if_fail (filename != NULL);

	fp = gnumeric_fopen_error_info (filename, "r", &open_error);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, open_error);
		return;
	}

	sheet = NULL;
	col = 0;
	row = -1;
	num = 0;
	while (fgets (buf, LINESIZE, fp) != NULL) {
		ptr = buf;
quick_hack :
		/* FIXME : This is an ugly hack.  I'll patch it a bit for now
		 * but we should migrate to libxml
		 */
		if (ptr == NULL)
			continue;

		if (NULL != (p = findtag (ptr, "<TABLE"))) {
			sheet = workbook_sheet_add (wb, NULL, FALSE);
			row = -1;
			ptr = strchr (p+6, '>');
			goto quick_hack;
		} else if (NULL != (p = findtag (ptr, "</TABLE>"))) {
			sheet = NULL;
			ptr = strchr (p+7, '>');
			goto quick_hack;
		} else if (NULL != (p = findtag (ptr, "<TR"))) {
			row++;
			col = 0;
			ptr = strchr (p+3, '>');
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
					if ((*p == ' ') && (*(p+1) != '>')) {
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
	fclose (fp);
}

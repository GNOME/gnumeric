/*
 * html.c
 *
 * Copyright (C) 1999, 2000 Rasca, Berlin
 * EMail: thron@gmx.de
 * Copyright (c) 2001 Andreas J. Guelzow
 * EMail: aguelzow@taliesin.ca
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
#include <rendered-value.h>

#include <gnome.h>
#include <errno.h>
#include <ctype.h>

/*
 * html_version_t:
 *
 * version selector 
 *
 */
typedef enum {
	HTML40 = 0,
	HTML32 = 1,
	HTML40F   = 2
} html_version_t;

/*
 * html_print_encoded:
 *
 * @fp: the file
 * @str: the string
 *
 * print the string to fp encoded all special chars
 *
 */
static void
html_print_encoded (FILE *fp, char *str)
{
	const char *p;
	guint i;

	if (!str)
		return;
	p = str;
	while (*p != '\0') {
		switch (*p) {
			case '<':
				fputs ("&lt;", fp);
				break;
			case '>':
				fputs ("&gt;", fp);
				break;
			case '&':
				fputs ("&amp;", fp);
				break;
			case '\"':
				fputs ("&quot;", fp);
				break;
			default:
				i = (unsigned char) *p;
				if ((( i >= 0x20) && (i < 0x80)) ||
				    (*p == '\n') || (*p == '\r') || (*p == '\t'))
					fputc (*p, fp);
				else {
					fprintf (fp, "&#%03u;", i);
				}
				break;
		}
		p++;
	}
}

/*
 * html_get_color:
 *
 * @mstyle: the cellstyle
 * @t:      which color
 * @r:      red component
 * @g:      green component
 * @b:      blue component
 *
 * Determine rgb components
 *
 */
static void
html_get_text_color (Cell *cell, MStyle *mstyle, int *r, int *g, int *b)
{
	StyleColor *textColor;

	textColor= cell->rendered_value->render_color;
	if (textColor == NULL)
		textColor = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE);
	
	*r = textColor->color.red >> 8;
	*g = textColor->color.green >> 8;
	*b = textColor->color.blue >> 8;
}
static void
html_get_color (MStyle *mstyle, MStyleElementType t, int *r, int *g, int *b)
{
	StyleColor *color;

	color = mstyle_get_color (mstyle, t);
	
	*r = color->color.red >> 8;
	*g = color->color.green >> 8;
	*b = color->color.blue >> 8;
}

static void
html_write_cell_content (FILE *fp, Cell *cell, MStyle *mstyle, html_version_t version)
{
	guint r = 0;
	guint g = 0;
	guint b = 0;
	char *rendered_string;

	if (mstyle != NULL) {
		if (mstyle_get_font_italic (mstyle))
			fputs ("<I>", fp);
		if (mstyle_get_font_bold (mstyle))
			fputs ("<B>", fp);
		if (font_is_monospaced (mstyle))
			fputs ("<TT>", fp);
	}

	if (cell != NULL) {
		if (mstyle != NULL && version != HTML40) {
			html_get_text_color (cell, mstyle, &r, &g, &b);
			if (r > 0 || g > 0 || b > 0)
				fprintf (fp, "<FONT color=\"#%02X%02X%02X\">", r, g, b);
		}
		rendered_string = cell_get_rendered_text (cell);
		html_print_encoded (fp, rendered_string);
		g_free (rendered_string);
	}

	if (r > 0 || g > 0 || b > 0)
		fputs ("</FONT>", fp);
	if (mstyle != NULL) {
		if (font_is_monospaced (mstyle))
			fputs ("</TT>", fp);
		if (mstyle_get_font_bold (mstyle))
			fputs ("</B>", fp);
		if (mstyle_get_font_italic (mstyle))
			fputs ("</I>", fp);
	}
}


/*
 * write_row:
 *
 * @fp: file
 * @sheet: the gnumeric sheet
 * @row: the row number
 * @col: the col number
 *
 * Output all cell info for the given cell 
 *
 */

static void
write_cell (FILE *fp, Sheet *sheet, gint row, gint col, html_version_t version)
{
	Cell *cell;
	MStyle *mstyle;
	guint r, g, b;

	
	mstyle = sheet_style_get (sheet, col, row);
	if (mstyle != NULL && version != HTML32 && version != HTML40) {
		html_get_color (mstyle, MSTYLE_COLOR_BACK, &r, &g, &b);
		if (r < 255 || g < 255 || b < 255) {
			fprintf (fp, " bgcolor=\"#%02X%02X%02X\"", r, g, b);
		}
	}

	cell = sheet_cell_get (sheet, col, row);
	if (cell != NULL) {

		switch (mstyle_get_align_v (mstyle)) {
		case VALIGN_TOP:
			fputs (" valign=\"top\" ", fp);
			break;
		case VALIGN_BOTTOM:
			fputs(" valign=\"bottom\" ", fp);
			break;
		case VALIGN_CENTER:
			fputs(" valign=\"center\" ", fp);
			break;
		case VALIGN_JUSTIFY:
			fputs(" valign=\"baseline\" ", fp);
			break;
		default:
			break;
		}
		switch (style_default_halign(mstyle, cell)) {
		case HALIGN_RIGHT:
			fputs (" align=\"right\" ", fp);
			break;
		case HALIGN_CENTER:
		case HALIGN_CENTER_ACROSS_SELECTION:
			fputs (" align=\"center\" ", fp);
			break;
		case HALIGN_LEFT:
			fputs (" align=\"left\" ", fp);
			break;
		case HALIGN_JUSTIFY:
			fputs (" align=\"justify\" ", fp);
			break;
		default:
			break;
		}

	}

	fprintf (fp, ">");
	html_write_cell_content (fp, cell, mstyle, version);
	fputs ("</TD>\n", fp);
}

/*
 * write_row:
 *
 * @fp: file
 * @sheet: the gnumeric sheet
 * @row: the row number
 *
 * Set up a TD node for each cell in the given row, witht eh  appropriate
 * colspan and rowspan.
 * Call write_cell for each cell. 
 *
 */

static void
write_row (FILE *fp, Sheet *sheet, gint row, Range *range, html_version_t version)
{
	gint col;
	ColRowInfo const * ri;
	
	ri = sheet_row_get_info (sheet, row);
	
	for (col = range->start.col; col <= range->end.col; col++) {
		CellSpanInfo const *the_span;
		Range const *merge_range;
		CellPos pos = {col, row};


		/* Is this a span */
		the_span = row_span_get (ri, col);
		if (the_span != NULL) {
			fprintf (fp, "<TD COLSPAN=%i ", the_span->right - col + 1);
			write_cell (fp, sheet, row, the_span->cell->pos.col, version);
			col = the_span->right;
			continue;
		}
		
                /* is this covered by a merge */
		merge_range = sheet_merge_contains_pos	(sheet, &pos);
		if (merge_range != NULL) {
			if (merge_range->start.col != col ||
			    merge_range->start.row != row)
				continue;
			fprintf (fp, "<TD COLSPAN=%i ROWSPAN=%i ", 
				 merge_range->end.col - merge_range->start.col + 1,
				 merge_range->end.row - merge_range->start.row + 1);
			write_cell (fp, sheet, row, col, version);
			col = merge_range->end.col;
			continue;
		}
		fputs ("<TD ", fp);
		write_cell (fp, sheet, row, col, version);
	}
} 

/*
 * write_sheet:
 *
 * @fp: file
 * @sheet: the gnumeric sheet
 *
 * set up a table and call write_row for each row
 *
 */

static void
write_sheet (FILE *fp, Sheet *sheet, html_version_t version)
{
	Range total_range;
	gint row;

	fputs ("<P><TABLE border=1>\n", fp);

	fputs ("<CAPTION>", fp);
	html_print_encoded (fp, sheet->name_unquoted);
	fputs ("</CAPTION>\n", fp);

	total_range = sheet_get_extent (sheet, TRUE);
	for (row = total_range.start.row; row <=  total_range.end.row; row++) {
		fputs ("<TR>\n", fp);
		write_row (fp, sheet, row, &total_range, version);
		fputs ("</TR>\n", fp);
	}
	fputs ("</TABLE>\n", fp);
}

/*
 * html_file_save:
 *
 * write the html file (version of html according to version argument)
 */
static void
html_file_save (GnumFileSaver const *fs, IOContext *io_context,
                  WorkbookView *wb_view, const gchar *file_name, html_version_t version)
{
	FILE *fp;
	GList *sheets, *ptr;
	Workbook *wb = wb_view_workbook (wb_view);
	ErrorInfo *open_error;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (file_name != NULL);

	fp = gnumeric_fopen_error_info (file_name, "w", &open_error);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, open_error);
		return;
	}

	switch (version) {
	case HTML32:
	fputs (
"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\n"
"<HTML>\n"
"<HEAD>\n\t<TITLE>Tables</TITLE>\n"
"<META http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">\n"
"\t<!-- \"G_PLUGIN_FOR_HTML\" -->\n"
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
"</HEAD>\n<BODY>\n", fp);
		break;
	case HTML40:
		fputs (
"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\"\n"
"\t\t\"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<HTML>\n"
"<HEAD>\n\t<TITLE>Tables</TITLE>\n"
"<META http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">\n"
"\t<!-- \"G_PLUGIN_FOR_HTML\" -->\n"
"<STYLE type=\"text/css\">\n"
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
"</STYLE>\n"
"</HEAD>\n<BODY>\n", fp);
		break;
	default:
	}

	sheets = workbook_sheets (wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		write_sheet (fp, (Sheet *) ptr->data, version);
	}
	g_list_free (sheets);
	if (version == HTML32 || version == HTML40) 
		fputs ("</BODY>\n</HTML>\n", fp);
	fclose (fp);
}


/*
 * write every sheet of the workbook to an html 4.0 table
 *
 */
void
html40_file_save (GnumFileSaver const *fs, IOContext *io_context,
                  WorkbookView *wb_view, const gchar *file_name)
{
	html_file_save (fs, io_context, wb_view, file_name, HTML40);
}

/*
 * write every sheet of the workbook to an html 3.2 table
 *
 */
void
html32_file_save (GnumFileSaver const *fs, IOContext *io_context,
                  WorkbookView *wb_view, const gchar *file_name)
{
	html_file_save (fs, io_context, wb_view, file_name, HTML32);
}

/*
 * write every sheet of the workbook to an html 3.2 table
 *
 */
void
html40frag_file_save (GnumFileSaver const *fs, IOContext *io_context,
                  WorkbookView *wb_view, const gchar *file_name)
{
	html_file_save (fs, io_context, wb_view, file_name, HTML40F);
}


static int
has_prefix (const char *txt, const char *prefix)
{
	return strncmp (txt, prefix, strlen (prefix)) == 0;
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

	buf[0] = buf[LINESIZE - 1] = '\0';
	if (!s)
		return NULL;
	q = buf;
	p = s;
	while (*p) {
		if (*p == '<') {
			if (!strncasecmp (p + 1, "/td>", 4)) {
				p += 5;
				break;
			}
			if (p[2] == '>') {
				if (p[1] == 'i' || p[1] == 'I')
					*flags |= HTML_ITALIC;
				else if (p[1] == 'b' || p[1] == 'B')
					*flags |= HTML_BOLD;
			}
			p = strchr (p, '>');
			if (p == NULL)
				break;
		} else if (*p == '&') {
			if (has_prefix (p, "&lt;")) {
				*q++ = '<';
				p += 3;
			} else if (has_prefix (p, "&gt;")) {
				*q++ = '>';
				p += 3;
			} else if (has_prefix (p, "&amp;")) {
				*q++ = '&';
				p += 4;
			} else if (has_prefix (p, "&apos;")) {
				*q++ = '\'';
				p += 5;
			} else if (has_prefix (p, "&quot;")) {
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
		buf = strchr (buf + 1, '<');
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
	fclose (fp);
}

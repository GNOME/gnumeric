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
#include "style-border.h"
#include <rendered-value.h>
#include "mstyle.h"
#include <errno.h>
#include <string.h>


/*
 * html_version_t:
 *
 * version selector
 *
 */
typedef enum {
	HTML40    = 0,
	HTML32    = 1,
	HTML40F   = 2,
	XHTML     = 3
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
html_get_text_color (Cell *cell, MStyle *mstyle, guint *r, guint *g, guint *b)
{
	StyleColor *textColor;

	textColor = cell_get_render_color (cell);
	if (textColor == NULL && mstyle_is_element_set (mstyle, MSTYLE_COLOR_FORE))
		textColor = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE);
	if (textColor == NULL)
		*r = *g = *b = 0;
	else {
		*r = textColor->color.red >> 8;
		*g = textColor->color.green >> 8;
		*b = textColor->color.blue >> 8;
	}
}
static void
html_get_color (MStyle *mstyle, MStyleElementType t, guint *r, guint *g, guint *b)
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
	gboolean hidden = mstyle_get_content_hidden (mstyle);
	

	if (version ==HTML32 && hidden)
	  fputs ("<!-- 'HIDDEN DATA' -->",fp);
	else
	  {

	if (mstyle != NULL) {
	        
	       
	        if (mstyle_get_font_italic (mstyle))
			fputs ("<i>", fp);
		if (mstyle_get_font_bold (mstyle))
			fputs ("<b>", fp);
		if (font_is_monospaced (mstyle))
			fputs ("<tt>", fp);
	}

	if (cell != NULL) {
		if (mstyle != NULL && version != HTML40) {
			html_get_text_color (cell, mstyle, &r, &g, &b);
			if (r > 0 || g > 0 || b > 0)
				fprintf (fp, "<font color=\"#%02X%02X%02X\">", r, g, b);
		}
		
		rendered_string = cell_get_rendered_text (cell);
		html_print_encoded (fp, rendered_string);
		g_free (rendered_string);
	}

	if (r > 0 || g > 0 || b > 0)
		fputs ("</font>", fp);
	if (mstyle != NULL) {
		if (font_is_monospaced (mstyle))
			fputs ("</tt>", fp);
		if (mstyle_get_font_bold (mstyle))
			fputs ("</b>", fp);
		if (mstyle_get_font_italic (mstyle))
			fputs ("</i>", fp);
	}}
}


/*
 * html_get_border_style :
 *
 *
 *
 */

static char *
html_get_border_style (StyleBorder *border)
{
	GString *text = g_string_new ("");
	char *result;

	switch (border->line_type) {
	case STYLE_BORDER_THIN:
		g_string_append (text, "thin solid");
		break;
	case STYLE_BORDER_MEDIUM:
		g_string_append (text, "medium solid");
		break;
	case STYLE_BORDER_DASHED:
		g_string_append (text, "thin dashed");
		break;
	case STYLE_BORDER_DOTTED:
		g_string_append (text, "thin dotted");
		break;
	case STYLE_BORDER_THICK:
		g_string_append (text, "thick solid");
		break;
	case STYLE_BORDER_DOUBLE:
		g_string_append (text, "thick double");
		break;
	case STYLE_BORDER_HAIR:
		g_string_append (text, "0.5pt solid");
		break;
	case STYLE_BORDER_MEDIUM_DASH:
		g_string_append (text, "medium dashed");
		break;
	case STYLE_BORDER_DASH_DOT:
		g_string_append (text, "thin dashed");
		break;
	case STYLE_BORDER_MEDIUM_DASH_DOT:
		g_string_append (text, "medium dashed");
		break;
	case STYLE_BORDER_DASH_DOT_DOT:
		g_string_append (text, "thin dotted");
		break;
	case STYLE_BORDER_MEDIUM_DASH_DOT_DOT:
		g_string_append (text, "medium dotted");
		break;
	case STYLE_BORDER_SLANTED_DASH_DOT:
		g_string_append (text, "thin dashed");
		break;
	default:
		break;
	}

	if (border->color) {
		guint r, g, b;
		r = border->color->color.red >> 8;
		g = border->color->color.green >> 8;
		b = border->color->color.blue >> 8;
		g_string_append_printf (text, " #%02X%02X%02X", r, g, b);
	}

	result = text->str;
	g_string_free (text, FALSE);
	return result;
}

/*
 * html_write_border_style_40 :
 *
 * @fp: file
 * @border: a non-blank border
 * @border_name:
 *
 *
 */

static void
html_write_one_border_style_40 (FILE *fp, StyleBorder *border, char const *border_name)
{
	char *text;
	text = html_get_border_style (border);
	if (text == NULL || strlen (text) == 0)
		return;
	fprintf (fp, " %s:%s;", border_name, text);
	g_free (text);
}
/*
 * html_write_border_style_40 :
 *
 * @fp: file
 * @mstyle: style
 *
 *
 */

static void
html_write_border_style_40 (FILE *fp, MStyle *mstyle)
{
	StyleBorder *border;

	border = mstyle_get_border (mstyle, MSTYLE_BORDER_TOP);
	if (!style_border_is_blank (border))
		html_write_one_border_style_40 (fp, border, "border-top");
	border = mstyle_get_border (mstyle, MSTYLE_BORDER_BOTTOM);
	if (!style_border_is_blank (border))
		html_write_one_border_style_40 (fp, border, "border-bottom");
	border = mstyle_get_border (mstyle, MSTYLE_BORDER_LEFT);
	if (!style_border_is_blank (border))
		html_write_one_border_style_40 (fp, border, "border-left");
	border = mstyle_get_border (mstyle, MSTYLE_BORDER_RIGHT);
	if (!style_border_is_blank (border))
		html_write_one_border_style_40 (fp, border, "border-right");
}

/*
 * write_cell:
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
	if (mstyle != NULL && version != HTML32 && version != HTML40 && 
	    mstyle_get_pattern (mstyle) != 0 &&
	    mstyle_is_element_set (mstyle, MSTYLE_COLOR_BACK)) {
		html_get_color (mstyle, MSTYLE_COLOR_BACK, &r, &g, &b);
		fprintf (fp, " bgcolor=\"#%02X%02X%02X\"", r, g, b);
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
	if (version == HTML40 || version == HTML40F) {
		if (mstyle != NULL) {
			fprintf (fp, " style=\"");
			if (mstyle_get_pattern (mstyle) != 0 &&
			    mstyle_is_element_set (mstyle, MSTYLE_COLOR_BACK)) {
				html_get_color (mstyle, MSTYLE_COLOR_BACK, &r, &g, &b);
				fprintf (fp, "background:#%02X%02X%02X;", r, g, b);
			}
			if (cell != NULL) {
				gint size = (int) (mstyle_get_font_size (mstyle) + 0.5);
				fprintf (fp, " font-size:%ipt;", size);
				html_get_text_color (cell, mstyle, &r, &g, &b);
				if (r > 0 || g > 0 || b > 0)
					fprintf (fp, " color:#%02X%02X%02X;", r, g, b);
				if(mstyle_get_content_hidden(mstyle))
				  fputs(" visibility:hidden;",fp);
			}
			
			html_write_border_style_40 (fp, mstyle);
			fprintf (fp, "\"");
		}
	}
	fprintf (fp, ">");
	html_write_cell_content (fp, cell, mstyle, version);
	fputs ("</td>\n", fp);
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
	if (ri->needs_respan)
		row_calc_spans ((ColRowInfo *) ri, sheet);

	for (col = range->start.col; col <= range->end.col; col++) {
		CellSpanInfo const *the_span;
		Range const *merge_range;
		CellPos pos;
		pos.col = col;
		pos.row = row;

		/* Is this a span */
		the_span = row_span_get (ri, col);
		if (the_span != NULL) {
			fprintf (fp, "<td colspan=\"%i\" ", the_span->right - col + 1);
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
			fprintf (fp, "<td colspan=\"%i\" rowspan=\"%i\" ",
				 merge_range->end.col - merge_range->start.col + 1,
				 merge_range->end.row - merge_range->start.row + 1);
			write_cell (fp, sheet, row, col, version);
			col = merge_range->end.col;
			continue;
		}
		fputs ("<td ", fp);
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

	switch (version) {
	case HTML40:
		fputs ("<p><table cellspacing=\"0\" cellpadding=\"3\">\n", fp);
		break;
	case XHTML:
		fputs ("<p /><table cellspacing=\"0\" cellpadding=\"3\">\n", fp);
		break;
	default:
		fputs ("<p><table border=\"1\">\n", fp);
		break;
	}
	
	fputs ("<caption>", fp);
	html_print_encoded (fp, sheet->name_unquoted);
	fputs ("</caption>\n", fp);

	total_range = sheet_get_extent (sheet, TRUE);
	for (row = total_range.start.row; row <=  total_range.end.row; row++) {
		fputs ("<tr>\n", fp);
		write_row (fp, sheet, row, &total_range, 
			   (version == XHTML) ? HTML40 : version);
		fputs ("</tr>\n", fp);
	}
	fputs ("</table>\n", fp);
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
"<html>\n"
"<head>\n\t<title>Tables</title>\n"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">\n"
"\t<!-- \"G_PLUGIN_FOR_HTML\" -->\n"
"<style><!--\n"
"tt {\n"
"\tfont-family: courier;\n"
"}\n"
"td {\n"
"\tfont-family: helvetica, sans-serif;\n"
"}\n"
"caption {\n"
"\tfont-family: helvetica, sans-serif;\n"
"\tfont-size: 14pt;\n"
"\ttext-align: left;\n"
"}\n"
"--></style>\n"
"</head>\n<body>\n", fp);
		break;
	case HTML40:
		fputs (
"<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\"\n"
"\t\t\"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
"<html>\n"
"<head>\n\t<title>Tables</title>\n"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\">\n"
"\t<!-- \"G_PLUGIN_FOR_HTML\" -->\n"
"<style type=\"text/css\">\n"
"tt {\n"
"\tfont-family: courier;\n"
"}\n"
"td {\n"
"\tfont-family: helvetica, sans-serif;\n"
"}\n"
"caption {\n"
"\tfont-family: helvetica, sans-serif;\n"
"\tfont-size: 14pt;\n"
"\ttext-align: left;\n"
"}\n"
"</style>\n"
"</head>\n<body>\n", fp);
		break;
	case XHTML  :
		fputs (
"<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>\n"
"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n"
"\t\t\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
"<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n"
"<head>\n\t<title>Tables</title>\n"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=iso-8859-1\" />\n"
"\t<!-- \"G_PLUGIN_FOR_HTML\" -->\n"
"<style type=\"text/css\">\n"
"tt {\n"
"\tfont-family: courier;\n"
"}\n"
"td {\n"
"\tfont-family: helvetica, sans-serif;\n"
"}\n"
"caption {\n"
"\tfont-family: helvetica, sans-serif;\n"
"\tfont-size: 14pt;\n"
"\ttext-align: left;\n"
"}\n"
"</style>\n"
"</head>\n<body>\n", fp);
		break;
	default:
		break;
	}

	sheets = workbook_sheets (wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		write_sheet (fp, (Sheet *) ptr->data, version);
	}
	g_list_free (sheets);
	if (version == HTML32 || version == HTML40 || version == XHTML)  
		fputs ("</body>\n</html>\n", fp);
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
 * write every sheet of the workbook to an html 4.0 fragment table
 *
 */
void
html40frag_file_save (GnumFileSaver const *fs, IOContext *io_context,
                  WorkbookView *wb_view, const gchar *file_name)
{
	html_file_save (fs, io_context, wb_view, file_name, HTML40F);
}
/*
 * write every sheet of the workbook to an xhtml 1.0 table
 *
 */

void
xhtml_file_save (GnumFileSaver const *fs, IOContext *io_context,
                  WorkbookView *wb_view, const gchar *file_name)
{
	html_file_save (fs, io_context, wb_view, file_name, XHTML);
}


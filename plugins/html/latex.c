/*
 * latex.c
 *
 * Copyright (C) 1999 Rasca, Berlin
 * EMail: thron@gmx.de
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

#include <gnome.h>
#include "config.h"
#include "latex.h"
#include "font.h"

/*
 * escape special characters
 */
static int
latex_fprintf (FILE *fp, const char *s)
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
			case '>':
			case '<':
				fprintf (fp, "$%c$", *p);
				break;
			case '&':
			case '_':
			case '%':
			case '"':
			case '$':
			case '#':
				fprintf (fp, "\\%c", *p);
				break;
			default:
				fprintf (fp, "%c", *p);
				break;
		}
		p++;
	}
	return len;
}

/*
 * write every sheet of the workbook to a latex table
 */
int
html_write_wb_latex (Workbook *wb, const char *filename)
{
	FILE *fp;
	GList *sheet_list;
	Sheet *sheet;
	Cell *cell;
	Style *style;
	int row, col;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	fp = fopen (filename, "w");
	if (!fp)
		return -1;

	fprintf (fp, "\\documentstyle[umlaut,a4]{article}\n");
	fprintf (fp, "\\oddsidemargin -0.54cm\n\\textwidth 17cm\n");
	fprintf (fp, "\\parskip 1em\n");
	fprintf (fp, "\\begin{document}\n\n");
	sheet_list = workbook_sheets (wb);
	while (sheet_list) {
		sheet = sheet_list->data;
		fprintf (fp, "%s\n\n", sheet->name);
		fprintf (fp, "\\begin{tabular}{|");
		for (col = 0; col < sheet->cols.max_used; col++) {
			fprintf (fp, "l|");
		}
		fprintf (fp, "}\\hline\n");

		for (row = 0; row <= sheet->rows.max_used; row++) {
			for (col = 0; col <= sheet->cols.max_used; col++) {
				cell = sheet_cell_get (sheet, col, row);
				if (!cell) {
					if (col)
						fprintf (fp, "\t&\n");
					else
						fprintf (fp, "\t\n");
				} else {
					style = cell->style;
					if (!style) {
						/* is this case posible? */
						fprintf (fp, "\t&");
						latex_fprintf (fp, cell->text->str);
						fprintf (fp, "&\n");
					} else {
						if (col != 0)
							fprintf (fp, "\t&");
						else
							fprintf (fp, "\t ");
						if (style->halign & HALIGN_RIGHT)
							fprintf (fp, "\\hfill ");
						if (style->halign & HALIGN_CENTER)
							fprintf (fp, "{\\centering ");	/* doesn't work */
						if (style->valign & VALIGN_TOP)
							;
						if (font_is_monospaced (style))
							fprintf (fp, "{\\tt ");
						else if (font_is_sansserif (style))
							fprintf (fp, "{\\sf ");
						if (style->font->is_bold)
							fprintf (fp, "{\\bf ");
						if (style->font->is_italic)
							fprintf (fp, "{\\em ");
						latex_fprintf (fp, cell->text->str);
						if (style->font->is_italic)
							fprintf (fp, "}");
						if (style->font->is_bold)
							fprintf (fp, "}");
						if (font_is_monospaced (style))
							fprintf (fp, "}");
						else if (font_is_sansserif (style))
							fprintf (fp, "}");
						if (style->halign & HALIGN_CENTER)
							fprintf (fp, "}");
						fprintf (fp, "\n");
					}
				}
			}
			fprintf (fp, "\\\\\\hline\n");
		}
		fprintf (fp, "\\end{tabular}\n\n");
		sheet_list = sheet_list->next;
	}
	fprintf (fp, "\\end{document}");
	fclose (fp);
	return 0;	/* Q: what do we have to return here?? */
}

/*
 * write every sheet of the workbook to a latex2e table
 */
int
html_write_wb_latex2e (Workbook *wb, const char *filename)
{
	FILE *fp;
	GList *sheet_list;
	Sheet *sheet;
	Cell *cell;
	Style *style;
	int row, col;
	unsigned char r,g,b;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	fp = fopen (filename, "w");
	if (!fp)
		return -1;

	fprintf (fp, "\\documentclass[11pt]{article}\n");
	fprintf (fp, "\t\\usepackage{umlaut}\n");
	fprintf (fp, "\t\\usepackage{color}\n");
	fprintf (fp, "\t\\oddsidemargin -0.54cm\n\t\\textwidth 17cm\n");
	fprintf (fp, "\t\\parskip 1em\n");
	fprintf (fp, "\\begin{document}\n\n");
	sheet_list = workbook_sheets (wb);
	while (sheet_list) {
		sheet = sheet_list->data;
		fprintf (fp, "%s\n\n", sheet->name);
		fprintf (fp, "\\begin{tabular}{|");
		for (col = 0; col <= sheet->cols.max_used; col++) {
			fprintf (fp, "l|");
		}
		fprintf (fp, "}\\hline\n");

		for (row = 0; row <= sheet->rows.max_used; row++) {
			for (col = 0; col <= sheet->cols.max_used; col++) {
				cell = sheet_cell_get (sheet, col, row);
				if (!cell) {
					if (col)
						fprintf (fp, "\t&\n");
					else
						fprintf (fp, "\t\n");
				} else {
					style = cell->style;
					if (!style) {
						/* is this case posible? */
						fprintf (fp, "\t&");
						latex_fprintf (fp, cell->text->str);
						fprintf (fp, "&\n");
					} else {
						if (col != 0)
							fprintf (fp, "\t&");
						else
							fprintf (fp, "\t ");
						if (style->halign & HALIGN_RIGHT)
							fprintf (fp, "\\hfill ");
						if (style->halign & HALIGN_CENTER)
							fprintf (fp, "\\centering ");	/* doesn't work */
						if (style->valign & VALIGN_TOP)
							;
						r = style->fore_color->color.red >> 8;
						g = style->fore_color->color.green >> 8;
						b = style->fore_color->color.blue >> 8;
						if (r != 0 || g != 0 || b != 0)
							fprintf (fp, "{\\color[rgb]{%.2f,%.2f,%.2f} ",
								(double)r/255, (double)g/255, (double)b/255);
						if (font_is_monospaced (style))
							fprintf (fp, "{\\tt ");
						else if (font_is_sansserif (style))
							fprintf (fp, "\\textsf{");
						if (style->font->is_bold)
							fprintf (fp, "\\textbf{");
						if (style->font->is_italic)
							fprintf (fp, "{\\em ");
						latex_fprintf (fp, cell->text->str);
						if (style->font->is_italic)
							fprintf (fp, "}");
						if (style->font->is_bold)
							fprintf (fp, "}");
						if (font_is_monospaced (style))
							fprintf (fp, "}");
						else if (font_is_sansserif (style))
							fprintf (fp, "}");
						if (r != 0 || g != 0 || b != 0)
							fprintf (fp, "}");
						/* if (style->halign & HALIGN_CENTER) */
							/* fprintf (fp, "\\hfill"); */
						fprintf (fp, "\n");
					}
				}
			}
			fprintf (fp, "\\\\\\hline\n");
		}
		fprintf (fp, "\\end{tabular}\n\n");
		sheet_list = sheet_list->next;
	}
	fprintf (fp, "\\end{document}");
	fclose (fp);
	return 0;	/* Q: what do we have to return here?? */
}

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

#include <errno.h>
#include <gnome.h>
#include "config.h"
#include "workbook.h"
#include "sheet.h"
#include "style.h"
#include "latex.h"
#include "font.h"
#include "cell.h"
#include "command-context.h"
#include "rendered-value.h"

/*
 * escape special characters
 */
static void
latex_fputs (const char *p, FILE *fp)
{
	for (; *p; p++) {
		switch (*p) {
		case '>': case '<':
			fprintf (fp, "$%c$", *p);
			break;
		case '^': case '~':
			fprintf (fp, "\\%c{ }", *p);
			break;
		case '\\':
			fputs ("$\\backslash$", fp);
			break;
		case '&': case '_': case '%': case '#':
		case '{': case '}': case '$':
			fprintf (fp, "\\%c", *p);
			break;
		default:
			fputc (*p, fp);
			break;
		}
	}
}


static void
latex_fprintf_cell (FILE *fp, const Cell *cell)
{
	char *s;

	if (cell_is_blank (cell))
		return;

	/*
	 * FIXME: this is wrong for formulae.  We should do the full math
	 * thing.
	 */
	s = cell_get_rendered_text (cell);
	latex_fputs (s, fp);
	g_free (s);
}

/*
 * write every sheet of the workbook to a latex table
 */
int
html_write_wb_latex (CommandContext *context, Workbook *wb,
		     const char *filename)
{
	FILE *fp;
	GList *sheet_list;
	Sheet *sheet;
	Cell *cell;
	int row, col;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	fp = fopen (filename, "w");
	if (!fp) {
		gnumeric_error_save (context, g_strerror (errno));
		return -1;
	}

	fprintf (fp, "\\documentstyle[umlaut,a4]{article}\n");
	fprintf (fp, "\\oddsidemargin -0.54cm\n\\textwidth 17cm\n");
	fprintf (fp, "\\parskip 1em\n");
	fprintf (fp, "\\begin{document}\n\n");
	sheet_list = workbook_sheets (wb);
	while (sheet_list) {
		sheet = sheet_list->data;
		latex_fputs (sheet->name_unquoted, fp);
		fprintf (fp, "\n\n");
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
					MStyle *mstyle = cell_get_mstyle (cell);

					if (!mstyle)
						break;

					if (col != 0)
						fprintf (fp, "\t&");
					else
						fprintf (fp, "\t ");
					if (mstyle_get_align_h (mstyle) == HALIGN_RIGHT)
						fprintf (fp, "\\hfill ");
					else if (mstyle_get_align_h (mstyle) == HALIGN_CENTER ||
						 /* FIXME : center across selection is wrong */
						 mstyle_get_align_h (mstyle) == HALIGN_CENTER_ACROSS_SELECTION)
						fprintf (fp, "{\\centering ");	/* doesn't work */
					if (mstyle_get_align_v (mstyle) & VALIGN_TOP)
						;
					if (font_is_monospaced (mstyle))
						fprintf (fp, "{\\tt ");
					else if (font_is_sansserif (mstyle))
						fprintf (fp, "{\\sf ");
					if (mstyle_get_font_bold (mstyle))
						fprintf (fp, "{\\bf ");
					if (mstyle_get_font_italic (mstyle))
						fprintf (fp, "{\\em ");
					latex_fprintf_cell (fp, cell);
					if (mstyle_get_font_italic (mstyle))
						fprintf (fp, "}");
					if (mstyle_get_font_bold (mstyle))
						fprintf (fp, "}");
					if (font_is_monospaced (mstyle))
						fprintf (fp, "}");
					else if (font_is_sansserif (mstyle))
						fprintf (fp, "}");
					if (mstyle_get_align_h (mstyle) & HALIGN_CENTER)
						fprintf (fp, "}");
					fprintf (fp, "\n");
					mstyle_unref (mstyle);
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
html_write_wb_latex2e (CommandContext *context, Workbook *wb,
		       const char *filename)
{
	FILE *fp;
	GList *sheet_list;
	Sheet *sheet;
	Cell *cell;
	int row, col;
	unsigned char r,g,b;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	fp = fopen (filename, "w");
	if (!fp) {
		gnumeric_error_save (context, g_strerror (errno));
		return -1;
	}

	fprintf (fp, "\\documentclass[11pt]{article}\n");
	fprintf (fp, "\t\\usepackage{umlaut}\n");
	fprintf (fp, "\t\\usepackage{color}\n");
	fprintf (fp, "\t\\oddsidemargin -0.54cm\n\t\\textwidth 17cm\n");
	fprintf (fp, "\t\\parskip 1em\n");
	fprintf (fp, "\\begin{document}\n\n");
	sheet_list = workbook_sheets (wb);
	while (sheet_list) {
		sheet = sheet_list->data;
		latex_fputs (sheet->name_unquoted, fp);
		fprintf (fp, "\n\n");
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
					MStyle *mstyle = cell_get_mstyle (cell);
					if (!mstyle)
						break;
					if (col != 0)
						fprintf (fp, "\t&");
					else
						fprintf (fp, "\t ");
					if (mstyle_get_align_h (mstyle) & HALIGN_RIGHT)
						fprintf (fp, "\\hfill ");
					if (mstyle_get_align_h (mstyle) & HALIGN_CENTER)
						fprintf (fp, "\\centering ");	/* doesn't work */
					if (mstyle_get_align_v (mstyle) & VALIGN_TOP)
						;
					r = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE)->color.red >> 8;
					g = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE)->color.green >> 8;
					b = mstyle_get_color (mstyle, MSTYLE_COLOR_FORE)->color.blue >> 8;
					if (r != 0 || g != 0 || b != 0)
						fprintf (fp, "{\\color[rgb]{%.2f,%.2f,%.2f} ",
							 (double)r/255, (double)g/255, (double)b/255);
					if (font_is_monospaced (mstyle))
						fprintf (fp, "{\\tt ");
					else if (font_is_sansserif (mstyle))
						fprintf (fp, "\\textsf{");
					if (mstyle_get_font_bold (mstyle))
						fprintf (fp, "\\textbf{");
					if (mstyle_get_font_italic (mstyle))
						fprintf (fp, "{\\em ");
					latex_fprintf_cell (fp, cell);
					if (mstyle_get_font_italic (mstyle))
						fprintf (fp, "}");
					if (mstyle_get_font_bold (mstyle))
						fprintf (fp, "}");
					if (font_is_monospaced (mstyle))
						fprintf (fp, "}");
					else if (font_is_sansserif (mstyle))
						fprintf (fp, "}");
					if (r != 0 || g != 0 || b != 0)
						fprintf (fp, "}");
					/* if (style->halign & HALIGN_CENTER) */
					/* fprintf (fp, "\\hfill"); */
					fprintf (fp, "\n");
					mstyle_unref (mstyle);
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

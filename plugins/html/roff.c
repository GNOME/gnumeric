/*
 * roff.c
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
#include "roff.h"
#include "font.h"


/*
 * escape special characters .. needs work
 */
static int
roff_fprintf (FILE *fp, const char *s)
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
			case '.':
				fprintf (fp, "\\.");
				break;
			case '\\':
				fprintf (fp, "\\\\");
				break;
			default:
				fprintf (fp, "%c", *p);
				break;
		}
		p++;
	}
	return len;
}

/* default point size */
#define DEFSIZE 10

/*
 * write every sheet of the workbook to a roff file
 */
static int
write_wb_roff (Workbook *wb, FILE *fp)
{
	GList *sheet_list;
	Sheet *sheet;
	Cell *cell;
	Style *style;
	int row, col, fontsize, v_size;

	g_return_val_if_fail (wb != NULL, -1);

	fprintf (fp, ".\\\" TROFF file\n");
	fprintf (fp, ".fo ''%%''\n");
	sheet_list = workbook_sheets (wb);
	while (sheet_list) {
		sheet = sheet_list->data;
		fprintf (fp, "%s\n\n", sheet->name);
		fprintf (fp, ".TS H\n");
		fprintf (fp, "allbox;\n");

		for (row = 0; row < (sheet->max_row_used+1); row++) {
			if (row)
				fprintf (fp, ".T&\n");
			/* define alignments, bold etc. per cell */
			v_size = DEFSIZE;
			for (col = 0; col < (sheet->max_col_used+1); col++) {
				cell = sheet_cell_get (sheet, col, row);
				if (col)
					fprintf (fp, " ");
				if (!cell) {
					fprintf (fp, "l");
				} else {
					style = cell->style;
					if (style->halign & HALIGN_RIGHT)
						fprintf (fp, "r");
					else if (style->halign & HALIGN_CENTER)
						fprintf (fp, "c");
					else
						fprintf (fp, "l");
					if (font_is_monospaced (style)) {
						if (style->font->is_bold &&
							style->font->is_italic)
							fprintf (fp, "fCBI");
						else if (style->font->is_bold)
							fprintf (fp, "fCB");
						else if (style->font->is_italic)
							fprintf (fp, "fCI");
						else
							fprintf (fp, "fCR");
					} else if (font_is_helvetica (style)) {
						if (style->font->is_bold &&
							style->font->is_italic)
							fprintf (fp, "fHBI");
						else if (style->font->is_bold)
							fprintf (fp, "fHB");
						else if (style->font->is_italic)
							fprintf (fp, "fHI");
						else
							fprintf (fp, "fHR");
					} else {
						/* default is times */
						if (style->font->is_bold &&
							style->font->is_italic)
							fprintf (fp, "fTBI");
						else if (style->font->is_bold)
							fprintf (fp, "fTB");
						else if (style->font->is_italic)
							fprintf (fp, "fTI");
					}
					fontsize = font_get_size (style);
					if (fontsize) {
						fprintf (fp, "p%d", fontsize);
						v_size = v_size > fontsize ? v_size : fontsize;
					}
				}
			}
			fprintf (fp, ".\n");
			fprintf (fp, ".vs %.2fp\n", 2.5 + (float)v_size);
			for (col = 0; col < (sheet->max_col_used+1); col++) {
				if (col)
					fprintf (fp, "\t");
				cell = sheet_cell_get (sheet, col, row);
				if (!cell) {	/* empty cell */
					fprintf (fp, " ");
				} else {
					roff_fprintf (fp, cell->text->str);
				}
			}
			fprintf (fp, "\n");
			if (!row)
				fprintf (fp, ".TH\n");
		}
		fprintf (fp, ".TE\n\n");
		sheet_list = sheet_list->next;
	}
	return 0;	/* what do we have to return here?? */
}

/*
 * write sheets to a PS file using groff as filter
 */
int
html_write_wb_roff_ps (Workbook *wb, const char *filename)
{
	FILE *fp;
	int rc = 0;
	char *cmd;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);
	cmd = g_malloc (strlen (filename) + 64);
	if (!cmd)
		return -1;

	sprintf (cmd, "groff -me -t -Tps - > %s", filename);
	fp = popen (cmd, "w");
	rc =  write_wb_roff (wb, fp);
	pclose (fp);
	g_free (cmd);
	return (rc);
}

/*
 * write sheets to a DVI file using groff as filter
 */
int
html_write_wb_roff_dvi (Workbook *wb, const char *filename)
{
	FILE *fp;
	int rc = 0;
	char *cmd;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);
	cmd = g_malloc (strlen (filename) + 64);
	if (!cmd)
		return -1;

	sprintf (cmd, "groff -me -t -Tdvi - > %s", filename);
	fp = popen (cmd, "w");
	rc =  write_wb_roff (wb, fp);
	pclose (fp);
	g_free (cmd);
	return (rc);
}

/*
 * write sheets to a PDF file using groff and gs as filter
 */
int
html_write_wb_roff_pdf (Workbook *wb, const char *filename)
{
	FILE *fp;
	int rc = 0;
	char *cmd;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);
	cmd = g_malloc (strlen (filename) + 256);
	if (!cmd)
		return -1;

	sprintf (cmd,
		"groff -me -t -Tps - |"
		"gs -q -dNOPAUSE -dBATCH -sDEVICE=pdfwrite -sOutputFile=%s"
		" -c save pop -f -", filename);
	fp = popen (cmd, "w");
	rc =  write_wb_roff (wb, fp);
	pclose (fp);
	g_free (cmd);
	return (rc);
}

/*
 * write sheets to a roff file
 */
int
html_write_wb_roff (Workbook *wb, const char *filename)
{
	FILE *fp;
	int rc = 0;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	fp = fopen (filename, "w");
	rc =  write_wb_roff (wb, fp);
	fclose (fp);
	return (rc);
}


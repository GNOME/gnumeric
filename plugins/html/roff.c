/*
 * roff.c
 *
 * Copyright (C) 1999, 2000 Rasca, Berlin
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
#include "workbook-view.h"
#include "workbook.h"
#include "sheet.h"
#include "style.h"
#include "roff.h"
#include "font.h"
#include "cell.h"
#include "io-context.h"
#include "error-info.h"
#include "plugin-util.h"

/*
 * escape special characters .. needs work
 */
static int
roff_fprintf (FILE *fp, const Cell *cell)
{
	int len, i;
	const char *p;
	char * s;

	if (cell_is_blank (cell))
		return 0;

	s = cell_get_rendered_text (cell);
	len = strlen (s);
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
	g_free (s);
	return len;
}

/* default point size */
#define DEFSIZE 10

/*
 * write every sheet of the workbook to a roff file
 *
 * FIXME: Should roff quote sheet name (and everything else)
 */
static void
write_wb_roff (IOContext *io_context, WorkbookView *wb_view, FILE *fp)
{
	GList *sheets, *ptr;
	Cell *cell;
	int row, col, fontsize, v_size;
	Workbook *wb = wb_view_workbook (wb_view);

	g_return_if_fail (wb != NULL);

	fprintf (fp, ".\\\" TROFF file\n");
	fprintf (fp, ".fo ''%%''\n");
	sheets = workbook_sheets (wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;
		Range r = sheet_get_extent (sheet);

		fprintf (fp, "%s\n\n", sheet->name_unquoted);
		fprintf (fp, ".TS H\n");
		fprintf (fp, "allbox;\n");

		for (row = r.start.row; row <= r.end.row; row++) {
			if (row)
				fprintf (fp, ".T&\n");
			/* define alignments, bold etc. per cell */
			v_size = DEFSIZE;
			for (col = r.start.col; col <= r.end.col; col++) {
				cell = sheet_cell_get (sheet, col, row);
				if (col)
					fprintf (fp, " ");
				if (!cell) {
					fprintf (fp, "l");
				} else {
					MStyle *mstyle = cell_get_mstyle (cell);
					if (!mstyle)
						break;
					if (mstyle_get_align_h (mstyle) & HALIGN_RIGHT)
						fprintf (fp, "r");
					else if (mstyle_get_align_h (mstyle) == HALIGN_CENTER ||
						 /* FIXME : center across selection is different */
						 mstyle_get_align_h (mstyle) == HALIGN_CENTER_ACROSS_SELECTION)
						fprintf (fp, "c");
					else
						fprintf (fp, "l");
					if (font_is_monospaced (mstyle)) {
						if (mstyle_get_font_bold (mstyle) &&
						    mstyle_get_font_italic (mstyle))
							fprintf (fp, "fCBI");
						else if (mstyle_get_font_bold (mstyle))
							fprintf (fp, "fCB");
						else if (mstyle_get_font_italic (mstyle))
							fprintf (fp, "fCI");
						else
							fprintf (fp, "fCR");
					} else if (font_is_helvetica (mstyle)) {
						if (mstyle_get_font_bold (mstyle) &&
						    mstyle_get_font_italic (mstyle))
							fprintf (fp, "fHBI");
						else if (mstyle_get_font_bold (mstyle))
							fprintf (fp, "fHB");
						else if (mstyle_get_font_italic (mstyle))
							fprintf (fp, "fHI");
						else
							fprintf (fp, "fHR");
					} else {
						/* default is times */
						if (mstyle_get_font_bold (mstyle) &&
						    mstyle_get_font_italic (mstyle))
							fprintf (fp, "fTBI");
						else if (mstyle_get_font_bold (mstyle))
							fprintf (fp, "fTB");
						else if (mstyle_get_font_italic (mstyle))
							fprintf (fp, "fTI");
					}
					fontsize = mstyle_get_font_size (mstyle);
					if (fontsize) {
						fprintf (fp, "p%d", fontsize);
						v_size = v_size > fontsize ? v_size :
							fontsize;
					}
				}
			}
			fprintf (fp, ".\n");
			fprintf (fp, ".vs %.2fp\n", 2.5 + (float)v_size);
			for (col = 0; col <= sheet->cols.max_used; col++) {
				if (col)
					fprintf (fp, "\t");
				cell = sheet_cell_get (sheet, col, row);
				if (!cell) {	/* empty cell */
					fprintf (fp, " ");
				} else {
					roff_fprintf (fp, cell);
				}
			}
			fprintf (fp, "\n");
			if (!row)
				fprintf (fp, ".TH\n");
		}
		fprintf (fp, ".TE\n\n");
	}
	g_list_free (sheets);
}

/*
 * write sheets to a DVI file using groff as filter
 */
void
roff_dvi_file_save (FileSaver const *fs, IOContext *io_context,
                    WorkbookView *wb_view, const gchar *file_name)
{
	FILE *fp;
	char *cmd;

	g_return_if_fail (wb_view != NULL);
	g_return_if_fail (file_name != NULL);

	cmd = g_strdup_printf ("groff -me -t -Tdvi - > %s", file_name);
	fp = popen (cmd, "w");
	g_free (cmd);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, error_info_new_str_with_details (
		                            _("Error executing groff."),
		                            error_info_new_from_errno ()));
		return;
	}
	write_wb_roff (io_context, wb_view, fp);
	pclose (fp);
}

/*
 * write sheets to a PDF file using groff and gs as filter
 */
void
roff_pdf_file_save (FileSaver const *fs, IOContext *io_context,
                    WorkbookView *wb_view, const gchar *file_name)
{
	FILE *fp;
	char *cmd;

	g_return_if_fail (wb_view != NULL);
	g_return_if_fail (file_name != NULL);

	cmd = g_strdup_printf (
		"groff -me -t -Tps - |"
		"gs -q -dNOPAUSE -dBATCH -sDEVICE=pdfwrite -sOutputFile=%s"
		" -c save pop -f -", file_name);
	fp = popen (cmd, "w");
	g_free (cmd);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, error_info_new_str_with_details (
		                            _("Error executing groff+gs."),
		                            error_info_new_from_errno ()));
		return;
	}
	write_wb_roff (io_context, wb_view, fp);
	pclose (fp);
}

/*
 * write sheets to a roff file
 */
void
roff_file_save (FileSaver const *fs, IOContext *io_context,
                WorkbookView *wb_view, const gchar *file_name)
{
	FILE *fp;
	ErrorInfo *error;

	g_return_if_fail (wb_view != NULL);
	g_return_if_fail (file_name != NULL);

	fp = gnumeric_fopen_error_info (file_name, "w", &error);
	if (fp == NULL) {
		gnumeric_io_error_info_set (io_context, error);
		return;
	}
	write_wb_roff (io_context, wb_view, fp);
	fclose (fp);
}

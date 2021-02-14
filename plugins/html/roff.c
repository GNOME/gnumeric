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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <style.h>
#include "roff.h"
#include "font.h"
#include <cell.h>
#include <cellspan.h>
#include <goffice/goffice.h>

#include <string.h>
#include <gsf/gsf-output.h>

/*
 * escape special characters .. needs work
 */
static int
roff_fprintf (GsfOutput *output, GnmCell *cell)
{
	int len, i;
	char const *p;
	char * s;
	GnmStyle const *style;

	if (gnm_cell_is_empty (cell))
		return 0;

	style = gnm_cell_get_effective_style (cell);
	if (style != NULL && gnm_style_get_contents_hidden (style))
		return 0;

	s = gnm_cell_get_rendered_text (cell);
	len = strlen (s);
	p = s;
	for (i = 0; i < len; i++) {
		switch (*p) {
			case '.':
				gsf_output_printf (output, "\\.");
				break;
			case '\\':
				gsf_output_printf (output, "\\\\");
				break;
			default:
				gsf_output_printf (output, "%c", *p);
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
void
roff_file_save (GOFileSaver const *fs, GOIOContext *io_context,
                WorkbookView const *wb_view, GsfOutput *output)
{
	GPtrArray *sheets;
	unsigned ui;
	GnmCell *cell;
	int row, col, fontsize, v_size;
	Workbook *wb = wb_view_get_workbook (wb_view);

	g_return_if_fail (wb != NULL);

	gsf_output_printf (output, ".\\\" TROFF file\n");
	gsf_output_printf (output, ".fo ''%%''\n");
	sheets = workbook_sheets (wb);
	for (ui = 0; ui < sheets->len; ui++) {
		Sheet *sheet = g_ptr_array_index (sheets, ui);
		GnmRange r = sheet_get_extent (sheet, FALSE, TRUE);

		gsf_output_printf (output, "%s\n\n", sheet->name_unquoted);
		gsf_output_printf (output, ".TS H\n");
		gsf_output_printf (output, "allbox;\n");

		for (row = r.start.row; row <= r.end.row; row++) {
			ColRowInfo const * ri;
			ri = sheet_row_get_info (sheet, row);
			if (ri->needs_respan)
				row_calc_spans ((ColRowInfo *) ri, row, sheet);

			if (row > r.start.row)
				gsf_output_printf (output, ".T&\n");
			/* define alignments, bold etc. per cell */
			v_size = DEFSIZE;
			for (col = r.start.col; col <= r.end.col; col++) {
				cell = sheet_cell_get (sheet, col, row);
				if (col > r.start.col)
					gsf_output_printf (output, " ");
				if (!cell) {
					gsf_output_printf (output, "l");
				} else {
					GnmStyle const *style = gnm_cell_get_effective_style (cell);
					if (!style)
						break;
					if (gnm_style_get_align_h (style) & GNM_HALIGN_RIGHT)
						gsf_output_printf (output, "r");
					else if (gnm_style_get_align_h (style) == GNM_HALIGN_CENTER ||
						 /* FIXME : center across selection is different */
						 gnm_style_get_align_h (style) == GNM_HALIGN_CENTER_ACROSS_SELECTION ||
						 gnm_style_get_align_h (style) == GNM_HALIGN_DISTRIBUTED)
						gsf_output_printf (output, "c");
					else
						gsf_output_printf (output, "l");
					if (font_is_monospaced (style)) {
						if (gnm_style_get_font_bold (style) &&
						    gnm_style_get_font_italic (style))
							gsf_output_printf (output, "fCBI");
						else if (gnm_style_get_font_bold (style))
							gsf_output_printf (output, "fCB");
						else if (gnm_style_get_font_italic (style))
							gsf_output_printf (output, "fCI");
						else
							gsf_output_printf (output, "fCR");
					} else if (font_is_helvetica (style)) {
						if (gnm_style_get_font_bold (style) &&
						    gnm_style_get_font_italic (style))
							gsf_output_printf (output, "fHBI");
						else if (gnm_style_get_font_bold (style))
							gsf_output_printf (output, "fHB");
						else if (gnm_style_get_font_italic (style))
							gsf_output_printf (output, "fHI");
						else
							gsf_output_printf (output, "fHR");
					} else {
						/* default is times */
						if (gnm_style_get_font_bold (style) &&
						    gnm_style_get_font_italic (style))
							gsf_output_printf (output, "fTBI");
						else if (gnm_style_get_font_bold (style))
							gsf_output_printf (output, "fTB");
						else if (gnm_style_get_font_italic (style))
							gsf_output_printf (output, "fTI");
					}
					fontsize = gnm_style_get_font_size (style);
					if (fontsize) {
						gsf_output_printf (output, "p%d", fontsize);
						v_size = v_size > fontsize ? v_size :
							fontsize;
					}
				}
			}
			gsf_output_printf (output, ".\n");
			gsf_output_printf (output, ".vs %.2fp\n", 2.5 + v_size);
			for (col = r.start.col; col <= r.end.col;  col++) {
				if (col > r.start.col)
					gsf_output_printf (output, "\t");
				cell = sheet_cell_get (sheet, col, row);
				if (!cell) {	/* empty cell */
					gsf_output_printf (output, " ");
				} else {
					roff_fprintf (output, cell);
				}
			}
			gsf_output_printf (output, "\n");
			if (row == r.start.row)
				gsf_output_printf (output, ".TH\n");
		}
		gsf_output_printf (output, ".TE\n\n");
	}
	g_ptr_array_unref (sheets);
}

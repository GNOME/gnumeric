/*
 * epsf.c
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
#include "epsf.h"
#include "ps.h"
#include "font.h"

#define CELL_DIM(cell,p) \
			(cell->p->units + cell->p->margin_a_pt + cell->p->margin_b_pt)
#define CELL_WIDTH(cell) CELL_DIM(cell,col)
#define CELL_HEIGHT(cell) CELL_DIM(cell,row)

#define COL_DIM(col) \
			(col->units + col->margin_a_pt + col->margin_b_pt)
#define ROW_HEIGHT(col) COL_DIM(col)
#define COL_WIDTH(col) COL_DIM(col)

/*
 * write a cell
 */
static void
epsf_write_cell (FILE *fp, Cell *cell, float x, float y)
{
	Style *style;
	int cell_width, cell_height;
	RGB_t rgb;
	int font_size;

	if (!cell) {	/* empty cell */
		return;
	} else {
		style = cell->style;
		if (!style) {
			/* is this case posible? */
			return;
		} else {
			rgb.r = style->back_color->color.red >> 8;
			rgb.g = style->back_color->color.green >> 8;
			rgb.b = style->back_color->color.blue >> 8;
			ps_set_color (fp, &rgb);

			cell_width = CELL_WIDTH(cell);
			cell_height= CELL_HEIGHT(cell);
			ps_box_filled (fp, x, y, cell_width, cell_height);

			rgb.r = style->fore_color->color.red >> 8;
			rgb.g = style->fore_color->color.green >> 8;
			rgb.b = style->fore_color->color.blue >> 8;
			ps_set_color (fp, &rgb);
			ps_box_bordered (fp, x, y, cell_width, cell_height, 0.5);

			font_size = font_get_size (style);

			if (!font_size)
				font_size = 10;
			if (font_is_sansserif (style)) {
				if (style->font->is_bold && style->font->is_italic)
					ps_set_font (fp, HELVETICA_BOLD_OBLIQUE, font_size);
				else if (style->font->is_bold)
					ps_set_font (fp, HELVETICA_BOLD, font_size);
				else if (style->font->is_italic)
					ps_set_font (fp, HELVETICA_OBLIQUE, font_size);
				else
					ps_set_font (fp, HELVETICA, font_size);
			} else if (font_is_monospaced (style)) {
				if (style->font->is_bold && style->font->is_italic)
					ps_set_font (fp, COURIER_BOLD_ITALIC, font_size);
				else if (style->font->is_bold)
					ps_set_font (fp, COURIER_BOLD, font_size);
				else if (style->font->is_italic)
					ps_set_font (fp, COURIER_ITALIC, font_size);
				else
					ps_set_font (fp, COURIER, font_size);
			} else {
				if (style->font->is_bold && style->font->is_italic)
					ps_set_font (fp, TIMES_BOLD_ITALIC, font_size);
				else if (style->font->is_bold)
					ps_set_font (fp, TIMES_BOLD, font_size);
				else if (style->font->is_italic)
					ps_set_font (fp, TIMES_ITALIC, font_size);
				else
					ps_set_font (fp, TIMES, font_size);
			}
			if (style->halign & HALIGN_RIGHT)
				ps_text_right (fp, cell->text->str,
					x + cell_width - 2, y+2 + (font_size/3));
			else if (style->halign & HALIGN_CENTER)
				ps_text_center (fp, cell->text->str,
					x + 2, x + cell_width - 2, y+2 + (font_size/3));
			else
				ps_text_left (fp, cell->text->str, x+2, y+2 + (font_size/3));
			ps_write_raw (fp, "\n");
		}
	}
}

/*
 * write every sheet of the workbook to a html 3.2 table
 */
int
epsf_write_wb (Workbook *wb, const char *filename)
{
	FILE *fp;
	Sheet *sheet;
	Cell *cell;
	ColRowInfo *col_info, *row_info;
	int row, col;
	int bx = 57, by = 57, bh, bw;	/* offsets of bounding box about 2 cm */
	float x_pos, y_pos;
	GList *oblist;
	SheetObject *obj;
	double *pos;

	g_return_val_if_fail (wb != NULL, -1);
	g_return_val_if_fail (filename != NULL, -1);

	fp = fopen (filename, "w");
	if (!fp)
		return -1;

	sheet = workbook_get_current_sheet (wb);
	if (sheet) {
		/* EPS does not allow more than one page, so we use
		 * the current sheet as the one for the EPS..
		 */
		bh = 0;
		for (row = 0; row <= sheet->rows.max_used; row++) {
			row_info = sheet_row_get_info (sheet, row);
			bh += ROW_HEIGHT(row_info);
		}
		bw = 0;
		for (col = 0; col <= sheet->cols.max_used; col++) {
			col_info = sheet_col_get_info (sheet, col);
			bw += COL_WIDTH(col_info);
		}
		ps_init_eps (fp, bx, by, bx + bw, by + bh);

		x_pos = 0;
		y_pos = bh;
		for (row = 0; row <= sheet->rows.max_used; row++) {
			row_info = sheet_row_get_info (sheet, row);
			y_pos -= ROW_HEIGHT(row_info);
			for (col = 0; col <= sheet->cols.max_used; col++) {
				cell = sheet_cell_get (sheet, col, row);
				col_info = sheet_col_get_info (sheet, col);
				epsf_write_cell (fp, cell, x_pos, y_pos);
				x_pos += COL_WIDTH(col_info);
			}
			x_pos = 0;
		}
		oblist = sheet->objects;
		while (oblist) {
			obj = oblist->data;
			if (SHEET_OBJECT_GRAPHIC(obj)) {
				SheetObjectGraphic *sog = (SheetObjectGraphic *)obj;
				pos = obj->bbox_points->coords;
				switch (sog->type) {
					case SHEET_OBJECT_LINE:
						ps_draw_line (fp,
							pos[0], bh-pos[1], pos[2], bh - pos[3],
							sog->width);
						break;
					case SHEET_OBJECT_BOX:
						ps_box_bordered (fp,
							pos[0], bh - pos[1] - (pos[3] - pos[1]),
							pos[2]-pos[0], pos[3] - pos[1],
							sog->width);
						break;
					case SHEET_OBJECT_OVAL:
						/* needs work */
						ps_draw_ellipse (fp,
							pos[0], bh - pos[1] - (pos[3] - pos[1]),
							pos[2]-pos[0], pos[3] - pos[1],
							sog->width);
						break;
					case SHEET_OBJECT_ARROW:
						ps_draw_line (fp,
							pos[0], bh-pos[1], pos[2], bh - pos[3],
							sog->width);
						/* needs work */
						ps_draw_circle (fp,
							pos[2], bh - pos[3], 2, 0.5);
						break;

					default :
						/* Ignore the rest for now */
						break;
				}
			}
			oblist = oblist->next;
		}
	}
	ps_write_raw (fp, "showpage\n%%EOF\n");
	fclose (fp);
	return 0;	/* what do we have to return here?? */
}


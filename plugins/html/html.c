/*
 * html.c
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
#include "html.h"

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
			default:
				fprintf (fp, "%c", *p);
				break;
		}
		p++;
	}
	return len;
}

/*
 * write every sheet of the workbook to a html 3.2 table
 */
int
html_write_wb_html32 (Workbook *wb, const char *filename)
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

	fprintf (fp, "<!DOCTYPE HTML PUBLIC \"-//W3C/DTD HTML 3.2/EN\">\n");
	fprintf (fp, "<HTML>\n");
	fprintf (fp, "<!-- "G_PLUGIN_FOR_HTML" -->\n");
	sheet_list = workbook_sheets (wb);
	while (sheet_list) {
		sheet = sheet_list->data;
		fprintf (fp, "<TABLE border=1>\n");

		for (row = 0; row < (sheet->max_row_used+1); row++) {
			fprintf (fp, "<TR>\n");
			for (col = 0; col < (sheet->max_col_used+1); col++) {
				cell = sheet_cell_get (sheet, col, row);
				if (!cell) {
					fprintf (fp, "\t<TD?>\n");
				} else {
					style = cell->style;
					if (!style) {
						/* is this case posible? */
						fprintf (fp, "\t<TD>");
						html_fprintf (fp, cell->text->str);
						fprintf (fp, "\n");
					} else {
						fprintf (fp, "\t<TD");
						if (style->halign & HALIGN_RIGHT)
							fprintf (fp, " align=right");
						if (style->halign & HALIGN_CENTER)
							fprintf (fp, " align=center");
						if (style->valign & VALIGN_TOP)
							fprintf (fp, " valign=top");
						r = style->back_color->color.red >> 8;
						g = style->back_color->color.green >> 8;
						b = style->back_color->color.blue >> 8;
						if (r != 255 || g != 255 || b != 255)
							fprintf (fp, " bgcolor=\"#%02X%02X%02X\"", r, g, b);
						fprintf (fp, ">");
						r = style->fore_color->color.red >> 8;
						g = style->fore_color->color.green >> 8;
						b = style->fore_color->color.blue >> 8;
						if (r != 0 || g != 0 || b != 0)
							fprintf (fp, "<FONT color=\"#%02X%02X%02X\">",
									r, g, b);
						if (style->font->hint_is_bold)
							fprintf (fp, "<B>");
						if (style->font->hint_is_italic)
							fprintf (fp, "<I>");
						html_fprintf (fp, cell->text->str);
						if (style->font->hint_is_bold)
							fprintf (fp, "</B>");
						if (style->font->hint_is_italic)
							fprintf (fp, "</I>");
						if (r != 0 || g != 0 || b != 0)
							fprintf (fp, "</FONT>");
						fprintf (fp, "\n");
					}
				}
			}
			fprintf (fp, "</TR>\n");
		}
		fprintf (fp, "</TABLE>\n<P>\n\n");
		sheet_list = sheet_list->next;
	}
	fprintf (fp, "</HTML>\n");
	fclose (fp);
	return 0;	/* what do we have to return here?? */
}

/*
 * write every sheet of the workbook to a html 4.0 table
 */
int
html_write_wb_html40 (Workbook *wb, const char *filename)
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

	fprintf (fp, "<!DOCTYPE HTML PUBLIC \"-//W3C/DTD HTML 4.0/EN\">\n");
	fprintf (fp, "<HTML>\n");
	fprintf (fp, "<!-- "G_PLUGIN_FOR_HTML" -->\n");
	sheet_list = workbook_sheets (wb);
	while (sheet_list) {
		sheet = sheet_list->data;
		fprintf (fp, "<TABLE border=1>\n");

		for (row = 0; row < (sheet->max_row_used+1); row++) {
			fprintf (fp, "<TR>\n");
			for (col = 0; col < (sheet->max_col_used+1); col++) {
				cell = sheet_cell_get (sheet, col, row);
				if (!cell) {
					fprintf (fp, "\t<TD?>\n");
				} else {
					style = cell->style;
					if (!style) {
						/* is this case posible? */
						fprintf (fp, "\t<TD>");
						html_fprintf (fp, cell->text->str);
						fprintf (fp, "\n");
					} else {
						fprintf (fp, "\t<TD");
						if (style->halign & HALIGN_RIGHT)
							fprintf (fp, " halign=right");
						if (style->halign & HALIGN_CENTER)
							fprintf (fp, " halign=center");
						if (style->valign & VALIGN_TOP)
							fprintf (fp, " valign=top");
						r = style->back_color->color.red >> 8;
						g = style->back_color->color.green >> 8;
						b = style->back_color->color.blue >> 8;
						if (r != 255 || g != 255 || b != 255)
							fprintf (fp, " bgcolor=\"#%02X%02X%02X\"", r, g, b);
						fprintf (fp, ">");
						r = style->fore_color->color.red >> 8;
						g = style->fore_color->color.green >> 8;
						b = style->fore_color->color.blue >> 8;
						if (r != 0 || g != 0 || b != 0)
							fprintf (fp, "<FONT color=\"#%02X%02X%02X\">",
									r, g, b);
						if (style->font->hint_is_bold)
							fprintf (fp, "<B>");
						if (style->font->hint_is_italic)
							fprintf (fp, "<I>");
						html_fprintf (fp, cell->text->str);
						if (style->font->hint_is_bold)
							fprintf (fp, "</B>");
						if (style->font->hint_is_italic)
							fprintf (fp, "</I>");
						if (r != 0 || g != 0 || b != 0)
							fprintf (fp, "</FONT>");
						fprintf (fp, "\n");
					}
				}
			}
			fprintf (fp, "</TR>\n");
		}
		fprintf (fp, "</TABLE>\n<P>\n\n");
		sheet_list = sheet_list->next;
	}
	fprintf (fp, "</HTML>\n");
	fclose (fp);
	return 0;	/* Q: what do we have to return here?? */
}

#define HTML_BOLD	1
#define HTML_ITALIC	2
#define HTML_RIGHT	4
#define HTML_CENTER	8

/*
 */
static char *
html_get_string (char *s, int *flags)
{
#define LINESIZE 1024
	static char buf[LINESIZE];
	char *p, *q;

	buf[0] = buf[LINESIZE-1] = '\0';
	if (!s)
		return NULL;
	q = buf;
	p = s;
	while (*p) {
		if (*p == '<') {
			if ((((*(p+1) == 'I') || (*(p+1) == 'i'))) && (*(p+2) == '>'))
				*flags |= HTML_ITALIC;
			if ((((*(p+1) == 'B') || (*(p+1) == 'b'))) && (*(p+2) == '>'))
				*flags |= HTML_BOLD;
			/* needs more work.. */
			while ((*p) && (*p != '>')) {
				p++;
			}
			if (!(*p))
				break;
		} else if (*p == '&') {
			if (strstr (p, "&lt;")) {
				*q++ = '<';
				p += 3;
			} else if (strstr (p, "&gt;")) {
				*q++ = '>';
				p += 3;
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
	*q = '\0';
	return buf;
}

/*
 * change the font of a cell to bold
 */
static void
html_cell_bold (Cell *cell)
{
	char *old_name, *new_name;
	Style *style;
	char *name[] = { "bold", "demibold", "extrabold", "heavy", NULL };
	int i;
	StyleFont *sf;

	if (!cell)
		return;
	style = cell->style;
	if (!style)
		return;
	for (i = 0; name[i]; i++) {
		old_name = style->font->font_name;
		new_name = font_change_component (old_name, 2, name[i]);
		sf = style_font_new_simple (new_name, style->font->units);
		g_free (new_name);
		if (sf) {
			cell_set_font_from_style (cell, sf);
			break;
		}
	}
}

/*
 * change the font of a cell to italic
 */
static void
html_cell_italic (Cell *cell)
{
	char *old_name, *new_name;
	Style *style;
	char *name[] = { "i", "o", NULL };
	int i;
	StyleFont *sf;

	if (!cell)
		return;
	style = cell->style;
	if (!style)
		return;
	for (i = 0; name[i]; i++) {
		old_name = style->font->font_name;
		new_name = font_change_component (old_name, 3, name[i]);
		sf = style_font_new_simple (new_name, style->font->units);
		g_free (new_name);
		if (sf) {
			cell_set_font_from_style (cell, sf);
			break;
		}
	}
}

/*
 * try at least to read back what we have written before..
 */
Workbook *
html_read (const char *filename)
{
	FILE *fp;
	Workbook *wb;
	Sheet *sheet;
	Cell *cell;
	int num, row, col, flags;
	char *p, *str;
	char name[64];
	char buf[LINESIZE];

	if (!filename)
		return NULL;

	wb = workbook_new();
	if (!wb)
		return NULL;
	workbook_set_filename (wb, filename);

	fp = fopen (filename, "r");
	if (!fp) {
		/* Q: how to free the "wb"? */
		return NULL;
	}
	sheet = NULL;
	col = 0;
	row = -1;
	num = 0;
	while (fgets (buf, LINESIZE, fp) != NULL) {
		if (strstr (buf, "<TABLE")) {
			sprintf (name, "Sheet %d", num++);
			sheet = sheet_new (wb, name);
			workbook_attach_sheet (wb, sheet);
			row = -1;
		} else if (strstr (buf, "</TABLE>")) {
			sheet = NULL;
		} else if (strstr (buf, "<TR>")) {
			row++;
			col = 0;
		} else if ((p = strstr (buf, "<TD")) != NULL) {
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
					str = html_get_string (p, &flags);
					cell = sheet_cell_fetch (sheet, col, row);
					if (str && cell) {
						/* set the attributes of the cell
						 */
						if (cell->style && cell->style->font && flags) {
							if (flags & HTML_BOLD) {
								html_cell_bold (cell);
							}
							if (flags & HTML_ITALIC) {
								html_cell_italic (cell);
							}
							if (flags & HTML_RIGHT) {
								cell_set_halign (cell, HALIGN_RIGHT);
							}
							if (flags & HTML_CENTER) {
								cell_set_halign (cell, HALIGN_CENTER);
							}
						}
						/* set the content of the cell */
						cell_set_text_simple (cell, str);
					}
				}
				col++;
			}
		}
	}
	fclose (fp);
	return wb;
}


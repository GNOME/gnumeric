/*
 * html_read.c
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

#include <errno.h>
#include <ctype.h>
#include <string.h>

#define HTML_BOLD	1
#define HTML_ITALIC	2
#define HTML_RIGHT	4
#define HTML_CENTER	8

static int
has_prefix (const char *txt, const char *prefix)
{
	return strncmp (txt, prefix, strlen (prefix)) == 0;
}

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

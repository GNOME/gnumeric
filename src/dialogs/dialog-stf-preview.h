/*
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
#ifndef GNUMERIC_DIALOG_STF_PREVIEW_H
#define GNUMERIC_DIALOG_STF_PREVIEW_H

#include <gui-gnumeric.h>
#include <stf.h>
#include <widgets/gnumeric-lazy-list.h>
#include <gtk/gtk.h>

#define LINE_DISPLAY_LIMIT 500
#define RAW_LINE_DISPLAY_LIMIT 4096
#define X_OVERFLOW_PROTECT 2048

#define COLUMN_CAPTION N_("Column %d")
#define MOUSE_SENSITIVITY 5

typedef struct {
	GtkWidget        *data_container;
	GPtrArray        *lines;
	GnumericLazyList *ll;
	GtkTreeView      *tree_view;
	int              colcount;

	int              startrow;        /* Row at which to start rendering */

	GPtrArray        *colformats;     /* Array containing the desired column formats */

	GnmDateConventions const *date_conv;
} RenderData_t;

/* This will actually draw the stuff on screen */
void               stf_preview_render                    (RenderData_t *renderdata, GPtrArray *lines);

/* These are for creation/deletion */
RenderData_t*      stf_preview_new                       (GtkWidget *data_container,
							  GnmDateConventions const *date_conv);
void               stf_preview_free                      (RenderData_t *data);

/* These are for manipulation */
void               stf_preview_set_startrow              (RenderData_t *data, int startrow);

void               stf_preview_colformats_clear          (RenderData_t *renderdata);
void               stf_preview_colformats_add            (RenderData_t *renderdata, StyleFormat *format);


GtkTreeViewColumn *stf_preview_get_column                (RenderData_t *renderdata, int col);
GtkCellRenderer   *stf_preview_get_cell_renderer         (RenderData_t *renderdata, int col);

#endif /* GNUMERIC_DIALOG_STF_PREVIEW_H */

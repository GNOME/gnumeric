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
#include <gtk/gtkwidget.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktooltips.h>

#define LINE_DISPLAY_LIMIT 500
#define COLUMN_CAPTION N_("Column %d")

typedef struct {
	GtkWidget        *data_container;
	GStringChunk     *lines_chunk;
	GPtrArray        *lines;
	GnumericLazyList *ll;
	GtkTreeView      *tree_view;
	GtkTooltips      *tooltips;

	int              colcount;
	int              startrow;        /* Row at which to start rendering */

	GPtrArray        *colformats;     /* Array containing the desired column formats */
	gboolean         ignore_formats;

	GnmDateConventions const *date_conv;
} RenderData_t;

/* These are for creation/deletion */
RenderData_t*      stf_preview_new                       (GtkWidget *data_container,
							  GnmDateConventions const *date_conv);
void               stf_preview_free                      (RenderData_t *data);

/* These are for manipulation */
void               stf_preview_set_lines                 (RenderData_t *data,
							  GStringChunk *lines_chunk,
							  GPtrArray *lines);
void               stf_preview_set_startrow              (RenderData_t *data, int startrow);

void               stf_preview_colformats_clear          (RenderData_t *renderdata);
void               stf_preview_colformats_add            (RenderData_t *renderdata, GnmStyleFormat *format);


GtkTreeViewColumn *stf_preview_get_column                (RenderData_t *renderdata, int col);
GtkCellRenderer   *stf_preview_get_cell_renderer         (RenderData_t *renderdata, int col);

#endif /* GNUMERIC_DIALOG_STF_PREVIEW_H */

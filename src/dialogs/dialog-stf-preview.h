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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#ifndef GNUMERIC_DIALOG_STF_PREVIEW_H
#define GNUMERIC_DIALOG_STF_PREVIEW_H

#include <gnumeric-fwd.h>
#include <stf.h>

#define STF_LINE_LENGTH_LIMIT 1000
#define STF_LINE_DISPLAY_LIMIT 500
#define COLUMN_CAPTION N_("Column %d")

typedef struct {
	GtkWidget        *data_container;
	GStringChunk     *lines_chunk;
	GPtrArray        *lines;
	GtkTreeView      *tree_view;

	int              colcount;
	int              startrow;        /* Row at which to start rendering */

	GPtrArray        *colformats;     /* Array containing the desired column formats */
	gboolean         ignore_formats;

	GODateConventions const *date_conv;
} RenderData_t;

/* These are for creation/deletion */
RenderData_t*      stf_preview_new                       (GtkWidget *data_container,
							  GODateConventions const *date_conv);
void               stf_preview_free                      (RenderData_t *data);

/* These are for manipulation */
void               stf_preview_set_lines                 (RenderData_t *data,
							  GStringChunk *lines_chunk,
							  GPtrArray *lines);

void               stf_preview_colformats_clear          (RenderData_t *renderdata);
void               stf_preview_colformats_add            (RenderData_t *renderdata, GOFormat *format);


GtkTreeViewColumn *stf_preview_get_column                (RenderData_t *renderdata, int col);
GtkCellRenderer   *stf_preview_get_cell_renderer         (RenderData_t *renderdata, int col);

void               stf_preview_find_column               (RenderData_t *renderdata, int x, int *pcol, int *dx);

#endif /* GNUMERIC_DIALOG_STF_PREVIEW_H */

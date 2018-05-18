/* gnm-cell-renderer-toggle.h
 *
 * Author:
 *        Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * Copyright (C) 2002  Andreas J. Guelzow <aguelzow@taliesin.ca>
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

#ifndef GNM__CELL_RENDERER_TOGGLE_H__
#define GNM__CELL_RENDERER_TOGGLE_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GNM_CELL_RENDERER_TOGGLE_TYPE		(gnm_cell_renderer_toggle_get_type ())
#define GNM_CELL_RENDERER_TOGGLE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_CELL_RENDERER_TOGGLE_TYPE, GnmCellRendererToggle))
#define GNM_IS_CELL_RENDERER_TOGGLE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_CELL_RENDERER_TOGGLE_TYPE))

typedef struct GnmCellRendererToggle_      GnmCellRendererToggle;
typedef struct GnmCellRendererToggleClass_ GnmCellRendererToggleClass;

struct GnmCellRendererToggle_
{
	GtkCellRendererToggle parent;

	GdkPixbuf *pixbuf;
};

struct GnmCellRendererToggleClass_
{
	GtkCellRendererToggleClass parent_class;
};

GType            gnm_cell_renderer_toggle_get_type (void);
GtkCellRenderer *gnm_cell_renderer_toggle_new      (void);

G_END_DECLS


#endif

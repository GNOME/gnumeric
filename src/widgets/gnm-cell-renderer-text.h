/* gnm-cell-renderer-text.h
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

#ifndef GNM__CELL_RENDERER_TEXT_H__
#define GNM__CELL_RENDERER_TEXT_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GNM_CELL_RENDERER_TEXT_TYPE	 (gnm_cell_renderer_text_get_type ())
#define GNM_CELL_RENDERER_TEXT(o)		 (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_CELL_RENDERER_TEXT_TYPE, GnmCellRendererText))
#define GNM_IS_CELL_RENDERER_TEXT(o)	 (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_CELL_RENDERER_TEXT_TYPE))

typedef struct GnmCellRendererText_      GnmCellRendererText;
typedef struct GnmCellRendererTextClass_ GnmCellRendererTextClass;

struct GnmCellRendererText_
{
  GtkCellRendererText parent;
};

struct GnmCellRendererTextClass_
{
  GtkCellRendererTextClass parent_class;
};

GType            gnm_cell_renderer_text_get_type (void);
GtkCellRenderer *gnm_cell_renderer_text_new      (void);

G_END_DECLS


#endif

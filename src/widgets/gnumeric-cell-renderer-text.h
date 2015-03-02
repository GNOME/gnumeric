/* gnumeric-cell-renderer-text.h
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GNUMERIC_CELL_RENDERER_TEXT_H__
#define __GNUMERIC_CELL_RENDERER_TEXT_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GNM_CELL_RENDERER_TEXT_TYPE	 (gnumeric_cell_renderer_text_get_type ())
#define GNM_CELL_RENDERER_TEXT(o)		 (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_CELL_RENDERER_TEXT_TYPE, GnumericCellRendererText))
#define GNM_IS_CELL_RENDERER_TEXT(o)	 (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_CELL_RENDERER_TEXT_TYPE))

typedef struct _GnumericCellRendererText      GnumericCellRendererText;
typedef struct _GnumericCellRendererTextClass GnumericCellRendererTextClass;

struct _GnumericCellRendererText
{
  GtkCellRendererText parent;
};

struct _GnumericCellRendererTextClass
{
  GtkCellRendererTextClass parent_class;
};

GType            gnumeric_cell_renderer_text_get_type (void);
GtkCellRenderer *gnumeric_cell_renderer_text_new      (void);

G_END_DECLS


#endif /* __GTK_CELL_RENDERER_TEXT_H__ */

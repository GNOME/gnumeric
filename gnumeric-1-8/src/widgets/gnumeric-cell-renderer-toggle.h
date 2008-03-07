/* gnumeric-cell-renderer-toggle.h
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __GNUMERIC_CELL_RENDERER_TOGGLE_H__
#define __GNUMERIC_CELL_RENDERER_TOGGLE_H__

#include <gtk/gtkcellrenderertoggle.h>

G_BEGIN_DECLS

#define GNUMERIC_TYPE_CELL_RENDERER_TOGGLE		(gnumeric_cell_renderer_toggle_get_type ())
#define GNUMERIC_CELL_RENDERER_TOGGLE(obj)		(GTK_CHECK_CAST ((obj), GNUMERIC_TYPE_CELL_RENDERER_TOGGLE, GnumericCellRendererToggle))
#define GNUMERIC_CELL_RENDERER_TOGGLE_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_CELL_RENDERER_TOGGLE, GnumericCellRendererToggleClass))
#define GNUMERIC_IS_CELL_RENDERER_TOGGLE(obj)		(GTK_CHECK_TYPE ((obj), GNUMERIC_TYPE_CELL_RENDERER_TOGGLE))
#define GNUMERIC_IS_CELL_RENDERER_TOGGLE_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), GNUMERIC_TYPE_CELL_RENDERER_TOGGLE))
#define GNUMERIC_CELL_RENDERER_TOGGLE_GET_CLASS(obj)   (GTK_CHECK_GET_CLASS ((obj), GNUMERIC_TYPE_CELL_RENDERER_TOGGLE, GnumericCellRendererToggleClass))

typedef struct _GnumericCellRendererToggle      GnumericCellRendererToggle;
typedef struct _GnumericCellRendererToggleClass GnumericCellRendererToggleClass;

struct _GnumericCellRendererToggle
{
	GtkCellRendererToggle parent;

	GdkPixbuf *pixbuf;
};

struct _GnumericCellRendererToggleClass
{
	GtkCellRendererToggleClass parent_class;
};

GType            gnumeric_cell_renderer_toggle_get_type (void);
GtkCellRenderer *gnumeric_cell_renderer_toggle_new      (void);

G_END_DECLS


#endif /* __GTK_CELL_RENDERER_TOGGLE_H__ */

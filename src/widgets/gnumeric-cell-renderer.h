/* gnumeric-cell-renderer.h
 * Copyright (C) 2002 Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * Most code is taken verbatim from:
 * 
 * gtkcellrenderertoggle.h
 * Copyright (C) 2000  Red Hat, Inc.,  Jonathan Blandford <jrb@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GNUMERIC_CELL_RENDERER_H__
#define __GNUMERIC_CELL_RENDERER_H__

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GNUMERIC_TYPE_CELL_RENDERER			(gnumeric_cell_renderer_get_type ())
#define GNUMERIC_CELL_RENDERER(obj)			(GTK_CHECK_CAST ((obj), GNUMERIC_TYPE_CELL_RENDERER, GnumericCellRenderer))
#define GNUMERIC_CELL_RENDERER_CLASS(klass)		(GTK_CHECK_CLASS_CAST ((klass), GNUMERIC_TYPE_CELL_RENDERER, GnumericCellRendererClass))
#define GNUMERIC_IS_CELL_RENDERER(obj)		(GTK_CHECK_TYPE ((obj), GNUMERIC_TYPE_CELL_RENDERER))
#define GNUMERIC_IS_CELL_RENDERER_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), GNUMERIC_TYPE_CELL_RENDERER))
#define GNUMERIC_CELL_RENDERER_GET_CLASS(obj)         (GTK_CHECK_GET_CLASS ((obj), GNUMERIC_TYPE_CELL_RENDERER, GnumericCellRendererClass))

typedef struct _GnumericCellRenderer GnumericCellRenderer;
typedef struct _GnumericCellRendererClass GnumericCellRendererClass;

struct _GnumericCellRenderer
{
	GtkCellRenderer parent;

	/*< private >*/
	guint active : 1;
	guint activatable : 1;
	GdkPixbuf *pixbuf;
};

struct _GnumericCellRendererClass
{
	GtkCellRendererClass parent_class;

	void (* toggled) (GnumericCellRenderer *cell_renderer,
			  const gchar                 *path);
};

GtkType          gnumeric_cell_renderer_get_type  (void);
GtkCellRenderer *gnumeric_cell_renderer_new       (void);

gboolean        gnumeric_cell_renderer_get_active (GnumericCellRenderer *renderer);
void            gnumeric_cell_renderer_set_active (GnumericCellRenderer *renderer,
						   gboolean setting);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GNUMERIC_CELL_RENDERER_H__ */

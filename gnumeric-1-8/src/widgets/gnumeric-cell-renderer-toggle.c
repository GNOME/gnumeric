/* gnumeric-cell-renderer-toggle.c
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

#include <gnumeric-config.h>
#include "gnumeric-cell-renderer-toggle.h"
#include <gsf/gsf-impl-utils.h>

static void gnumeric_cell_renderer_toggle_get_property  (GObject         *object,
							 guint            param_id,
							 GValue          *value,
							 GParamSpec      *pspec);
static void gnumeric_cell_renderer_toggle_set_property  (GObject         *object,
							 guint            param_id,
							 const GValue    *value,
							 GParamSpec      *pspec);

static void gnumeric_cell_renderer_toggle_get_size   (GtkCellRenderer            *cell,
						      GtkWidget                  *widget,
						      GdkRectangle               *cell_area,
						      gint                       *x_offset,
						      gint                       *y_offset,
						      gint                       *width,
						      gint                       *height);

static void gnumeric_cell_renderer_toggle_render (GtkCellRenderer *cell,
						  GdkWindow       *window,
						  GtkWidget       *widget,
						  GdkRectangle    *background_area,
						  GdkRectangle    *cell_area,
						  GdkRectangle    *expose_area,
						  GtkCellRendererState flags);

static void gnumeric_cell_renderer_toggle_class_init
                                      (GnumericCellRendererToggleClass *cell_toggle_class);

static GtkCellRendererToggleClass *parent_class = NULL;

enum {
	PROP_ZERO,
	PROP_PIXBUF
};

GType
gnumeric_cell_renderer_toggle_get_type (void)
{
	static GType cell_toggle_type = 0;

	if (!cell_toggle_type)
	{
		static const GTypeInfo cell_toggle_info =
			{
				sizeof (GnumericCellRendererToggleClass),
				NULL,		/* base_init */
				NULL,		/* base_finalize */
				(GClassInitFunc)gnumeric_cell_renderer_toggle_class_init,
				NULL,		/* class_finalize */
				NULL,		/* class_data */
				sizeof (GnumericCellRendererToggle),
				0,              /* n_preallocs */
				(GInstanceInitFunc) NULL,
			};

		cell_toggle_type = g_type_register_static (GTK_TYPE_CELL_RENDERER_TOGGLE,
							   "GnumericCellRendererToggle",
							   &cell_toggle_info, 0);
	}

	return cell_toggle_type;
}

static void
gnumeric_cell_renderer_toggle_class_init (GnumericCellRendererToggleClass *class)
{
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS  (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (object_class);

	object_class->get_property = gnumeric_cell_renderer_toggle_get_property;
	object_class->set_property = gnumeric_cell_renderer_toggle_set_property;

	cell_class->render = gnumeric_cell_renderer_toggle_render;
	cell_class->get_size = gnumeric_cell_renderer_toggle_get_size;

	g_object_class_install_property
		(object_class, PROP_PIXBUF,
		 g_param_spec_object ("pixbuf",
				      "Pixbuf Object",
				      "The pixbuf to render.",
				      GDK_TYPE_PIXBUF,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));
}


GtkCellRenderer *
gnumeric_cell_renderer_toggle_new (void)
{
	return GTK_CELL_RENDERER (g_object_new (GNUMERIC_TYPE_CELL_RENDERER_TOGGLE, NULL));
}

static void
gnumeric_cell_renderer_toggle_get_property (GObject     *object,
					    guint        param_id,
					    GValue      *value,
					    GParamSpec  *pspec)
{
	GnumericCellRendererToggle *celltoggle = GNUMERIC_CELL_RENDERER_TOGGLE (object);

	switch (param_id) {
	case PROP_PIXBUF:
		g_value_set_object (value,
				    celltoggle->pixbuf ? G_OBJECT (celltoggle->pixbuf) : NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}


static void
gnumeric_cell_renderer_toggle_set_property (GObject      *object,
					    guint         param_id,
					    const GValue *value,
					    GParamSpec   *pspec)
{
	GnumericCellRendererToggle *celltoggle = GNUMERIC_CELL_RENDERER_TOGGLE (object);
	GdkPixbuf *pixbuf;

	switch (param_id) {
	case PROP_PIXBUF:
		pixbuf = (GdkPixbuf*) g_value_get_object (value);
		if (pixbuf)
			g_object_ref (G_OBJECT (pixbuf));
		if (celltoggle->pixbuf)
			g_object_unref (G_OBJECT (celltoggle->pixbuf));
		celltoggle->pixbuf = pixbuf;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gnumeric_cell_renderer_toggle_get_size (GtkCellRenderer *cell,
					G_GNUC_UNUSED GtkWidget *widget,
					GdkRectangle    *cell_area,
					gint            *x_offset,
					gint            *y_offset,
					gint            *width,
					gint            *height)
{
	GnumericCellRendererToggle *cellpixbuf = (GnumericCellRendererToggle *) cell;
	gint pixbuf_width = 0;
	gint pixbuf_height = 0;
	gint calc_width;
	gint calc_height;

	if (cellpixbuf->pixbuf)
	{
		pixbuf_width = gdk_pixbuf_get_width (cellpixbuf->pixbuf);
		pixbuf_height = gdk_pixbuf_get_height (cellpixbuf->pixbuf);
	}

	calc_width = (gint) GTK_CELL_RENDERER (cellpixbuf)->xpad * 2 + pixbuf_width;
	calc_height = (gint) GTK_CELL_RENDERER (cellpixbuf)->ypad * 2 + pixbuf_height;

	if (x_offset) *x_offset = 0;
	if (y_offset) *y_offset = 0;

	if (cell_area && pixbuf_width > 0 && pixbuf_height > 0)
	{
		if (x_offset)
		{
			*x_offset = GTK_CELL_RENDERER
				(cellpixbuf)->xalign * (cell_area->width - calc_width -
							(2 * GTK_CELL_RENDERER (cellpixbuf)->xpad));
			*x_offset = MAX (*x_offset, 0) +
				GTK_CELL_RENDERER (cellpixbuf)->xpad;
		}
		if (y_offset)
		{
			*y_offset = GTK_CELL_RENDERER
				(cellpixbuf)->yalign * (cell_area->height - calc_height -
							(2 * GTK_CELL_RENDERER (cellpixbuf)->ypad));
			*y_offset = MAX (*y_offset, 0) + GTK_CELL_RENDERER (cellpixbuf)->ypad;
		}
	}

	if (calc_width)
		*width = calc_width;

	if (height)
		*height = calc_height;

}

static void
gnumeric_cell_renderer_toggle_render (GtkCellRenderer *cell,
				      GdkWindow       *window,
				      GtkWidget       *widget,
				      G_GNUC_UNUSED GdkRectangle *background_area,
				      GdkRectangle    *cell_area,
				      G_GNUC_UNUSED GdkRectangle *expose_area,
				      G_GNUC_UNUSED GtkCellRendererState flags)
{
	GnumericCellRendererToggle *cellpixbuf = (GnumericCellRendererToggle *) cell;
	GdkPixbuf *pixbuf;
	GdkRectangle pix_rect;
	GdkRectangle draw_rect;

	pixbuf = cellpixbuf->pixbuf;

	if (!pixbuf)
		return;

	gnumeric_cell_renderer_toggle_get_size (cell, widget, cell_area,
						&pix_rect.x,
						&pix_rect.y,
						&pix_rect.width,
						&pix_rect.height);

	pix_rect.x += cell_area->x;
	pix_rect.y += cell_area->y;
	pix_rect.width -= cell->xpad * 2;
	pix_rect.height -= cell->ypad * 2;

	if (gdk_rectangle_intersect (cell_area, &pix_rect, &draw_rect))
		gdk_draw_pixbuf (window, NULL, pixbuf,
				 /* pixbuf 0, 0 is at pix_rect.x, pix_rect.y */
				 draw_rect.x - pix_rect.x, draw_rect.y - pix_rect.y,
				 draw_rect.x, draw_rect.y,
				 draw_rect.width, draw_rect.height,
				 GDK_RGB_DITHER_NORMAL, 0, 0);
}

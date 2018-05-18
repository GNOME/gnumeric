/* gnm-cell-renderer-toggle.c
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

#include <gnumeric-config.h>
#include <widgets/gnm-cell-renderer-toggle.h>
#include <gsf/gsf-impl-utils.h>
#include <gnm-i18n.h>

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
						      GdkRectangle const         *cell_area,
						      gint                       *x_offset,
						      gint                       *y_offset,
						      gint                       *width,
						      gint                       *height);

static void gnumeric_cell_renderer_toggle_render (GtkCellRenderer *cell,
						  cairo_t         *cr,
						  GtkWidget       *widget,
						  GdkRectangle const *background_area,
						  GdkRectangle const  *cell_area,
						  GtkCellRendererState flags);

static void gnumeric_cell_renderer_toggle_class_init
                                      (GnmCellRendererToggleClass *cell_toggle_class);

static GtkCellRendererToggleClass *parent_class = NULL;

enum {
	PROP_ZERO,
	PROP_PIXBUF
};

GType
gnm_cell_renderer_toggle_get_type (void)
{
	static GType cell_toggle_type = 0;

	if (!cell_toggle_type)
	{
		static const GTypeInfo cell_toggle_info =
			{
				sizeof (GnmCellRendererToggleClass),
				NULL,		/* base_init */
				NULL,		/* base_finalize */
				(GClassInitFunc)gnumeric_cell_renderer_toggle_class_init,
				NULL,		/* class_finalize */
				NULL,		/* class_data */
				sizeof (GnmCellRendererToggle),
				0,              /* n_preallocs */
				(GInstanceInitFunc) NULL,
			};

		cell_toggle_type = g_type_register_static (GTK_TYPE_CELL_RENDERER_TOGGLE,
							   "GnmCellRendererToggle",
							   &cell_toggle_info, 0);
	}

	return cell_toggle_type;
}

static void
gnumeric_cell_renderer_toggle_dispose (GObject *obj)
{
	GnmCellRendererToggle *celltoggle = GNM_CELL_RENDERER_TOGGLE (obj);
	g_clear_object (&celltoggle->pixbuf);
	G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gnumeric_cell_renderer_toggle_class_init (GnmCellRendererToggleClass *class)
{
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS  (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (object_class);

	object_class->get_property = gnumeric_cell_renderer_toggle_get_property;
	object_class->set_property = gnumeric_cell_renderer_toggle_set_property;
	object_class->dispose = gnumeric_cell_renderer_toggle_dispose;

	cell_class->render = gnumeric_cell_renderer_toggle_render;
	cell_class->get_size = gnumeric_cell_renderer_toggle_get_size;

	g_object_class_install_property
		(object_class, PROP_PIXBUF,
		 g_param_spec_object ("pixbuf",
				      P_("Pixbuf Object"),
				      P_("The pixbuf to render."),
				      GDK_TYPE_PIXBUF,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));
}


GtkCellRenderer *
gnm_cell_renderer_toggle_new (void)
{
	return GTK_CELL_RENDERER (g_object_new (GNM_CELL_RENDERER_TOGGLE_TYPE, NULL));
}

static void
gnumeric_cell_renderer_toggle_get_property (GObject     *object,
					    guint        param_id,
					    GValue      *value,
					    GParamSpec  *pspec)
{
	GnmCellRendererToggle *celltoggle = GNM_CELL_RENDERER_TOGGLE (object);

	switch (param_id) {
	case PROP_PIXBUF:
		g_value_set_object (value, celltoggle->pixbuf);
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
	GnmCellRendererToggle *celltoggle = GNM_CELL_RENDERER_TOGGLE (object);
	GdkPixbuf *pixbuf;

	switch (param_id) {
	case PROP_PIXBUF:
		pixbuf = (GdkPixbuf*) g_value_get_object (value);
		if (pixbuf)
			g_object_ref (pixbuf);
		if (celltoggle->pixbuf)
			g_object_unref (celltoggle->pixbuf);
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
					GdkRectangle const *cell_area,
					gint            *x_offset,
					gint            *y_offset,
					gint            *width,
					gint            *height)
{
	GnmCellRendererToggle *cellpixbuf = (GnmCellRendererToggle *) cell;
	gint pixbuf_width = 0;
	gint pixbuf_height = 0;
	gint calc_width;
	gint calc_height;
	int xpad, ypad;
	gfloat xalign, yalign;

	if (cellpixbuf->pixbuf)
	{
		pixbuf_width = gdk_pixbuf_get_width (cellpixbuf->pixbuf);
		pixbuf_height = gdk_pixbuf_get_height (cellpixbuf->pixbuf);
	}

	gtk_cell_renderer_get_padding (GTK_CELL_RENDERER (cellpixbuf),
				       &xpad, &ypad);
	gtk_cell_renderer_get_alignment (GTK_CELL_RENDERER (cellpixbuf),
					 &xalign, &yalign);
	calc_width = xpad * 2 + pixbuf_width;
	calc_height = ypad * 2 + pixbuf_height;

	if (x_offset) *x_offset = 0;
	if (y_offset) *y_offset = 0;

	if (cell_area && pixbuf_width > 0 && pixbuf_height > 0)
	{
		if (x_offset)
		{
			*x_offset = xalign * (cell_area->width - calc_width -
					      (2 * xpad));
			*x_offset = MAX (*x_offset, 0) + xpad;
		}
		if (y_offset)
		{
			*y_offset = yalign * (cell_area->height - calc_height -
					      (2 * ypad));
			*y_offset = MAX (*y_offset, 0) + ypad;
		}
	}

	if (calc_width)
		*width = calc_width;

	if (height)
		*height = calc_height;

}

static void
gnumeric_cell_renderer_toggle_render (GtkCellRenderer *cell,
				      cairo_t         *cr,
				      GtkWidget       *widget,
				      G_GNUC_UNUSED GdkRectangle const *background_area,
				      GdkRectangle const *cell_area,
				      G_GNUC_UNUSED GtkCellRendererState flags)
{
	GnmCellRendererToggle *cellpixbuf = (GnmCellRendererToggle *) cell;
	GdkPixbuf *pixbuf;
	GdkRectangle pix_rect;
	GdkRectangle draw_rect;
	int xpad, ypad;

	pixbuf = cellpixbuf->pixbuf;

	if (!pixbuf)
		return;

	gnumeric_cell_renderer_toggle_get_size (cell, widget, cell_area,
						&pix_rect.x,
						&pix_rect.y,
						&pix_rect.width,
						&pix_rect.height);

	gtk_cell_renderer_get_padding (cell, &xpad, &ypad);

	pix_rect.x += cell_area->x;
	pix_rect.y += cell_area->y;
	pix_rect.width -= xpad * 2;
	pix_rect.height -= ypad * 2;

	if (gdk_rectangle_intersect (cell_area, &pix_rect, &draw_rect)) {
		gdk_cairo_set_source_pixbuf (cr, pixbuf,
		                 draw_rect.x, draw_rect.y);
		cairo_rectangle (cr, draw_rect.x, draw_rect.y, draw_rect.width, draw_rect.height);
		cairo_fill (cr);
	}
}

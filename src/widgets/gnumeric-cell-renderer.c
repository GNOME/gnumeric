/* gnumeric-cell-renderer.c
 * Copyright (C) 2002 Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * Most code is taken verbatim from:
 * 
 * gtkcellrenderertoggle.c
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

#include <stdlib.h>
#include "gnumeric-cell-renderer.h"
#include <gtk/gtk.h>
#include <libgnome/gnome-i18n.h>
#include "gnm-marshalers.h"

static void gnumeric_cell_renderer_get_property  (GObject                    *object,
						  guint                       param_id,
						  GValue                     *value,
						  GParamSpec                 *pspec);
static void gnumeric_cell_renderer_set_property  (GObject                    *object,
						  guint                       param_id,
						  const GValue               *value,
						  GParamSpec                 *pspec);
static void gnumeric_cell_renderer_init       (GnumericCellRenderer      *celltext);
static void gnumeric_cell_renderer_class_init (GnumericCellRendererClass *class);
static void gnumeric_cell_renderer_get_size   (GtkCellRenderer            *cell,
					       GtkWidget                  *widget,
					       GdkRectangle               *cell_area,
					       gint                       *x_offset,
					       gint                       *y_offset,
					       gint                       *width,
					       gint                       *height);
static void gnumeric_cell_renderer_render     (GtkCellRenderer            *cell,
					       GdkWindow                  *window,
					       GtkWidget                  *widget,
					       GdkRectangle               *background_area,
					       GdkRectangle               *cell_area,
					       GdkRectangle               *expose_area,
					       guint                       flags);
static gboolean gnumeric_cell_renderer_activate  (GtkCellRenderer            *cell,
						  GdkEvent                   *event,
						  GtkWidget                  *widget,
						  const gchar                *path,
						  GdkRectangle               *background_area,
						  GdkRectangle               *cell_area,
						  guint                       flags);


enum {
	TOGGLED,
	LAST_SIGNAL
};

enum {
	PROP_ZERO,
	PROP_ACTIVATABLE,
	PROP_ACTIVE,
	PROP_PIXBUF
};


static guint toggle_cell_signals[LAST_SIGNAL] = { 0 };


GtkType
gnumeric_cell_renderer_get_type (void)
{
	static GtkType cell_toggle_type = 0;
	
	if (!cell_toggle_type)
	{
		static const GTypeInfo cell_toggle_info =
			{
				sizeof (GnumericCellRendererClass),
				NULL,		/* base_init */
				NULL,		/* base_finalize */
				(GClassInitFunc) gnumeric_cell_renderer_class_init,
				NULL,		/* class_finalize */
				NULL,		/* class_data */
				sizeof (GnumericCellRenderer),
				0,              /* n_preallocs */
				(GInstanceInitFunc) gnumeric_cell_renderer_init,
			};
		
		cell_toggle_type = g_type_register_static 
			(GTK_TYPE_CELL_RENDERER, "GnumericCellRenderer", &cell_toggle_info, 0);
	}
	
	return cell_toggle_type;
}

static void
gnumeric_cell_renderer_init (GnumericCellRenderer *celltoggle)
{
	celltoggle->activatable = TRUE;
	celltoggle->active = FALSE;
	celltoggle->pixbuf = NULL;
	GTK_CELL_RENDERER (celltoggle)->mode = GTK_CELL_RENDERER_MODE_ACTIVATABLE;
	GTK_CELL_RENDERER (celltoggle)->xpad = 2;
	GTK_CELL_RENDERER (celltoggle)->ypad = 0;
}

static void
gnumeric_cell_renderer_class_init (GnumericCellRendererClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS (class);
	
	object_class->get_property = gnumeric_cell_renderer_get_property;
	object_class->set_property = gnumeric_cell_renderer_set_property;
	
	cell_class->get_size = gnumeric_cell_renderer_get_size;
	cell_class->render = gnumeric_cell_renderer_render;
	cell_class->activate = gnumeric_cell_renderer_activate;
	
	g_object_class_install_property 
		(object_class, PROP_ACTIVE,
		 g_param_spec_boolean ("active",
				       _("Toggle state"),
				       _("The toggle state of the button"),
				       FALSE,
				       G_PARAM_READABLE |
				       G_PARAM_WRITABLE));
	
	g_object_class_install_property 
		(object_class, PROP_ACTIVATABLE,
		 g_param_spec_boolean ("activatable",
				       _("Activatable"),
				       _("The toggle button can be activated"),
				       TRUE,
				       G_PARAM_READABLE |
				       G_PARAM_WRITABLE));
	
	g_object_class_install_property 
		(object_class, PROP_PIXBUF,
		 g_param_spec_object ("pixbuf",
				      _("Pixbuf Object"),
				      _("The pixbuf to render."),
				      GDK_TYPE_PIXBUF,
				      G_PARAM_READABLE |
				      G_PARAM_WRITABLE));
	
	toggle_cell_signals[TOGGLED] =
		gtk_signal_new ("toggled",
				GTK_RUN_LAST,
				GTK_CLASS_TYPE (object_class),
				GTK_SIGNAL_OFFSET (GnumericCellRendererClass, toggled),
				gnm__VOID__STRING,
				GTK_TYPE_NONE, 1,
				GTK_TYPE_STRING);
}

static void
gnumeric_cell_renderer_get_property (GObject     *object,
				     guint        param_id,
				     GValue      *value,
				     GParamSpec  *pspec)
{
	GnumericCellRenderer *celltoggle = GNUMERIC_CELL_RENDERER (object);
	
	switch (param_id)
	{
	case PROP_ACTIVE:
		g_value_set_boolean (value, celltoggle->active);
		break;
	case PROP_ACTIVATABLE:
		g_value_set_boolean (value, celltoggle->activatable);
		break;
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
gnumeric_cell_renderer_set_property (GObject      *object,
				     guint         param_id,
				     const GValue *value,
				     GParamSpec   *pspec)
{
	GnumericCellRenderer *celltoggle = GNUMERIC_CELL_RENDERER (object);
	GdkPixbuf *pixbuf;
	
	switch (param_id)
	{
	case PROP_ACTIVE:
		celltoggle->active = g_value_get_boolean (value);
		g_object_notify (G_OBJECT(object), "active");
		break;
	case PROP_ACTIVATABLE:
		celltoggle->activatable = g_value_get_boolean (value);
		g_object_notify (G_OBJECT(object), "activatable");
		break;
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

/**
 * gnumeric_cell_renderer_new:
 * 
 * Creates a new #GnumericCellRenderer. Adjust rendering
 * parameters using object properties. Object properties can be set
 * globally (with g_object_set()). Also, with #GtkTreeViewColumn, you
 * can bind a property to a value in a #GtkTreeModel. For example, you
 * can bind the "active" property on the cell renderer to a boolean value
 * in the model, thus causing the check button to reflect the state of
 * the model.
 * 
 * Return value: the new cell renderer
 **/
GtkCellRenderer *
gnumeric_cell_renderer_new (void)
{
	return GTK_CELL_RENDERER (gtk_type_new (gnumeric_cell_renderer_get_type ()));
}

static void
gnumeric_cell_renderer_get_size (GtkCellRenderer *cell,
				 GtkWidget       *widget,
				 GdkRectangle    *cell_area,
				 gint            *x_offset,
				 gint            *y_offset,
				 gint            *width,
				 gint            *height)
{
	GnumericCellRenderer *cellpixbuf = (GnumericCellRenderer*) cell;
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
gnumeric_cell_renderer_render (GtkCellRenderer *cell,
			       GdkWindow       *window,
			       GtkWidget       *widget,
			       GdkRectangle    *background_area,
			       GdkRectangle    *cell_area,
			       GdkRectangle    *expose_area,
			       guint            flags)
{
	GnumericCellRenderer *cellpixbuf = (GnumericCellRenderer *) cell;
	GdkPixbuf *pixbuf;
	GdkRectangle pix_rect;
	GdkRectangle draw_rect;
	
	pixbuf = cellpixbuf->pixbuf;
	
	if (!pixbuf)
		return;
	
	gnumeric_cell_renderer_get_size (cell, widget, cell_area,
					 &pix_rect.x,
					 &pix_rect.y,
					 &pix_rect.width,
					 &pix_rect.height);
	
	pix_rect.x += cell_area->x;
	pix_rect.y += cell_area->y;
	pix_rect.width -= cell->xpad * 2;
	pix_rect.height -= cell->ypad * 2;
	
	if (gdk_rectangle_intersect (cell_area, &pix_rect, &draw_rect))
		gdk_pixbuf_render_to_drawable_alpha (pixbuf,
						     window,
						     /* pixbuf 0, 0 is at pix_rect.x, pix_rect.y */
						     draw_rect.x - pix_rect.x,
						     draw_rect.y - pix_rect.y,
						     draw_rect.x,
						     draw_rect.y,
						     draw_rect.width,
						     draw_rect.height,
						     GDK_PIXBUF_ALPHA_FULL,
						     0,
						     GDK_RGB_DITHER_NORMAL,
						     0, 0);
}

static gint
gnumeric_cell_renderer_activate (GtkCellRenderer *cell,
				 GdkEvent        *event,
				 GtkWidget       *widget,
				 const gchar     *path,
				 GdkRectangle    *background_area,
				 GdkRectangle    *cell_area,
				 guint            flags)
{
	GnumericCellRenderer *celltoggle;
	
	celltoggle = GNUMERIC_CELL_RENDERER (cell);
	if (celltoggle->activatable)
	{
		gtk_signal_emit (GTK_OBJECT (cell), toggle_cell_signals[TOGGLED], path);
		return TRUE;
	}
	
	return FALSE;
}

/**
 * gnumeric_cell_renderer_get_active:
 * @toggle: a #GnumericCellRenderer
 *
 * Returns whether the cell renderer is active. See
 * gnumeric_cell_renderer_set_active().
 *
 * Return value: %TRUE if the cell renderer is active.
 **/
gboolean
gnumeric_cell_renderer_get_active (GnumericCellRenderer *toggle)
{
	g_return_val_if_fail (GNUMERIC_IS_CELL_RENDERER (toggle), FALSE);
	
	return toggle->active;
}

/**
 * gnumeric_cell_renderer_set_active:
 * @toggle: a #GnumericCellRenderer.
 * @setting: the value to set.
 *
 * Activates or deactivates a cell renderer.
 **/
void
gnumeric_cell_renderer_set_active (GnumericCellRenderer *toggle,
				   gboolean               setting)
{
	g_return_if_fail (GNUMERIC_IS_CELL_RENDERER (toggle));
	
	g_object_set (G_OBJECT (toggle), "active", setting?TRUE:FALSE, NULL);
}

/* gnumeric-cell-renderer-text.c
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
#include "gnumeric-cell-renderer-text.h"
#include <dead-kittens.h>
#include <gui-util.h>

static void gnumeric_cell_renderer_text_class_init
    (GnumericCellRendererTextClass *cell_text_class);

static GtkCellRendererTextClass *parent_class = NULL;

GType
gnumeric_cell_renderer_text_get_type (void)
{
	static GType cell_text_type = 0;

	if (!cell_text_type)
	{
		static const GTypeInfo cell_text_info =
			{
				sizeof (GnumericCellRendererTextClass),
				NULL,		/* base_init */
				NULL,		/* base_finalize */
				(GClassInitFunc)gnumeric_cell_renderer_text_class_init,
				NULL,		/* class_finalize */
				NULL,		/* class_data */
				sizeof (GnumericCellRendererText),
				0,              /* n_preallocs */
				(GInstanceInitFunc) NULL,
			};

		cell_text_type = g_type_register_static (GTK_TYPE_CELL_RENDERER_TEXT, "GnumericCellRendererText", &cell_text_info, 0);
	}

	return cell_text_type;
}

static void
gnumeric_cell_renderer_text_render (GtkCellRenderer     *cell,
				    GdkWindow           *window,
				    GtkWidget           *widget,
				    GdkRectangle        *background_area,
				    GdkRectangle        *cell_area,
				    GdkRectangle        *expose_area,
				    GtkCellRendererState flags)

{
	GtkCellRendererText *celltext = (GtkCellRendererText *) cell;
	GtkStateType state, frame_state;
	cairo_t *cr = gdk_cairo_create (window);

	if ((flags & GTK_CELL_RENDERER_SELECTED) == GTK_CELL_RENDERER_SELECTED)	{
		frame_state = GTK_STATE_ACTIVE;
		if (gtk_widget_has_focus (widget))
			state = GTK_STATE_SELECTED;
		else
			state = GTK_STATE_ACTIVE;
	} else {
		frame_state = GTK_STATE_INSENSITIVE;
		if (gtk_widget_get_state (widget) == GTK_STATE_INSENSITIVE)
			state = GTK_STATE_INSENSITIVE;
		else
			state = GTK_STATE_NORMAL;
	}

	if (gtk_cell_renderer_text_get_background_set (celltext)) {
		int ypad;

		gnm_cell_renderer_text_copy_background_to_cairo
			(celltext, cr);

		if (expose_area) {
			gdk_cairo_rectangle (cr, background_area);
			cairo_clip (cr);
		}

		gtk_cell_renderer_get_padding (cell, NULL, &ypad);
		cairo_rectangle (cr,
				 background_area->x,
				 background_area->y + ypad,
				 background_area->width,
				 background_area->height - 2 * ypad);
		cairo_fill (cr);

		if (expose_area)
			cairo_reset_clip (cr);
	}

	if (gtk_cell_renderer_text_get_editable (celltext)) {
		GtkStyle *style = gtk_widget_get_style (widget);
		gdk_cairo_set_source_color (cr, &style->bg[frame_state]);
		gdk_cairo_rectangle (cr, background_area);
		cairo_clip (cr);
		gdk_cairo_rectangle (cr, background_area);
		cairo_stroke (cr);
	}

	cairo_destroy (cr);

	if (gtk_cell_renderer_text_get_foreground_set (celltext)) {
		GTK_CELL_RENDERER_CLASS (parent_class)->render
			(cell, window, widget, background_area,
			 cell_area, expose_area, 0);
	} else
		GTK_CELL_RENDERER_CLASS (parent_class)->render
			(cell, window, widget, background_area,
			 cell_area, expose_area, flags);
}

static void
gnumeric_cell_renderer_text_class_init (GnumericCellRendererTextClass *class)
{
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS  (class);
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	parent_class = g_type_class_peek_parent (object_class);

	cell_class->render = gnumeric_cell_renderer_text_render;
}


GtkCellRenderer *
gnumeric_cell_renderer_text_new (void)
{
	return GTK_CELL_RENDERER (g_object_new (GNUMERIC_TYPE_CELL_RENDERER_TEXT, NULL));
}

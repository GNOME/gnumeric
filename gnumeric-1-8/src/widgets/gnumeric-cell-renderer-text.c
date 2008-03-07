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
	GtkStateType state;
	GdkGC *gc = gdk_gc_new (window);

	if ((flags & GTK_CELL_RENDERER_SELECTED) == GTK_CELL_RENDERER_SELECTED)
	{
		if (GTK_WIDGET_HAS_FOCUS (widget))
			state = GTK_STATE_SELECTED;
		else
			state = GTK_STATE_ACTIVE;
	}
	else
	{
		if (GTK_WIDGET_STATE (widget) == GTK_STATE_INSENSITIVE)
			state = GTK_STATE_INSENSITIVE;
		else
			state = GTK_STATE_NORMAL;
	}

	if (celltext->background_set)
	{
		GdkColor color;

		color.red = celltext->background.red;
		color.green = celltext->background.green;
		color.blue = celltext->background.blue;

		gdk_gc_set_rgb_fg_color (gc, &color);

		if (expose_area)
			gdk_gc_set_clip_rectangle (gc, expose_area);
		gdk_draw_rectangle (window,
				    gc,
				    TRUE,
				    background_area->x,
				    background_area->y + cell->ypad,
				    background_area->width,
				    background_area->height - 2 * cell->ypad);
		if (expose_area)
			gdk_gc_set_clip_rectangle (gc, NULL);
	}

	gdk_gc_set_rgb_fg_color (gc, &widget->style->bg[
		     ((flags & GTK_CELL_RENDERER_SELECTED) == GTK_CELL_RENDERER_SELECTED) ?
					 GTK_STATE_ACTIVE : GTK_STATE_INSENSITIVE]);

	if (celltext->editable) {
		gdk_draw_rectangle (window, gc, FALSE,
				    background_area->x,
				    background_area->y,
				    background_area->width - 1,
				    background_area->height - 1);

	}

	g_object_unref (G_OBJECT (gc));

	
	if (celltext->foreground_set) {
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

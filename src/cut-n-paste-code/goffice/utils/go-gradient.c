/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gog-gradient.c :
 *
 * Copyright (C) 2003 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gnumeric-config.h>
#include <goffice/utils/go-gradient.h>

#include <src/gnumeric-i18n.h>
#include <widgets/widget-pixmap-combo.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <string.h>

GtkWidget *
go_gradient_selector (GOColor start, GOColor end)
{
	/*PixmapComboElement data for the graident combo, inline_gdkpixbuf initially set to NULL*/
	static PixmapComboElement elements[] = {
		{ NULL, NULL, GO_GRADIENT_N_TO_S },
		{ NULL, NULL, GO_GRADIENT_S_TO_N },
		{ NULL, NULL, GO_GRADIENT_N_TO_S_MIRRORED },
		{ NULL, NULL, GO_GRADIENT_S_TO_N_MIRRORED },
		{ NULL, NULL, GO_GRADIENT_W_TO_E },
		{ NULL, NULL, GO_GRADIENT_E_TO_W },
		{ NULL, NULL, GO_GRADIENT_W_TO_E_MIRRORED },
		{ NULL, NULL, GO_GRADIENT_E_TO_W_MIRRORED },
		{ NULL, NULL, GO_GRADIENT_NW_TO_SE },
		{ NULL, NULL, GO_GRADIENT_SE_TO_NW },
		{ NULL, NULL, GO_GRADIENT_NW_TO_SE_MIRRORED },
		{ NULL, NULL, GO_GRADIENT_SE_TO_NW_MIRRORED },
		{ NULL, NULL, GO_GRADIENT_NE_TO_SW },
		{ NULL, NULL, GO_GRADIENT_SW_TO_NE },
		{ NULL, NULL, GO_GRADIENT_SW_TO_NE_MIRRORED },
		{ NULL, NULL, GO_GRADIENT_NE_TO_SW_MIRRORED }	
	};
	guint i, length;
	GdkPixbuf  *pixbuf;
	GdkPixdata  pixdata;
	GtkWidget  *w;
	gpointer    data;
	ArtRender *render;
	ArtGradientLinear gradient;
	ArtGradientStop stops[2];

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 20, 20);
	for (i = 0; i < G_N_ELEMENTS (elements); i++) {
		memset (gdk_pixbuf_get_pixels (pixbuf), 0, gdk_pixbuf_get_rowstride (pixbuf) * 20);
		render = art_render_new (0, 0, 20, 20,
			gdk_pixbuf_get_pixels (pixbuf),
			gdk_pixbuf_get_rowstride (pixbuf),
			gdk_pixbuf_get_n_channels (pixbuf) - 1,
			8, ART_ALPHA_SEPARATE, NULL);
		if (elements[i].inline_gdkpixbuf != NULL)
			g_free ((gpointer) elements[i].inline_gdkpixbuf);

		go_gradient_setup (&gradient,
				   i, start, end,
				   0, 0,
				   20, 20,
				   stops);

		art_render_gradient_linear (render,
			&gradient, ART_FILTER_NEAREST);
		art_render_invoke (render);

		data = gdk_pixdata_from_pixbuf (&pixdata, pixbuf, FALSE);
		elements[i].inline_gdkpixbuf = gdk_pixdata_serialize (&pixdata, &length);
		g_free (data);
	}
	g_object_unref (pixbuf);
	w = pixmap_combo_new (elements, 4, 4);
	gtk_combo_box_set_tearable (GTK_COMBO_BOX (w), FALSE);
	return w;
}

void
go_gradient_setup (ArtGradientLinear *gradient,
		   GOGradientDirection dir, GOColor col0, GOColor col1,
		   double x0, double y0, double x1, double y1,
		   ArtGradientStop *stops)
{
	double dx = x1 - x0;
	double dy = y1 - y0;

	if (dir < 4) {
		gradient->a = 0.;
		gradient->b = 1. / (dy ? dy : 1);
		gradient->c = -(gradient->a * x0 + gradient->b * y0);
	} else if (dir < 8) {
		gradient->a = 1. / (dx ? dx : 1);
		gradient->b = 0.;
		gradient->c = -(gradient->a * x0 + gradient->b * y0);
	} else if (dir < 12) {
		double d = dx * dx + dy * dy;
		if (!d) d = 1;
		gradient->a = dx / d;
		gradient->b = dy / d;
		gradient->c = -(gradient->a * x0 + gradient->b * y0);
	} else {
		double d = dx * dx + dy * dy;
		if (!d) d = 1;
		gradient->a = -dx / d;
		gradient->b = dy / d;
		/* Note: this gradient is anchored at (x1,y0).  */
		gradient->c = -(gradient->a * x1 + gradient->b * y0);
	}

	gradient->stops = stops;
	gradient->n_stops = 2;
	stops[0].offset = 0;
	stops[1].offset = 1;

	switch (dir % 4) {
	case 0:
		gradient->spread = ART_GRADIENT_REPEAT;
		go_color_to_artpix (stops[0].color, col0);
		go_color_to_artpix (stops[1].color, col1);
		break;
	case 1:
		gradient->spread = ART_GRADIENT_REPEAT;
		go_color_to_artpix (stops[0].color, col1);
		go_color_to_artpix (stops[1].color, col0);
		break;
	case 2:
		gradient->spread = ART_GRADIENT_REFLECT;
		go_color_to_artpix (stops[0].color, col0);
		go_color_to_artpix (stops[1].color, col1);
		gradient->a *= 2;
		gradient->b *= 2;
		gradient->c *= 2;
		break;
	case 3:
		gradient->spread = ART_GRADIENT_REFLECT;
		go_color_to_artpix (stops[0].color, col1);
		go_color_to_artpix (stops[1].color, col0);
		gradient->a *= 2;
		gradient->b *= 2;
		gradient->c *= 2;
		break;
	}
}

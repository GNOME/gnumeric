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

#include <libart_lgpl/libart.h>
#include <libart_lgpl/art_render_gradient.h>
#include <src/gnumeric-i18n.h>
#include <widgets/widget-pixmap-combo.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <string.h>

gpointer
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
	ArtGradientStop stops[] = {
		{ 0., { 0, 0, 0, 0 }},
		{ 1., { 0, 0, 0, 0 }}
	};

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
		if (i < 4) {
			gradient.a = 0.;
			/*The values used in the two lines below might seem strange
			but using the more natural 1./19. and 0. give very strange results*/
			gradient.b = .998 / 19.;
			gradient.c =  0.001;
		} else if (i < 8) {
			gradient.a = 1. / 19.;
			gradient.b = 0.;
			gradient.c = 0.;
		} else if (i < 12) {
			gradient.a = 1. / 39.;
			gradient.b = 1. / 39.;
			gradient.c = 0;
		} else {
			gradient.a = -1. / 39.;
			gradient.b = 1. / 39.;
			/* Note: this gradient is anchored at (x1,y0).  */
			gradient.c = 0.5;
		}

		if (i & 1) {
			go_color_to_artpix (stops[0].color, end);
			go_color_to_artpix (stops[1].color, start);
		} else {
			go_color_to_artpix (stops[0].color, start);
			go_color_to_artpix (stops[1].color, end);
		}
		switch (i % 4) {
		case 0:
			gradient.spread = ART_GRADIENT_REPEAT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;
			break;
		case 1:
			gradient.spread = ART_GRADIENT_REPEAT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;
			break;
		case 2:
			gradient.spread = ART_GRADIENT_REFLECT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;
			gradient.a *= 39. / 19.;
			gradient.b *= 39. / 19.;
			gradient.c *= 39. / 19.;
			break;
		case 3:
			gradient.spread = ART_GRADIENT_REFLECT;
			gradient.n_stops = G_N_ELEMENTS (stops);
			gradient.stops = stops;
			gradient.a *= 2.;
			gradient.b *= 2.;
			gradient.c *= 2.;
			break;
		}
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

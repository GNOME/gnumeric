/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * color-palette.h - A color selector palette
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 * This code was extracted from widget-color-combo.c
 *   written by Miguel de Icaza (miguel@kernel.org) and
 *   Dom Lachowicz (dominicl@seas.upenn.edu). The extracted
 *   code was re-packaged into a separate object by
 *   Michael Levy (mlevy@genoscope.cns.fr)
 *   And later revised and polished by
 *   Almer S. Tigelaar (almer@gnome.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef GNUMERIC_COLOR_PALETTE_H
#define GNUMERIC_COLOR_PALETTE_H

#include <widgets/color-group.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtktooltips.h>
#include <gtk/gtktable.h>

G_BEGIN_DECLS

typedef struct _ColorNamePair ColorNamePair;
typedef struct _ColorPalette  ColorPalette;

#define COLOR_PALETTE_TYPE     (color_palette_get_type ())
#define COLOR_PALETTE(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), COLOR_PALETTE_TYPE, ColorPalette))
#define COLOR_PALETTE_CLASS(k) (G_TYPE_CHECK_CLASS_CAST(k), COLOR_PALETTE_TYPE)
#define IS_COLOR_PALETTE(obj)  (G_TYPE_CHECK_INSTANCE_TYPE((obj), COLOR_PALETTE_TYPE))

GtkType    color_palette_get_type (void);

GtkWidget *color_palette_new	   (char const *no_color_label,
				    GdkColor const *default_color,
				    ColorGroup *color_group);
GtkWidget *color_palette_make_menu (char const *no_color_label,
				    GdkColor const *default_color,
				    ColorGroup *color_group);

void	   color_palette_set_group	      (ColorPalette *P, ColorGroup *cg);
void       color_palette_set_current_color    (ColorPalette *P, GdkColor *color);
void       color_palette_set_color_to_default (ColorPalette *P);
GdkColor  *color_palette_get_current_color    (ColorPalette *P, gboolean *is_default);
GtkWidget *color_palette_get_color_picker     (ColorPalette *P);
void	   color_palette_set_allow_alpha      (ColorPalette *P, gboolean allow_alpha);

G_END_DECLS

#endif /* GNUMERIC_PALETTE_H */

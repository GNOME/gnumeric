/*
 * go-combo-color.h - A color selector combo box
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Miguel de Icaza (miguel@kernel.org)
 *   Dom Lachowicz (dominicl@seas.upenn.edu)
 *
 * Reworked and split up into a separate ColorPalette object:
 *   Michael Levy (mlevy@genoscope.cns.fr)
 *
 * And later revised and polished by:
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
#ifndef GO_COMBO_COLOR_H
#define GO_COMBO_COLOR_H

#include <glib-object.h>
#include <goffice/gui-utils/go-color-group.h>
#include <goffice/utils/go-color.h>

G_BEGIN_DECLS

#define COLOR_COMBO_TYPE	(color_combo_get_type ())
#define COLOR_COMBO(o)		(G_TYPE_CHECK_INSTANCE_CAST((o), COLOR_COMBO_TYPE, ColorCombo))
#define IS_COLOR_COMBO(o)	(G_TYPE_CHECK_INSTANCE_TYPE((o), COLOR_COMBO_TYPE))
#define COLOR_COMBO_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST(k), COLOR_COMBO_TYPE)

typedef struct _ColorCombo ColorCombo;

GtkType    color_combo_get_type   (void);
GtkWidget *color_combo_new        (GdkPixbuf   *icon,
				   char const *no_color_label,
				   GOColor default_color,
				   GOColorGroup  *color_group);
void      color_combo_set_color_to_default (ColorCombo *cc);
void      color_combo_set_color   (ColorCombo  *cc, GdkColor *color);
GOColor   color_combo_get_color   (ColorCombo  *cc, gboolean *is_default);
void      color_combo_set_gocolor (ColorCombo  *cc, GOColor   color);

void color_combo_set_allow_alpha    (ColorCombo *cc, gboolean allow_alpha);
void color_combo_set_instant_apply  (ColorCombo *cc, gboolean active);

G_END_DECLS

#endif /* GO_COMBO_COLOR_H */

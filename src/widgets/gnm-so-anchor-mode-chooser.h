/*
 * gnm-so-anchor-mode-chooser.h
 *
 * Copyright (C) 2015 Jean Bréfort <jean.brefort@normalesup.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>
 */

#ifndef GNM_SO_ANCHOR_MODE_CHOOSER_H
#define GNM_SO_ANCHOR_MODE_CHOOSER_H

#include <gnumeric.h>
#include <sheet-object.h>
#include <glib-object.h>

typedef struct _GnmSOAnchorModeChooser GnmSOAnchorModeChooser;

#define GNM_SO_ANCHOR_MODE_CHOOSER_TYPE     (gnm_so_anchor_mode_chooser_get_type ())
#define GNM_SO_ANCHOR_MODE_CHOOSER(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), GNM_SO_ANCHOR_MODE_CHOOSER_TYPE, GnmSOAnchorModeChooser))
#define GNM_IS_SO_ANCHOR_MODE_CHOOSER(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), GNM_SO_ANCHOR_MODE_CHOOSER_TYPE))
GType gnm_so_anchor_mode_chooser_get_type (void);

GtkWidget *gnm_so_anchor_mode_chooser_new (gboolean resize);
void gnm_so_anchor_mode_chooser_set_mode (GnmSOAnchorModeChooser *chooser,
                                          GnmSOAnchorMode mode);
GnmSOAnchorMode gnm_so_anchor_mode_chooser_get_mode (GnmSOAnchorModeChooser const *chooser);

#endif /* GNM_SO_ANCHOR_MODE_CHOOSER_H */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * widget-pixmap-combo.h - A pixmap selector combo box
 * Copyright 2000-2003, Ximian, Inc.
 *
 * Authors:
 *   Jody Goldberg <jody@gnome.org>
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

#ifndef GNUMERIC_WIDGET_PIXMAP_COMBO_H
#define GNUMERIC_WIDGET_PIXMAP_COMBO_H

#include <widgets/gnm-combo-box.h>
#include <gtk/gtktooltips.h>

G_BEGIN_DECLS

#define PIXMAP_COMBO_TYPE     (pixmap_combo_get_type ())
#define PIXMAP_COMBO(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), PIXMAP_COMBO_TYPE, PixmapCombo))
#define PIXMAP_COMBO_CLASS(k) (G_TYPE_CHECK_CLASS_CAST(k), PIXMAP_COMBO_TYPE)
#define IS_PIXMAP_COMBO(obj)  (G_TYPE_CHECK_INSTANCE_TYPE((obj), PIXMAP_COMBO_TYPE))

typedef struct {
	char const *untranslated_tooltip;
	guint8 const *inline_gdkpixbuf;
	int  id;
} PixmapComboElement;

typedef struct {
	GnmComboBox     combo_box;

	/* Static information */
	int cols, rows;
	int num_elements;
	GdkPixbuf **pixbufs;
	int *ids;

	/* State info */
	int last_index;

	/* Interface elements */
	GtkWidget    *combo_table, *preview_button;
	GtkWidget    *preview_pixmap;
	GtkTooltips  *tool_tip;
} PixmapCombo;

GtkType    pixmap_combo_get_type      (void);
GtkWidget *pixmap_combo_new           (PixmapComboElement const *elements,
				       int ncols, int nrows, gboolean copy_images);
void       pixmap_combo_select_pixmap (PixmapCombo *combo, int index);

typedef struct {
	GnmComboBoxClass parent_class;

	/* Signals emited by this widget */
	void (* changed) (PixmapCombo *pixmap_combo, int index);
} PixmapComboClass;

G_END_DECLS

#endif /* GNUMERIC_WIDGET_PIXMAP_COMBO_H */

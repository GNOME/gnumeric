/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * widget-pixmap-combo.c - A pixmap selector combo box
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Jody Goldberg <jgoldberg@home.com>
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

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include "gnm-combo-box.h"
#include "widget-pixmap-combo.h"
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkwindow.h>
#include <gtk/gtktable.h>
#include <gtk/gtkbutton.h>
#include <gtk/gtkimage.h>

#define PIXMAP_PREVIEW_WIDTH 15
#define PIXMAP_PREVIEW_HEIGHT 15

enum {
	CHANGED,
	LAST_SIGNAL
};

static guint pixmap_combo_signals [LAST_SIGNAL] = { 0, };

#define PARENT_TYPE GNM_COMBO_BOX_TYPE
static GtkObjectClass *pixmap_combo_parent_class;

/***************************************************************************/

static void
pixmap_combo_destroy (GtkObject *object)
{
	PixmapCombo *pc = PIXMAP_COMBO (object);
	int i;

	if (pc->tool_tip) {
		g_object_unref (pc->tool_tip);
		pc->tool_tip = NULL;
	}

	if (pc->pixbufs) {
		for (i = 0; i < pc->num_elements; i++)
			if (pc->pixbufs[i])
				g_object_unref (pc->pixbufs[i]);
		g_free (pc->pixbufs);
		pc->pixbufs = NULL;
	}

	if (pc->ids) {
		g_free (pc->ids);
		pc->ids = NULL;
	}

	(*pixmap_combo_parent_class->destroy) (object);
}

static void
cb_screen_changed (PixmapCombo *pc, GdkScreen *previous_screen)
{
	GtkWidget *w = GTK_WIDGET (pc);
	GdkScreen *screen = gtk_widget_has_screen (w)
		? gtk_widget_get_screen (w)
		: NULL;

	if (screen) {
		GtkWidget *toplevel = gtk_widget_get_toplevel (pc->combo_table);
		gtk_window_set_screen (GTK_WINDOW (toplevel), screen);
	}
}

static void
pixmap_combo_class_init (GtkObjectClass *object_class)
{
	object_class->destroy = pixmap_combo_destroy;

	pixmap_combo_parent_class = g_type_class_ref (PARENT_TYPE);

	pixmap_combo_signals [CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PixmapComboClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1, G_TYPE_INT);
}

GSF_CLASS(PixmapCombo,pixmap_combo,pixmap_combo_class_init,NULL,PARENT_TYPE)

static void
emit_change (GtkWidget *button, PixmapCombo *pc)
{
	g_return_if_fail (pc != NULL);
	g_return_if_fail (0 <= pc->last_index);
	g_return_if_fail (pc->last_index < pc->num_elements);

	g_signal_emit (pc, pixmap_combo_signals [CHANGED], 0,
		       pc->ids[pc->last_index]);
}

static void
pixmap_clicked (GtkWidget *button, PixmapCombo *pc)
{
	int index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "gal"));
	pixmap_combo_select_pixmap (pc, index);
	emit_change (button, pc);
	gnm_combo_box_popup_hide (GNM_COMBO_BOX (pc));
}

static void
pixmap_combo_construct (PixmapCombo *pc, PixmapComboElement const *elements,
			int ncols, int nrows, gboolean copy_images)
{
	int row, col;

	g_return_if_fail (pc != NULL);
	g_return_if_fail (IS_PIXMAP_COMBO (pc));

	/* Our table selector */
	pc->cols = ncols;
	pc->rows = nrows;
	pc->pixbufs = g_new0 (GdkPixbuf *, ncols * nrows);
	pc->ids = g_new0 (int, ncols * nrows);
	pc->num_elements = 0;

	pc->combo_table = gtk_table_new (ncols, nrows, 0);
	pc->tool_tip = gtk_tooltips_new ();
	g_object_ref (pc->tool_tip);
	gtk_object_sink (GTK_OBJECT (pc->tool_tip));

	for (row = 0; row < nrows; row++) {
		for (col = 0; col < ncols; col++) {
			PixmapComboElement const *element = elements + pc->num_elements;
			guint8 const *data = element->inline_gdkpixbuf;
			GtkWidget *button;
			GdkPixbuf *pixbuf;

			if (!data)
				goto nomore;

			pixbuf = gdk_pixbuf_new_from_inline (-1, data, copy_images, NULL);
			pc->ids[pc->num_elements] = element->id;
			pc->pixbufs[pc->num_elements] = pixbuf;

			button = gtk_button_new ();
			gtk_container_add (GTK_CONTAINER (button),
					   gtk_image_new_from_pixbuf (pixbuf));
			gtk_button_set_relief (GTK_BUTTON (button),
					       GTK_RELIEF_NONE);
			gtk_tooltips_set_tip (pc->tool_tip,
					      button,
					      gettext (element->untranslated_tooltip),
					      "What goes here?");

			gtk_table_attach (GTK_TABLE (pc->combo_table), button,
					  col, col + 1,
					  row + 1, row + 2,
					  GTK_FILL, GTK_FILL, 1, 1);

			g_signal_connect (button, "clicked",
					  G_CALLBACK (pixmap_clicked), pc);
			g_object_set_data (G_OBJECT (button), "gal",
					   GINT_TO_POINTER (pc->num_elements));

			pc->num_elements++;
		}
	}
 nomore:
	gtk_widget_show_all (pc->combo_table);

	pc->preview_button = gtk_button_new ();
	gtk_button_set_relief (GTK_BUTTON (pc->preview_button), GTK_RELIEF_NONE);

	pc->preview_pixmap = gtk_image_new_from_pixbuf (pc->pixbufs[0]);

	gtk_container_add (GTK_CONTAINER (pc->preview_button), GTK_WIDGET (pc->preview_pixmap));
	gtk_widget_set_size_request (GTK_WIDGET (pc->preview_pixmap), 24, 24);
	g_signal_connect (G_OBJECT (pc), "screen-changed", G_CALLBACK (cb_screen_changed), NULL);
	g_signal_connect (pc->preview_button, "clicked",
			  G_CALLBACK (emit_change), pc);

	gtk_widget_show_all (pc->preview_button);

	gnm_combo_box_construct (GNM_COMBO_BOX (pc),
				 pc->preview_button,
				 pc->combo_table);
}

GtkWidget *
pixmap_combo_new (PixmapComboElement const *elements,
		  int ncols, int nrows, gboolean copy_images)
{
	PixmapCombo *pc;

	g_return_val_if_fail (elements != NULL, NULL);
	g_return_val_if_fail (ncols > 0, NULL);
	g_return_val_if_fail (nrows > 0, NULL);

	pc = g_object_new (PIXMAP_COMBO_TYPE, NULL);

	pixmap_combo_construct (pc, elements, ncols, nrows, copy_images);

	return GTK_WIDGET (pc);
}

void
pixmap_combo_select_pixmap (PixmapCombo *pc, int index)
{
	g_return_if_fail (pc != NULL);
	g_return_if_fail (IS_PIXMAP_COMBO (pc));
	g_return_if_fail (0 <= index);
	g_return_if_fail (index < pc->num_elements);

	pc->last_index = index;

	gtk_container_remove (
		GTK_CONTAINER (pc->preview_button),
		pc->preview_pixmap);

	pc->preview_pixmap =
		gtk_image_new_from_pixbuf (pc->pixbufs[index]);
	gtk_widget_show (pc->preview_pixmap);

	gtk_container_add (
		GTK_CONTAINER (pc->preview_button), pc->preview_pixmap);
}

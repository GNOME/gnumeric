/*
 * gnm-so-anchor-mode-chooser.c
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

#include <gnumeric-config.h>
#include <widgets/gnm-so-anchor-mode-chooser.h>
#include <gsf/gsf-impl-utils.h>
#include <glib/gi18n-lib.h>

struct  _GnmSOAnchorModeChooser{
		 GtkComboBox parent;
};
typedef GtkComboBoxClass GnmSOAnchorModeChooserClass;


GtkWidget *
gnm_so_anchor_mode_chooser_new (gboolean resize)
{
	GtkWidget *widget = g_object_new (GNM_SO_ANCHOR_MODE_CHOOSER_TYPE, NULL);
	GtkListStore *model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
	GtkCellRenderer *cell = gtk_cell_renderer_text_new ();
	GtkTreeIter iter;
	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), GTK_TREE_MODEL (model));
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (widget), cell, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (widget), cell,
					                "text", 0, NULL);
	if (resize) {
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, _("Move and resize with cells"), 1, GNM_SO_ANCHOR_TWO_CELLS, -1);
	}
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("Move with cells"), 1, GNM_SO_ANCHOR_ONE_CELL, -1);
	gtk_list_store_append (model, &iter);
	gtk_list_store_set (model, &iter, 0, _("Absolute size and position"), 1, GNM_SO_ANCHOR_ABSOLUTE, -1);
	return widget;
}

void
gnm_so_anchor_mode_chooser_set_mode (GnmSOAnchorModeChooser *chooser,
                                     GnmSOAnchorMode mode)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkComboBox *combo;
	GnmSOAnchorMode cur;
	g_return_if_fail (GNM_IS_SO_ANCHOR_MODE_CHOOSER (chooser));

	combo = GTK_COMBO_BOX (chooser);
	model = gtk_combo_box_get_model (combo);
	if (!gtk_tree_model_get_iter_first (model, &iter))
		return;
	do {
		gtk_tree_model_get (model, &iter, 1, &cur, -1);
		if (cur == mode) {
			gtk_combo_box_set_active_iter (combo, &iter);
			return;
		}
	} while (gtk_tree_model_iter_next (model, &iter));
}

GnmSOAnchorMode
gnm_so_anchor_mode_chooser_get_mode (GnmSOAnchorModeChooser const *chooser)
{
	GtkTreeIter iter;
	GtkComboBox *combo;
	GnmSOAnchorMode mode;
	g_return_val_if_fail (GNM_IS_SO_ANCHOR_MODE_CHOOSER (chooser), GNM_SO_ANCHOR_ONE_CELL);

	combo = GTK_COMBO_BOX (chooser);
	if (!gtk_combo_box_get_active_iter (combo, &iter))
		return GNM_SO_ANCHOR_ONE_CELL;
	gtk_tree_model_get (gtk_combo_box_get_model (combo), &iter, 1, &mode, -1);
	return mode;
}

GSF_CLASS (GnmSOAnchorModeChooser, gnm_so_anchor_mode_chooser,
	   NULL, NULL,
	   GTK_TYPE_COMBO_BOX)

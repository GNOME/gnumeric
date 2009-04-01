/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-new-sheet.c:
 *
 * Author: Jean Brefort <jean.brefort@normalesup.org>
 *
 * (C) Copyright 2008 by Jean Brefort <jean.brefort@normalesup.org>
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
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialog-new-sheet.h"
#include <wbc-gtk.h>
#include <gui-util.h>

static void
cb_columns_changed (GtkSpinButton *btn, struct NewSheetState *state)
{
	state->columns = gtk_spin_button_get_value_as_int (btn);
}

static void
cb_rows_changed (GtkSpinButton *btn, struct NewSheetState *state)
{
	state->rows = gtk_spin_button_get_value_as_int (btn);
}

static void
cb_position_changed (GtkComboBox *box, struct NewSheetState *state)
{
	state->position = gtk_combo_box_get_active (box);
}

static void
cb_name_changed (GtkEntry *entry, struct NewSheetState *state)
{
	g_free (state->name);
	state->name = g_strdup (gtk_entry_get_text (entry));
}

static void
cb_activation_changed (GtkToggleButton *btn, struct NewSheetState *state)
{
	state->activate = gtk_toggle_button_get_active (btn);
}

GtkWidget *
dialog_new_sheet (WBCGtk *wbcg, struct NewSheetState *state)
{
	GtkWidget *w;
	GladeXML *gui;

	g_return_val_if_fail (wbcg != NULL, NULL);

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"new-sheet.glade", NULL, NULL);
	w = glade_xml_get_widget (gui, "columns");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), state->columns);
	g_signal_connect (w, "value-changed", G_CALLBACK (cb_columns_changed), state);
	w = glade_xml_get_widget (gui, "rows");
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (w), state->rows);
	g_signal_connect (w, "value-changed", G_CALLBACK (cb_rows_changed), state);
	w = glade_xml_get_widget (gui, "position");
	gtk_combo_box_set_active (GTK_COMBO_BOX (w), state->position);
	g_signal_connect (w, "changed", G_CALLBACK (cb_position_changed), state);
	w = glade_xml_get_widget (gui, "name");
	gtk_entry_set_text (GTK_ENTRY (w), state->name);
	g_signal_connect (w, "changed", G_CALLBACK (cb_name_changed), state);
	w = glade_xml_get_widget (gui, "activate");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), state->activate);
	g_signal_connect (w, "toggled", G_CALLBACK (cb_activation_changed), state);

        return glade_xml_get_widget (gui, "new_sheet");
}

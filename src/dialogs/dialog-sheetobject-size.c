/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * dialog-sheetobject-size.c:
 *
 * Author:
 *        Andreas J. Guelzow <aguelzow@pyrshep.ca>
 *
 * (c) Copyright 2009 Andreas J. Guelzow <aguelzow@pyrshep.ca>
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-control-gui-priv.h>
#include <application.h>
#include <workbook-cmd-format.h>
#include <sheet-object-widget.h>
#include <sheet-control-gui.h>

#include <glade/glade.h>
#include <gtk/gtk.h>

#define SO_SIZE_DIALOG_KEY "so-size-dialog"

typedef struct {
	GladeXML           *gui;
	WBCGtk *wbcg;
	Sheet              *sheet;
	SheetView	   *sv;
	SheetControlGUI    *scg;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *apply_button;
	GtkWidget          *cancel_button;
	GtkWidget          *wpoints;
	GtkSpinButton      *wspin;
	GtkWidget          *hpoints;
	GtkSpinButton      *hspin;
	GtkEntry           *nameentry;

	SheetObject        *so;
	SheetObjectAnchor  *old_anchor;
	SheetObjectAnchor  *active_anchor;
	double              coords[4];
	gchar              *old_name;
	gboolean            so_needs_restore;
	gboolean            so_name_changed;
} SOSizeState;

static void
cb_dialog_so_size_value_changed_update_points (GtkSpinButton *spinbutton,
					       GtkLabel *points)
{
	gint value = gtk_spin_button_get_value_as_int (spinbutton);
	double size_points = value *  72./gnm_app_display_dpi_get (FALSE);
	gchar *pts = g_strdup_printf ("%.2f",size_points);
	gtk_label_set_text (points, pts);
	g_free (pts);
}

static void
dialog_so_size_button_sensitivity (SOSizeState *state)
{
	gtk_widget_set_sensitive 
		(state->ok_button, 
		 state->so_needs_restore || state->so_name_changed);
	gtk_widget_set_sensitive 
		(state->apply_button, 
		 state->so_needs_restore || state->so_name_changed);
}

static void
cb_dialog_so_size_destroy (SOSizeState *state)
{
	if (state->so_needs_restore)
		sheet_object_set_anchor	(state->so, state->old_anchor);
	g_free (state->old_anchor);
	g_free (state->active_anchor);
	g_free (state->old_name);
	if (state->so!= NULL)
		g_object_unref (G_OBJECT (state->so));
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static void
cb_dialog_so_size_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
				    SOSizeState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_dialog_so_size_value_changed (G_GNUC_UNUSED GtkSpinButton *spinbutton,
				   SOSizeState *state)
{
	int width, height;
	int new_width, new_height;
	
	width = state->coords[2] - state->coords[0];
	height = state->coords[3] - state->coords[1];
	if (width < 0) width = - width;
	if (height < 0) height = - height;
	
	new_width = gtk_spin_button_get_value_as_int (state->wspin);
	new_height = gtk_spin_button_get_value_as_int (state->hspin);

	state->so_needs_restore = (new_width != width) || (new_height != height);

	*(state->active_anchor) = *(state->old_anchor);

	if (state->so_needs_restore) {
		gdouble new_coords[4];
		
		new_coords[0] = state->coords[0];
		new_coords[1] = state->coords[1];
		new_coords[2] = state->coords[2];
		new_coords[3] = state->coords[3];
		if (new_coords[0] < new_coords[2])
			new_coords[2] = new_coords[0] + new_width;
		else
			new_coords[0] = new_coords[2] + new_width;
		if (new_coords[1] < new_coords[3])
			new_coords[3] = new_coords[1] + new_height;
		else
			new_coords[1] = new_coords[3] + new_height;
		
		scg_object_coords_to_anchor (state->scg, new_coords, 
					     state->active_anchor);
	}

	sheet_object_set_anchor	(state->so, state->active_anchor);

	dialog_so_size_button_sensitivity (state);
}

static void
dialog_so_size_load (SOSizeState *state)
{
	g_free (state->old_anchor);
	state->old_anchor = sheet_object_anchor_dup 
		(sheet_object_get_anchor (state->so));
	scg_object_anchor_to_coords (state->scg, 
				     state->old_anchor, 
				     state->coords);
	state->so_needs_restore = FALSE;
}


static void
cb_dialog_so_size_apply_clicked (G_GNUC_UNUSED GtkWidget *button,
				   SOSizeState *state)
{
	char const *name;

	if (state->so_needs_restore) {
		sheet_object_set_anchor	(state->so, state->old_anchor);
		if (!cmd_objects_move (WORKBOOK_CONTROL (state->wbcg), 
				       g_slist_prepend (NULL, state->so),
				       g_slist_prepend 
				       (NULL, sheet_object_anchor_dup 
					(state->active_anchor)),
				       FALSE, _("Resize Object")))
			dialog_so_size_load (state);
	}

	name = gtk_entry_get_text (state->nameentry);
	if (name == NULL)
		name = "";
	if (strcmp (name, state->old_name) != 0)
		state->so_name_changed 
			= cmd_so_rename (WORKBOOK_CONTROL (state->wbcg),
					 state->so, 
					 (*name == '\0') ? NULL : name);

	dialog_so_size_button_sensitivity (state);

	return;
}

static void
cb_dialog_so_size_ok_clicked (GtkWidget *button, SOSizeState *state)
{
	cb_dialog_so_size_apply_clicked (button, state);
	if (!state->so_needs_restore)
		gtk_widget_destroy (state->dialog);
	return;
}

static gboolean
cb_dialog_so_size_name_changed (GtkEntry *entry,
				GdkEventFocus *event,
				SOSizeState *state)
{
	char const *name = gtk_entry_get_text (entry);
	if (name == NULL)
		name = "";
	state->so_name_changed 
		= (strcmp (name, state->old_name) != 0);
	dialog_so_size_button_sensitivity (state);
	return FALSE;
}


void
dialog_so_size (WBCGtk *wbcg, GObject *so)
{
	GladeXML *gui;
	SOSizeState *state;
	int width, height;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, SO_SIZE_DIALOG_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"sheetobject-size.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new (SOSizeState, 1);
	state->wbcg  = wbcg;
	state->sv = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->sheet = sv_sheet (state->sv);
	state->scg = wbcg_get_nth_scg (wbcg, state->sheet->index_in_wb);
	state->gui    = gui;
	state->dialog = glade_xml_get_widget (state->gui, "object-size");

	state->so = SHEET_OBJECT (so);
	g_object_ref (so);
	
	state->nameentry = GTK_ENTRY (glade_xml_get_widget (state->gui, "name-entry"));
	state->old_anchor = NULL;
	state->old_name = NULL;
	g_object_get (so, "name", &state->old_name, NULL);
	if (state->old_name == NULL)
		state->old_name = g_strdup ("");
	gtk_entry_set_text (state->nameentry, state->old_name);
	state->so_name_changed = FALSE;
	g_signal_connect (G_OBJECT (state->nameentry),
			  "focus-out-event",
			  G_CALLBACK (cb_dialog_so_size_name_changed),
			  state);

	
		

	state->wpoints = GTK_WIDGET (glade_xml_get_widget (state->gui, "w-pts-label"));
	state->wspin  = GTK_SPIN_BUTTON (glade_xml_get_widget (state->gui, "w-spin"));
	state->hpoints = GTK_WIDGET (glade_xml_get_widget (state->gui, "h-pts-label"));
	state->hspin  = GTK_SPIN_BUTTON (glade_xml_get_widget (state->gui, "h-spin"));

	dialog_so_size_load (state);
	state->active_anchor = sheet_object_anchor_dup (sheet_object_get_anchor (state->so));
	width = state->coords[2] - state->coords[0];
	height = state->coords[3] - state->coords[1];

	g_signal_connect (G_OBJECT (state->wspin),
			  "value-changed",
			  G_CALLBACK (cb_dialog_so_size_value_changed_update_points),
			  state->wpoints);
	g_signal_connect (G_OBJECT (state->hspin),
			  "value-changed",
			  G_CALLBACK (cb_dialog_so_size_value_changed_update_points),
			  state->hpoints);
	gtk_spin_button_set_value (state->wspin, (width < 0) ? - width : width);
	gtk_spin_button_set_value (state->hspin, (height < 0) ? - height : height);

	g_signal_connect (G_OBJECT (state->wspin),
		"value-changed",
		G_CALLBACK (cb_dialog_so_size_value_changed), state);
	g_signal_connect (G_OBJECT (state->hspin),
		"value-changed",
		G_CALLBACK (cb_dialog_so_size_value_changed), state);

	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_so_size_ok_clicked), state);
	state->apply_button = glade_xml_get_widget (state->gui, "apply_button");
	g_signal_connect (G_OBJECT (state->apply_button),
		"clicked",
		G_CALLBACK (cb_dialog_so_size_apply_clicked), state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_dialog_so_size_cancel_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_SO_SIZE);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_so_size_destroy);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       SO_SIZE_DIALOG_KEY);
	dialog_so_size_button_sensitivity (state);
	gtk_widget_show (state->dialog);
}

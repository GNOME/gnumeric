/*
 * dialog-autosave.c:
 *
 * Authors:
 *        Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
 *        Miguel de Icaza (miguel@kernel.org)
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
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
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <workbook.h>
#include <workbook-control-gui-priv.h>
#include <gui-util.h>

#include <glade/glade.h>
#include <gtk/gtktogglebutton.h>

typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
        GtkWidget *minutes_entry;
        GtkWidget *prompt_cb;
	GtkWidget *autosave_on_off;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
} autosave_t;

#define AUTOSAVE_KEY            "autosave-setup-dialog"

static void
autosave_set_sensitivity (G_GNUC_UNUSED GtkWidget *widget,
			  autosave_t *state)
{
        gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->autosave_on_off));
	gint minutes;
	gboolean minutes_err = entry_to_int (GTK_ENTRY (state->minutes_entry), &minutes, FALSE);

	gtk_widget_set_sensitive (state->minutes_entry, active);
	gtk_widget_set_sensitive (state->prompt_cb, active);

	gtk_widget_set_sensitive (state->ok_button,
				  !active || (!minutes_err && minutes > 0));
}

gboolean
dialog_autosave_prompt (WorkbookControlGUI *wbcg)
{
	gint result;
	GtkWidget *dialog;
	const char *uri;

	uri = workbook_get_uri (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));
	dialog = gtk_message_dialog_new (wbcg_toplevel (wbcg),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_YES_NO,
					 _("Do you want to save the workbook %s ?"),
					 uri);
	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	return result == GTK_RESPONSE_YES;
}



/**
 * dialog_autosave:
 * @window:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
dialog_autosave_destroy (GtkObject *w, autosave_t  *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;

	g_free (state);

	return FALSE;
}

/**
 * cb_autosave_cancel:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_autosave_cancel (G_GNUC_UNUSED GtkWidget *button,
		    autosave_t *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * cb_autosave_ok:
 * @button:
 * @state:
 **/
static void
cb_autosave_ok (G_GNUC_UNUSED GtkWidget *button, autosave_t *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->autosave_on_off))) {
		int minutes;
		gboolean minutes_err = entry_to_int (GTK_ENTRY (state->minutes_entry),
						     &minutes, TRUE);

		g_return_if_fail (!minutes_err); /* Why is ok active? */

		wbcg_autosave_set (state->wbcg, minutes,
				   gtk_toggle_button_get_active (
					   GTK_TOGGLE_BUTTON (state->prompt_cb)));
	} else
		wbcg_autosave_set (state->wbcg, 0, FALSE);
	gtk_widget_destroy (state->dialog);
}

void
dialog_autosave (WorkbookControlGUI *wbcg)
{
	GladeXML *gui;
	autosave_t *state;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, AUTOSAVE_KEY))
		return;
	gui = gnm_glade_xml_new (GNM_CMD_CONTEXT (wbcg),
		"autosave.glade", NULL, NULL);
        if (gui == NULL)
                return;


	state = g_new (autosave_t, 1);
	state->wbcg = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->gui  = gui;

	state->dialog = glade_xml_get_widget (state->gui, "AutoSave");
	state->minutes_entry = glade_xml_get_widget (state->gui, "minutes");
	state->prompt_cb = glade_xml_get_widget (state->gui, "prompt_on_off");
	state->autosave_on_off = glade_xml_get_widget (state->gui, "autosave_on_off");
	state->ok_button = glade_xml_get_widget (state->gui, "button1");
	state->cancel_button = glade_xml_get_widget (state->gui, "button2");

	if (!state->dialog || !state->minutes_entry || !state->prompt_cb ||
	    !state->autosave_on_off) {
		gnumeric_notice (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the autosave dialog."));
		g_free (state);
		return;
	}

	float_to_entry (GTK_ENTRY (state->minutes_entry), wbcg->autosave_minutes);

	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  state->minutes_entry);

	g_signal_connect (G_OBJECT (state->autosave_on_off),
		"toggled",
		G_CALLBACK (autosave_set_sensitivity), state);
	g_signal_connect (G_OBJECT (state->minutes_entry),
		"changed",
		G_CALLBACK (autosave_set_sensitivity), state);
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_autosave_ok), state);
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_autosave_cancel), state);
	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (dialog_autosave_destroy), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "button3"),
		GNUMERIC_HELP_LINK_AUTOSAVE);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->autosave_on_off),
				      wbcg->autosave);
	gtk_toggle_button_set_active ((GtkToggleButton *) state->prompt_cb,
				      wbcg->autosave_prompt);

	autosave_set_sensitivity (NULL, state);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       AUTOSAVE_KEY);
	gtk_widget_show (state->dialog);

}

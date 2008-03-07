/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <gui-util.h>

#include <goffice/app/go-doc.h>
#include <glib/gi18n-lib.h>

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
	WBCGtk  *wbcg;
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
dialog_autosave_prompt (WBCGtk *wbcg)
{
	char const *uri   = go_doc_get_uri (
		wb_control_get_doc (WORKBOOK_CONTROL (wbcg)));
	GtkWidget *dialog = gtk_message_dialog_new (wbcg_toplevel (wbcg),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_YES_NO,
					 _("Do you want to save the workbook %s ?"),
					 uri);
	gint result = gtk_dialog_run (GTK_DIALOG (dialog));

	gtk_widget_destroy (dialog);

	return result == GTK_RESPONSE_YES;
}

static void
cb_dialog_autosave_destroy (autosave_t  *state)
{
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static void
cb_autosave_cancel (G_GNUC_UNUSED GtkWidget *button,
		    autosave_t *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_autosave_ok (G_GNUC_UNUSED GtkWidget *button, autosave_t *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->autosave_on_off))) {
		gboolean prompt = gtk_toggle_button_get_active
			(GTK_TOGGLE_BUTTON (state->prompt_cb));
		int minutes, secs;
		gboolean minutes_err = entry_to_int (GTK_ENTRY (state->minutes_entry),
						     &minutes, TRUE);

		g_return_if_fail (!minutes_err); /* Why is ok active? */

		secs = 60 * MIN (minutes, G_MAXINT / 60);
		g_object_set (state->wbcg,
			      "autosave-time", secs,
			      "autosave-prompt", prompt,
			      NULL);
	} else {
		g_object_set (state->wbcg, "autosave-time", 0, NULL);
	}
	gtk_widget_destroy (state->dialog);
}

void
dialog_autosave (WBCGtk *wbcg)
{
	GladeXML *gui;
	autosave_t *state;
	int secs;
	gboolean prompt;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, AUTOSAVE_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"autosave.glade", NULL, NULL);
        if (gui == NULL)
                return;

	g_object_get (wbcg,
		      "autosave-time", &secs,
		      "autosave-prompt", &prompt,
		      NULL);

	state = g_new (autosave_t, 1);
	state->wbcg = wbcg;
	state->wb   = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	state->gui  = gui;

	state->dialog = glade_xml_get_widget (state->gui, "AutoSave");
	state->minutes_entry = glade_xml_get_widget (state->gui, "minutes");
	state->prompt_cb = glade_xml_get_widget (state->gui, "prompt_on_off");
	state->autosave_on_off = glade_xml_get_widget (state->gui, "autosave_on_off");
	state->ok_button = glade_xml_get_widget (state->gui, "button1");
	state->cancel_button = glade_xml_get_widget (state->gui, "button2");

	if (!state->dialog || !state->minutes_entry || !state->prompt_cb ||
	    !state->autosave_on_off) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the autosave dialog."));
		g_free (state);
		return;
	}

	float_to_entry (GTK_ENTRY (state->minutes_entry),
			secs / 60);

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

	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_autosave_destroy);
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "button3"),
		GNUMERIC_HELP_LINK_AUTOSAVE);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->autosave_on_off),
				      secs > 0);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->prompt_cb),
				      prompt);

	autosave_set_sensitivity (NULL, state);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       AUTOSAVE_KEY);
	gtk_widget_show (state->dialog);
}

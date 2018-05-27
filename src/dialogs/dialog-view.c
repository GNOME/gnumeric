/*
 * dialog-view.c:  New view dialog.
 *
 * Author:
 *        Morten Welinder <terra@gnome.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>
#include <application.h>
#include <gui-util.h>
#include <wbc-gtk.h>

#include <glib/gi18n-lib.h>

#define VIEW_DIALOG_KEY "view-dialog"

typedef struct {
	WBCGtk *wbcg;
	GtkWidget          *dialog;
	GtkBuilder         *gui;
	GtkRadioButton     *location_elsewhere;
	GtkEntry           *location_display_name;
} ViewState;

static void
cb_view_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			ViewState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_view_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
		    ViewState *state)
{
	WBCGtk *wbcg = state->wbcg;
	WorkbookControl *wbc = GNM_WBC (wbcg);
	WorkbookControl *new_wbc;
	gboolean shared;
	GdkScreen *screen;
	GSList *buttons = gtk_radio_button_get_group (state->location_elsewhere);

	shared = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui, "view_shared")));

	while (buttons)
		if (gtk_toggle_button_get_active (buttons->data))
			break;
		else
			buttons = buttons->next;

	if (!buttons) {
		g_assert_not_reached ();
		return;
	} else if (buttons->data == state->location_elsewhere) {
		const char *name = gtk_entry_get_text (state->location_display_name);
		GdkDisplay *display;
		if (!name)
			return;  /* Just ignore */

		display = gdk_display_open (name);
		if (!display) {
			char *error_str =
				g_strdup_printf (_("Display \"%s\" could not be opened."),
						 name);
			gtk_widget_destroy (state->dialog);
			go_gtk_notice_dialog (wbcg_toplevel (wbcg),
					      GTK_MESSAGE_ERROR,
					      "%s", error_str);
			g_free (error_str);
			return;
		}

		screen = gdk_display_get_default_screen (display);
	} else {
		screen = g_object_get_data (buttons->data, "screen");
	}

	gtk_widget_destroy (state->dialog);

	new_wbc = workbook_control_new_wrapper
		(wbc,
		 shared ? wb_control_view (wbc) : NULL,
		 wb_control_get_workbook (wbc),
		 screen);

	if (GNM_IS_WBC_GTK (new_wbc)) {
		/* What else would it be?  */
		WBCGtk *new_wbcg = WBC_GTK (new_wbc);
		wbcg_copy_toolbar_visibility (new_wbcg, wbcg);
		gnm_app_flag_windows_changed_ ();
	}
}

static void
cb_view_destroy (ViewState *state)
{
	if (state->gui != NULL) {
		g_object_unref (state->gui);
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

void
dialog_new_view (WBCGtk *wbcg)
{
	ViewState *state;
	GtkBuilder *gui;

	if (gnm_dialog_raise_if_exists (wbcg, VIEW_DIALOG_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/view.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new (ViewState, 1);
	state->wbcg = wbcg;
	state->gui = gui;
	state->dialog = go_gtk_builder_get_widget (gui, "View");
	state->location_elsewhere = GTK_RADIO_BUTTON (go_gtk_builder_get_widget (gui, "location_elsewhere"));
	state->location_display_name = GTK_ENTRY (go_gtk_builder_get_widget (gui, "location_display_name"));
	g_return_if_fail (state->dialog != NULL);

	{
		GdkScreen *this_screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
		GdkDisplay *this_display = gdk_screen_get_display (this_screen);
		int n_screens = gdk_display_get_n_screens (this_display);
		GtkBox *box = GTK_BOX (go_gtk_builder_get_widget (gui, "location_screens_vbox"));
		int i;

		for (i = 0; i < n_screens; i++) {
			/* Get this for every screen -- it might change.  */
			GSList *group =
				gtk_radio_button_get_group (state->location_elsewhere);
			GdkScreen *screen = gdk_display_get_screen (this_display, i);
			char *label =
				screen == this_screen
				? (n_screens == 1
				   ? g_strdup (_("This screen"))
				   : g_strdup_printf (_("Screen %d [This screen]"), i))
				: g_strdup_printf (_("Screen %d"), i);
			GtkWidget *button =
				gtk_radio_button_new_with_label (group, label);
			g_free (label);

			if (screen == this_screen)
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

			g_object_set_data (G_OBJECT (button),
					   "screen", screen);

			gtk_box_pack_start (box, button, TRUE, TRUE, 0);
		}
	}

	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_view_ok_clicked), state);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_view_cancel_clicked), state);

	gnm_link_button_and_entry (GTK_WIDGET (state->location_elsewhere),
				   GTK_WIDGET (state->location_display_name));

	gnm_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->location_display_name));

	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_VIEW);
	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       VIEW_DIALOG_KEY);

	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state, (GDestroyNotify) cb_view_destroy);
	gtk_widget_show_all (state->dialog);
}

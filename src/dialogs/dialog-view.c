/**
 * dialog-view.c:  New view dialog.
 *
 * Author:
 *        Morten Welinder <terra@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"
#include <glade/glade.h>
#include <gui-util.h>
#include <workbook-control-gui.h>

#define VIEW_DIALOG_KEY "view-dialog"

static const char *shared_group[] = {
	"view_shared",
	"view_unshared",
	0
};


typedef struct {
	WorkbookControlGUI *wbcg;
	GtkWidget          *dialog;
	GladeXML           *gui;
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
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);
	gboolean shared;
	GdkScreen *screen = NULL;
	GSList *buttons = gtk_radio_button_get_group (state->location_elsewhere);

	shared = gnumeric_glade_group_value (state->gui, shared_group) == 0;

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

		gnumeric_notice (state->wbcg, GTK_MESSAGE_ERROR,
				 _("Connecting to a different display has been disabled "
				   "due to bugs in GTK+."));
		return;

		display = gdk_display_open (name);
		if (!display) {		
			char *error_str =
				g_strdup_printf (_("Display \"%s\" could not be opened."),
						 name);				
			gtk_widget_destroy (state->dialog);
			gnumeric_notice (state->wbcg, GTK_MESSAGE_ERROR, error_str);
			g_free (error_str);
			return;
		}

		screen = gdk_display_get_default_screen (display);
	} else {
		screen = g_object_get_data (buttons->data, "screen");
		/* screen will be NULL for current screen.  */
	}

	gtk_widget_destroy (state->dialog);

	(void) wb_control_wrapper_new
		(wbc,
		 shared ? wb_control_view (wbc) : NULL,
		 wb_control_workbook (wbc),
		 screen);
}

static void
cb_view_destroy (ViewState *state)
{
	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

static void
cb_focus_to_location_display_name (GtkToggleButton *togglebutton, ViewState *state)
{
	if (gtk_toggle_button_get_active (togglebutton))
		gtk_widget_grab_focus (GTK_WIDGET (state->location_display_name));
}

static gboolean
cb_display_name_selected (G_GNUC_UNUSED GtkWidget *widget,
			  G_GNUC_UNUSED GdkEventFocus *event, ViewState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->location_elsewhere), TRUE);
	return FALSE;
}

void
dialog_new_view (WorkbookControlGUI *wbcg)
{
	ViewState *state;
	GladeXML *gui;

	if (gnumeric_dialog_raise_if_exists (wbcg, VIEW_DIALOG_KEY))
		return;
	gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg),
				 "view.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new (ViewState, 1);
	state->wbcg = wbcg;
	state->gui = gui;
	state->dialog = glade_xml_get_widget (gui, "View");
	state->location_elsewhere = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "location_elsewhere"));
	state->location_display_name = GTK_ENTRY (glade_xml_get_widget (gui, "location_display_name"));
	g_return_if_fail (state->dialog != NULL);

	{
		GdkScreen *this_screen = gtk_window_get_screen (wbcg_toplevel (wbcg));
		GdkDisplay *this_display = gdk_screen_get_display (this_screen);
		int n_screens = gdk_display_get_n_screens (this_display);
		GtkBox *box = GTK_BOX (glade_xml_get_widget (gui, "location_screens_vbox"));
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
			else
				g_object_set_data (G_OBJECT (button),
						   "screen",
						   screen);

			gtk_box_pack_start (box, button, TRUE, TRUE, 0);
		}
	}

	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "ok_button")),
			  "clicked",
			  G_CALLBACK (cb_view_ok_clicked), state);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "cancel_button")),
			  "clicked",
			  G_CALLBACK (cb_view_cancel_clicked), state);

	g_signal_connect (G_OBJECT (state->location_elsewhere),
			  "clicked", G_CALLBACK (cb_focus_to_location_display_name),
			  (gpointer) state);
	g_signal_connect (G_OBJECT (state->location_display_name),
			  "focus_in_event",
			  G_CALLBACK (cb_display_name_selected), state);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->location_display_name));
	
	gnumeric_init_help_button (
		glade_xml_get_widget (gui, "help_button"),
		"view.html");
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       VIEW_DIALOG_KEY);

	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state, (GDestroyNotify) cb_view_destroy);
	gtk_widget_show_all (state->dialog);
}

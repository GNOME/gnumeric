/**
 * dialog-zoom.c:  Sets the magnification factor
 *
 * Author:
 *        Jody Goldberg <jody@gnome.org>
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <workbook-edit.h>
#include <sheet.h>

#include <glade/glade.h>

#define GLADE_FILE "dialog-zoom.glade"
#define ZOOM_DIALOG_KEY "zoom-dialog"
#define ZOOM_DIALOG_FACTOR_KEY "zoom-dialog-factor"


typedef struct {
	WorkbookControlGUI *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *entry;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GtkRadioButton     *custom;
	GladeXML           *gui;

	GtkSpinButton  *zoom;
	GtkCList       *list;
} ZoomState;

static struct {
	char const * const name;
	gint const factor;
} buttons[] = {
	{ "radio_200", 200 },
	{ "radio_100", 100 },
	{ "radio_75",  75 },
	{ "radio_50",  50 },
	{ "radio_25",  25 },
	{ NULL, 0}
};

static void
cb_zoom_destroy (ZoomState *state)
{
	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

static void
cb_zoom_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			ZoomState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
radio_toggled (GtkToggleButton *togglebutton, ZoomState *state)
{
	gint factor;

	/* We are only interested in the new state */
	if (gtk_toggle_button_get_active (togglebutton)) {
		factor = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (togglebutton),
							     ZOOM_DIALOG_FACTOR_KEY));
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (state->zoom),
					   factor);
	}
}

static void
focus_to_custom (GtkToggleButton *togglebutton, ZoomState *state)
{
	if (gtk_toggle_button_get_active (togglebutton))
		gtk_widget_grab_focus (GTK_WIDGET (&state->zoom->entry));
}

static gboolean
custom_selected (G_GNUC_UNUSED GtkWidget *widget,
		 G_GNUC_UNUSED GdkEventFocus *event, ZoomState *state)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->custom), TRUE);
	return FALSE;
}

static void
cb_zoom_ok_clicked (G_GNUC_UNUSED GtkWidget *button, ZoomState *state)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (state->wbcg);
	GSList *sheets = NULL;
	GList  *l;
	float const new_zoom =  gtk_spin_button_get_value_as_float (state->zoom) / 100;

	for (l = state->list->selection; l != NULL ; l = l->next) {
		Sheet * s = gtk_clist_get_row_data (state->list, GPOINTER_TO_INT (l->data));
		sheets = g_slist_prepend (sheets, s);
	}
	sheets = g_slist_reverse (sheets);

	cmd_zoom (wbc, sheets, new_zoom);
	gtk_widget_destroy (state->dialog);
	return;
}

void
dialog_zoom (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	ZoomState *state;
	GList *l, *sheets;
	int i, cur_row;
	gboolean is_custom = TRUE;
	GtkRadioButton *radio;
	GtkWidget *focus_target;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, ZOOM_DIALOG_KEY))
		return;

	state = g_new (ZoomState, 1);
	state->wbcg  = wbcg;
	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
	g_return_if_fail (state->gui != NULL);

	state->dialog = glade_xml_get_widget (state->gui, "Zoom");
	g_return_if_fail (state->dialog != NULL);

	/* Get the list of sheets */
	state->list = GTK_CLIST (glade_xml_get_widget (state->gui, "sheet_list"));
	gtk_clist_freeze (state->list);

	sheets = workbook_sheets (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));
	cur_row = 0;
	for (l = sheets; l; l = l->next) {
		Sheet *this_sheet = l->data;
		int const row = gtk_clist_append (state->list, &this_sheet->name_unquoted);

		if (this_sheet == sheet)
			cur_row = row;
		gtk_clist_set_row_data (state->list, row, this_sheet);
	}
	g_list_free (sheets);
	gtk_clist_select_row (state->list, cur_row, 0);
	gtk_clist_thaw (state->list);
	gnumeric_clist_moveto (state->list, cur_row);

	state->zoom  = GTK_SPIN_BUTTON (glade_xml_get_widget (state->gui, "zoom"));
	g_return_if_fail (state->zoom != NULL);
	state->custom = GTK_RADIO_BUTTON (glade_xml_get_widget (state->gui, "radio_custom"));
	g_return_if_fail (state->custom != NULL);
	focus_target = GTK_WIDGET (state->custom);
	g_signal_connect (G_OBJECT (state->custom),
		"clicked",
		G_CALLBACK (focus_to_custom), (gpointer) state);
	g_signal_connect (G_OBJECT (state->zoom),
		"focus_in_event",
		G_CALLBACK (custom_selected), state);

	for (i = 0; buttons[i].name != NULL; i++) {
		radio  = GTK_RADIO_BUTTON (glade_xml_get_widget (state->gui, buttons[i].name));
		g_return_if_fail (radio != NULL);

		g_object_set_data (G_OBJECT (radio), ZOOM_DIALOG_FACTOR_KEY, 
				   GINT_TO_POINTER(buttons[i].factor));

		g_signal_connect (G_OBJECT (radio),
			"toggled",
			G_CALLBACK (radio_toggled), state);

		if (((int)(sheet->last_zoom_factor_used * 100. + .5)) == buttons[i].factor) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
			is_custom = FALSE;
			focus_target = GTK_WIDGET (radio);
		}
	}

	if (is_custom) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->custom), TRUE);
		gtk_spin_button_set_value (state->zoom,
					   (int)(sheet->last_zoom_factor_used * 100. + .5));
	}
	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_zoom_ok_clicked), state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_zoom_cancel_clicked), state);

	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (&state->zoom->entry));

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"zoom.html");
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       ZOOM_DIALOG_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_zoom_destroy);
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show (state->dialog);

	gtk_widget_grab_focus (focus_target);
}

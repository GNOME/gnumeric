/**
 * dialog-zoom.c:  Sets the magnification factor
 *
 * Author:
 *        Jody Goldberg <jody@gnome.org>
 *
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <sheet.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>

#define NUM_RADIO_BUTTONS	5
#define GLADE_FILE "dialog-zoom.glade"

typedef struct {
	int             factor;
	GtkSpinButton  *zoom;
} radio_cb_data;

static void
radio_toggled (GtkToggleButton *togglebutton,
	       radio_cb_data   *dat)
{
	/* We are only interested in the new state */
	if (gtk_toggle_button_get_active (togglebutton))
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (dat->zoom),
					   dat->factor);
}

static void
focus_to_custom (GtkToggleButton *togglebutton, GtkSpinButton *zoom)
{
	if (gtk_toggle_button_get_active (togglebutton))
		gtk_widget_grab_focus (GTK_WIDGET (&zoom->entry));
}

static gboolean
custom_selected (GtkWidget *widget, GdkEventFocus   *event,
		 GtkWidget *custom_button)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (custom_button), TRUE);
	return FALSE;
}

static void
dialog_zoom_impl (WorkbookControlGUI *wbcg, Sheet *cur_sheet, GladeXML  *gui)
{
	static struct {
		char const * const name;
		float const factor;
	} buttons[NUM_RADIO_BUTTONS] = {
		{ "radio_200", 2. },
		{ "radio_100", 1. },
		{ "radio_75", .75 },
		{ "radio_50", .50 },
		{ "radio_25", .25 }
	};
	radio_cb_data cb_data[NUM_RADIO_BUTTONS];

	GtkCList *list;
	GtkWidget *dialog, *focus_target;
	GtkRadioButton *radio, *custom;
	GtkSpinButton *zoom;
	GList *l, *sheets;
	int i, res, cur_row;
	gboolean is_custom = TRUE;

	list = GTK_CLIST (glade_xml_get_widget (gui, "sheet_list"));
	g_return_if_fail (list);
	zoom  = GTK_SPIN_BUTTON (glade_xml_get_widget (gui, "zoom"));
	g_return_if_fail (zoom != NULL);
	custom = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "radio_custom"));
	g_return_if_fail (custom != NULL);
	g_signal_connect (G_OBJECT (custom),
		"clicked",
		G_CALLBACK (focus_to_custom), (gpointer) zoom);
	g_signal_connect (G_OBJECT (zoom),
		"focus_in_event",
		G_CALLBACK (custom_selected), custom);
	focus_target = GTK_WIDGET (custom);

	for (i = NUM_RADIO_BUTTONS; --i >= 0; ) {
		radio  = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, buttons[i].name));
		g_return_if_fail (radio != NULL);

		cb_data[i].factor = (int)(buttons[i].factor * 100.);
		cb_data[i].zoom   = zoom;

		g_signal_connect (G_OBJECT (radio),
			"toggled",
			G_CALLBACK (radio_toggled), &(cb_data[i]));

		if (cur_sheet->last_zoom_factor_used == buttons[i].factor) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
			is_custom = FALSE;
			focus_target = GTK_WIDGET (radio);
		}
	}

	if (is_custom) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (custom), TRUE);
		gtk_spin_button_set_value (zoom,
					   (int)(cur_sheet->last_zoom_factor_used * 100. + .5));
	}

	/* Get the list of sheets */
	gtk_clist_freeze (list);

	sheets = workbook_sheets (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));
	cur_row = 0;
	for (l = sheets; l; l = l->next) {
		Sheet *sheet = l->data;
		int const row = gtk_clist_append (list, &sheet->name_unquoted);

		if (sheet == cur_sheet)
			cur_row = row;
		gtk_clist_set_row_data (list, row, sheet);
	}
	g_list_free (sheets);

	gtk_clist_select_row (list, cur_row, 0);
	gtk_clist_thaw (list);
	gnumeric_clist_moveto (list, cur_row);

	dialog = glade_xml_get_widget (gui, "Zoom");
	if (dialog == NULL) {
		printf ("Corrupt file " GLADE_FILE "\n");
		return;
	}

	/* Hitting enter in the spin box should Press 'Ok' */
	gnome_dialog_editable_enters (GNOME_DIALOG (dialog),
				      GTK_EDITABLE (zoom));

	gtk_widget_grab_focus (focus_target);
	/* Bring up the dialog */
	res = gnumeric_dialog_run (wbcg, GTK_DIALOG (dialog));
	if (res == 0) {
		float const new_zoom = gtk_spin_button_get_value_as_int (zoom) / 100.;
		GSList *sheets = NULL;

		for (l = list->selection; l != NULL ; l = l->next) {
			Sheet * s = gtk_clist_get_row_data (list, GPOINTER_TO_INT (l->data));

			sheets = g_slist_prepend (sheets, s);
		}
		sheets = g_slist_reverse (sheets);

		/* The GSList of sheet passed will be freed by cmd_zoom */
		cmd_zoom (WORKBOOK_CONTROL (wbcg), sheets, new_zoom);
	}

	/* If the user closed the dialog with prejudice, its already destroyed */
	if (res >= 0)
		gnome_dialog_close (GNOME_DIALOG (dialog));
}

/* Wrapper to ensure the libglade object gets removed on error */
void
dialog_zoom (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	GladeXML  *gui;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (sheet != NULL);

	gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (gui == NULL)
                return;

	dialog_zoom_impl (wbcg, sheet, gui);

	g_object_unref (G_OBJECT (gui));
}

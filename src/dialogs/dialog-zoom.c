/**
 * dialog-zoom.c:  Sets the magnification factor
 *
 * Author:
 *        Jody Goldberg <jgoldberg@home.com>
 *
 **/
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"


#define NUM_RADIO_BUTTONS	5
typedef struct {
	GtkRadioButton *me;
	int             factor;

	GtkSpinButton  *zoom;
	GtkRadioButton *custom;
} radio_cb_data;

typedef struct {
	Sheet *sheet;
	radio_cb_data *cb_data;
} sheet_closure_t;

static void
radio_toggled (GtkToggleButton *togglebutton,
	       radio_cb_data   *dat)
{
	/* We are only interested in the new state */
	if (gtk_toggle_button_get_active (togglebutton))
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (dat->zoom),
					   dat->factor);
}

static gboolean
custom_selected (GtkWidget *widget, GdkEventFocus   *event,
		 GtkWidget *custom_button)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (custom_button), TRUE);
	return FALSE;
}

static void
update_zoom_buttons (const Sheet *sheet, radio_cb_data *cb_data)
{
	int      i;
	gboolean is_custom = TRUE;

	for (i = NUM_RADIO_BUTTONS; --i >= 0; ) {
		if (sheet->last_zoom_factor_used * 100.0 == cb_data[i].factor) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cb_data[i].me), TRUE);
			is_custom = FALSE;
		}
	}

	if (is_custom) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cb_data[0].custom), TRUE);
		gtk_spin_button_set_value (cb_data[0].zoom, 
					   (int)(sheet->last_zoom_factor_used * 100. + .5));
	}

}

static void
select_sheet_cb (GtkCList *clist, gint row, gint col, GdkEventButton *event,
		 sheet_closure_t *sheet_data)
{
	g_return_if_fail (sheet_data != NULL);

	update_zoom_buttons (sheet_data->sheet, sheet_data->cb_data);
}

static void
dialog_zoom_impl (Workbook *wb, Sheet *cur_sheet, GladeXML  *gui)
{
	static struct {
		char const * const name;
		float const factor;
	} buttons[NUM_RADIO_BUTTONS] = {
		{ "radio_200", 2. },
		{ "radio_100", 1. },
		{ "radio_75", .75 },
		{ "radio_50", .50 },
		{ "radio_25", .25 },
	};
	radio_cb_data	cb_data[NUM_RADIO_BUTTONS];
	GList          *sheet_data;

	GtkCList *list;
	GtkWidget *dialog;
	GtkRadioButton *radio, *custom;
	GtkSpinButton *zoom;
	GList *l, *sheets;
	int i, res, cur_row;

	list = GTK_CLIST (glade_xml_get_widget (gui, "sheet_list"));
	g_return_if_fail (list);
	zoom  = GTK_SPIN_BUTTON (glade_xml_get_widget (gui, "zoom"));
	g_return_if_fail (zoom != NULL);
	custom = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "radio_custom"));
	g_return_if_fail (custom != NULL);
	gtk_signal_connect (GTK_OBJECT (zoom), "focus_in_event",
			    GTK_SIGNAL_FUNC (custom_selected),
			    custom);

	for (i = NUM_RADIO_BUTTONS; --i >= 0; ) {
		radio  = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, buttons[i].name));
		g_return_if_fail (radio != NULL);

		cb_data[i].factor = (int)(buttons[i].factor * 100.);
		cb_data[i].me     = radio;

		cb_data[i].zoom   = zoom;
		cb_data[i].custom = custom;

		gtk_signal_connect (GTK_OBJECT (radio), "toggled",
				    GTK_SIGNAL_FUNC (radio_toggled),
				    &(cb_data[i]));
	}

	update_zoom_buttons (cur_sheet, &cb_data[0]);

	gtk_clist_freeze (list);
	sheets = workbook_sheets (wb);
	cur_row = -1;
	sheet_data = NULL;
	for (l = sheets; l; l = l->next) {
		Sheet *sheet = l->data;
		int const row = gtk_clist_append (list, &sheet->name);
		sheet_closure_t *sdata = g_new (sheet_closure_t, 1);

		gtk_clist_set_row_data (list, row, sheet);
		sdata->sheet   = sheet;
		sdata->cb_data = &cb_data[0];
		sheet_data = g_list_append (sheet_data, sdata);

		if (sheet == cur_sheet)
			cur_row = row;

		gtk_signal_connect (GTK_OBJECT (list), "select-row",
				    GTK_SIGNAL_FUNC (select_sheet_cb),
				    sdata);
	}
	g_list_free (sheets);
	gtk_clist_thaw (list);

	gtk_clist_select_row (list, cur_row, 0);
	gtk_clist_moveto     (list, cur_row, 0, .5, 0.0);

	dialog = glade_xml_get_widget (gui, "Zoom");
	if (dialog == NULL) {
		printf ("Corrupt file dialog-zoom.glade\n");
		return;
	}

	/* Make the dialog a child of the application so that it will iconify */
	gnome_dialog_set_parent (GNOME_DIALOG (dialog), GTK_WINDOW (wb->toplevel));

	/* Bring up the dialog */
	res = gnome_dialog_run (GNOME_DIALOG (dialog));
	if (res == 0) {
		float const new_zoom = gtk_spin_button_get_value_as_int(zoom) / 100.;
		for (l = list->selection; l != NULL ; l = l->next) {
			Sheet * s = gtk_clist_get_row_data (list, GPOINTER_TO_INT(l->data));
			sheet_set_zoom_factor (s, new_zoom);
		}
	}

	/* If the user closed the dialog with prejudice, its already destroyed */
	if (res >= 0)
		gnome_dialog_close (GNOME_DIALOG (dialog));


	for (l = sheet_data; l; l = g_list_next (l))
		g_free (l->data);

	g_list_free (sheet_data);
}

/* Wrapper to ensure the libglade object gets removed on error */
void
dialog_zoom (Workbook *wb, Sheet *sheet)
{
	GladeXML  *gui;

	g_return_if_fail (wb != NULL);
	g_return_if_fail (sheet != NULL);

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/dialog-zoom.glade", NULL);
	if (!gui) {
		printf ("Could not find dialog-zoom.glade\n");
		return;
	}

	dialog_zoom_impl (wb, sheet, gui);
	
	gtk_object_unref (GTK_OBJECT (gui));
}

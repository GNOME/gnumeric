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

typedef struct {
	GtkSpinButton *zoom;
	int factor;
} radio_cb_data;

static void
radio_toggled (GtkToggleButton *togglebutton,
	       gpointer user_data)
{
	/* We are only interested in the new state */
	if (gtk_toggle_button_get_active(togglebutton)) {
		radio_cb_data const * dat = user_data;
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (dat->zoom),
					   dat->factor);
	}
}

static gboolean
custom_selected (GtkWidget       *widget, GdkEventFocus   *event,
		 gpointer         user_data)
{
	GtkWidget *custom_button = user_data;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (custom_button), TRUE);
	return FALSE;
}

static void
dialog_zoom_impl (Workbook *wb, Sheet *cur_sheet, GladeXML  *gui)
{
#define NUM_RADIO_BUTTONS	5
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

	GtkCList *list;
	GtkWidget *dialog;
	GtkRadioButton *radio;
	GtkSpinButton *zoom;
	GList *l, *sheets;
	gboolean is_custom = TRUE;
	int i;

	list = GTK_CLIST (glade_xml_get_widget (gui, "sheet_list"));
	g_return_if_fail (list);
	zoom  = GTK_SPIN_BUTTON (glade_xml_get_widget (gui, "zoom"));
	g_return_if_fail (zoom != NULL);
	radio  = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "radio_custom"));
	g_return_if_fail (radio != NULL);
	gtk_signal_connect (GTK_OBJECT (zoom), "focus_in_event",
			    GTK_SIGNAL_FUNC (custom_selected),
			    radio);

	for (i = NUM_RADIO_BUTTONS; --i >= 0; ) {
		radio  = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, buttons[i].name));
		g_return_if_fail (radio != NULL);

		cb_data[i].zoom = zoom;
		cb_data[i].factor = (int)(buttons[i].factor * 100.);

		gtk_signal_connect (GTK_OBJECT (radio), "toggled",
				    GTK_SIGNAL_FUNC (radio_toggled),
				    &(cb_data[i]));
		if (cur_sheet->last_zoom_factor_used == buttons[i].factor) {
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
			is_custom = FALSE;
		}
	}

	if (is_custom) {
		radio  = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "radio_custom"));
		g_return_if_fail (radio != NULL);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio), TRUE);
	}

	gtk_clist_freeze (list);
	sheets = workbook_sheets (wb);
	for (l = sheets; l; l = l->next){
		Sheet *sheet = l->data;
		int const row = gtk_clist_append (list, &sheet->name);
		gtk_clist_set_row_data (list, row, sheet);

		if (sheet == cur_sheet) {
			gtk_clist_select_row (list, row, 0);
			gtk_clist_moveto (list, row, 0, .5, 0.);
		}
	}
	g_list_free (sheets);
	gtk_clist_thaw (list);

	dialog = glade_xml_get_widget (gui, "Zoom");
	if (dialog == NULL) {
		printf ("Corrupt file dialog-zoom.glade\n");
		return;
	}

	/* TODO : Apply vs Ok ?  do we need both ? */
	if (gnome_dialog_run (GNOME_DIALOG (dialog)) > 0)
		for (l = list->selection; l != NULL ; l = l->next) {
			Sheet * s = gtk_clist_get_row_data (list, GPOINTER_TO_INT(l->data));
			printf ("%s\n", s->name);
#if 0
			sheet_set_zoom_factor (sheet, zoom);
#endif
		}
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
		printf ("Could not find zoom.glade\n");
		return;
	}

	dialog_zoom_impl (wb, sheet, gui);
	
	gtk_object_unref (GTK_OBJECT (gui));
}

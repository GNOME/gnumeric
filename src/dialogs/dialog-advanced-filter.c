/*
 * dialog-advanced-filter.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000 by Jukka-Pekka Iivonen <iivonen@iki.fi>
 **/
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "gnumeric-sheet.h"
#include "dialogs.h"


static gboolean unique_only_flag;

static void
unique_only_toggled(GtkWidget *widget, Workbook *wb)
{
        unique_only_flag = GTK_TOGGLE_BUTTON (widget)->active;
}


/* Returns TRUE on error, FALSE otherwise.
 */
static gboolean
advanced_filter (Workbook *wb,
		 gint     input_col_b,    gint input_row_b,
		 gint     input_col_e,    gint input_row_e,
		 gint     criteria_col_b, gint criteria_row_b,
		 gint     criteria_col_e, gint criteria_row_e,
		 gboolean unique_only_flag)
{
        return FALSE;
}

void
dialog_advanced_filter (Workbook *wb)
{
	GladeXML  *gui;
	GtkWidget *dia;
	GtkWidget *list_range; 
	GtkWidget *criteria_range;
	GtkWidget *copy_to;
	GtkWidget *unique_only;
	gint      v, ok_flag;
	gint      list_col_b, list_col_e, list_row_b, list_row_e;
	gint      crit_col_b, crit_col_e, crit_row_b, crit_row_e;
	gchar     *text;

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/advanced-filter.glade", NULL);
	if (!gui) {
		printf ("Could not find advanced-filter.glade\n");
		return;
	}
	
	dia = glade_xml_get_widget (gui, "AdvancedFilter");
	if (!dia) {
		printf ("Corrupt file advanced-filter.glade\n");
		return;
	}

	gnome_dialog_set_parent (GNOME_DIALOG (dia),
				 GTK_WINDOW (wb->toplevel));

	list_range = glade_xml_get_widget (gui, "entry1");
	criteria_range = glade_xml_get_widget (gui, "entry2");
	copy_to = glade_xml_get_widget (gui, "entry3");
	unique_only = glade_xml_get_widget (gui, "checkbutton1");

        if (unique_only_flag)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      unique_only, unique_only_flag);

	gtk_signal_connect (GTK_OBJECT (unique_only), "toggled",
			    GTK_SIGNAL_FUNC (unique_only_toggled), wb);

	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (list_range));
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (criteria_range));
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (copy_to));
	gtk_widget_grab_focus (list_range);
loop:	
	v = gnumeric_dialog_run (wb, GNOME_DIALOG (dia));

	if (v == 1) {
	        /* Canceled */
		gtk_object_destroy (GTK_OBJECT (dia));
		gtk_object_unref (GTK_OBJECT (gui));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (list_range));
	ok_flag = parse_range (text, &list_col_b, &list_row_b, 
				  &list_col_e, &list_row_e);
	if (! ok_flag) {
 	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell names "
				   "in 'List Range:'"));
		gtk_widget_grab_focus (list_range);
		gtk_entry_set_position(GTK_ENTRY (list_range), 0);
		gtk_entry_select_region(GTK_ENTRY (list_range), 0, 
					GTK_ENTRY(list_range)->text_length);
		goto loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (criteria_range));
	ok_flag = parse_range (text, &crit_col_b, &crit_row_b, 
				  &crit_col_e, &crit_row_e);
	if (! ok_flag) {
 	        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell names "
				   "in 'Criteria Range:'"));
		gtk_widget_grab_focus (criteria_range);
		gtk_entry_set_position(GTK_ENTRY (criteria_range), 0);
		gtk_entry_select_region(GTK_ENTRY (criteria_range), 0, 
				       GTK_ENTRY(criteria_range)->text_length);
		goto loop;
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));

	if (advanced_filter (wb, list_col_b, list_row_b, list_col_e,
			     list_row_e, crit_col_b, crit_row_b, crit_col_e,
			     crit_row_e, unique_only_flag)) {
	  printf("not done yet.\n");
	}
}

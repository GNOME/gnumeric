/*
 * dialog-autosave.c:
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
#include "workbook.h"
#include "gnumeric-util.h"
#include "dialogs.h"

static void
autosave_on_off_toggled(GtkWidget *widget, Workbook *wb)
{
        wb->autosave = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
prompt_on_off_toggled(GtkWidget *widget, Workbook *wb)
{
        wb->autosave_prompt = GTK_TOGGLE_BUTTON (widget)->active;
}

gint
dialog_autosave_callback (gpointer *data)
{
        Workbook *wb = (Workbook *) data;

	if (wb->autosave && workbook_is_dirty (wb)) {
	        if (wb->autosave_prompt) {
		        GladeXML  *gui = 
			        glade_xml_new (GNUMERIC_GLADEDIR
					       "/autosave-prompt.glade", NULL);
			GtkWidget *dia;
			gint      v;

			dia = glade_xml_get_widget (gui, "AutoSavePrompt");
			if (!dia) {
			        printf("Corrupt file autosave-prompt.glade\n");
				return 0;
			}

			v = gnumeric_dialog_run (wb, GNOME_DIALOG (dia));
			if (v != -1)
			        gtk_object_destroy (GTK_OBJECT (dia));
			gtk_object_unref (GTK_OBJECT (gui));
			
			if (v != 0)
			        goto out;
		}
		workbook_save (workbook_command_context_gui (wb), wb);
	}
out:
	wb->autosave_timer =
	       gtk_timeout_add(wb->autosave_minutes*60000,
			       (GtkFunction) dialog_autosave_callback, wb);

	return 0;
}

void
dialog_autosave (Workbook *wb)
{
	GladeXML  *gui = glade_xml_new (GNUMERIC_GLADEDIR "/autosave.glade",
					NULL);
	GtkWidget *dia;
	GtkWidget *autosave_on_off; 
	GtkWidget *minutes;
	GtkWidget *prompt_on_off;
	gchar     buf[20];
	gint      v, old_autosave, old_prompt, old_minutes;

	old_autosave = wb->autosave;
	old_prompt = wb->autosave_prompt;
	old_minutes = wb->autosave_minutes;

	gtk_timeout_remove (wb->autosave_timer);

	if (!gui) {
		printf ("Could not find autosave.glade\n");
		return;
	}
	
	dia = glade_xml_get_widget (gui, "AutoSave");
	if (!dia) {
		printf ("Corrupt file autosave.glade\n");
		return;
	}

	autosave_on_off = glade_xml_get_widget (gui, "autosave_on_off");

        if (wb->autosave)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      autosave_on_off, wb->autosave);
	gtk_signal_connect (GTK_OBJECT (autosave_on_off), "toggled",
			    GTK_SIGNAL_FUNC (autosave_on_off_toggled), wb);
	prompt_on_off = glade_xml_get_widget (gui, "prompt_on_off");
	if (wb->autosave_prompt)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      prompt_on_off,
					      wb->autosave_prompt);
	gtk_signal_connect (GTK_OBJECT (prompt_on_off), "toggled",
			    GTK_SIGNAL_FUNC (prompt_on_off_toggled), wb);

	minutes = glade_xml_get_widget (gui, "minutes");
	sprintf(buf, "%d", wb->autosave_minutes);
	gtk_entry_set_text (GTK_ENTRY (minutes), buf);

	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (minutes));

loop:	
	v = gnumeric_dialog_run (wb, GNOME_DIALOG (dia));

	if (v == 0) {
		gchar *txt;

		txt = gtk_entry_get_text (GTK_ENTRY (minutes));
		wb->autosave_minutes = atoi(txt);
		if (wb->autosave_minutes <= 0) {
		        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
					 _("You should introduce a proper "
					   "number of minutes in the entry."));
			gtk_widget_grab_focus (minutes);
			goto loop;
		}
	} else {
	        wb->autosave = old_autosave;
	        wb->autosave_prompt = old_prompt;
		wb->autosave_minutes = old_minutes;
	}
	wb->autosave_timer = 
		gtk_timeout_add(wb->autosave_minutes*60000,
				(GtkFunction) dialog_autosave_callback, wb);
	
	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
}

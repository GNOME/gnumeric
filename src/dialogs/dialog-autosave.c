/*
 * dialog-autosave.c:
 *
 * Authors:
 *        Jukka-Pekka Iivonen <iivonen@iki.fi>
 *        Miguel de Icaza (miguel@kernel.org)
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
	GtkWidget *entry = gtk_object_get_user_data (GTK_OBJECT (widget));
	
        wb->autosave = GTK_TOGGLE_BUTTON (widget)->active;
	gtk_widget_set_sensitive (entry, wb->autosave);
}

static void
prompt_on_off_toggled(GtkWidget *widget, Workbook *wb)
{
        wb->autosave_prompt = GTK_TOGGLE_BUTTON (widget)->active;
}

gboolean
dialog_autosave_prompt (Workbook *wb)
{
	GtkWidget *dia;
	GladeXML *gui = glade_xml_new (GNUMERIC_GLADEDIR "/autosave-prompt.glade", NULL);
	gint v;
	
	dia = glade_xml_get_widget (gui, "AutoSavePrompt");
	if (!dia) {
		printf("Corrupt file autosave-prompt.glade\n");
		return 0;
	}
	
	v = gnumeric_dialog_run (wb, GNOME_DIALOG (dia));
	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));
	gtk_object_unref (GTK_OBJECT (gui));
	
	if (v == 0)
		return TRUE;
	else
		return FALSE;
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

	minutes = glade_xml_get_widget (gui, "minutes");
	sprintf(buf, "%d", wb->autosave_minutes);
	gtk_entry_set_text (GTK_ENTRY (minutes), buf);

	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (minutes));

	autosave_on_off = glade_xml_get_widget (gui, "autosave_on_off");

	gtk_signal_connect (GTK_OBJECT (autosave_on_off), "toggled",
			    GTK_SIGNAL_FUNC (autosave_on_off_toggled), wb);
	gtk_object_set_user_data (GTK_OBJECT (autosave_on_off), minutes);
	
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autosave_on_off), wb->autosave);
	if (!wb->autosave)
		gtk_widget_set_sensitive (minutes, FALSE);
	
	prompt_on_off = glade_xml_get_widget (gui, "prompt_on_off");
	if (wb->autosave_prompt)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      prompt_on_off,
					      wb->autosave_prompt);
	gtk_signal_connect (GTK_OBJECT (prompt_on_off), "toggled",
			    GTK_SIGNAL_FUNC (prompt_on_off_toggled), wb);

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
	} else if (v == 2) {
		GnomeHelpMenuEntry help_ref = { "gnumeric", "autosave.html" };
		
		gnome_help_display (NULL, &help_ref);
		
	} else if (v == 1) {
		workbook_autosave_set (wb, old_minutes, old_prompt);
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
}

/*
 * dialog-autosave.c:
 *
 * Authors:
 *        Jukka-Pekka Iivonen (iivonen@iki.fi)
 *        Miguel de Icaza (miguel@kernel.org)
 *
 * (C) Copyright 2000 by Jukka-Pekka Iivonen (iivonen@iki.fi)
 **/
#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "workbook.h"
#include "gnumeric-util.h"
#include "dialogs.h"


typedef struct {
        GtkWidget *minutes_entry;
        GtkWidget *prompt_cb;
} autosave_t;

static void
autosave_on_off_toggled(GtkWidget *widget, gboolean *flag)
{
	autosave_t *p = gtk_object_get_user_data (GTK_OBJECT (widget));
	
        *flag = GTK_TOGGLE_BUTTON (widget)->active;
	gtk_widget_set_sensitive (p->minutes_entry, *flag);
	gtk_widget_set_sensitive (p->prompt_cb, *flag);
}

static void
prompt_on_off_toggled(GtkWidget *widget, gboolean *flag)
{
        *flag = GTK_TOGGLE_BUTTON (widget)->active;
}

gboolean
dialog_autosave_prompt (Workbook *wb)
{
	GtkWidget *dia;
	GladeXML *gui;
	gint v;

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/autosave-prompt.glade",
			     NULL);	
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
	GtkWidget  *dia;
	GtkWidget  *autosave_on_off; 
	gchar      buf[20];
	gint       v;
	gboolean   autosave_flag, prompt_flag;
	autosave_t p;

	if (wb->autosave_timer != 0)
		gtk_timeout_remove (wb->autosave_timer);

	if (!gui) {
		printf ("Could not find autosave.glade\n");
		return;
	}
	
	dia = glade_xml_get_widget (gui, "AutoSave");
	p.minutes_entry = glade_xml_get_widget (gui, "minutes");
	p.prompt_cb = glade_xml_get_widget (gui, "prompt_on_off");
	autosave_on_off = glade_xml_get_widget (gui, "autosave_on_off");

	if (!dia || !p.minutes_entry || !p.prompt_cb || !autosave_on_off) {
		printf ("Corrupt file autosave.glade\n");
		return;
	}

	sprintf(buf, "%d", wb->autosave_minutes);
	gtk_entry_set_text (GTK_ENTRY (p.minutes_entry), buf);

	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (p.minutes_entry));

	gtk_signal_connect (GTK_OBJECT (autosave_on_off), "toggled",
			    GTK_SIGNAL_FUNC (autosave_on_off_toggled),
			    &autosave_flag);
	gtk_object_set_user_data (GTK_OBJECT (autosave_on_off), &p);
	
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autosave_on_off),
				      wb->autosave);

	if (!wb->autosave) {
		gtk_widget_set_sensitive (p.minutes_entry, FALSE);
		gtk_widget_set_sensitive (p.prompt_cb, FALSE);
	}
	
	if (wb->autosave_prompt)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      p.prompt_cb,
					      wb->autosave_prompt);
	gtk_signal_connect (GTK_OBJECT (p.prompt_cb), "toggled",
			    GTK_SIGNAL_FUNC (prompt_on_off_toggled),
			    &prompt_flag);

loop:	
	v = gnumeric_dialog_run (wb, GNOME_DIALOG (dia));

	if (v == 0) {
		gchar *txt;
		int   tmp;

		txt = gtk_entry_get_text (GTK_ENTRY (p.minutes_entry));
		tmp = atoi (txt);
		if (tmp <= 0) {
		        gnumeric_notice (wb, GNOME_MESSAGE_BOX_ERROR,
					 _("You should introduce a proper "
					   "number of minutes in the entry."));
			gtk_widget_grab_focus (p.minutes_entry);
			goto loop;
		}
		if (autosave_flag)
		        workbook_autosave_set (wb, tmp, prompt_flag);
		else
		        workbook_autosave_cancel (wb);
	} else if (v == 2) {
		GnomeHelpMenuEntry help_ref = { "gnumeric", "autosave.html" };
		gnome_help_display (NULL, &help_ref);
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
}

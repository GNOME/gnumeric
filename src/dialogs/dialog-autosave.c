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
#include "workbook-control-gui-priv.h"
#include "gnumeric-util.h"
#include "dialogs.h"


typedef struct {
        GtkWidget *minutes_entry;
        GtkWidget *prompt_cb;
} autosave_t;

static void
autosave_on_off_toggled(GtkWidget *widget, autosave_t *p)
{
        gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));
	
	gtk_widget_set_sensitive (p->minutes_entry, active);
	gtk_widget_set_sensitive (p->prompt_cb, active);
}

gboolean
dialog_autosave_prompt (WorkbookControlGUI *wbcg)
{
	GtkWidget *dia;
	GladeXML *gui;
	gint v;

	gui = gnumeric_glade_xml_new (wbcg, "autosave-prompt.glade");
        if (gui == NULL)
                return 0;

	dia = glade_xml_get_widget (gui, "AutoSavePrompt");
	if (!dia) {
		printf("Corrupt file autosave-prompt.glade\n");
		return 0;
	}

	v = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dia));
	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));
	gtk_object_unref (GTK_OBJECT (gui));

	if (v == 0)
		return TRUE;
	else
		return FALSE;
}

void
dialog_autosave (WorkbookControlGUI *wbcg)
{
	GladeXML  *gui;
	GtkWidget  *dia;
	GtkWidget  *autosave_on_off;
	gchar      buf[20];
	gint       v;
	autosave_t p;

	wb_control_gui_autosave_cancel (wbcg);

	gui = gnumeric_glade_xml_new (wbcg, "autosave.glade");
        if (gui == NULL)
                return;

	dia = glade_xml_get_widget (gui, "AutoSave");
	p.minutes_entry = glade_xml_get_widget (gui, "minutes");
	p.prompt_cb = glade_xml_get_widget (gui, "prompt_on_off");
	autosave_on_off = glade_xml_get_widget (gui, "autosave_on_off");

	if (!dia || !p.minutes_entry || !p.prompt_cb || !autosave_on_off) {
		printf ("Corrupt file autosave.glade\n");
		return;
	}

	sprintf(buf, "%d", wbcg->autosave_minutes);
	gtk_entry_set_text (GTK_ENTRY (p.minutes_entry), buf);

	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (p.minutes_entry));

	gtk_signal_connect (GTK_OBJECT (autosave_on_off), "toggled",
			    GTK_SIGNAL_FUNC (autosave_on_off_toggled),
			    &p);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (autosave_on_off),
				      wbcg->autosave);

	if (!wbcg->autosave) {
		gtk_widget_set_sensitive (p.minutes_entry, FALSE);
		gtk_widget_set_sensitive (p.prompt_cb, FALSE);
	}

	gtk_toggle_button_set_active ((GtkToggleButton *) p.prompt_cb,
				      wbcg->autosave_prompt);
loop:
	v = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dia));

	if (v == 0) {
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (autosave_on_off))) {
			gchar *txt;
			int   tmp;

			txt = gtk_entry_get_text (GTK_ENTRY (p.minutes_entry));
			tmp = atoi (txt);
			if (tmp <= 0) {
				gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
						 _("You should introduce a proper "
						   "number of minutes in the entry."));
				gtk_widget_grab_focus (p.minutes_entry);
				goto loop;
			}
			
		        wb_control_gui_autosave_set (
				wbcg, tmp, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (p.prompt_cb)));
		} else
			wb_control_gui_autosave_set (wbcg, 0, FALSE);
	} else if (v == 2) {
		GnomeHelpMenuEntry help_ref = { "gnumeric", "autosave.html" };
		gnome_help_display (NULL, &help_ref);
	}

	if (v != -1)
		gtk_object_destroy (GTK_OBJECT (dia));

	gtk_object_unref (GTK_OBJECT (gui));
}


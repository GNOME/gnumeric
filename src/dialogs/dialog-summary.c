/**
 * dialog-summary.c:  Implements the summary info stuff
 *
 * Author:
 *        Michael Meeks <michael@imaginator.com>
 *        Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <workbook.h>
#include <workbook-edit.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>


#define GLADE_FILE "summary.glade"
#define SUMMARY_DIALOG_KEY "summary-dialog"

typedef struct {
	GladeXML           *gui;
	WorkbookControlGUI *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GtkWidget          *apply_button;
} SummaryState;


static void
dialog_summary_get (SummaryState *state)
{
	int lp;
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (state->wbcg));
	SummaryInfo *sin = wb->summary_info;

	for (lp = 0; lp < SUMMARY_I_MAX; lp++) {
		SummaryItem *sit;
		gchar *name = g_strconcat ("glade_", summary_item_name[lp], NULL);
		GtkWidget *w = glade_xml_get_widget (state->gui, name);

		if (w == NULL) {
			g_free (name);
			continue;
		}

		if (lp == SUMMARY_I_COMMENTS) {
			char *comments;
			GtkTextIter start;
			GtkTextIter end;
			GtkTextBuffer* buffer;

			buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w));
			gtk_text_buffer_get_bounds  (buffer, &start, &end);
			comments = gtk_text_buffer_get_text (buffer, &start,
							     &end, FALSE);
			sit = summary_item_new_string (summary_item_name[lp],
				comments, FALSE);
		} else {
			const char *txt = gtk_entry_get_text (GTK_ENTRY (w));
			sit = summary_item_new_string (summary_item_name[lp],
				txt, TRUE);
		}

		summary_info_add (sin, sit);
		g_free (name);
	}
}

static void
dialog_summary_put (SummaryState *state)
{
	GList *l, *m;
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (state->wbcg));
	SummaryInfo *sin = wb->summary_info;
	GtkWidget   *w ;
	
	m = l = summary_info_as_list (sin);
	while (l) {
		gchar       *name =  NULL;
		SummaryItem *sit = l->data;

		if (sit && (sit->type == SUMMARY_STRING)) {
			name = g_strconcat ("glade_", sit->name, NULL);
			w = glade_xml_get_widget (state->gui, name);
			if (w) {
				gchar *txt = sit->v.txt;

				if (g_strcasecmp (sit->name, 
						  summary_item_name[SUMMARY_I_COMMENTS]) == 0) {
					GtkTextBuffer* buffer;
					buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (w));
					gtk_text_buffer_set_text (buffer, txt, -1);
				} else
					gtk_entry_set_text (GTK_ENTRY (w), txt);
			}
			g_free (name);
		}
		l = g_list_next (l);
	}
	g_list_free (m);
	
	w = glade_xml_get_widget (state->gui, "doc_name");
	if (w) {
		gtk_entry_set_text (GTK_ENTRY (w), wb->filename);
	}
}

static void
cb_dialog_summary_cancel_clicked (GtkWidget *button, SummaryState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_dialog_summary_apply_clicked (GtkWidget *button, SummaryState *state)
{
	dialog_summary_get (state);
	return;
}

static void
cb_dialog_summary_ok_clicked (GtkWidget *button, SummaryState *state)
{
	cb_dialog_summary_apply_clicked (button, state);
	gtk_widget_destroy (state->dialog);
	return;
}

static gboolean
cb_dialog_summary_destroy (GtkObject *w, SummaryState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}


void
dialog_summary_update (WorkbookControlGUI *wbcg)
{
	SummaryState *state;
	int i;
	static const char *names[] = {
	     "glade_title",
	     "glade_author",
	     "glade_category",
	     "glade_keywords",
	     "glade_manager",
	     "glade_company"
	};

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, SUMMARY_DIALOG_KEY))
		return;

	state = g_new (SummaryState, 1);
	state->wbcg  = wbcg;

	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
	g_return_if_fail (state->gui != NULL);
	
	state->dialog = glade_xml_get_widget (state->gui, "SummaryInformation");
	g_return_if_fail (state->dialog != NULL);

	for (i = 0; i < (int) (sizeof (names)/sizeof (char *)); i++) {
		GtkWidget *entry;
		entry = glade_xml_get_widget (state->gui, names[i]);
		gnumeric_editable_enters (GTK_WINDOW (state->dialog),
					      GTK_WIDGET (entry));
	}

	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (cb_dialog_summary_destroy), state);
	state->ok_button = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_summary_ok_clicked), state);
	state->apply_button = glade_xml_get_widget (state->gui, "apply_button");
	g_signal_connect (G_OBJECT (state->apply_button),
		"clicked",
		G_CALLBACK (cb_dialog_summary_apply_clicked), state);

	state->cancel_button = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_dialog_summary_cancel_clicked), state);

	/* FIXME: that's not the proper help location */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"summary.html");

	dialog_summary_put (state);
	
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SUMMARY_DIALOG_KEY);

	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}

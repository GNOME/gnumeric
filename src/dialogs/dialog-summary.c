/**
 * dialog-summary.c:  Implements the summary info stuff
 *
 * Author:
 *        Michael Meeks <michael@ximian.com>
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <workbook-priv.h> /* for Workbook::summary_info */
#include <workbook-edit.h>
#include <commands.h>

#include <glade/glade.h>


#define GLADE_FILE "summary.glade"
#define SUMMARY_DIALOG_KEY "summary-dialog"
#define SUMMARY_DIALOG_KEY_DIALOG "summary-dialog-SummaryState"

typedef struct {
	GladeXML           *gui;
	WorkbookControlGUI *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	GtkWidget          *apply_button;
	gulong              signal_handler_filename_changed;
	gulong              signal_handler_summary_changed;
} SummaryState;

static const char *dialog_summary_names[] = {
	"title",
	"author",
	"category",
	"keywords",
	"manager",
	"company",
	NULL
};

static gboolean
dialog_summary_get (SummaryState *state)
{
	int lp;
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (state->wbcg));
	GSList * changes = NULL;
	GtkWidget *w;
	gchar *old_content;

	for (lp = 0; dialog_summary_names[lp]; lp++) {
		SummaryItem *sit = NULL;
		char const *txt;

		w = glade_xml_get_widget (state->gui, dialog_summary_names[lp]);
		if (w == NULL)
			continue;

		old_content = summary_item_as_text_by_name (dialog_summary_names[lp],
							    wb->summary_info);
		txt = gtk_entry_get_text (GTK_ENTRY (w));

		if (0 != strcmp (old_content, txt))
			sit = summary_item_new_string (dialog_summary_names[lp],
						       txt, TRUE);
		g_free (old_content);
		if (sit)
			changes = g_slist_prepend (changes, sit);
	}

	{
		char *comments;
		SummaryItem *sit = NULL;

		w = glade_xml_get_widget (state->gui, summary_item_name[SUMMARY_I_COMMENTS]);
		comments = gnumeric_textview_get_text (GTK_TEXT_VIEW (w));
		old_content = summary_item_as_text_by_name (summary_item_name[SUMMARY_I_COMMENTS],
							    wb->summary_info);
		if (0 != strcmp (old_content, comments))
			sit = summary_item_new_string (summary_item_name[SUMMARY_I_COMMENTS],
						       comments, FALSE);
		else
			g_free (comments);
		g_free (old_content);
		if (sit)
			changes = g_slist_prepend (changes, sit);
	}

	if (changes)
		return cmd_change_summary (WORKBOOK_CONTROL (state->wbcg), changes);
	return FALSE;
}

static void
dialog_summary_put (SummaryState *state)
{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (state->wbcg));
	SummaryInfo *sin = wb->summary_info;
	GtkWidget   *w ;
	int i;

	for (i = 0; dialog_summary_names[i]; i++) {
		w = glade_xml_get_widget (state->gui, dialog_summary_names[i]);
		if (w) {
			char *txt = summary_item_as_text_by_name (dialog_summary_names[i], sin);
			gtk_entry_set_text (GTK_ENTRY (w), txt);
			g_free (txt);
		}
	}
	w = glade_xml_get_widget (state->gui, summary_item_name[SUMMARY_I_COMMENTS]);
	if (w) {
		char *txt = summary_item_as_text_by_name (summary_item_name[SUMMARY_I_COMMENTS],
							  sin);
		gnumeric_textview_set_text (GTK_TEXT_VIEW (w), txt);
		g_free (txt);
	}
	w = glade_xml_get_widget (state->gui, "doc_name");
	if (w)
		gtk_entry_set_text (GTK_ENTRY (w), workbook_get_filename (wb));
}

static void
cb_info_changed (G_GNUC_UNUSED Workbook *wb, SummaryState *state)
{
	dialog_summary_put (state);
	return;
}



static void
cb_dialog_summary_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
				  SummaryState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_dialog_summary_apply_clicked (G_GNUC_UNUSED GtkWidget *button,
				 SummaryState *state)
{
	dialog_summary_get (state);
	return;
}

static void
cb_dialog_summary_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
			      SummaryState *state)
{
	if (!dialog_summary_get (state))
		gtk_widget_destroy (state->dialog);
	return;
}

static gboolean
cb_dialog_summary_destroy (GtkObject *w, SummaryState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	g_signal_handler_disconnect (
		G_OBJECT (wb_control_workbook (WORKBOOK_CONTROL (state->wbcg))),
		state->signal_handler_filename_changed);
	g_signal_handler_disconnect (
		G_OBJECT (wb_control_workbook (WORKBOOK_CONTROL (state->wbcg))),
		state->signal_handler_summary_changed);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}


void
dialog_summary_update (WorkbookControlGUI *wbcg, gboolean open_dialog)
{
	SummaryState *state;
	int i;
	GtkWidget *dialog;
	GladeXML  *gui;

	g_return_if_fail (wbcg != NULL);

	dialog = gnumeric_dialog_raise_if_exists (wbcg, SUMMARY_DIALOG_KEY);
	if (dialog) {
		state = g_object_get_data (G_OBJECT (dialog),
					   SUMMARY_DIALOG_KEY_DIALOG);
		dialog_summary_put (state);
		return;
	}

	if (!open_dialog)
		return;

	gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg), GLADE_FILE, NULL, NULL);
	if (gui == NULL)
		return;

	dialog = glade_xml_get_widget (gui, "SummaryInformation");
	g_return_if_fail (dialog != NULL);

	state = g_new (SummaryState, 1);
	state->wbcg  = wbcg;
	state->gui  = gui;
	state->dialog  = dialog;

	for (i = 0; dialog_summary_names[i]; i++) {
		GtkWidget *entry;
		entry = glade_xml_get_widget (state->gui, dialog_summary_names[i]);
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

	g_object_set_data (G_OBJECT (state->dialog), SUMMARY_DIALOG_KEY_DIALOG,
			   state);

	state->signal_handler_filename_changed = g_signal_connect (
		G_OBJECT (wb_control_workbook (WORKBOOK_CONTROL (state->wbcg))),
		"filename_changed", G_CALLBACK (cb_info_changed), state) ;
	state->signal_handler_summary_changed = g_signal_connect (
		G_OBJECT (wb_control_workbook (WORKBOOK_CONTROL (state->wbcg))),
		"summary_changed", G_CALLBACK (cb_info_changed), state) ;

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       SUMMARY_DIALOG_KEY);

	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}



/*
 * dialog-search-replace.c:
 *   Dialog for entering a search-and-replace query.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
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
#include <search.h>
#include <widgets/gnumeric-expr-entry.h>
#include <workbook-edit.h>
#include <selection.h>

#include <libgnomeui/gnome-entry.h>
#include <glade/glade.h>

#define SEARCH_REPLACE_KEY "search-replace-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	GladeXML *gui;
	GtkDialog *dialog;
	GnomeEntry *search_text;
	GnomeEntry *replace_text;
	GnumericExprEntry *rangetext;
	SearchDialogCallback cb;
} DialogState;

static const char *error_group[] = {
	"error_fail",
	"error_skip",
	"error_query",
	"error_error",
	"error_string",
	0
};

static const char *search_type_group[] = {
	"search_type_text",
	"search_type_regexp",
	0
};

static const char *scope_group[] = {
	"scope_workbook",
	"scope_sheet",
	"scope_range",
	0
};

static const char *direction_group[] = {
	"row_major",
	"column_major",
	0
};

static gboolean
is_checked (GladeXML *gui, const char *name)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
}

static void
set_checked (GladeXML *gui, const char *name, gboolean checked)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), checked);
}

static void
ok_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GladeXML *gui = dd->gui;
	GtkDialog *dialog = dd->dialog;
	WorkbookControlGUI *wbcg = dd->wbcg;
	SearchDialogCallback cb = dd->cb;
	SearchReplace *sr;
	char *err;
	int i;

	sr = search_replace_new ();

	sr->search_text = g_strdup (gtk_entry_get_text 
				    (GTK_ENTRY (gnome_entry_gtk_entry (dd->search_text))));
	sr->replace_text = g_strdup (gtk_entry_get_text 
				     (GTK_ENTRY (gnome_entry_gtk_entry (dd->replace_text))));

	/* Save the contents of both gnome-entry's. */
	gnome_entry_append_history (dd->search_text, TRUE, sr->search_text);
	gnome_entry_append_history (dd->replace_text, TRUE, sr->replace_text);

	i = gnumeric_glade_group_value (gui, search_type_group);
	sr->is_regexp = (i == 1);

	i = gnumeric_glade_group_value (gui, scope_group);
	sr->scope = (i == -1) ? SRS_sheet : (SearchReplaceScope)i;

	/* FIXME: parsing of an gnm_expr_entry should happen by the gee */
	sr->range_text = g_strdup (gnm_expr_entry_get_text (dd->rangetext));
	sr->curr_sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));

	sr->query = is_checked (gui, "query");
	sr->preserve_case = is_checked (gui, "preserve_case");
	sr->ignore_case = is_checked (gui, "ignore_case");
	sr->match_words = is_checked (gui, "match_words");

	sr->search_strings = is_checked (gui, "search_string");
	sr->search_other_values = is_checked (gui, "search_other");
	sr->search_expressions = is_checked (gui, "search_expr");
	sr->search_expression_results = FALSE;
	sr->search_comments = is_checked (gui, "search_comments");

	i = gnumeric_glade_group_value (gui, error_group);
	sr->error_behaviour = (i == -1) ? SRE_fail : (SearchReplaceError)i;

	i = gnumeric_glade_group_value (gui, direction_group);
	sr->by_row = (i == 0);

	err = search_replace_verify (sr, TRUE);
	if (err) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR, err);
		g_free (err);
		search_replace_free (sr);
		return;
	} else if (!sr->search_strings &&
		   !sr->search_other_values &&
		   !sr->search_expressions &&
		   !sr->search_expression_results &&
		   !sr->search_comments) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("You must select some cell types to search."));
		search_replace_free (sr);
		return;
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
	dd = NULL;  /* Destroyed */

	cb (wbcg, sr);
	search_replace_free (sr);
}

static void
cancel_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GtkDialog *dialog = dd->dialog;

	gtk_widget_destroy (GTK_WIDGET (dialog));
}


static void
dialog_destroy (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GladeXML *gui = dd->gui;
	g_object_unref (G_OBJECT (gui));
	wbcg_edit_detach_guru (dd->wbcg);
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}

static gboolean
range_focused (G_GNUC_UNUSED GtkWidget *widget,
	       G_GNUC_UNUSED GdkEventFocus *event,
	       DialogState *dd)
{
	GtkWidget *scope_range = glade_xml_get_widget (dd->gui, "scope_range");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scope_range), TRUE);
	return FALSE;
}

static void
non_modal_dialog (WorkbookControlGUI *wbcg,
		  GtkDialog *dialog,
		  const char *key)
{
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), key);

	gtk_widget_show (GTK_WIDGET (dialog));
}


void
dialog_search_replace (WorkbookControlGUI *wbcg,
		       SearchDialogCallback cb)
{
	GladeXML *gui;
	GtkDialog *dialog;
	DialogState *dd;
	GtkTable *table;
	char *selection_text;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbcg_edit_has_guru (wbcg))
		return;

	if (gnumeric_dialog_raise_if_exists (wbcg, SEARCH_REPLACE_KEY))
		return;

	gui = gnumeric_glade_xml_new (wbcg, "search-replace.glade");
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "search_replace_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->cb = cb;
	dd->dialog = dialog;

	table = GTK_TABLE (glade_xml_get_widget (gui, "search_table"));
	dd->search_text = GNOME_ENTRY (gnome_entry_new ("search_entry"));
	gtk_table_attach (table, GTK_WIDGET (dd->search_text),
			  1, 4, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters
		(GTK_WINDOW (dialog), gnome_entry_gtk_entry (dd->search_text));

	dd->replace_text = GNOME_ENTRY (gnome_entry_new ("replace_entry"));
	gtk_table_attach (table, GTK_WIDGET (dd->replace_text),
			  1, 4, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters
		(GTK_WINDOW (dialog), gnome_entry_gtk_entry (dd->replace_text));

	table = GTK_TABLE (glade_xml_get_widget (gui, "scope_table"));
	dd->rangetext = gnumeric_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (dd->rangetext, 0, GNUM_EE_MASK);
	gnm_expr_entry_set_scg (dd->rangetext, wbcg_cur_scg (wbcg));
	gtk_table_attach (table, GTK_WIDGET (dd->rangetext),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	selection_text = selection_to_string (
		wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)),
		TRUE);
	gnm_expr_entry_load_from_text  (dd->rangetext, selection_text);
	g_free (selection_text);
	gtk_widget_show (GTK_WIDGET (dd->rangetext));

	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "ok_button")),
		"clicked",
		G_CALLBACK (ok_clicked), dd);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cancel_clicked), dd);
	g_signal_connect (G_OBJECT (dialog),
		"destroy",
		G_CALLBACK (dialog_destroy), dd);
	g_signal_connect (GTK_OBJECT (gnm_expr_entry_get_entry (dd->rangetext)),
		"focus-in-event",
		G_CALLBACK (range_focused), dd);

/* FIXME: Add correct helpfile address */
	gnumeric_init_help_button (
		glade_xml_get_widget (gui, "help_button"),
		"search-replace.html");

	gtk_widget_show_all (dialog->vbox);
	gtk_widget_grab_focus (gnome_entry_gtk_entry (dd->search_text));

	wbcg_edit_attach_guru (wbcg, GTK_WIDGET (dialog));
	non_modal_dialog (wbcg, dialog, SEARCH_REPLACE_KEY);
}

int
dialog_search_replace_query (WorkbookControlGUI *wbcg,
			     SearchReplace *sr,
			     const char *location,
			     const char *old_text,
			     const char *new_text)
{
	GladeXML *gui;
	GtkDialog *dialog;
	int res;
	GtkWindow *toplevel;

	g_return_val_if_fail (wbcg != NULL, 0);

	gui = gnumeric_glade_xml_new (wbcg, "search-replace.glade");
        if (gui == NULL)
                return 0;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "query_dialog"));

	gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (gui, "qd_location")),
			    location);
	gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (gui, "qd_old_text")),
			    old_text);
	gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (gui, "qd_new_text")),
			    new_text);
	set_checked (gui, "qd_query", sr->query);


	toplevel = wbcg_toplevel (wbcg);
	if (GTK_WINDOW (dialog)->transient_parent != toplevel)
		gtk_window_set_transient_for (GTK_WINDOW (dialog), toplevel);

	gtk_tooltips_set_tip (gtk_tooltips_new (),
			      gtk_dialog_add_button (dialog, GTK_STOCK_CANCEL, 
						     GTK_RESPONSE_CANCEL),
			      _("Perform no more replacements"), NULL);
	gtk_tooltips_set_tip (gtk_tooltips_new (),
			      gtk_dialog_add_button (dialog, GTK_STOCK_NO, 
						     GTK_RESPONSE_NO),
			      _("Do not perform this replacement"), NULL);
	gtk_tooltips_set_tip (gtk_tooltips_new (),
			      gtk_dialog_add_button (dialog, GTK_STOCK_YES, 
						     GTK_RESPONSE_YES),
			      _("Perform this replacement"), NULL);

	gtk_widget_show_all (GTK_WIDGET (dialog));
	res = gtk_dialog_run (dialog);

	/* Unless cancel is pressed, propagate the query setting back.  */
	if (res != GTK_RESPONSE_CANCEL && res != GTK_RESPONSE_NONE &&
	    res != GTK_RESPONSE_DELETE_EVENT)
		sr->query = is_checked (gui, "qd_query");

	gtk_widget_destroy (GTK_WIDGET (dialog));

/*FIXME: rather than recoding the result value we should change the tests down stream */

	if (res == GTK_RESPONSE_YES)
		return 0;
	if (res == GTK_RESPONSE_NO)
		return 1;

	return -1;
}

/* ------------------------------------------------------------------------- */

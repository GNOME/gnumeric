/*
 * dialog-search-replace.c:
 *   Dialog for entering a search-and-replace query.
 *
 * Author:
 *   Morten Welinder (terra@gnome.org)
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <search.h>
#include <widgets/gnumeric-expr-entry.h>
#include <wbc-gtk.h>
#include <selection.h>

#include <glade/glade.h>
#include <goffice/gtk/goffice-gtk.h>
#include <gtk/gtk.h>
#include <string.h>

#define SEARCH_REPLACE_KEY "search-replace-dialog"

typedef struct {
	WBCGtk *wbcg;
	GladeXML *gui;
	GtkDialog *dialog;
	GtkEntry *search_text;
	GtkEntry *replace_text;
	GnmExprEntry *rangetext;
	SearchDialogCallback cb;
} DialogState;

static const char * const error_group[] = {
	"error_fail",
	"error_skip",
	"error_query",
	"error_error",
	"error_string",
	NULL
};

static const char * const search_type_group[] = {
	"search_type_text",
	"search_type_regexp",
	NULL
};

static const char * const scope_group[] = {
	"scope_workbook",
	"scope_sheet",
	"scope_range",
	NULL
};

static const char * const direction_group[] = {
	"row_major",
	"column_major",
	NULL
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
	WBCGtk *wbcg = dd->wbcg;
	SearchDialogCallback cb = dd->cb;
	GnmSearchReplace *sr;
	char *err;
	int i;
	GnmSearchReplaceScope scope;
	char *search_text, *replace_text;

	i = gnumeric_glade_group_value (gui, scope_group);
	scope = (i == -1) ? GNM_SRS_SHEET : (GnmSearchReplaceScope)i;

	search_text = g_utf8_normalize (gtk_entry_get_text (dd->search_text), -1, G_NORMALIZE_DEFAULT);
	replace_text = g_utf8_normalize (gtk_entry_get_text (dd->replace_text), -1, G_NORMALIZE_DEFAULT);

	sr = g_object_new (GNM_SEARCH_REPLACE_TYPE,
			   "sheet", wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)),
			   "scope", scope,
			   "range-text", gnm_expr_entry_get_text (dd->rangetext),
			   "search-text", search_text,
			   "replace-text", replace_text,
			   "is-regexp", gnumeric_glade_group_value (gui, search_type_group) == 1,
			   "ignore-case", is_checked (gui, "ignore_case"),
			   "match-words", is_checked (gui, "match_words"),
			   "preserve-case", is_checked (gui, "preserve_case"),
			   "query", is_checked (gui, "query"),
			   "replace-keep-strings", is_checked (gui, "keep_strings"),
			   "search-strings", is_checked (gui, "search_string"),
			   "search-other-values", is_checked (gui, "search_other"),
			   "search-expressions", is_checked (gui, "search_expr"),
			   "search-expression-results", FALSE,
			   "search-comments", is_checked (gui, "search_comments"),
			   "by-row", gnumeric_glade_group_value (gui, direction_group) == 0,
			   NULL);

	g_free (search_text);
	g_free (replace_text);

	i = gnumeric_glade_group_value (gui, error_group);
	sr->error_behaviour = (i == -1) ? GNM_SRE_FAIL : (GnmSearchReplaceError)i;

	err = gnm_search_replace_verify (sr, TRUE);
	if (err) {
		go_gtk_notice_dialog (GTK_WINDOW (dialog), GTK_MESSAGE_ERROR, err);
		g_free (err);
		g_object_unref (sr);
		return;
	} else if (!sr->search_strings &&
		   !sr->search_other_values &&
		   !sr->search_expressions &&
		   !sr->search_expression_results &&
		   !sr->search_comments) {
		go_gtk_notice_dialog (GTK_WINDOW (dialog), GTK_MESSAGE_ERROR,
				 _("You must select some cell types to search."));
		g_object_unref (sr);
		return;
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
	dd = NULL;  /* Destroyed */

	cb (wbcg, sr);
	g_object_unref (sr);
}

static void
cancel_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GtkDialog *dialog = dd->dialog;

	gtk_widget_destroy (GTK_WIDGET (dialog));
}


static void
cb_dialog_destroy (DialogState *dd)
{
	g_object_unref (G_OBJECT (dd->gui));
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
non_modal_dialog (WBCGtk *wbcg,
		  GtkDialog *dialog,
		  const char *key)
{
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), key);

	gtk_widget_show (GTK_WIDGET (dialog));
}


void
dialog_search_replace (WBCGtk *wbcg,
		       SearchDialogCallback cb)
{
	GladeXML *gui;
	GtkDialog *dialog;
	DialogState *dd;
	GtkTable *table;
	char *selection_text;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	if (gnumeric_dialog_raise_if_exists (wbcg, SEARCH_REPLACE_KEY))
		return;

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"search-replace.glade", NULL, NULL);
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "search_replace_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->cb = cb;
	dd->dialog = dialog;

	table = GTK_TABLE (glade_xml_get_widget (gui, "search_table"));
	dd->search_text = GTK_ENTRY (gtk_entry_new ());
	gtk_table_attach (table, GTK_WIDGET (dd->search_text),
			  1, 4, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (dialog),
				  GTK_WIDGET (dd->search_text));

	dd->replace_text = GTK_ENTRY (gtk_entry_new ());
	gtk_table_attach (table, GTK_WIDGET (dd->replace_text),
			  1, 4, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (dialog),
				  GTK_WIDGET (dd->replace_text));

	table = GTK_TABLE (glade_xml_get_widget (gui, "scope_table"));
	dd->rangetext = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (dd->rangetext, 0, GNM_EE_MASK);
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
	g_signal_connect (GTK_OBJECT (gnm_expr_entry_get_entry (dd->rangetext)),
		"focus-in-event",
		G_CALLBACK (range_focused), dd);

	gnumeric_init_help_button (
		glade_xml_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SEARCH_REPLACE);
	g_object_set_data_full (G_OBJECT (dialog),
		"state", dd, (GDestroyNotify) cb_dialog_destroy);

	gtk_widget_show_all (dialog->vbox);
	gtk_widget_grab_focus (GTK_WIDGET (dd->search_text));

	gnm_dialog_setup_destroy_handlers (dialog, wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	wbc_gtk_attach_guru (wbcg, GTK_WIDGET (dialog));
	non_modal_dialog (wbcg, dialog, SEARCH_REPLACE_KEY);
}

int
dialog_search_replace_query (WBCGtk *wbcg,
			     GnmSearchReplace *sr,
			     const char *location,
			     const char *old_text,
			     const char *new_text)
{
	GladeXML *gui;
	GtkDialog *dialog;
	int res;

	g_return_val_if_fail (wbcg != NULL, 0);

	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"search-replace.glade", NULL, NULL);
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

	wbcg_set_transient (wbcg, GTK_WINDOW (dialog));
	go_dialog_guess_alternative_button_order (dialog);
	gtk_widget_show_all (GTK_WIDGET (dialog));

	gnm_dialog_setup_destroy_handlers (dialog, wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	res = gtk_dialog_run (dialog);
	switch (res) {
	case GTK_RESPONSE_YES:
	case GTK_RESPONSE_NO:
		sr->query = is_checked (gui, "qd_query");
		break;
	default:
		res = GTK_RESPONSE_CANCEL;
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));

	return res;
}

/* ------------------------------------------------------------------------- */

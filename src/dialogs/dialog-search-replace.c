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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gnumeric-conf.h>
#include <gui-util.h>
#include <search.h>
#include <widgets/gnm-expr-entry.h>
#include <wbc-gtk.h>
#include <selection.h>

#include <goffice/goffice.h>
#include <string.h>

#define SEARCH_REPLACE_KEY "search-replace-dialog"

typedef struct {
	WBCGtk *wbcg;
	GtkBuilder *gui;
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
is_checked (GtkBuilder *gui, const char *name)
{
	GtkWidget *w = go_gtk_builder_get_widget (gui, name);
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
}

static void
set_checked (GtkBuilder *gui, const char *name, gboolean checked)
{
	GtkWidget *w = go_gtk_builder_get_widget (gui, name);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), checked);
}

static void
dialog_search_replace_save_in_prefs (DialogState *dd)
{
	GtkBuilder *gui = dd->gui;

#define SETW(w,f) f (is_checked (gui, w));
	SETW("search_expr", gnm_conf_set_searchreplace_change_cell_expressions);
	SETW("search_other", gnm_conf_set_searchreplace_change_cell_other);
	SETW("search_string", gnm_conf_set_searchreplace_change_cell_strings);
	SETW("search_comments", gnm_conf_set_searchreplace_change_comments);
	SETW("ignore_case", gnm_conf_set_searchreplace_ignore_case);
	SETW("keep_strings", gnm_conf_set_searchreplace_keep_strings);
	SETW("preserve_case", gnm_conf_set_searchreplace_preserve_case);
	SETW("query", gnm_conf_set_searchreplace_query);
	SETW("match_words", gnm_conf_set_searchreplace_whole_words_only);
	SETW("column_major", gnm_conf_set_searchreplace_columnmajor);
#undef SETW

	gnm_conf_set_searchreplace_regex
		(gnm_gui_group_value (gui, search_type_group));
	gnm_conf_set_searchreplace_error_behaviour
		(gnm_gui_group_value (gui, error_group));
	gnm_conf_set_searchreplace_scope
		(gnm_gui_group_value (gui, scope_group));
}

static void
apply_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GtkBuilder *gui = dd->gui;
	GtkDialog *dialog = dd->dialog;
	WBCGtk *wbcg = dd->wbcg;
	SearchDialogCallback cb = dd->cb;
	GnmSearchReplace *sr;
	char *err;
	int i;
	GnmSearchReplaceScope scope;
	char *search_text, *replace_text;

	i = gnm_gui_group_value (gui, scope_group);
	scope = (i == -1) ? GNM_SRS_SHEET : (GnmSearchReplaceScope)i;

	search_text = gnm_search_normalize (gtk_entry_get_text (dd->search_text));
	replace_text = gnm_search_normalize (gtk_entry_get_text (dd->replace_text));

	sr = g_object_new (GNM_SEARCH_REPLACE_TYPE,
			   "sheet", wb_control_cur_sheet (GNM_WBC (wbcg)),
			   "scope", scope,
			   "range-text", gnm_expr_entry_get_text (dd->rangetext),
			   "search-text", search_text,
			   "replace-text", replace_text,
			   "is-regexp", gnm_gui_group_value (gui, search_type_group) == 1,
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
			   "by-row", gnm_gui_group_value (gui, direction_group) == 0,
			   NULL);

	g_free (search_text);
	g_free (replace_text);

	i = gnm_gui_group_value (gui, error_group);
	sr->error_behaviour = (i == -1) ? GNM_SRE_FAIL : (GnmSearchReplaceError)i;

	if  (is_checked (gui, "save-in-prefs"))
		dialog_search_replace_save_in_prefs (dd);


	err = gnm_search_replace_verify (sr, TRUE);
	if (err) {
		go_gtk_notice_dialog (GTK_WINDOW (dialog), GTK_MESSAGE_ERROR,
				      "%s", err);
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

	cb (wbcg, sr);
	g_object_unref (sr);
}

static void
ok_clicked (GtkWidget *widget, DialogState *dd)
{
        apply_clicked (widget, dd);

	gtk_widget_destroy (GTK_WIDGET (dd->dialog));
	dd = NULL;  /* Destroyed */
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
	g_object_unref (dd->gui);
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}

static gboolean
range_focused (G_GNUC_UNUSED GtkWidget *widget,
	       G_GNUC_UNUSED GdkEventFocus *event,
	       DialogState *dd)
{
	GtkWidget *scope_range = go_gtk_builder_get_widget (dd->gui, "scope_range");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scope_range), TRUE);
	return FALSE;
}

/**
 * dialog_search_replace: (skip)
 */
void
dialog_search_replace (WBCGtk *wbcg,
		       SearchDialogCallback cb)
{
	GtkBuilder *gui;
	GtkDialog *dialog;
	DialogState *dd;
	GtkGrid *grid;
	char *selection_text;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	if (gnm_dialog_raise_if_exists (wbcg, SEARCH_REPLACE_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/search-replace.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (go_gtk_builder_get_widget (gui, "search_replace_dialog"));

	/* Fairly silly: we need to destroy the other dialog in the file. */
	gtk_widget_destroy (go_gtk_builder_get_widget (gui, "query_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->cb = cb;
	dd->dialog = dialog;

	grid = GTK_GRID (go_gtk_builder_get_widget (gui, "normal-grid"));
	dd->search_text = GTK_ENTRY (gtk_entry_new ());
	gtk_widget_set_hexpand (GTK_WIDGET (dd->search_text), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (dd->search_text), 1, 1, 2, 1);
	gnm_editable_enters (GTK_WINDOW (dialog),
				  GTK_WIDGET (dd->search_text));

	dd->replace_text = GTK_ENTRY (gtk_entry_new ());
	gtk_widget_set_hexpand (GTK_WIDGET (dd->replace_text), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (dd->replace_text), 1, 2, 2, 1);
	gnm_editable_enters (GTK_WINDOW (dialog),
				  GTK_WIDGET (dd->replace_text));

	dd->rangetext = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (dd->rangetext, 0, GNM_EE_MASK);
	gtk_widget_set_hexpand (GTK_WIDGET (dd->rangetext), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (dd->rangetext), 1, 9, 2, 1);
	selection_text = selection_to_string (
		wb_control_cur_sheet_view (GNM_WBC (wbcg)),
		TRUE);
	gnm_expr_entry_load_from_text  (dd->rangetext, selection_text);
	g_free (selection_text);
	gtk_widget_show (GTK_WIDGET (dd->rangetext));

#define SETW(w,f) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (gui, w)),  f())
	SETW("search_expr", gnm_conf_get_searchreplace_change_cell_expressions);
	SETW("search_other", gnm_conf_get_searchreplace_change_cell_other);
	SETW("search_string", gnm_conf_get_searchreplace_change_cell_strings);
	SETW("search_comments", gnm_conf_get_searchreplace_change_comments);
	SETW("ignore_case", gnm_conf_get_searchreplace_ignore_case);
	SETW("keep_strings", gnm_conf_get_searchreplace_keep_strings);
	SETW("preserve_case", gnm_conf_get_searchreplace_preserve_case);
	SETW("query", gnm_conf_get_searchreplace_query);
	SETW("match_words", gnm_conf_get_searchreplace_whole_words_only);
#undef SETW

	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON
	   (go_gtk_builder_get_widget
	    (gui,
	     search_type_group[gnm_conf_get_searchreplace_regex () ? 1 : 0])), TRUE);
	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON
	   (go_gtk_builder_get_widget
	    (gui,
	     direction_group[gnm_conf_get_searchreplace_columnmajor () ? 1 : 0])), TRUE);
	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON
	   (go_gtk_builder_get_widget
	    (gui,
	     error_group[gnm_conf_get_searchreplace_error_behaviour ()])), TRUE);
	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON
	   (go_gtk_builder_get_widget
	    (gui,
	     scope_group[gnm_conf_get_searchreplace_scope ()])), TRUE);


	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
		"clicked",
		G_CALLBACK (ok_clicked), dd);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "apply_button")),
		"clicked",
		G_CALLBACK (apply_clicked), dd);
	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cancel_clicked), dd);
	g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (dd->rangetext)),
		"focus-in-event",
		G_CALLBACK (range_focused), dd);

	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SEARCH_REPLACE);
	g_object_set_data_full (G_OBJECT (dialog),
		"state", dd, (GDestroyNotify) cb_dialog_destroy);

	gtk_widget_show_all (gtk_dialog_get_content_area (dialog));
	gtk_widget_grab_focus (GTK_WIDGET (dd->search_text));

	gnm_dialog_setup_destroy_handlers (dialog, wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	wbc_gtk_attach_guru (wbcg, GTK_WIDGET (dialog));

	gnm_keyed_dialog (wbcg, GTK_WINDOW (dialog), SEARCH_REPLACE_KEY);
	gtk_widget_show (GTK_WIDGET (dialog));
}

int
dialog_search_replace_query (WBCGtk *wbcg,
			     GnmSearchReplace *sr,
			     const char *location,
			     const char *old_text,
			     const char *new_text)
{
	GtkBuilder *gui;
	GtkDialog *dialog;
	int res;

	g_return_val_if_fail (wbcg != NULL, 0);

	gui = gnm_gtk_builder_load ("res:ui/search-replace.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return 0;

	dialog = GTK_DIALOG (go_gtk_builder_get_widget (gui, "query_dialog"));

	/* Fairly silly: we need to destroy the other dialog in the file. */
	gtk_widget_destroy (go_gtk_builder_get_widget (gui, "search_replace_dialog"));

	gtk_entry_set_text (GTK_ENTRY (go_gtk_builder_get_widget (gui, "qd_location")),
			    location);
	gtk_entry_set_text (GTK_ENTRY (go_gtk_builder_get_widget (gui, "qd_old_text")),
			    old_text);
	gtk_entry_set_text (GTK_ENTRY (go_gtk_builder_get_widget (gui, "qd_new_text")),
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
	g_object_unref (gui);

	return res;
}

/* ------------------------------------------------------------------------- */

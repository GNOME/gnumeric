/*
 * dialog-search.c:
 *   Dialog for entering a search query.
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
#include <sheet.h>
#include <workbook.h>
#include <workbook-view.h>
#include <selection.h>
#include <cell.h>
#include <value.h>
#include <parse-util.h>
#include <workbook-edit.h>
#include <sheet-object-cell-comment.h>
#include <selection.h>

#include <widgets/gnumeric-expr-entry.h>
#include <libgnomeui/gnome-entry.h>
#include <glade/glade.h>

#define SEARCH_KEY "search-dialog"

enum {
	COL_SHEET = 0,
	COL_CELL,
	COL_TYPE,
	COL_CONTENTS
};

typedef struct {
	WorkbookControlGUI *wbcg;

	GladeXML *gui;
	GtkDialog *dialog;
	GnumericExprEntry *rangetext;
	GnomeEntry *gentry;
	GtkWidget *prev_button, *next_button;
	GtkNotebook *notebook;
	int notebook_matches_page;

	GtkTreeView *matches_table;
	GtkTreeModel *matches_model;

	GPtrArray *matches;
} DialogState;

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

/* ------------------------------------------------------------------------- */

static void
free_state (DialogState *dd)
{
	search_filter_matching_free (dd->matches);
#if 0
	clear_strings (dd);
	g_hash_table_destroy (dd->e_table_strings);
#endif
	g_object_unref (G_OBJECT (dd->gui));
	g_object_unref (G_OBJECT (dd->matches_model));
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}

static void
non_model_dialog (WorkbookControlGUI *wbcg,
		  GtkDialog *dialog,
		  const char *key)
{
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), key);

	gtk_widget_show_all (GTK_WIDGET (dialog));
}

static gboolean
range_focused (GtkWidget *widget, GdkEventFocus   *event, DialogState *dd)
{
	GtkWidget *scope_range = glade_xml_get_widget (dd->gui, "scope_range");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (scope_range), TRUE);
	return FALSE;
}

static void
dialog_destroy (GtkWidget *widget, DialogState *dd)
{
	wbcg_edit_detach_guru (dd->wbcg);
	free_state (dd);
}

static void
close_clicked (GtkWidget *widget, DialogState *dd)
{
	GtkDialog *dialog = dd->dialog;
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gboolean
is_checked (GladeXML *gui, const char *name)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
}

static void
cursor_change (GtkTreeView *tree_view, DialogState *dd)
{
	int matchno;
	int lastmatch = dd->matches->len - 1;
	GtkTreePath *path;

	gtk_tree_view_get_cursor (tree_view, &path, NULL);
	if (path) {
		matchno = gtk_tree_path_get_indices (path)[0];
		gtk_tree_path_free (path);
	} else {
		matchno = -1;
	}

	gtk_widget_set_sensitive (dd->prev_button, matchno > 0);
	gtk_widget_set_sensitive (dd->next_button,
				  matchno >= 0 && matchno < lastmatch);

	if (matchno >= 0 && matchno <= lastmatch) {
		SearchFilterResult *item = g_ptr_array_index (dd->matches, matchno);
		int col = item->ep.eval.col;
		int row = item->ep.eval.row;
		WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (dd->wbcg));
		SheetView *sv;

		wb_view_sheet_focus (wbv, item->ep.sheet);
		sv = wb_view_cur_sheet_view (wbv);
		sv_selection_set (sv, &item->ep.eval, col, row, col, row);
		sv_make_cell_visible (sv, col, row, FALSE);
	}
}


static void
search_clicked (GtkWidget *widget, DialogState *dd)
{
	GladeXML *gui = dd->gui;
	WorkbookControlGUI *wbcg = dd->wbcg;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SearchReplace *sr;
	char *err;
	int i;

	sr = search_replace_new ();

	sr->search_text = g_strdup (gtk_entry_get_text (GTK_ENTRY
							(gnome_entry_gtk_entry (dd->gentry))));
	sr->replace_text = NULL;

	i = gnumeric_glade_group_value (gui, search_type_group);
	sr->is_regexp = (i == 1);

	i = gnumeric_glade_group_value (gui, scope_group);
	sr->scope = (i == -1) ? SRS_sheet : (SearchReplaceScope)i;

	/* FIXME: parsing of an gnm_expr_entry should happen by the gee */
	sr->range_text = g_strdup (gnm_expr_entry_get_text (dd->rangetext));
	sr->curr_sheet = wb_control_cur_sheet (wbc);

#if 0
	if (dd->repl) {
		sr->query = is_checked (gui, "query");
		sr->preserve_case = is_checked (gui, "preserve_case");
	}
#endif
	sr->ignore_case = is_checked (gui, "ignore_case");
	sr->match_words = is_checked (gui, "match_words");

	sr->search_strings = is_checked (gui, "search_string");
	sr->search_other_values = is_checked (gui, "search_other");
	sr->search_expressions = is_checked (gui, "search_expr");
	sr->search_comments = is_checked (gui, "search_comments");

#if 0
	if (dd->repl) {
		i = gnumeric_glade_group_value (gui, error_group);
		sr->error_behaviour = (i == -1) ? SRE_fail : (SearchReplaceError)i;
	}
#endif

	i = gnumeric_glade_group_value (gui, direction_group);
	sr->by_row = (i == 0);

	err = search_replace_verify (sr, FALSE);
	if (err) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR, err);
		g_free (err);
		search_replace_free (sr);
		return;
	} else if (!sr->search_strings &&
		   !sr->search_other_values &&
		   !sr->search_expressions &&
		   !sr->search_comments) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("You must select some cell types to search."));
		search_replace_free (sr);
		return;
	}

	{
		unsigned int i;
		GtkTreeStore *tree_store = GTK_TREE_STORE (dd->matches_model);
		GPtrArray *cells = search_collect_cells (sr, wb_control_cur_sheet (wbc));

		search_filter_matching_free (dd->matches);
		dd->matches = search_filter_matching (sr, cells);
		search_collect_cells_free (cells);

		gtk_tree_store_clear (tree_store);
		for (i = 0; i < dd->matches->len; i++) {
			SearchFilterResult *item = g_ptr_array_index (dd->matches, i);
			Cell *cell = item->cell;
			GtkTreeIter iter;
			const char *type;
			char *content;

			if (cell) {
				Value *v = cell->value;

				gboolean is_expr = cell_has_expr (cell);
				gboolean is_value = !is_expr && !cell_is_blank (cell) && v;

				if (is_expr)
					type = _("Expression");
				else if (is_value && v->type == VALUE_STRING)
					type = _("String");
				else if (is_value && v->type == VALUE_INTEGER)
					type = _("Integer");
				else if (is_value && v->type == VALUE_FLOAT)
					type = _("Number");
				else
					type = _("Other value");
				content = cell_get_entered_text (cell);
			} else {
				type = _("Comment");
				content = g_strdup (cell_comment_text_get (item->comment));
			}

			gtk_tree_store_append (tree_store, &iter, NULL);
			gtk_tree_store_set (tree_store, &iter, COL_SHEET,
					    item->ep.sheet->name_unquoted, -1);
			gtk_tree_store_set (tree_store, &iter, COL_CELL,
					    cellpos_as_string (&item->ep.eval), -1);
			gtk_tree_store_set (tree_store, &iter, COL_TYPE,
					    type, -1);
			gtk_tree_store_set (tree_store, &iter, COL_CONTENTS,
					    content, -1);

			g_free (content);
		}

		/* Set sensitivity of buttons.  */
		cursor_change (dd->matches_table, dd);
	}

	gtk_notebook_set_page (dd->notebook, dd->notebook_matches_page);
	gtk_widget_grab_focus (GTK_WIDGET (dd->matches_table));

	/* Save the contents of the search in the gnome-entry. */
	gnome_entry_append_history (dd->gentry, TRUE, sr->search_text);

	search_replace_free (sr);
}

static void
prev_next_clicked (DialogState *dd, int delta)
{
	gboolean res;
	GtkWidget *w = GTK_WIDGET (dd->matches_table);

	gtk_widget_grab_focus (w);
	g_signal_emit_by_name (w, "move_cursor",
			       GTK_MOVEMENT_DISPLAY_LINES, delta,
			       &res);
}

static void
prev_clicked (GtkWidget *widget, DialogState *dd)
{
	prev_next_clicked (dd, -1);
}

static void
next_clicked (GtkWidget *widget, DialogState *dd)
{
	prev_next_clicked (dd, +1);
}

static void
cb_focus_on_entry (GtkWidget *widget, GtkWidget *entry)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
		gtk_widget_grab_focus (GTK_WIDGET (gnm_expr_entry_get_entry
						   (GNUMERIC_EXPR_ENTRY (entry))));
}

static const struct {
	const char *title;
	const char *type;
} columns[] = {
	{ N_("Sheet"), "text" },
	{ N_("Cell"), "text" },
	{ N_("Type"), "text" },
	{ N_("Content"), "text" },
	{ 0, 0 }
};

static GtkTreeView *
make_matches_table (GtkTreeModel *model)
{
	GtkTreeView *tree_view;
	int i;

	tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (model));

	for (i = 0; columns[i].title; i++) {
		GtkCellRenderer *cell = gtk_cell_renderer_text_new ();
		GtkTreeViewColumn *column =
			gtk_tree_view_column_new_with_attributes (_(columns[i].title), cell,
								  columns[i].type, i,
								  NULL);
		gtk_tree_view_column_set_sort_column_id (column, i);
		gtk_tree_view_append_column (tree_view, column);
	}

	return tree_view;
}


void
dialog_search (WorkbookControlGUI *wbcg)
{
	GladeXML *gui;
	GtkDialog *dialog;
	DialogState *dd;
	GtkTable *table;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbcg_edit_has_guru (wbcg))
		return;

	if (gnumeric_dialog_raise_if_exists (wbcg, SEARCH_KEY))
		return;

	gui = gnumeric_glade_xml_new (wbcg, "search.glade");
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "search_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->dialog = dialog;
	dd->matches = g_ptr_array_new ();

	dd->prev_button = glade_xml_get_widget (gui, "prev_button");
	dd->next_button = glade_xml_get_widget (gui, "next_button");

	dd->notebook = GTK_NOTEBOOK (glade_xml_get_widget (gui, "notebook"));
	dd->notebook_matches_page =
		gtk_notebook_page_num (dd->notebook,
				       glade_xml_get_widget (gui, "matches_tab"));

	dd->rangetext = gnumeric_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (dd->rangetext, 0, GNUM_EE_MASK);
	table = GTK_TABLE (glade_xml_get_widget (gui, "page1-table"));
	gnm_expr_entry_set_scg (dd->rangetext, wbcg_cur_scg (wbcg));
	gtk_table_attach (table, GTK_WIDGET (dd->rangetext),
			  1, 2, 6, 7,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	{
		char *selection_text =
			selection_to_string (
				wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)),
				TRUE);
		gnm_expr_entry_load_from_text  (dd->rangetext, selection_text);
		g_free (selection_text);
	}

	dd->gentry = GNOME_ENTRY (gnome_entry_new ("search_entry"));
	gtk_table_attach (table, GTK_WIDGET (dd->gentry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_grab_focus (gnome_entry_gtk_entry (dd->gentry));
	gnumeric_editable_enters
		(GTK_WINDOW (dialog), gnome_entry_gtk_entry (dd->gentry));

	dd->matches_model = GTK_TREE_MODEL
		(gtk_tree_store_new (4,
				     G_TYPE_STRING,
				     G_TYPE_STRING,
				     G_TYPE_STRING,
				     G_TYPE_STRING));
	dd->matches_table = make_matches_table (dd->matches_model);

	{
		GtkWidget *scrolled_window =
			gtk_scrolled_window_new (NULL, NULL);
		gtk_container_add (GTK_CONTAINER (scrolled_window),
				   GTK_WIDGET (dd->matches_table));
		gtk_box_pack_start (GTK_BOX (glade_xml_get_widget (gui, "matches_vbox")),
				    scrolled_window,
				    TRUE, TRUE, 0);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
						GTK_POLICY_NEVER,
						GTK_POLICY_ALWAYS);
	}

	/* Set sensitivity of buttons.  */
	cursor_change (dd->matches_table, dd);

	g_signal_connect (G_OBJECT (dd->matches_table),
		"cursor_changed",
		G_CALLBACK (cursor_change), dd);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "search_button")),
		"clicked",
		G_CALLBACK (search_clicked), dd);
	g_signal_connect (G_OBJECT (dd->prev_button),
		"clicked",
		G_CALLBACK (prev_clicked), dd);
	g_signal_connect (G_OBJECT (dd->next_button),
		"clicked",
		G_CALLBACK (next_clicked), dd);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "close_button")),
		"clicked",
		G_CALLBACK (close_clicked), dd);
	g_signal_connect (G_OBJECT (dialog),
		"destroy",
		G_CALLBACK (dialog_destroy), dd);
	g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (dd->rangetext)),
		"focus-in-event",
		G_CALLBACK (range_focused), dd);
	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "scope_range")),
		"toggled",
		G_CALLBACK (cb_focus_on_entry), dd->rangetext);

#warning FIXME: Add correct helpfile address
	gnumeric_init_help_button (
		glade_xml_get_widget (gui, "help_button"),
		"search.html");

	wbcg_edit_attach_guru_with_unfocused_rs (wbcg, GTK_WIDGET (dialog), dd->rangetext);
	non_model_dialog (wbcg, dialog, SEARCH_KEY);
}

/* ------------------------------------------------------------------------- */

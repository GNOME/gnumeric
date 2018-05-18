/*
 * dialog-search.c:
 *   Dialog for entering a search query.
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

#include <gui-util.h>
#include <gnumeric-conf.h>
#include <search.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook.h>
#include <workbook-view.h>
#include <selection.h>
#include <cell.h>
#include <value.h>
#include <parse-util.h>
#include <wbc-gtk.h>
#include <sheet-object-cell-comment.h>
#include <selection.h>

#include <widgets/gnm-expr-entry.h>
#include <string.h>

#define SEARCH_KEY "search-dialog"

#undef USE_GURU

enum {
	COL_SHEET = 0,
	COL_CELL,
	COL_TYPE,
	COL_CONTENTS
};

enum {
	ITEM_MATCH
};

typedef struct {
	WBCGtk *wbcg;

	GtkBuilder *gui;
	GtkDialog *dialog;
	GnmExprEntry *rangetext;
	GtkEntry *gentry;
	GtkWidget *prev_button, *next_button;
	GtkNotebook *notebook;
	int notebook_matches_page;

	GtkTreeView *matches_table;
	GPtrArray *matches;
} DialogState;

static const char * const search_type_group[] = {
	"search_type_text",
	"search_type_regexp",
	"search_type_number",
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

/* ------------------------------------------------------------------------- */

static GtkTreeModel *
make_matches_model (DialogState *dd)
{
	GtkListStore *list_store = gtk_list_store_new (1, G_TYPE_POINTER);
	unsigned ui;
	GPtrArray *matches = dd->matches;

	for (ui = 0; ui < matches->len; ui++) {
		GtkTreeIter iter;

		gtk_list_store_append (list_store, &iter);
		gtk_list_store_set (list_store, &iter,
				    ITEM_MATCH, g_ptr_array_index (matches, ui),
				    -1);
	}

	return GTK_TREE_MODEL (list_store);
}

static void
free_state (DialogState *dd)
{
	gnm_search_filter_matching_free (dd->matches);
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

static gboolean
is_checked (GtkBuilder *gui, const char *name)
{
	GtkWidget *w = go_gtk_builder_get_widget (gui, name);
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
}

static void
dialog_search_save_in_prefs (DialogState *dd)
{
	GtkBuilder *gui = dd->gui;

#define SETW(w,f) f (is_checked (gui, w));
	SETW("search_expr", gnm_conf_set_searchreplace_change_cell_expressions);
	SETW("search_other", gnm_conf_set_searchreplace_change_cell_other);
	SETW("search_string", gnm_conf_set_searchreplace_change_cell_strings);
	SETW("search_comments", gnm_conf_set_searchreplace_change_comments);
	SETW("search_expr_results", gnm_conf_set_searchreplace_search_results);
	SETW("ignore_case", gnm_conf_set_searchreplace_ignore_case);
	SETW("match_words", gnm_conf_set_searchreplace_whole_words_only);
	SETW("column_major", gnm_conf_set_searchreplace_columnmajor);
#undef SETW

	gnm_conf_set_searchreplace_regex
		(go_gtk_builder_group_value (gui, search_type_group));
	gnm_conf_set_searchreplace_scope
		(go_gtk_builder_group_value (gui, scope_group));
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
		GnmSearchFilterResult *item = g_ptr_array_index (dd->matches, matchno);
		int col = item->ep.eval.col;
		int row = item->ep.eval.row;
		WorkbookControl *wbc = GNM_WBC (dd->wbcg);
		WorkbookView *wbv = wb_control_view (wbc);
		SheetView *sv;

		if (!sheet_is_visible (item->ep.sheet))
			return;

		if (wb_control_cur_sheet (wbc) != item->ep.sheet)
			wb_view_sheet_focus (wbv, item->ep.sheet);
		sv = wb_view_cur_sheet_view (wbv);
		gnm_sheet_view_set_edit_pos (sv, &item->ep.eval);
		sv_selection_set (sv, &item->ep.eval, col, row, col, row);
		gnm_sheet_view_make_cell_visible (sv, col, row, FALSE);
		gnm_sheet_view_update (sv);
	}
}


static void
search_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GtkBuilder *gui = dd->gui;
	WBCGtk *wbcg = dd->wbcg;
	WorkbookControl *wbc = GNM_WBC (wbcg);
	GnmSearchReplace *sr;
	char *err;
	int i;
	GnmSearchReplaceScope scope;
	char *text;
	gboolean is_regexp, is_number;

	i = go_gtk_builder_group_value (gui, scope_group);
	scope = (i == -1) ? GNM_SRS_SHEET : (GnmSearchReplaceScope)i;

	i = go_gtk_builder_group_value (gui, search_type_group);
	is_regexp = (i == 1);
	is_number = (i == 2);

	text = gnm_search_normalize (gtk_entry_get_text (dd->gentry));

	sr = g_object_new (GNM_SEARCH_REPLACE_TYPE,
			   "sheet", wb_control_cur_sheet (wbc),
			   "scope", scope,
			   "range-text", gnm_expr_entry_get_text (dd->rangetext),
			   "search-text", text,
			   "is-regexp", is_regexp,
			   "is-number", is_number,
			   "ignore-case", is_checked (gui, "ignore_case"),
			   "match-words", is_checked (gui, "match_words"),
			   "search-strings", is_checked (gui, "search_string"),
			   "search-other-values", is_checked (gui, "search_other"),
			   "search-expressions", is_checked (gui, "search_expr"),
			   "search-expression-results", is_checked (gui, "search_expr_results"),
			   "search-comments", is_checked (gui, "search_comments"),
			   "by-row", go_gtk_builder_group_value (gui, direction_group) == 0,
			   NULL);

	g_free (text);

	err = gnm_search_replace_verify (sr, FALSE);
	if (err) {
		go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
				      GTK_MESSAGE_ERROR, "%s", err);
		g_free (err);
		g_object_unref (sr);
		return;
	} else if (!sr->search_strings &&
		   !sr->search_other_values &&
		   !sr->search_expressions &&
		   !sr->search_expression_results &&
		   !sr->search_comments) {
		go_gtk_notice_dialog (GTK_WINDOW (dd->dialog), GTK_MESSAGE_ERROR,
				      _("You must select some cell types to search."));
		g_object_unref (sr);
		return;
	}

	if  (is_checked (gui, "save-in-prefs"))
		dialog_search_save_in_prefs (dd);

	{
		GtkTreeModel *model;
		GPtrArray *cells;

		/* Clear current table.  */
		gtk_tree_view_set_model (dd->matches_table, NULL);
		gnm_search_filter_matching_free (dd->matches);

		cells = gnm_search_collect_cells (sr);
		dd->matches = gnm_search_filter_matching (sr, cells);
		gnm_search_collect_cells_free (cells);

		model = make_matches_model (dd);
		gtk_tree_view_set_model (dd->matches_table, model);
		g_object_unref (model);

		/* Set sensitivity of buttons.  */
		cursor_change (dd->matches_table, dd);
	}

	gtk_notebook_set_current_page (dd->notebook, dd->notebook_matches_page);
	gtk_widget_grab_focus (GTK_WIDGET (dd->matches_table));

	g_object_unref (sr);
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
prev_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	prev_next_clicked (dd, -1);
}

static void
next_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	prev_next_clicked (dd, +1);
}

static gboolean
cb_next (G_GNUC_UNUSED GtkWidget *widget,
	 G_GNUC_UNUSED gboolean start_editing,
	 DialogState *dd)
{
	prev_next_clicked (dd, +1);
	return TRUE;
}

static void
cb_focus_on_entry (GtkWidget *widget, GtkWidget *entry)
{
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		gtk_widget_grab_focus (GTK_WIDGET (gnm_expr_entry_get_entry
						   (GNM_EXPR_ENTRY (entry))));
}

static void
match_renderer_func (GtkTreeViewColumn *tree_column,
		     GtkCellRenderer   *cr,
		     GtkTreeModel      *model,
		     GtkTreeIter       *iter,
		     gpointer           user_data)
{
	int column = GPOINTER_TO_INT (user_data);
	GnmSearchFilterResult *m;
	GnmCell *cell;
	GnmComment *comment;
	const char *text = NULL;
	char *free_text = NULL;

	gtk_tree_model_get (model, iter, ITEM_MATCH, &m, -1);

	if (m->locus == GNM_SRL_COMMENT) {
		cell = NULL;
		comment = sheet_get_comment (m->ep.sheet, &m->ep.eval);
	} else {
		cell = sheet_cell_get (m->ep.sheet,
				       m->ep.eval.col,
				       m->ep.eval.row);
		comment = NULL;
	}

	switch (column) {
	case COL_SHEET:
		text = m->ep.sheet->name_unquoted;
		break;
	case COL_CELL:
		text = cellpos_as_string (&m->ep.eval);
		break;
	case COL_TYPE:
		switch (m->locus) {
		case GNM_SRL_COMMENT:
			text = _("Comment");
			break;
		case GNM_SRL_VALUE:
			text = _("Result");
			break;
		case GNM_SRL_CONTENTS: {
			GnmValue *v = cell ? cell->value : NULL;
			gboolean is_expr = cell && gnm_cell_has_expr (cell);
			gboolean is_value = !is_expr && !gnm_cell_is_empty (cell) && v;

			if (!cell)
				text = _("Deleted");
			else if (is_expr)
				text = _("Expression");
			else if (is_value && VALUE_IS_STRING (v))
				text = _("String");
			else if (is_value && VALUE_IS_FLOAT (v))
				text = _("Number");
			else
				text = _("Other value");
			break;
		}
		default:
			g_assert_not_reached ();
		}
		break;

	case COL_CONTENTS:
		switch (m->locus) {
		case GNM_SRL_COMMENT:
			text = comment
				? cell_comment_text_get (comment)
				: _("Deleted");
			break;
		case GNM_SRL_VALUE:
			text = cell && cell->value
				? value_peek_string (cell->value)
				: _("Deleted");
			break;
		case GNM_SRL_CONTENTS:
			text = cell
				? (free_text = gnm_cell_get_entered_text (cell))
				: _("Deleted");
			break;
		default:
			g_assert_not_reached ();
		}
		break;

	default:
		g_assert_not_reached ();
	}

	g_object_set (cr, "text", text, NULL);
	g_free (free_text);
}


static GtkTreeView *
make_matches_table (DialogState *dd)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model = GTK_TREE_MODEL (make_matches_model (dd));
	int i;
	static const char *const columns[4] = {
		N_("Sheet"), N_("Cell"), N_("Type"), N_("Content")
	};

	tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (model));

	for (i = 0; i < (int)G_N_ELEMENTS (columns); i++) {
		GtkTreeViewColumn *tvc = gtk_tree_view_column_new ();
		GtkCellRenderer *cr = gtk_cell_renderer_text_new ();

		g_object_set (cr, "single-paragraph-mode", TRUE, NULL);
		if (i == COL_CONTENTS)
			g_object_set (cr, "ellipsize", PANGO_ELLIPSIZE_END, NULL);

		gtk_tree_view_column_set_title (tvc, _(columns[i]));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr,
			 match_renderer_func,
			 GINT_TO_POINTER (i), NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);

		gtk_tree_view_column_set_sizing (tvc, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
		gtk_tree_view_append_column (tree_view, tvc);
	}

	g_object_unref (model);
	return tree_view;
}

void
dialog_search (WBCGtk *wbcg)
{
	GtkBuilder *gui;
	GtkDialog *dialog;
	DialogState *dd;
	GtkGrid *grid;

	g_return_if_fail (wbcg != NULL);

#ifdef USE_GURU
	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;
#endif

	gui = gnm_gtk_builder_load ("res:ui/search.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (gtk_builder_get_object (gui, "search_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->dialog = dialog;
	dd->matches = g_ptr_array_new ();

	dd->prev_button = go_gtk_builder_get_widget (gui, "prev_button");
	dd->next_button = go_gtk_builder_get_widget (gui, "next_button");

	dd->notebook = GTK_NOTEBOOK (gtk_builder_get_object (gui, "notebook"));
	dd->notebook_matches_page =
		gtk_notebook_page_num (dd->notebook,
				       go_gtk_builder_get_widget (gui, "matches_tab"));

	dd->rangetext = gnm_expr_entry_new
		(wbcg,
#ifdef USE_GURU
		 TRUE
#else
		 FALSE
#endif
			);
	gnm_expr_entry_set_flags (dd->rangetext, 0, GNM_EE_MASK);
	grid = GTK_GRID (gtk_builder_get_object (gui, "normal-grid"));
	gtk_widget_set_hexpand (GTK_WIDGET (dd->rangetext), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (dd->rangetext), 1, 6, 1, 1);
	{
		char *selection_text =
			selection_to_string (
				wb_control_cur_sheet_view (GNM_WBC (wbcg)),
				TRUE);
		gnm_expr_entry_load_from_text  (dd->rangetext, selection_text);
		g_free (selection_text);
	}

	dd->gentry = GTK_ENTRY (gtk_entry_new ());
	gtk_widget_set_hexpand (GTK_WIDGET (dd->gentry), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (dd->gentry), 1, 0, 1, 1);
	gtk_widget_grab_focus (GTK_WIDGET (dd->gentry));
	gnm_editable_enters (GTK_WINDOW (dialog), GTK_WIDGET (dd->gentry));

	dd->matches_table = make_matches_table (dd);

	{
		GtkWidget *scrolled_window =
			gtk_scrolled_window_new (NULL, NULL);
		gtk_container_add (GTK_CONTAINER (scrolled_window),
				   GTK_WIDGET (dd->matches_table));
		gtk_box_pack_start (GTK_BOX (gtk_builder_get_object (gui, "matches_vbox")),
				    scrolled_window,
				    TRUE, TRUE, 0);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
						GTK_POLICY_NEVER,
						GTK_POLICY_ALWAYS);
	}

	/* Set sensitivity of buttons.  */
	cursor_change (dd->matches_table, dd);

#define SETW(w,f) gtk_toggle_button_set_active				\
	     (GTK_TOGGLE_BUTTON (gtk_builder_get_object (gui, w)),  f())
	SETW("search_expr", gnm_conf_get_searchreplace_change_cell_expressions);
	SETW("search_other", gnm_conf_get_searchreplace_change_cell_other);
	SETW("search_string", gnm_conf_get_searchreplace_change_cell_strings);
	SETW("search_comments", gnm_conf_get_searchreplace_change_comments);
	SETW("search_expr_results", gnm_conf_get_searchreplace_search_results);
	SETW("ignore_case", gnm_conf_get_searchreplace_ignore_case);
	SETW("match_words", gnm_conf_get_searchreplace_whole_words_only);
#undef SETW

	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON
	   (gtk_builder_get_object
	    (gui,
	     search_type_group[gnm_conf_get_searchreplace_regex ()])), TRUE);
	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON
	   (gtk_builder_get_object
	    (gui,
	     direction_group
	     [gnm_conf_get_searchreplace_columnmajor () ? 1 : 0])), TRUE);
	gtk_toggle_button_set_active
	  (GTK_TOGGLE_BUTTON
	   (gtk_builder_get_object
	    (gui,
	     scope_group[gnm_conf_get_searchreplace_scope ()])), TRUE);

	g_signal_connect (G_OBJECT (dd->matches_table), "cursor_changed",
		G_CALLBACK (cursor_change), dd);
	g_signal_connect (G_OBJECT (dd->matches_table), "select_cursor_row",
		G_CALLBACK (cb_next), dd);
	go_gtk_builder_signal_connect (gui, "search_button", "clicked",
		G_CALLBACK (search_clicked), dd);
	g_signal_connect (G_OBJECT (dd->prev_button), "clicked",
		G_CALLBACK (prev_clicked), dd);
	g_signal_connect (G_OBJECT (dd->next_button), "clicked",
		G_CALLBACK (next_clicked), dd);
	go_gtk_builder_signal_connect_swapped (gui, "close_button", "clicked",
		G_CALLBACK (gtk_widget_destroy), dd->dialog);
	g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (dd->rangetext)), "focus-in-event",
		G_CALLBACK (range_focused), dd);
	go_gtk_builder_signal_connect (gui, "scope_range", "toggled",
		G_CALLBACK (cb_focus_on_entry), dd->rangetext);

#ifdef USE_GURU
	wbc_gtk_attach_guru_with_unfocused_rs (wbcg, GTK_WIDGET (dialog), dd->rangetext);
#endif
	g_object_set_data_full (G_OBJECT (dialog),
		"state", dd, (GDestroyNotify) free_state);
	gnm_dialog_setup_destroy_handlers (dialog, wbcg,
		GNM_DIALOG_DESTROY_SHEET_REMOVED);
	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SEARCH);
	gnm_restore_window_geometry (GTK_WINDOW (dialog), SEARCH_KEY);

	go_gtk_nonmodal_dialog (wbcg_toplevel (wbcg), GTK_WINDOW (dialog));
	gtk_widget_show_all (GTK_WIDGET (dialog));
}

/* ------------------------------------------------------------------------- */

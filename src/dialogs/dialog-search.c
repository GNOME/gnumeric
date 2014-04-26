/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

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

#include <widgets/gnumeric-expr-entry.h>
#include <widgets/gnumeric-lazy-list.h>
#include <gtk/gtk.h>
#include <string.h>

#define SEARCH_KEY "search-dialog"

#undef USE_GURU

enum {
	COL_SHEET = 0,
	COL_CELL,
	COL_TYPE,
	COL_CONTENTS
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


/* ------------------------------------------------------------------------- */

static void
search_get_value (gint row, gint column, gpointer _dd, GValue *value)
{
	DialogState *dd = (DialogState *)_dd;
	GnumericLazyList *ll = GNUMERIC_LAZY_LIST (gtk_tree_view_get_model (dd->matches_table));
	GnmSearchFilterResult *item = g_ptr_array_index (dd->matches, row);
	GnmCell *cell;
	GnmComment *comment;

	if (item->locus == GNM_SRL_COMMENT) {
		cell = NULL;
		comment = sheet_get_comment (item->ep.sheet, &item->ep.eval);
	} else {
		cell = sheet_cell_get (item->ep.sheet,
				       item->ep.eval.col,
				       item->ep.eval.row);
		comment = NULL;
	}

	g_value_init (value, ll->column_headers[column]);

#if 0
	g_print ("col=%d,row=%d\n", column, row);
#endif

	switch (column) {
	case COL_SHEET:
		g_value_set_string (value, item->ep.sheet->name_unquoted);
		return;
	case COL_CELL:
		g_value_set_string (value, cellpos_as_string (&item->ep.eval));
		return;
	case COL_TYPE:
		switch (item->locus) {
		case GNM_SRL_COMMENT:
			g_value_set_static_string (value, _("Comment"));
			return;
		case GNM_SRL_VALUE:
			g_value_set_static_string (value, _("Result"));
			return;
		case GNM_SRL_CONTENTS: {
			GnmValue *v = cell ? cell->value : NULL;
			char const *type;

			gboolean is_expr = cell && gnm_cell_has_expr (cell);
			gboolean is_value = !is_expr && !gnm_cell_is_empty (cell) && v;

			if (!cell)
				type = _("Deleted");
			else if (is_expr)
				type = _("Expression");
			else if (is_value && VALUE_IS_STRING (v))
				type = _("String");
			else if (is_value && VALUE_IS_FLOAT (v))
				type = _("Number");
			else
				type = _("Other value");

			g_value_set_static_string (value, type);
			return;
		}

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
#endif
		}

	case COL_CONTENTS:
		switch (item->locus) {
		case GNM_SRL_COMMENT:
			if (comment)
				g_value_set_string (value, cell_comment_text_get (comment));
			else
				g_value_set_static_string (value, _("Deleted"));
			return;
		case GNM_SRL_VALUE:
			if (cell && cell->value)
				g_value_take_string (value, value_get_as_string (cell->value));
			else
				g_value_set_static_string (value, _("Deleted"));
			return;
		case GNM_SRL_CONTENTS:
			if (cell)
				g_value_take_string (value, gnm_cell_get_entered_text (cell));
			else
				g_value_set_static_string (value, _("Deleted"));
			return;
#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
#endif
		}

#ifndef DEBUG_SWITCH_ENUM
	default:
		g_assert_not_reached ();
#endif
	}
}

/* ------------------------------------------------------------------------- */

static GnumericLazyList *
make_matches_model (DialogState *dd, int rows)
{
	return gnumeric_lazy_list_new (search_get_value,
				       dd,
				       rows,
				       4,
				       G_TYPE_STRING,
				       G_TYPE_STRING,
				       G_TYPE_STRING,
				       G_TYPE_STRING);
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
		WorkbookControl *wbc = WORKBOOK_CONTROL (dd->wbcg);
		WorkbookView *wbv = wb_control_view (wbc);
		SheetView *sv;

		if (!sheet_is_visible (item->ep.sheet))
			return;

		if (wb_control_cur_sheet (wbc) != item->ep.sheet)
			wb_view_sheet_focus (wbv, item->ep.sheet);
		sv = wb_view_cur_sheet_view (wbv);
		sv_set_edit_pos (sv, &item->ep.eval);
		sv_selection_set (sv, &item->ep.eval, col, row, col, row);
		sv_make_cell_visible (sv, col, row, FALSE);
		sv_update (sv);
	}
}


static void
search_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GtkBuilder *gui = dd->gui;
	WBCGtk *wbcg = dd->wbcg;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
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
		GnumericLazyList *ll;
		GPtrArray *cells;

		/* Clear current table.  */
		gtk_tree_view_set_model (dd->matches_table, NULL);
		gnm_search_filter_matching_free (dd->matches);

		cells = gnm_search_collect_cells (sr);
		dd->matches = gnm_search_filter_matching (sr, cells);
		gnm_search_collect_cells_free (cells);

		ll = make_matches_model (dd, dd->matches->len);
		gtk_tree_view_set_model (dd->matches_table, GTK_TREE_MODEL (ll));
		g_object_unref (ll);

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

static const struct {
	const char *title;
	const char *type;
} columns[] = {
	{ N_("Sheet"), "text" },
	{ N_("Cell"), "text" },
	{ N_("Type"), "text" },
	{ N_("Content"), "text" }
};

static GtkTreeView *
make_matches_table (DialogState *dd)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model = GTK_TREE_MODEL (make_matches_model (dd, 0));
	int i;

	tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (model));
#ifdef NOT_YET
	/* Gtk+ isn't ready yet -- 20031224.  */
	g_object_set (tree_view, "fixed-height-mode", TRUE, NULL);
#endif

	for (i = 0; i < (int)G_N_ELEMENTS (columns); i++) {
		GtkCellRenderer *cell = gtk_cell_renderer_text_new ();
		GtkTreeViewColumn *column =
			gtk_tree_view_column_new_with_attributes (_(columns[i].title), cell,
								  columns[i].type, i,
								  NULL);
		/* Set single_paragraph_mode to ensure fixed height.  */
		g_object_set (cell, "single-paragraph-mode", TRUE, NULL);
		if (i == COL_CONTENTS)
			g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
		gtk_tree_view_append_column (tree_view, column);
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

	gui = gnm_gtk_builder_load ("search.ui", NULL, GO_CMD_CONTEXT (wbcg));
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
				wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)),
				TRUE);
		gnm_expr_entry_load_from_text  (dd->rangetext, selection_text);
		g_free (selection_text);
	}

	dd->gentry = GTK_ENTRY (gtk_entry_new ());
	gtk_widget_set_hexpand (GTK_WIDGET (dd->gentry), TRUE);
	gtk_grid_attach (grid, GTK_WIDGET (dd->gentry), 1, 0, 1, 1);
	gtk_widget_grab_focus (GTK_WIDGET (dd->gentry));
	gnumeric_editable_enters (GTK_WINDOW (dialog), GTK_WIDGET (dd->gentry));

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
	gnumeric_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SEARCH);
	gnumeric_restore_window_geometry (GTK_WINDOW (dialog), SEARCH_KEY);

	go_gtk_nonmodal_dialog (wbcg_toplevel (wbcg), GTK_WINDOW (dialog));
	gtk_widget_show_all (GTK_WIDGET (dialog));
}

/* ------------------------------------------------------------------------- */

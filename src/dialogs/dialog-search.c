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
#undef GTK_DISABLE_DEPRECATED
#warning "This file uses GTK_DISABLE_DEPRECATED for GtkCombo in GnomeEntry"
#undef GNOME_DISABLE_DEPRECATED
#warning "This file uses GNOME_DISABLE_DEPRECATED for GnomeEntry"

#include <gnumeric-config.h>
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <search.h>
#include <sheet.h>
#include <sheet-view.h>
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
#include <widgets/gnumeric-lazy-list.h>
#include <libgnomeui/gnome-entry.h>
#include <glade/glade.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkscrolledwindow.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtktable.h>
#include <string.h>

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
	GnmExprEntry *rangetext;
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


/* ------------------------------------------------------------------------- */

static void
search_get_value (gint row, gint column, gpointer _dd, GValue *value)
{
	DialogState *dd = (DialogState *)_dd;
	GnumericLazyList *ll = GNUMERIC_LAZY_LIST (dd->matches_model);
	SearchFilterResult *item = g_ptr_array_index (dd->matches, row);
	GnmCell *cell = item->cell;

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
		case SRL_commment:
			g_value_set_static_string (value, _("Comment"));
			return;
		case SRL_value:
			g_value_set_static_string (value, _("Result"));
			return;
		case SRL_contents: {
			GnmValue *v = cell->value;
			char const *type;

			gboolean is_expr = cell_has_expr (cell);
			gboolean is_value = !is_expr && !cell_is_empty (cell) && v;

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
		case SRL_commment:
			g_value_set_string (value, cell_comment_text_get (item->comment));
			return;
		case SRL_value:
			g_value_take_string (value,
				value_get_as_string (cell->value));
			return;
		case SRL_contents:
			g_value_take_string (value,
				cell_get_entered_text (cell));
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

static void
free_state (DialogState *dd)
{
	search_filter_matching_free (dd->matches);
	g_object_unref (G_OBJECT (dd->gui));
	g_object_unref (G_OBJECT (dd->matches_model));
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
dialog_destroy (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	wbcg_edit_detach_guru (dd->wbcg);
	free_state (dd);
}

static void
close_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
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
		WorkbookControl *wbc = WORKBOOK_CONTROL (dd->wbcg);
		WorkbookView *wbv = wb_control_view (wbc);
		SheetView *sv;

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
	GladeXML *gui = dd->gui;
	WorkbookControlGUI *wbcg = dd->wbcg;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmSearchReplace *sr;
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
	sr->search_expression_results = is_checked (gui, "search_expr_results");
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
		   !sr->search_expression_results &&
		   !sr->search_comments) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("You must select some cell types to search."));
		search_replace_free (sr);
		return;
	}

	{
		GnumericLazyList *ll = GNUMERIC_LAZY_LIST (dd->matches_model);
		GPtrArray *cells = search_collect_cells (sr, wb_control_cur_sheet (wbc));

		gnumeric_lazy_list_set_rows (ll, 0);

		search_filter_matching_free (dd->matches);
		dd->matches = search_filter_matching (sr, cells);
		search_collect_cells_free (cells);

		gnumeric_lazy_list_set_rows (ll, dd->matches->len);

		/* Set sensitivity of buttons.  */
		cursor_change (dd->matches_table, dd);
	}

	gtk_notebook_set_current_page (dd->notebook, dd->notebook_matches_page);
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
prev_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	prev_next_clicked (dd, -1);
}

static void
next_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	prev_next_clicked (dd, +1);
}

static void
cb_focus_on_entry (GtkWidget *widget, GtkWidget *entry)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
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
	{ N_("Content"), "text" },
	{ 0, 0 }
};

static GtkTreeView *
make_matches_table (GtkTreeModel *model)
{
	GtkTreeView *tree_view;
	int i;

	tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (model));
#ifdef NOT_YET
	/* Gtk+ isn't really yet -- 20031224.  */
	g_object_set (tree_view, "fixed_height_mode", TRUE, NULL);
#endif

	for (i = 0; columns[i].title; i++) {
		GtkCellRenderer *cell = gtk_cell_renderer_text_new ();
		GtkTreeViewColumn *column =
			gtk_tree_view_column_new_with_attributes (_(columns[i].title), cell,
								  columns[i].type, i,
								  NULL);
		/* Set single_paragraph_mode to ensure fixed height.  */
		g_object_set (cell, "single_paragraph_mode", TRUE, NULL);
		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
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
	if (wbcg_edit_get_guru (wbcg))
		return;

	if (gnumeric_dialog_raise_if_exists (wbcg, SEARCH_KEY))
		return;

	gui = gnm_glade_xml_new (GNM_CMD_CONTEXT (wbcg),
		"search.glade", NULL, NULL);
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

	dd->rangetext = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (dd->rangetext, 0, GNM_EE_MASK);
	table = GTK_TABLE (glade_xml_get_widget (gui, "page1-table"));
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
		(gnumeric_lazy_list_new (search_get_value,
					 dd,
					 4,
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

	gnumeric_init_help_button (
		glade_xml_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_SEARCH);

	wbcg_edit_attach_guru_with_unfocused_rs (wbcg, GTK_WIDGET (dialog), dd->rangetext);

	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), SEARCH_KEY);
	gtk_widget_show_all (GTK_WIDGET (dialog));
}

/* ------------------------------------------------------------------------- */

/*
 * dialog-search.c:
 *   Dialog for entering a search query.
 *
 * Author:
 *   Morten Welinder (terra@diku.dk)
 */
#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include "gnumeric.h"
#include "gui-util.h"
#include "dialogs.h"
#include "search.h"
#include "sheet.h"
#include "workbook.h"
#include "workbook-view.h"
#include "selection.h"
#include "cell.h"
#include "value.h"
#include "parse-util.h"
#include "workbook-edit.h"
#include "sheet-object-cell-comment.h"

#include "widgets/gnumeric-expr-entry.h"
#include "gal/widgets/e-cursors.h"
#include "gal/e-table/e-table-simple.h"
#include "gal/e-table/e-table.h"
#include "gal/e-table/e-table-scrolled.h"

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
	GnomeDialog *dialog;
	GnumericExprEntry *rangetext;
	GtkWidget *prev_button, *next_button;
	GtkNotebook *notebook;
	int notebook_matches_page;

	ETable *e_table;
	ETableScrolled *e_table_scrolled;
	ETableModel *e_table_model;
	GHashTable *e_table_strings;

	GPtrArray *matches;
} DialogState;

#if 0
static const char *error_group[] = {
	"error_fail",
	"error_skip",
	"error_query",
	"error_error",
	"error_string",
	0
};
#endif

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

static int
col_count (ETableModel *etc, void *data)
{
	return 4;
}

static int
row_count (ETableModel *etc, void *data)
{
	DialogState *dd = data;
	return dd->matches->len;
}

static void *
value_at (ETableModel *etc, int col, int row, void *data)
{
	DialogState *dd = data;
	SearchFilterResult *item = g_ptr_array_index (dd->matches, row);
	Cell *cell = item->cell;
	char *result, *cached;

	switch (col) {
	case COL_SHEET:
		return (void *)(item->ep.sheet->name_unquoted);

	case COL_CELL:
		result = g_strdup (cell_pos_name (&item->ep.eval));
		break;

	case COL_TYPE:
		if (cell) {
			Value *v = cell->value;

			gboolean is_expr = cell_has_expr (cell);
			gboolean is_value = !is_expr && !cell_is_blank (cell) && v;

			if (is_expr)
				return (void *)_("Expression");
			else if (is_value && v->type == VALUE_STRING)
				return (void *)_("String");
			else if (is_value && v->type == VALUE_INTEGER)
				return (void *)_("Integer");
			else if (is_value && v->type == VALUE_FLOAT)
				return (void *)_("Number");
			else
				return (void *)_("Other value");
		} else
			return (void *)_("Comment");

	case COL_CONTENTS:
		if (cell) {
			result = cell_get_entered_text (cell);
			break;
		} else
			return (void *)cell_comment_text_get (item->comment);

	default:
		return NULL;
	}

	cached = g_hash_table_lookup (dd->e_table_strings, result);
	if (cached) {
		g_free (result);
		return cached;
	}

	g_hash_table_insert (dd->e_table_strings, result, result);
	return result;
}

static void *
duplicate_value (ETableModel *etc, int col, const void *value, void *data)
{
	return g_strdup (value);
}

static void
free_value (ETableModel *etc, int col, void *value, void *data)
{
	g_free (value);
}

static void *
initialize_value (ETableModel *etc, int col, void *data)
{
	return g_strdup ("");
}

static gboolean
value_is_empty (ETableModel *etc, int col, const void *value, void *data)
{
	return !(value && *(char *)value);
}

static char *
value_to_string (ETableModel *etc, int col, const void *value, void *data)
{
	return g_strdup (value);
}

/* ------------------------------------------------------------------------- */

static gboolean
cb_clear_strings (gpointer	key, gpointer value, gpointer user_data)
{
	g_free (key);
	return TRUE;
}

static void
clear_strings (DialogState *dd)
{
	g_hash_table_foreach_remove (dd->e_table_strings, cb_clear_strings, NULL);
}

static void
free_state (DialogState *dd)
{
	search_filter_matching_free (dd->matches);
	clear_strings (dd);
	g_hash_table_destroy (dd->e_table_strings);
	gtk_object_unref (GTK_OBJECT (dd->gui));
	gtk_object_unref (GTK_OBJECT (dd->e_table_model));
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}

static void
non_model_dialog (WorkbookControlGUI *wbcg,
		  GnomeDialog *dialog,
		  const char *key)
{
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), key);

	gtk_widget_show (GTK_WIDGET (dialog));
}

static void
set_focus (GtkWidget *widget, GtkWidget *focus_widget, DialogState *dd)
{
	if (IS_GNUMERIC_EXPR_ENTRY (focus_widget))
		wbcg_set_entry (dd->wbcg,
				    GNUMERIC_EXPR_ENTRY (focus_widget));
	else
		wbcg_set_entry (dd->wbcg, NULL);

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
	GnomeDialog *dialog = dd->dialog;
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static char *
get_text (GladeXML *gui, const char *name)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return g_strdup (gtk_entry_get_text (GTK_ENTRY (w)));
}

static gboolean
is_checked (GladeXML *gui, const char *name)
{
	GtkWidget *w = glade_xml_get_widget (gui, name);
	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w));
}

static void
cb_selection_changed (int row, gpointer closure)
{
	/* Silly, eh?  */
	*(int *)closure = row;
}

static void
selection_changed (GtkWidget *w, DialogState *dd)
{
	int matchno = -1;
	e_selection_model_foreach (E_SELECTION_MODEL (dd->e_table->selection),
				   cb_selection_changed,
				   &matchno);

	gtk_widget_set_sensitive (dd->prev_button, matchno > 0);
	gtk_widget_set_sensitive (dd->next_button,
				  matchno >= 0 && matchno < (int)dd->matches->len - 1);

	if (matchno >= 0) {
		SearchFilterResult *item = g_ptr_array_index (dd->matches, matchno);
		Sheet *sheet = item->ep.sheet;
		int col = item->ep.eval.col;
		int row = item->ep.eval.row;

		WORKBOOK_FOREACH_VIEW (sheet->workbook, view, {
			wb_view_sheet_focus (view, sheet);
		});
		sheet_selection_set (sheet, col, row, col, row, col, row);
		sheet_make_cell_visible (sheet, col, row, FALSE);
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

	sr->search_text = get_text (gui, "searchtext");
	sr->replace_text = NULL;

	i = gnumeric_glade_group_value (gui, search_type_group);
	sr->is_regexp = (i == 1);

	i = gnumeric_glade_group_value (gui, scope_group);
	sr->scope = (i == -1) ? SRS_sheet : (SearchReplaceScope)i;
	sr->range_text = g_strdup (
		gtk_entry_get_text (GTK_ENTRY (dd->rangetext)));
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
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, err);
		g_free (err);
		search_replace_free (sr);
		return;
	} else if (!sr->search_strings &&
		   !sr->search_other_values &&
		   !sr->search_expressions &&
		   !sr->search_comments) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You must select some cell types to search."));
		search_replace_free (sr);
		return;
	}

	{
		GPtrArray *cells = search_collect_cells (sr, wb_control_cur_sheet (wbc));
		search_filter_matching_free (dd->matches);
		dd->matches = search_filter_matching (sr, cells);
		search_collect_cells_free (cells);

		e_table_model_pre_change (dd->e_table_model);
		e_table_model_changed (dd->e_table_model);
		clear_strings (dd);

		/*
		 * The following seems necessary in order to force the scrollbar
		 * to resize.
		 */
		gtk_widget_queue_resize (GTK_WIDGET (dd->e_table_scrolled));

		e_selection_model_select_single_row (E_SELECTION_MODEL (dd->e_table->selection),
						     0);
		e_table_set_cursor_row (dd->e_table, 0);
		selection_changed (NULL, dd);
	}

	gtk_notebook_set_page (dd->notebook, dd->notebook_matches_page);

	search_replace_free (sr);
}


static void
prev_next_clicked (DialogState *dd, int delta)
{
	int current = e_table_get_cursor_row (dd->e_table);

	if (current != -1) {
		current += delta;
		e_selection_model_select_single_row (E_SELECTION_MODEL (dd->e_table->selection),
						     current);
		e_table_set_cursor_row (dd->e_table, current);
	}
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

void
dialog_search (WorkbookControlGUI *wbcg)
{
	GladeXML *gui;
	GnomeDialog *dialog;
	DialogState *dd;
	const char *spec = "\
<ETableSpecification cursor-mode=\"line\"\
                     selection-mode=\"single\"\
                     allow_grouping=\"false\"\
                     draw-focus=\"true\">\
  <ETableColumn model_col=\"0\" _title=\"Sheet\" minimum_width=\"50\" resizable=\"true\" cell=\"string\" compare=\"string\"/>\n\
  <ETableColumn model_col=\"1\" _title=\"Cell\" minimum_width=\"40\" resizable=\"true\" cell=\"string\" compare=\"string\"/>\n\
  <ETableColumn model_col=\"2\" _title=\"Type\" minimum_width=\"70\" resizable=\"true\" cell=\"string\" compare=\"string\"/>\n\
  <ETableColumn model_col=\"3\" _title=\"Content\" minimum_width=\"300\" resizable=\"true\" cell=\"string\" compare=\"string\"/>\n\
  <ETableState>\n\
      <column source=\"0\"/>\n\
      <column source=\"1\"/>\n\
      <column source=\"2\"/>\n\
      <column source=\"3\"/>\n\
      <grouping></grouping>\n\
  </ETableState>\n\
</ETableSpecification>";

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbcg_edit_has_guru (wbcg))
		return;

	if (gnumeric_dialog_raise_if_exists (wbcg, SEARCH_KEY))
		return;

	gui = gnumeric_glade_xml_new (wbcg, "search.glade");
        if (gui == NULL)
                return;

	dialog = GNOME_DIALOG (glade_xml_get_widget (gui, "search_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->dialog = dialog;
	dd->matches = g_ptr_array_new ();
	dd->e_table_strings = g_hash_table_new (g_str_hash, g_str_equal);

	dd->prev_button = glade_xml_get_widget (gui, "prev_button");
	dd->next_button = glade_xml_get_widget (gui, "next_button");

	dd->notebook = GTK_NOTEBOOK (glade_xml_get_widget (gui, "notebook"));
	dd->notebook_matches_page =
		gtk_notebook_page_num (dd->notebook,
				       glade_xml_get_widget (gui, "matches_tab"));

	gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, TRUE, FALSE);

	dd->rangetext = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (wbcg));
	gnumeric_expr_entry_set_flags (dd->rangetext, 0, GNUM_EE_MASK);
	gtk_box_pack_start (GTK_BOX (glade_xml_get_widget (gui, "range_hbox")),
			    GTK_WIDGET (dd->rangetext),
			    TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (dd->rangetext));

	dd->e_table_model =
		e_table_simple_new (col_count, row_count, NULL,
				    value_at, NULL, NULL,
				    NULL, NULL,
				    duplicate_value, free_value,
				    initialize_value, value_is_empty,
				    value_to_string,
				    dd);
	dd->e_table_scrolled =
		E_TABLE_SCROLLED (
			e_table_scrolled_new (dd->e_table_model,
					      NULL, spec, NULL));
	dd->e_table = e_table_scrolled_get_table (dd->e_table_scrolled);
	/* This makes value_at not called just to determine row height.  */
	gtk_object_set (GTK_OBJECT (dd->e_table),
			"uniform_row_height", 1,
			NULL);
	e_scroll_frame_set_policy (E_SCROLL_FRAME (dd->e_table_scrolled),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_ALWAYS);

	gtk_box_pack_start (GTK_BOX (glade_xml_get_widget (gui, "matches_vbox")),
			    GTK_WIDGET (dd->e_table_scrolled),
			    TRUE, TRUE, 0);

	gtk_widget_grab_focus (glade_xml_get_widget (gui, "searchtext"));
	gnome_dialog_editable_enters
		(dialog, GTK_EDITABLE (glade_xml_get_widget (gui, "searchtext")));

	gtk_signal_connect (GTK_OBJECT (dd->e_table->selection),
			    "selection_changed",
			    GTK_SIGNAL_FUNC (selection_changed),
			    dd);

	gtk_signal_connect (GTK_OBJECT (glade_xml_get_widget (gui, "search_button")),
			    "clicked",
			    GTK_SIGNAL_FUNC (search_clicked),
			    dd);
	gtk_signal_connect (GTK_OBJECT (dd->prev_button),
			    "clicked",
			    GTK_SIGNAL_FUNC (prev_clicked),
			    dd);
	gtk_signal_connect (GTK_OBJECT (dd->next_button),
			    "clicked",
			    GTK_SIGNAL_FUNC (next_clicked),
			    dd);

	gtk_signal_connect (GTK_OBJECT (glade_xml_get_widget (gui, "close_button")),
			    "clicked",
			    GTK_SIGNAL_FUNC (close_clicked),
			    dd);
	gtk_signal_connect (GTK_OBJECT (dialog),
			    "destroy",
			    GTK_SIGNAL_FUNC (dialog_destroy),
			    dd);
	gtk_signal_connect (GTK_OBJECT (dialog), "set-focus",
			    GTK_SIGNAL_FUNC (set_focus), dd);
	gtk_signal_connect (GTK_OBJECT (dd->rangetext), "focus-in-event",
			    GTK_SIGNAL_FUNC (range_focused), dd);

	selection_changed (NULL, dd);
	gtk_widget_show_all (dialog->vbox);
	gnumeric_expr_entry_set_scg (dd->rangetext,
				     wb_control_gui_cur_sheet (wbcg));
	wbcg_edit_attach_guru (wbcg, GTK_WIDGET (dialog));

	non_model_dialog (wbcg, dialog, SEARCH_KEY);
}

/* ------------------------------------------------------------------------- */

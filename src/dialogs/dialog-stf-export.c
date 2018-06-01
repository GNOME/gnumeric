/*
 * dialog-stf-export.c : implementation of the STF export dialog
 *
 * Copyright (C) Almer. S. Tigelaar.
 * EMail: almer1@dds.nl or almer-t@bigfoot.com
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
#include <gnumeric.h>
#include <dialogs/dialog-stf-export.h>

#include <command-context.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <sheet.h>
#include <gui-util.h>
#include <gnumeric-conf.h>
#include <goffice/goffice.h>

#include <glib/gi18n-lib.h>
#include <string.h>

typedef enum {
	PAGE_SHEETS,
	PAGE_FORMAT
} TextExportPage;
enum {
	STF_EXPORT_COL_EXPORTED,
	STF_EXPORT_COL_SHEET_NAME,
	STF_EXPORT_COL_SHEET,
	STF_EXPORT_COL_NON_EMPTY,
	STF_EXPORT_COL_MAX
};

typedef struct {
	Workbook		*wb;

	GtkBuilder		*gui;
	WBCGtk	*wbcg;
	GtkWindow		*window;
	GtkWidget		*notebook;
	GtkWidget		*back_button, *next_button, *finish_button;

	struct {
		GtkListStore *model;
		GtkTreeView  *view;
		GtkWidget    *select_all, *select_none;
		GtkWidget    *up, *down, *top, *bottom;
		int num, num_selected, non_empty;
	} sheets;
	struct {
		GtkComboBox	 *termination;
		GtkComboBox	 *separator;
		GtkWidget	 *custom;
		GtkComboBox	 *quote;
		GtkComboBoxText  *quotechar;
		GtkWidget	 *charset;
		GtkWidget	 *locale;
		GtkComboBox	 *transliterate;
		GtkComboBox	 *format;
	} format;

	GnmStfExport *stfe;
	gboolean cancelled;
} TextExportState;

static const char *format_seps[] = {
	" ", "\t", "!", ":", ",", "-", "|", ";", "/", NULL
};

static void
sheet_page_separator_menu_changed (TextExportState *state)
{
	unsigned active = gtk_combo_box_get_active (state->format.separator);
	if (active >= G_N_ELEMENTS (format_seps))
		active = 0;

	if (!format_seps[active]) {
		gtk_widget_grab_focus (state->format.custom);
		gtk_editable_select_region (GTK_EDITABLE (state->format.custom), 0, -1);
	} else {
		gtk_entry_set_text (GTK_ENTRY (state->format.custom),
				    format_seps[active]);
	}
}

static void
cb_custom_separator_changed (TextExportState *state)
{
	const char *text = gtk_entry_get_text (GTK_ENTRY (state->format.custom));
	unsigned active = gtk_combo_box_get_active (state->format.separator);
	unsigned ui;

	for (ui = 0; format_seps[ui]; ui++)
		if (strcmp (text, format_seps[ui]) == 0)
			break;

	if (ui != active)
		gtk_combo_box_set_active (state->format.separator, ui);
}

static void
stf_export_dialog_format_page_init (TextExportState *state)
{
	GtkWidget *grid;
	GObject *sobj = G_OBJECT (state->stfe);

	{
		char *eol;
		int i;

		state->format.termination = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "format_termination"));
		g_object_get (sobj, "eol", &eol, NULL);
		if (strcmp (eol, "\r") == 0)
			i = 1;
		else if (strcmp (eol, "\r\n") == 0)
			i = 2;
		else
			i = 0;
		gtk_combo_box_set_active (state->format.termination, i);
		g_free (eol);
	}

	{
		char *s;
		unsigned ui;
		gint pos = 0;

		state->format.separator = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "format_separator"));
		state->format.custom = go_gtk_builder_get_widget (state->gui, "format_custom");
		g_object_get (sobj, "separator", &s, NULL);
		for (ui = 0; ui < G_N_ELEMENTS (format_seps) - 1; ui++)
			if (strcmp (s, format_seps[ui]) == 0)
				break;
		gtk_combo_box_set_active (state->format.separator, ui);
		if (!format_seps[ui]) {
			gtk_editable_insert_text (GTK_EDITABLE (state->format.custom),
						  s, -1,
						  &pos);
		}
		g_free (s);
	}

	{
		GsfOutputCsvQuotingMode	quotingmode;
		int i;

		state->format.quote = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "format_quote"));
		g_object_get (sobj, "quoting-mode", &quotingmode, NULL);
		switch (quotingmode) {
		default:
		case GSF_OUTPUT_CSV_QUOTING_MODE_AUTO: i = 0; break;
		case GSF_OUTPUT_CSV_QUOTING_MODE_ALWAYS: i = 1; break;
		case GSF_OUTPUT_CSV_QUOTING_MODE_NEVER: i = 2; break;
		}
		gtk_combo_box_set_active (state->format.quote, i);
	}

	{
		char *s;
		gint pos;

		state->format.quotechar = GTK_COMBO_BOX_TEXT (go_gtk_builder_get_widget (state->gui, "format_quotechar"));
		g_object_get (sobj, "quote", &s, NULL);

		gtk_editable_insert_text (GTK_EDITABLE (gtk_bin_get_child (GTK_BIN (state->format.quotechar))),
					  s, -1, &pos);
		g_free (s);
	}

	{
		GnmStfFormatMode format;
		int i;

		state->format.format = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "format"));
		g_object_get (sobj, "format", &format, NULL);
		switch (format) {
		default:
		case GNM_STF_FORMAT_AUTO: i = 0; break;
		case GNM_STF_FORMAT_RAW: i = 1; break;
		case GNM_STF_FORMAT_PRESERVE: i = 2; break;
		}
		gtk_combo_box_set_active (state->format.format, i);
	}

	{
		char *charset;
		state->format.charset = go_charmap_sel_new (GO_CHARMAP_SEL_FROM_UTF8);
		g_object_get (sobj, "charset", &charset, NULL);
		if (charset) {
			go_charmap_sel_set_encoding (GO_CHARMAP_SEL (state->format.charset),
						     charset);
			g_free (charset);
		}
	}

	{
		char *locale;
		state->format.locale = go_locale_sel_new ();
		g_object_get (sobj, "locale", &locale, NULL);
		if (locale) {
			go_locale_sel_set_locale (GO_LOCALE_SEL (state->format.locale),
						  locale);
			g_free (locale);
		}
	}

	{
		GnmStfTransliterateMode mode;
		int i;

		state->format.transliterate = GTK_COMBO_BOX (go_gtk_builder_get_widget (state->gui, "format_transliterate"));
		g_object_get (sobj, "transliterate-mode", &mode, NULL);
		if (!gnm_stf_export_can_transliterate ()) {
			if (mode == GNM_STF_TRANSLITERATE_MODE_TRANS)
				mode = GNM_STF_TRANSLITERATE_MODE_ESCAPE;
			/* It might be better to render insensitive
			 * only one option than the whole list as in
			 * the following line but it is not possible
			 * with gtk-2.4. May be it should be changed
			 * when 2.6 is available (inactivate only the
			 * transliterate item)
			 */
			gtk_widget_set_sensitive (GTK_WIDGET (state->format.transliterate), FALSE);
		}
		switch (mode) {
		default:
		case GNM_STF_TRANSLITERATE_MODE_TRANS: i = 0; break;
		case GNM_STF_TRANSLITERATE_MODE_ESCAPE: i = 1; break;
		}
		gtk_combo_box_set_active (state->format.transliterate, i);
	}

	gnm_editable_enters (state->window, state->format.custom);
	gnm_editable_enters (state->window,
			gtk_bin_get_child (GTK_BIN (state->format.quotechar)));

	grid = go_gtk_builder_get_widget (state->gui, "format-grid");
	gtk_grid_attach (GTK_GRID (grid), state->format.charset,
			 1, 6, 1, 1);
	gtk_grid_attach (GTK_GRID (grid), state->format.locale,
			 1, 7, 1, 1);
	gtk_widget_show_all (grid);

	g_signal_connect_swapped (state->format.separator,
		"changed",
		G_CALLBACK (sheet_page_separator_menu_changed), state);
	g_signal_connect_swapped (state->format.custom,
		"changed",
		G_CALLBACK (cb_custom_separator_changed), state);

	sheet_page_separator_menu_changed (state);
}

static gboolean
cb_collect_exported_sheets (GtkTreeModel *model, G_GNUC_UNUSED GtkTreePath *path, GtkTreeIter *iter,
			    TextExportState *state)
{
	gboolean exported;
	Sheet *sheet;

	gtk_tree_model_get (model, iter,
		STF_EXPORT_COL_EXPORTED, &exported,
		STF_EXPORT_COL_SHEET,	 &sheet,
		-1);
	if (exported)
		gnm_stf_export_options_sheet_list_add (state->stfe, sheet);
	g_object_unref (sheet);
	return FALSE;
}
static void
stf_export_dialog_finish (TextExportState *state)
{
	GsfOutputCsvQuotingMode	quotingmode;
	GnmStfTransliterateMode transliteratemode;
	GnmStfFormatMode format;
	const char *eol;
	GString *triggers = g_string_new (NULL);
	char *separator, *quote;
	const char *charset;
	char *locale;

	/* What options */
	switch (gtk_combo_box_get_active (state->format.termination)) {
	default:
	case 0: eol = "\n"; break;
	case 1: eol = "\r"; break;
	case 2: eol = "\r\n"; break;
	}

	switch (gtk_combo_box_get_active (state->format.quote)) {
	default:
	case 0: quotingmode = GSF_OUTPUT_CSV_QUOTING_MODE_AUTO; break;
	case 1: quotingmode = GSF_OUTPUT_CSV_QUOTING_MODE_ALWAYS; break;
	case 2: quotingmode = GSF_OUTPUT_CSV_QUOTING_MODE_NEVER; break;
	}

	switch (gtk_combo_box_get_active (state->format.transliterate)) {
	case 0 :  transliteratemode = GNM_STF_TRANSLITERATE_MODE_TRANS; break;
	default:
	case 1 :  transliteratemode = GNM_STF_TRANSLITERATE_MODE_ESCAPE; break;
	}

	switch (gtk_combo_box_get_active (state->format.format)) {
	default:
	case 0: format = GNM_STF_FORMAT_AUTO; break;
	case 1: format = GNM_STF_FORMAT_RAW; break;
	case 2: format = GNM_STF_FORMAT_PRESERVE; break;
	}

	quote = gtk_editable_get_chars (GTK_EDITABLE (gtk_bin_get_child (GTK_BIN (state->format.quotechar))), 0, -1);

	{
		unsigned u = gtk_combo_box_get_active (state->format.separator);
		if (u >= G_N_ELEMENTS (format_seps))
			u = 4;
		separator = format_seps[u]
			? g_strdup (format_seps[u])
			: gtk_editable_get_chars (GTK_EDITABLE (state->format.custom), 0, -1);
	}

	charset = go_charmap_sel_get_encoding (GO_CHARMAP_SEL (state->format.charset));
	locale = go_locale_sel_get_locale (GO_LOCALE_SEL (state->format.locale));

	if (quotingmode == GSF_OUTPUT_CSV_QUOTING_MODE_AUTO) {
		g_string_append (triggers, " \t");
		g_string_append (triggers, eol);
		g_string_append (triggers, quote);
		g_string_append (triggers, separator);
	}

	g_object_set (state->stfe,
		      "eol", eol,
		      "quote", quote,
		      "quoting-mode", quotingmode,
		      "quoting-triggers", triggers->str,
		      "separator", separator,
		      "transliterate-mode", transliteratemode,
		      "format", format,
		      "charset", charset,
		      "locale", locale,
		      NULL);

	if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui,
						      "save-as-default-check")))) {
		gnm_conf_set_stf_export_separator (separator);
		gnm_conf_set_stf_export_stringindicator (quote);
		gnm_conf_set_stf_export_terminator (eol);
		gnm_conf_set_stf_export_quoting (quotingmode);
		gnm_conf_set_stf_export_format (format);
		gnm_conf_set_stf_export_transliteration (transliteratemode == GNM_STF_TRANSLITERATE_MODE_TRANS);
		gnm_conf_set_stf_export_locale (locale);
		gnm_conf_set_stf_export_encoding (charset);
	}

	/* Which sheets?  */
	gnm_stf_export_options_sheet_list_clear (state->stfe);
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->sheets.model),
		(GtkTreeModelForeachFunc) cb_collect_exported_sheets, state);

	g_free (separator);
	g_free (quote);
	g_string_free (triggers, TRUE);
	g_free (locale);

	state->cancelled = FALSE;
	gtk_dialog_response (GTK_DIALOG (state->window), GTK_RESPONSE_OK);
}

static void
set_sheet_selection_count (TextExportState *state, int n)
{
	state->sheets.num_selected = n;
	gtk_widget_set_sensitive (state->sheets.select_all,
				  state->sheets.non_empty > n);
	gtk_widget_set_sensitive (state->sheets.select_none, n != 0);
	gtk_widget_set_sensitive (state->next_button, n != 0);
}

static gboolean
cb_set_sheet (GtkTreeModel *model, G_GNUC_UNUSED GtkTreePath *path, GtkTreeIter *iter,
	      gpointer data)
{
	gboolean value;

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
			    STF_EXPORT_COL_NON_EMPTY, &value,
			    -1);
	if (value)
		gtk_list_store_set (GTK_LIST_STORE (model), iter,
				    STF_EXPORT_COL_EXPORTED,
				    GPOINTER_TO_INT (data),
				    -1);
	return FALSE;
}

static void
cb_sheet_select_all (TextExportState *state)
{
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->sheets.model),
		cb_set_sheet, GINT_TO_POINTER (TRUE));
	set_sheet_selection_count (state, state->sheets.non_empty);
}
static void
cb_sheet_select_none (TextExportState *state)
{
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->sheets.model),
		cb_set_sheet, GINT_TO_POINTER (FALSE));
	set_sheet_selection_count (state, 0);
}

/*
 * Refreshes the buttons on a row (un)selection and selects the chosen sheet
 * for this view.
 */
static void
cb_selection_changed (GtkTreeSelection *new_selection,
		      TextExportState *state)
{
	GtkTreeIter iter, it;
	gboolean first_selected = TRUE, last_selected = TRUE;

	GtkTreeSelection  *selection =
		(new_selection == NULL)
		? gtk_tree_view_get_selection (state->sheets.view)
		: new_selection;

	if (selection != NULL
	    && gtk_tree_selection_count_selected_rows (selection) > 0
	    && gtk_tree_model_get_iter_first
		       (GTK_TREE_MODEL (state->sheets.model),
			&iter)) {
		first_selected = gtk_tree_selection_iter_is_selected
			(selection, &iter);
		it = iter;
		while (gtk_tree_model_iter_next
		       (GTK_TREE_MODEL (state->sheets.model),
			&it))
			iter = it;
		last_selected = gtk_tree_selection_iter_is_selected
			(selection, &iter);
	}

	gtk_widget_set_sensitive (state->sheets.top, !first_selected);
	gtk_widget_set_sensitive (state->sheets.up, !first_selected);
	gtk_widget_set_sensitive (state->sheets.bottom, !last_selected);
	gtk_widget_set_sensitive (state->sheets.down, !last_selected);

	return;
}

static void
move_element (TextExportState *state, gnm_iter_search_t iter_search)
{
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheets.view);
	GtkTreeModel *model;
	GtkTreeIter  a, b;

	g_return_if_fail (selection != NULL);

	if (!gtk_tree_selection_get_selected  (selection, &model, &a))
		return;

	b = a;
	if (!iter_search (model, &b))
		return;

	gtk_list_store_swap (state->sheets.model, &a, &b);
	cb_selection_changed (NULL, state);
}

static void
cb_sheet_up   (TextExportState *state)
{
	move_element (state, gtk_tree_model_iter_previous);
}

static void
cb_sheet_down (TextExportState *state)
{
	move_element (state, gtk_tree_model_iter_next);
}

static void
cb_sheet_top   (TextExportState *state)
{
	GtkTreeIter this_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheets.view);

	g_return_if_fail (selection != NULL);

	if (!gtk_tree_selection_get_selected  (selection, NULL, &this_iter))
		return;

	gtk_list_store_move_after (state->sheets.model, &this_iter, NULL);
	cb_selection_changed (NULL, state);
}

static void
cb_sheet_bottom (TextExportState *state)
{
	GtkTreeIter this_iter;
	GtkTreeSelection  *selection = gtk_tree_view_get_selection (state->sheets.view);

	g_return_if_fail (selection != NULL);

	if (!gtk_tree_selection_get_selected  (selection, NULL, &this_iter))
		return;
	gtk_list_store_move_before (state->sheets.model, &this_iter, NULL);
	cb_selection_changed (NULL, state);
}

static void
cb_sheet_export_toggled (G_GNUC_UNUSED GtkCellRendererToggle *cell,
			 const gchar *path_string,
			 TextExportState *state)
{
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter  iter;
	gboolean value;

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (state->sheets.model),
				     &iter, path)) {
		gtk_tree_model_get
			(GTK_TREE_MODEL (state->sheets.model), &iter,
			 STF_EXPORT_COL_EXPORTED,	&value,
			 -1);
		gtk_list_store_set
			(state->sheets.model, &iter,
			 STF_EXPORT_COL_EXPORTED,	!value,
			 -1);
		set_sheet_selection_count
			(state,
			 state->sheets.num_selected + (value ? -1 : 1));
	} else {
		g_warning ("Did not get a valid iterator");
	}
	gtk_tree_path_free (path);
}

static void
stf_export_dialog_sheet_page_init (TextExportState *state)
{
	int i;
	GtkTreeSelection  *selection;
	GtkTreeIter iter;
	GtkCellRenderer *renderer;
	GSList *sheet_list;

	state->sheets.select_all  = go_gtk_builder_get_widget (state->gui, "sheet_select_all");
	state->sheets.select_none = go_gtk_builder_get_widget (state->gui, "sheet_select_none");
	state->sheets.up	  = go_gtk_builder_get_widget (state->gui, "sheet_up");
	state->sheets.down	  = go_gtk_builder_get_widget (state->gui, "sheet_down");
	state->sheets.top	  = go_gtk_builder_get_widget (state->gui, "sheet_top");
	state->sheets.bottom	  = go_gtk_builder_get_widget (state->gui, "sheet_bottom");
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.up), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.down), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.top), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.bottom), 0., .5);

	state->sheets.model	  = gtk_list_store_new (STF_EXPORT_COL_MAX,
		G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_BOOLEAN);
	state->sheets.view	  = GTK_TREE_VIEW (
		go_gtk_builder_get_widget (state->gui, "sheet_list"));
	gtk_tree_view_set_model (state->sheets.view, GTK_TREE_MODEL (state->sheets.model));

	renderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (G_OBJECT (renderer),
		"toggled",
		G_CALLBACK (cb_sheet_export_toggled), state);
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->sheets.view),
		gtk_tree_view_column_new_with_attributes
				     (_("Export"),
				      renderer,
				      "active", STF_EXPORT_COL_EXPORTED,
				      "activatable", STF_EXPORT_COL_NON_EMPTY,
				      NULL));
	gtk_tree_view_append_column (GTK_TREE_VIEW (state->sheets.view),
		gtk_tree_view_column_new_with_attributes (_("Sheet"),
			gtk_cell_renderer_text_new (),
			"text", STF_EXPORT_COL_SHEET_NAME, NULL));

	selection = gtk_tree_view_get_selection (state->sheets.view);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	state->sheets.num = workbook_sheet_count (state->wb);
	state->sheets.num_selected = 0;
	state->sheets.non_empty = 0;

	sheet_list = gnm_stf_export_options_sheet_list_get (state->stfe);

	for (i = 0 ; i < state->sheets.num ; i++) {
		Sheet *sheet = workbook_sheet_by_index (state->wb, i);
		GnmRange total_range = sheet_get_extent (sheet, TRUE, TRUE);
		gboolean empty = sheet_is_region_empty (sheet, &total_range);
		gboolean export =
			!sheet_list || g_slist_find (sheet_list, sheet);
		/* The above adds up to n^2 in number of sheets.  Tough.  */

		gtk_list_store_append (state->sheets.model, &iter);
		gtk_list_store_set (state->sheets.model, &iter,
				    STF_EXPORT_COL_EXPORTED,	export && !empty,
				    STF_EXPORT_COL_SHEET_NAME,	sheet->name_quoted,
				    STF_EXPORT_COL_SHEET,	sheet,
				    STF_EXPORT_COL_NON_EMPTY,   !empty,
				    -1);
		if (!empty)
			state->sheets.non_empty++;

		if (export)
			state->sheets.num_selected++;
	}
	set_sheet_selection_count (state, state->sheets.num_selected);

	g_signal_connect_swapped (G_OBJECT (state->sheets.select_all),
		"clicked",
		G_CALLBACK (cb_sheet_select_all), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.select_none),
		"clicked",
		G_CALLBACK (cb_sheet_select_none), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.up),
		"clicked",
		G_CALLBACK (cb_sheet_up), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.down),
		"clicked",
		G_CALLBACK (cb_sheet_down), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.top),
		"clicked",
		G_CALLBACK (cb_sheet_top), state);
	g_signal_connect_swapped (G_OBJECT (state->sheets.bottom),
		"clicked",
		G_CALLBACK (cb_sheet_bottom), state);

	cb_selection_changed (NULL, state);
	g_signal_connect (selection,
			  "changed",
			  G_CALLBACK (cb_selection_changed), state);

	gtk_tree_view_set_reorderable (state->sheets.view, TRUE);
}

static void
stf_export_dialog_switch_page (TextExportState *state, TextExportPage new_page)
{
	gtk_notebook_set_current_page (GTK_NOTEBOOK (state->notebook),
				       new_page);
	if (new_page == PAGE_FORMAT) {
		gtk_widget_hide (state->next_button);
		gtk_widget_show (state->finish_button);
		gtk_widget_grab_default (state->finish_button);
		gtk_widget_grab_focus (state->finish_button);
	} else {
		gtk_widget_show (state->next_button);
		gtk_widget_hide (state->finish_button);
		gtk_widget_grab_default (state->next_button);
		gtk_widget_grab_focus (state->next_button);
	}

	if (state->sheets.non_empty > 1) {
		gtk_widget_show (state->back_button);
		gtk_widget_set_sensitive (state->back_button, new_page > 0);
	} else
		gtk_widget_hide (state->back_button);
}

static void
cb_back_page (TextExportState *state)
{
	int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (state->notebook));
	stf_export_dialog_switch_page (state, p - 1);
}

static void
cb_next_page (TextExportState *state)
{
	int p = gtk_notebook_get_current_page (GTK_NOTEBOOK (state->notebook));
	stf_export_dialog_switch_page (state, p + 1);
}

/**
 * stf_export_dialog:
 * @wbcg: (nullable): #WBCGtk
 * @stfe: An exporter to set up (and take defaults from)
 * @wb: The #Workbook to export
 *
 * This will start the export assistant.
 *
 * Returns: %TRUE if cancelled.
 **/
gboolean
stf_export_dialog (WBCGtk *wbcg, GnmStfExport *stfe, Workbook *wb)
{
	TextExportState state;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), TRUE);
	g_return_val_if_fail (GNM_IS_STF_EXPORT (stfe), TRUE);

	state.gui = gnm_gtk_builder_load ("res:ui/dialog-stf-export.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (state.gui == NULL)
		return TRUE;

	state.wb	  = wb;
	state.wbcg	  = wbcg;
	state.window	  = GTK_WINDOW (go_gtk_builder_get_widget (state.gui, "text-export"));
	state.notebook	  = go_gtk_builder_get_widget (state.gui, "text-export-notebook");
	state.back_button = go_gtk_builder_get_widget (state.gui, "button-back");
	state.next_button = go_gtk_builder_get_widget (state.gui, "button-next");
	state.finish_button = go_gtk_builder_get_widget (state.gui, "button-finish");
	state.cancelled = TRUE;
	state.stfe	  = stfe;
	stf_export_dialog_sheet_page_init (&state);
	stf_export_dialog_format_page_init (&state);
	if (state.sheets.non_empty == 0) {
		gtk_widget_destroy (GTK_WIDGET (state.window));
		go_gtk_notice_dialog (wbcg_toplevel (wbcg),  GTK_MESSAGE_ERROR,
				 _("This workbook does not contain any "
				   "exportable content."));
	} else {
		stf_export_dialog_switch_page
			(&state,
			 (1 >= state.sheets.non_empty) ?
			 PAGE_FORMAT : PAGE_SHEETS);
		g_signal_connect_swapped (G_OBJECT (state.back_button),
					  "clicked",
					  G_CALLBACK (cb_back_page), &state);
		g_signal_connect_swapped (G_OBJECT (state.next_button),
					  "clicked",
					  G_CALLBACK (cb_next_page), &state);
		g_signal_connect_swapped (G_OBJECT (state.finish_button),
					  "clicked",
					  G_CALLBACK (stf_export_dialog_finish),
					  &state);

		go_gtk_dialog_run (GTK_DIALOG (state.window), wbcg_toplevel (wbcg));
	}
	g_object_unref (state.gui);
	g_object_unref (state.sheets.model);

	return state.cancelled;
}

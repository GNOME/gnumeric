/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialog-stf-export.h"

#include <command-context.h>
#include <workbook.h>
#include <sheet.h>
#include <gui-util.h>
#include <widgets/widget-charmap-selector.h>

#include <gtk/gtkmessagedialog.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtkcomboboxentry.h>
#include <gtk/gtkmain.h>
#include <gtk/gtktreeview.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcellrenderertoggle.h>
#include <gtk/gtktable.h>
#include <gtk/gtkentry.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtktogglebutton.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkstock.h>
#include <gtk/gtklabel.h>
#include <glib/gi18n.h>

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

	GladeXML		*gui;
	WorkbookControlGUI	*wbcg;
	GtkWindow		*window;
	GtkWidget		*notebook;
	GtkWidget		*back_button, *next_button, *next_label, *next_image;

	struct {
		GtkListStore *model;
		GtkTreeView  *view;
		GtkWidget    *select_all, *select_none;
		GtkWidget    *up, *down, *top, *bottom;
		int num, num_selected, non_empty;
	} sheets;
	struct {
		GtkComboBox 	*termination;
		GtkComboBox 	*separator;
		GtkWidget      	*custom;
		GtkComboBox 	*quote;
		GtkComboBoxEntry      	*quotechar;
		GtkWidget	*charset;
		GtkComboBox 	*transliterate;
		GtkWidget	*preserve;
	} format;

	TextExportPage	cur_page;
	StfExportOptions_t *result;
} TextExportState;

static void
sheet_page_separator_menu_changed (TextExportState *state)
{
	/* 9 == the custom entry */
	if (gtk_combo_box_get_active (state->format.separator) == 9) {
		gtk_widget_set_sensitive (state->format.custom, TRUE);
		gtk_widget_grab_focus      (state->format.custom);
		gtk_editable_select_region (GTK_EDITABLE (state->format.custom), 0, -1);
	} else {
		gtk_widget_set_sensitive (state->format.custom, FALSE);
		/* If we don't use this the selection will remain blue */
		gtk_editable_select_region (GTK_EDITABLE (state->format.custom), 0, 0);
	}
}

static void
stf_export_dialog_format_page_init (TextExportState *state)
{
	GtkWidget *table;

	state->format.termination = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format_termination"));
	gtk_combo_box_set_active (state->format.termination, 0);
	state->format.separator   = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format_separator"));
	gtk_combo_box_set_active (state->format.separator, 0);
	state->format.custom      = glade_xml_get_widget (state->gui, "format_custom");
	state->format.quote       = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format_quote"));
	gtk_combo_box_set_active (state->format.quote, 0);
	state->format.quotechar   = GTK_COMBO_BOX_ENTRY      (glade_xml_get_widget (state->gui, "format_quotechar"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (state->format.quotechar), 0);
	state->format.charset	  = charmap_selector_new (CHARMAP_SELECTOR_FROM_UTF8);
	state->format.transliterate = GTK_COMBO_BOX (glade_xml_get_widget (state->gui, "format_transliterate"));
	gnumeric_editable_enters (state->window, state->format.custom);
	gnumeric_editable_enters (state->window,
			gtk_bin_get_child (GTK_BIN (state->format.quotechar)));

	if (stf_export_can_transliterate ()) {
		gtk_combo_box_set_active (state->format.transliterate,
			TRANSLITERATE_MODE_TRANS);
	} else {
		gtk_combo_box_set_active (state->format.transliterate,
			TRANSLITERATE_MODE_ESCAPE);
		/* It might be better to render insensitive only one option than
		the whole list as in the following line but it is not possible
		with gtk-2.4. May be it should be changed when 2.6 is available	
		(inactivate only the transliterate item) */
		gtk_widget_set_sensitive (GTK_WIDGET (state->format.transliterate), FALSE);
	}

	state->format.preserve = glade_xml_get_widget (state->gui, "format_preserve");

	table = glade_xml_get_widget (state->gui, "format_table");
	gtk_table_attach_defaults (GTK_TABLE (table), state->format.charset, 1, 2, 5, 6);
	gtk_widget_show_all (table);

	g_signal_connect_swapped (state->format.separator,
		"changed",
		G_CALLBACK (sheet_page_separator_menu_changed), state);
}

static gboolean
cb_collect_exported_sheets (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
			    TextExportState *state)
{
	gboolean exported;
	Sheet *sheet;

	gtk_tree_model_get (model, iter,
		STF_EXPORT_COL_EXPORTED, &exported,
		STF_EXPORT_COL_SHEET,	 &sheet,
		-1);
	if (exported)
		stf_export_options_sheet_list_add (state->result, sheet);
	return FALSE;
}
static void
stf_export_dialog_finish (TextExportState *state)
{
	StfTerminatorType_t	terminator;
	StfQuotingMode_t	quotingmode;
	StfTransliterateMode_t	transliteratemode;
	char *text;
	gunichar separator;

	state->result = stf_export_options_new ();

	/* Which sheets */
	stf_export_options_sheet_list_clear (state->result);
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->sheets.model),
		(GtkTreeModelForeachFunc) cb_collect_exported_sheets, state);

	/* What options */
	switch (gtk_combo_box_get_active (state->format.termination)) {
	case 0 :  terminator = TERMINATOR_TYPE_LINEFEED; break;
	case 1 :  terminator = TERMINATOR_TYPE_RETURN; break;
	case 2 :  terminator = TERMINATOR_TYPE_RETURN_LINEFEED; break;
	default : terminator = TERMINATOR_TYPE_UNKNOWN; break;
	}
	stf_export_options_set_terminator_type (state->result, terminator);

	switch (gtk_combo_box_get_active (state->format.quote)) {
	case 0 :  quotingmode = QUOTING_MODE_AUTO; break;
	case 1 :  quotingmode = QUOTING_MODE_ALWAYS; break;
	case 2 :  quotingmode = QUOTING_MODE_NEVER; break;
	default : quotingmode = QUOTING_MODE_UNKNOWN; break;
	}
	stf_export_options_set_quoting_mode (state->result, quotingmode);

	switch (gtk_combo_box_get_active (state->format.transliterate)) {
	case 0 :  transliteratemode = TRANSLITERATE_MODE_TRANS; break;
	case 1 :  transliteratemode = TRANSLITERATE_MODE_ESCAPE; break;
	default : transliteratemode  = TRANSLITERATE_MODE_UNKNOWN; break;
	}
	stf_export_options_set_transliterate_mode (state->result, transliteratemode);

	stf_export_options_set_format_mode (state->result,
		 gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->format.preserve)));

	text = gtk_editable_get_chars (GTK_EDITABLE (gtk_bin_get_child (GTK_BIN (state->format.quotechar))), 0, -1);
	stf_export_options_set_quoting_char (state->result, g_utf8_get_char (text));
	g_free (text);

	separator = 0;
	switch (gtk_combo_box_get_active (state->format.separator)) {
	case 0 : separator = ' '; break;
	case 1 : separator = '\t'; break;
	case 2 : separator = '!'; break;
	case 3 : separator = ':'; break;
	case 4 : separator = ','; break;
	case 5 : separator = '-'; break;
	case 6 : separator = '|'; break;
	case 7 : separator = ';'; break;
	case 8 : separator = '/'; break;
	case 9 :
		text = gtk_editable_get_chars (GTK_EDITABLE (state->format.custom), 0, -1);
		separator = g_utf8_get_char (text);
		g_free (text);
		break;
	default :
		g_warning ("Unknown separator");
		break;
	}
	stf_export_options_set_cell_separator (state->result, separator);

	stf_export_options_set_charset (state->result,
		charmap_selector_get_encoding (CHARMAP_SELECTOR (state->format.charset)));

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
cb_set_sheet (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
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

typedef gboolean gnm_iter_search_t (GtkTreeModel *model, GtkTreeIter* iter);

#define gnm_tree_model_iter_next gtk_tree_model_iter_next

static gboolean 
gnm_tree_model_iter_prev (GtkTreeModel *model, GtkTreeIter* iter)
{
	GtkTreePath *path = gtk_tree_model_get_path (model, iter);

	if (gtk_tree_path_prev (path) &&
	    gtk_tree_model_get_iter (model, iter, path)) {
		gtk_tree_path_free (path);
		return TRUE;
	}
	gtk_tree_path_free (path);
	return FALSE;
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
}

static void 
cb_sheet_up   (TextExportState *state) 
{ 
	move_element (state, gnm_tree_model_iter_prev); 
}

static void 
cb_sheet_down (TextExportState *state) 
{ 
	move_element (state, gnm_tree_model_iter_next); 
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
}

static void
cb_sheet_export_toggled (GtkCellRendererToggle *cell,
			 const gchar *path_string,
			 TextExportState *state)
{
	GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
	GtkTreeIter  iter;
	gboolean value;

	gtk_tree_model_get_iter (GTK_TREE_MODEL (state->sheets.model), &iter, path);
	gtk_tree_path_free (path);
	gtk_tree_model_get (GTK_TREE_MODEL (state->sheets.model), &iter,
		STF_EXPORT_COL_EXPORTED,	&value,
		-1);
	gtk_list_store_set (state->sheets.model, &iter,
		STF_EXPORT_COL_EXPORTED,	!value,
		-1);
	set_sheet_selection_count (state,
		state->sheets.num_selected + (value ? -1 : 1));
}

static void
stf_export_dialog_sheet_page_init (TextExportState *state)
{
	int i;
	Sheet *sheet, *cur_sheet;
	GtkTreeSelection  *selection;
	GtkTreeIter iter;
	GtkCellRenderer *renderer;

	state->sheets.select_all  = glade_xml_get_widget (state->gui, "sheet_select_all");
	state->sheets.select_none = glade_xml_get_widget (state->gui, "sheet_select_none");
	state->sheets.up	  = glade_xml_get_widget (state->gui, "sheet_up");
	state->sheets.down	  = glade_xml_get_widget (state->gui, "sheet_down");
	state->sheets.top	  = glade_xml_get_widget (state->gui, "sheet_top");
	state->sheets.bottom	  = glade_xml_get_widget (state->gui, "sheet_bottom");
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.up), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.down), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.top), 0., .5);
	gtk_button_set_alignment (GTK_BUTTON (state->sheets.bottom), 0., .5);

	state->sheets.model	  = gtk_list_store_new (STF_EXPORT_COL_MAX,
		G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_OBJECT, G_TYPE_BOOLEAN);
	state->sheets.view	  = GTK_TREE_VIEW (
		glade_xml_get_widget (state->gui, "sheet_list"));
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

	cur_sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (state->wbcg));
	state->sheets.num = workbook_sheet_count (state->wb);
	state->sheets.num_selected = 0;
	state->sheets.non_empty = 0;
	for (i = 0 ; i < state->sheets.num ; i++) {
		GnmRange total_range;
		gboolean export;
		sheet = workbook_sheet_by_index (state->wb, i);
		total_range = sheet_get_extent (sheet, TRUE);
		export =  !sheet_is_region_empty (sheet, &total_range);
		gtk_list_store_append (state->sheets.model, &iter);
		gtk_list_store_set (state->sheets.model, &iter,
				    STF_EXPORT_COL_EXPORTED,	export,
				    STF_EXPORT_COL_SHEET_NAME,	sheet->name_quoted,
				    STF_EXPORT_COL_SHEET,	sheet,
				    STF_EXPORT_COL_NON_EMPTY,   export,
				    -1);
		if (export) {
			state->sheets.num_selected++;
			state->sheets.non_empty++;
			gtk_tree_selection_select_iter (selection, &iter);
		}
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
}

static void
stf_export_dialog_switch_page (TextExportState *state, TextExportPage new_page)
{
	char const *label, *image;

	gtk_notebook_set_current_page (GTK_NOTEBOOK (state->notebook),
		state->cur_page = new_page);
	if (state->cur_page != PAGE_FORMAT) {
		label = _("_Next");
		image = GTK_STOCK_GO_FORWARD;
	} else {
		label = _("_Finish");
		image = GTK_STOCK_APPLY;
	}
	gtk_widget_set_sensitive (state->back_button,
				  (state->cur_page != PAGE_SHEETS) &&
				  (state->sheets.non_empty > 1));
	gtk_label_set_label (GTK_LABEL (state->next_label), label);
	gtk_image_set_from_stock (GTK_IMAGE (state->next_image),
		image, GTK_ICON_SIZE_BUTTON);
}

static void
cb_back_page (TextExportState *state)
{
	stf_export_dialog_switch_page (state, state->cur_page-1);
}

static void
cb_next_page (TextExportState *state)
{
	if (state->cur_page == PAGE_FORMAT)
		stf_export_dialog_finish (state);
	else
		stf_export_dialog_switch_page (state, state->cur_page+1);
}

/**
 * stf_dialog
 * @wbcg : #WorkbookControlGUI (can be NULL)
 * @wb : The #Workbook to export
 *
 * This will start the export assistant.
 * returns : A newly allocated StfExportOptions_t struct on success, NULL otherwise.
 **/
StfExportOptions_t *
stf_export_dialog (WorkbookControlGUI *wbcg, Workbook *wb)
{
	TextExportState state;

	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);

	state.gui = gnm_glade_xml_new (GNM_CMD_CONTEXT (wbcg),
		"dialog-stf-export.glade", NULL, NULL);
	if (state.gui == NULL)
		return NULL;

	state.wb	  = wb;
	state.wbcg	  = wbcg;
	state.window	  = GTK_WINDOW (glade_xml_get_widget (state.gui, "text-export"));
	state.notebook	  = glade_xml_get_widget (state.gui, "text-export-notebook");
	state.back_button = glade_xml_get_widget (state.gui, "button-back");
	state.next_button = glade_xml_get_widget (state.gui, "button-next");
	state.next_label  = glade_xml_get_widget (state.gui, "button-next-label");
	state.next_image  = glade_xml_get_widget (state.gui, "button-next-image");
	state.result	  = NULL;
	stf_export_dialog_sheet_page_init (&state);
	stf_export_dialog_format_page_init (&state);
	if (state.sheets.non_empty == 0) {
		gnumeric_notice (wbcg_toplevel (wbcg),  GTK_MESSAGE_ERROR, 
				 _("This workbook does not contain any "
				   "exportable content."));
	} else {
		stf_export_dialog_switch_page 
			(&state,
			 (1 >= state.sheets.non_empty) ? 
			 PAGE_FORMAT : PAGE_SHEETS);
		gtk_widget_grab_default (state.next_button);
		g_signal_connect_swapped (G_OBJECT (state.back_button),
					  "clicked",
					  G_CALLBACK (cb_back_page), &state);
		g_signal_connect_swapped (G_OBJECT (state.next_button),
					  "clicked",
					  G_CALLBACK (cb_next_page), &state);

		gnumeric_dialog_run (wbcg_toplevel (wbcg), 
				     GTK_DIALOG (state.window));
	}
	g_object_unref (G_OBJECT (state.gui));

	return state.result;
}

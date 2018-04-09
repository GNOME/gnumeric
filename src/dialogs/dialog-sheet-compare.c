/*
 * dialog-comparet-order.c: Dialog to compare two sheets.
 *
 * (C) Copyright 2018 Morten Welinder (terra@gnome.org)
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
#include "sheet-diff.h"
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <workbook.h>
#include <sheet.h>
#include <ranges.h>
#include <application.h>

#define SHEET_COMPARE_KEY          "sheet-compare-dialog"

enum {
	ITEM_DESC,
	NUM_COLMNS
};


typedef struct {
	WBCGtk  *wbcg;

	GtkBuilder *gui;
	GtkWidget *dialog;
	GtkWidget *notebook;

	GtkWidget *cancel_btn;
	GtkWidget *compare_btn;

	GtkWidget *sheet_sel_A;
	GtkWidget *sheet_sel_B;
	GtkWidget *wb_sel_A;
	GtkWidget *wb_sel_B;

	GtkTreeView *results_view;
	GtkTreeStore *results;

	gboolean has_cell_section;
	GtkTreeIter cell_section_iter;

	gboolean has_style_section;
	GtkTreeIter style_section_iter;
} SheetCompare;


static void
cb_sheet_compare_destroy (SheetCompare *state)
{
	Workbook *wb = wb_control_get_workbook (GNM_WBC (state->wbcg));

	g_object_unref (state->gui);
	g_object_set_data (G_OBJECT (wb), SHEET_COMPARE_KEY, NULL);
	state->gui = NULL;

	g_free (state);
}

static void
cb_cancel_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		   SheetCompare *state)
{
	    gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void
reset_sheet_menu (GtkWidget *sheet_sel, Workbook *wb, int def_sheet)
{
	GOOptionMenu *om = GO_OPTION_MENU (sheet_sel);
	GtkMenu *menu;
	GtkWidget *act = NULL;

	if (wb == g_object_get_data (G_OBJECT (om), "wb"))
		return;
	g_object_set_data (G_OBJECT (om), "wb", wb);

	menu = GTK_MENU (gtk_menu_new ());
	WORKBOOK_FOREACH_SHEET (wb, sheet, {
		GtkWidget *item =
			gtk_check_menu_item_new_with_label
			(sheet->name_unquoted);
		gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), FALSE);
		g_object_set_data (G_OBJECT (item), "sheet", sheet);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		if (def_sheet-- == 0)
			act = item;
	});

	gtk_widget_show_all (GTK_WIDGET (menu));
	go_option_menu_set_menu (om, GTK_WIDGET (menu));

	if (act)
		go_option_menu_select_item (om, GTK_MENU_ITEM (act));
}

static GtkWidget *
create_sheet_selector (gboolean qnew)
{
	GtkWidget *w = go_option_menu_new ();
	g_object_set_data (G_OBJECT (w), "qnew", GUINT_TO_POINTER (qnew));
	return w;
}


static void
cb_wb_changed (GOOptionMenu *om, SheetCompare *state)
{
	GtkWidget *item = go_option_menu_get_history (om);
	Workbook *wb = g_object_get_data (G_OBJECT (item), "wb");
	GtkWidget *sheet_sel = g_object_get_data (G_OBJECT (om), "sheet_sel");

	if (wb)
		reset_sheet_menu (sheet_sel, wb, -1);
}

static GtkWidget *
create_wb_selector (SheetCompare *state, GtkWidget *sheet_sel,
		    Workbook *wb0, gboolean qnew)
{
	GtkMenu *menu;
	GOOptionMenu *om;
	GList *l, *wbs;
	GtkWidget *act = NULL;

	om = GO_OPTION_MENU (go_option_menu_new ());
        menu = GTK_MENU (gtk_menu_new ());

	wbs = gnm_app_workbook_list ();
	for (l = wbs; l; l = l->next) {
		Workbook *wb = l->data;
		GtkWidget *item, *child;
		const char *uri;
		char *markup, *shortname, *filename, *dirname, *longname, *duri;

		uri = go_doc_get_uri (GO_DOC (wb));
		filename = go_filename_from_uri (uri);
		if (filename) {
			shortname = g_filename_display_basename (filename);
		} else {
			shortname = g_filename_display_basename (uri);
		}

		dirname = g_path_get_dirname (filename);
		duri = g_uri_unescape_string (dirname, NULL);
		longname = duri
			? g_filename_display_name (duri)
			: g_strdup (uri);

		markup = g_markup_printf_escaped
			(_("%s\n<small>%s</small>"),
			 shortname, longname);

		item = gtk_menu_item_new_with_label ("");
		child = gtk_bin_get_child (GTK_BIN (item));
		gtk_label_set_markup (GTK_LABEL (child), markup);
		gtk_label_set_ellipsize (GTK_LABEL (child), PANGO_ELLIPSIZE_MIDDLE);

		g_free (markup);
		g_free (shortname);
		g_free (dirname);
		g_free (longname);
		g_free (duri);
		g_free (filename);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_object_set_data (G_OBJECT (item), "wb", wb);
		if (wb == wb0)
			act = item;
	}

	gtk_widget_show_all (GTK_WIDGET (menu));
	go_option_menu_set_menu (om, GTK_WIDGET (menu));

	if (act)
		go_option_menu_select_item (om, GTK_MENU_ITEM (act));

	reset_sheet_menu (sheet_sel, WORKBOOK (wbs->data),
			  qnew ? 1 : 0);

	g_object_set_data (G_OBJECT (om), "sheet_sel", sheet_sel);

	g_signal_connect (G_OBJECT (om), "changed",
                          G_CALLBACK (cb_wb_changed), state);

	return GTK_WIDGET (om);
}

/* ------------------------------------------------------------------------- */

static void
dsc_cell_changed (gpointer user, GnmCell const *oc, GnmCell const *nc)
{
	SheetCompare *state = user;
	GtkTreeIter iter;
	char *text;
	const char *loc;

	if (!state->has_cell_section) {
		gtk_tree_store_insert (state->results,
				       &state->cell_section_iter,
				       NULL, -1);
		gtk_tree_store_set (state->results,
				    &state->cell_section_iter,
				    ITEM_DESC, _("Cells"),
				    -1);
		state->has_cell_section = TRUE;
	}

	loc = cell_name (oc ? oc : nc);
	if (oc && nc)
		text = g_strdup_printf (_("Cell %s changed"), loc);
	else if (oc)
		text = g_strdup_printf (_("Cell %s removed."), loc);
	else if (nc)
		text = g_strdup_printf (_("Cell %s added."), loc);
	else
		g_assert_not_reached ();
	gtk_tree_store_insert (state->results, &iter,
			       &state->cell_section_iter,
			       -1);
	gtk_tree_store_set (state->results, &iter,
			    ITEM_DESC, text,
			    -1);
	g_free (text);
}

static void
dsc_style_changed (gpointer user, GnmRange const *r,
		   G_GNUC_UNUSED GnmStyle const *os,
		   G_GNUC_UNUSED GnmStyle const *ns)
{
	SheetCompare *state = user;
	GtkTreeIter iter;
	char *text;

	if (!state->has_style_section) {
		gtk_tree_store_insert (state->results,
				       &state->style_section_iter,
				       NULL, -1);
		gtk_tree_store_set (state->results,
				    &state->style_section_iter,
				    ITEM_DESC, _("Formatting"),
				    -1);
		state->has_style_section = TRUE;
	}

	text = g_strdup_printf (_("Style for range %s changed"),
				range_as_string (r));
	gtk_tree_store_insert (state->results, &iter,
			       &state->style_section_iter,
			       -1);
	gtk_tree_store_set (state->results, &iter,
			    ITEM_DESC, text,
			    -1);
	g_free (text);
}

static const GnmDiffActions dsc_actions = {
	.cell_changed = dsc_cell_changed,
	.style_changed = dsc_style_changed,
};

static void
cb_compare_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		    SheetCompare *state)
{
	GtkTreeView *tv = state->results_view;
	GtkTreeStore *ts = gtk_tree_store_new (NUM_COLMNS, G_TYPE_STRING);
	GtkWidget *w;
	Sheet *sheet_A, *sheet_B;

	if (gtk_tree_view_get_n_columns (tv) == 0) {
		GtkTreeViewColumn *tvc;

		tvc = gtk_tree_view_column_new_with_attributes
			(_("Description"),
			 gtk_cell_renderer_text_new (),
			 "text", ITEM_DESC, NULL);
		gtk_tree_view_append_column (tv, tvc);
	}

	w = go_option_menu_get_history (GO_OPTION_MENU (state->sheet_sel_A));
	sheet_A = w ? g_object_get_data (G_OBJECT (w), "sheet") : NULL;
	w = go_option_menu_get_history (GO_OPTION_MENU (state->sheet_sel_B));
	sheet_B = w ? g_object_get_data (G_OBJECT (w), "sheet") : NULL;

	if (sheet_A && sheet_B) {
		state->results = ts;
		gnm_diff_sheets (&dsc_actions, state, sheet_A, sheet_B);
		state->results = NULL;
	}

	gtk_tree_view_set_model (tv, GTK_TREE_MODEL (ts));
	g_object_unref (ts);

	gtk_notebook_set_current_page (GTK_NOTEBOOK (state->notebook), 1);
}

/* ------------------------------------------------------------------------- */

void
dialog_sheet_compare (WBCGtk *wbcg)
{
	SheetCompare *state;
	GtkBuilder *gui;
	Workbook *wb;

	g_return_if_fail (wbcg != NULL);

	wb = wb_control_get_workbook (GNM_WBC (wbcg));

	gui = gnm_gtk_builder_load ("sheet-compare.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SHEET_COMPARE_KEY))
		return;


	g_object_set_data (G_OBJECT (wb), SHEET_COMPARE_KEY, (gpointer) gui);
	state = g_new0 (SheetCompare, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->dialog = go_gtk_builder_get_widget (gui, "sheet-compare-dialog");
	state->notebook = go_gtk_builder_get_widget (gui, "notebook");
	state->cancel_btn = go_gtk_builder_get_widget (gui, "cancel_button");
	state->compare_btn = go_gtk_builder_get_widget (gui, "compare_button");
	state->results_view = GTK_TREE_VIEW (go_gtk_builder_get_widget (gui, "results_treeview"));

	state->sheet_sel_A = create_sheet_selector (FALSE);
	state->wb_sel_A = create_wb_selector (state, state->sheet_sel_A,
					      wb, FALSE);
	go_gtk_widget_replace (go_gtk_builder_get_widget (gui, "sheet_selector_A"),
			       state->sheet_sel_A);
	go_gtk_widget_replace (go_gtk_builder_get_widget (gui, "wb_selector_A"),
			       state->wb_sel_A);

	state->sheet_sel_B = create_sheet_selector (TRUE);
	state->wb_sel_B = create_wb_selector (state, state->sheet_sel_B,
					      wb, TRUE);
	go_gtk_widget_replace (go_gtk_builder_get_widget (gui, "sheet_selector_B"),
			       state->sheet_sel_B);
	go_gtk_widget_replace (go_gtk_builder_get_widget (gui, "wb_selector_B"),
			       state->wb_sel_B);

#define CONNECT(o,s,c) g_signal_connect(G_OBJECT(o),s,G_CALLBACK(c),state)
	CONNECT (state->cancel_btn, "clicked", cb_cancel_clicked);
	CONNECT (state->compare_btn, "clicked", cb_compare_clicked);
#undef CONNECT

	/* a candidate for merging into attach guru */
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state,
				(GDestroyNotify) cb_sheet_compare_destroy);

	gnm_restore_window_geometry (GTK_WINDOW (state->dialog),
				     SHEET_COMPARE_KEY);

	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				GTK_WINDOW (state->dialog));
	gtk_widget_show_all (GTK_WIDGET (state->dialog));
}

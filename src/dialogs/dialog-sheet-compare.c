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
#include <cell.h>
#include <application.h>

#define SHEET_COMPARE_KEY          "sheet-compare-dialog"

enum {
	ITEM_SECTION,
	ITEM_DIRECTION,
	ITEM_OLD_LOC,
	ITEM_NEW_LOC,
	NUM_COLUMNS
};

enum {
	SEC_CELLS,
	SEC_STYLE
};

enum {
	DIR_NA,
	DIR_ADDED,
	DIR_REMOVED,
	DIR_CHANGED,
	DIR_QUIET // Like CHANGED, but for always-changed context
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
	GtkWidget *results_window;

	GtkTreeView *results_view;
	GtkTreeStore *results;

	gboolean has_cell_section;
	GtkTreeIter cell_section_iter;

	gboolean has_style_section;
	GtkTreeIter style_section_iter;

	Sheet *old_sheet;
	Sheet *new_sheet;
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
setup_section (SheetCompare *state, gboolean *phas, GtkTreeIter *iter,
	       int section)
{
	if (!*phas) {
		gtk_tree_store_insert (state->results, iter, NULL, -1);
		gtk_tree_store_set (state->results, iter,
				    ITEM_SECTION, section,
				    ITEM_DIRECTION, DIR_NA,
				    -1);
		*phas = TRUE;
	}
}

static void
extract_range (GnmRangeRef const *rr, GnmRange *r, Sheet **psheet)
{
	*psheet = rr->a.sheet;
	r->start.col = rr->a.col;
	r->start.row = rr->a.row;
	r->end.col = rr->b.col;
	r->end.row = rr->b.row;
}

static void
section_renderer_func (GtkTreeViewColumn *tree_column,
		       GtkCellRenderer   *cell,
		       GtkTreeModel      *model,
		       GtkTreeIter       *iter,
		       gpointer           user_data)
{
	int section, dir;
	const char *text = "?";

	gtk_tree_model_get (model, iter,
			    ITEM_SECTION, &section,
			    ITEM_DIRECTION, &dir,
			    -1);
	switch (dir) {
	case DIR_NA:
		switch (section) {
		case SEC_CELLS: text = _("Cells"); break;
		case SEC_STYLE: text = _("Formatting"); break;
		}
		break;
	case DIR_QUIET: text = ""; break;
	case DIR_ADDED: text = _("Added"); break;
	case DIR_REMOVED: text = _("Removed"); break;
	case DIR_CHANGED: text = _("Changed"); break;
	}

	g_object_set (cell, "text", text, NULL);
}

static void
location_renderer_func (GtkTreeViewColumn *tree_column,
			GtkCellRenderer   *cell,
			GtkTreeModel      *model,
			GtkTreeIter       *iter,
			gpointer           user_data)
{
	GnmRangeRef *loc_old = NULL;
	GnmRangeRef *loc_new = NULL;
	GnmRangeRef *loc;

	gtk_tree_model_get (model, iter,
			    ITEM_OLD_LOC, &loc_new,
			    ITEM_NEW_LOC, &loc_old,
			    -1);

	loc = loc_old ? loc_old : loc_new;
	if (loc) {
		GnmRange r;
		Sheet *sheet;
		extract_range (loc, &r, &sheet);
		g_object_set (cell, "text", range_as_string (&r), NULL);
	} else
		g_object_set (cell, "text", "", NULL);

	g_free (loc_old);
	g_free (loc_new);
}

static void
oldnew_renderer_func (GtkTreeViewColumn *tree_column,
		      GtkCellRenderer   *cell,
		      GtkTreeModel      *model,
		      GtkTreeIter       *iter,
		      gpointer           user_data)
{
	gboolean qnew = GPOINTER_TO_UINT (user_data);

	GnmRangeRef *loc = NULL;
	int section, dir;
	char *text = NULL;

	gtk_tree_model_get (model, iter,
			    ITEM_SECTION, &section,
			    ITEM_DIRECTION, &dir,
			    (qnew ? ITEM_NEW_LOC : ITEM_OLD_LOC), &loc,
			    -1);
	if (dir == DIR_NA)
		goto done;

	if (section == SEC_CELLS) {
		GnmCell const *cell;

		if (!loc)
			goto done;
		cell = sheet_cell_get (loc->a.sheet, loc->a.col, loc->a.row);
		if (!cell)
			goto error;
		text = gnm_cell_get_entered_text (cell);
	} else if (section == SEC_STYLE) {
		// TBD
	}

done:
	g_object_set (cell, "text", (text ? text : ""), NULL);
	g_free (text);

	g_free (loc);
	return;

error:
	text = g_strdup ("?");
	goto done;
}

static void
dsc_sheet_start (gpointer user, Sheet const *os, Sheet const *ns)
{
	SheetCompare *state = user;
	state->old_sheet = (Sheet *)os;
	state->new_sheet = (Sheet *)ns;
}

static void
dsc_sheet_end (gpointer user)
{
	SheetCompare *state = user;
	state->old_sheet = NULL;
	state->new_sheet = NULL;
}

static void
dsc_cell_changed (gpointer user, GnmCell const *oc, GnmCell const *nc)
{
	SheetCompare *state = user;
	GtkTreeIter iter;
	int dir;

	setup_section (state,
		       &state->has_cell_section,
		       &state->cell_section_iter,
		       SEC_CELLS);

	dir = (oc ? (nc ? DIR_CHANGED : DIR_REMOVED) : DIR_ADDED);

	gtk_tree_store_insert (state->results, &iter,
			       &state->cell_section_iter,
			       -1);
	gtk_tree_store_set (state->results, &iter,
			    ITEM_SECTION, SEC_CELLS,
			    ITEM_DIRECTION, dir,
			    -1);

	if (oc) {
		GnmRangeRef loc;
		gnm_cellref_init (&loc.a, oc->base.sheet,
				  oc->pos.col, oc->pos.row,
				  FALSE);
		loc.b = loc.a;
		gtk_tree_store_set (state->results, &iter,
				    ITEM_OLD_LOC, &loc,
				    -1);
	}

	if (nc) {
		GnmRangeRef loc;
		gnm_cellref_init (&loc.a, nc->base.sheet,
				  nc->pos.col, nc->pos.row,
				  FALSE);
		loc.b = loc.a;
		gtk_tree_store_set (state->results, &iter,
				    ITEM_NEW_LOC, &loc,
				    -1);
	}
}

static void
dsc_style_changed (gpointer user, GnmRange const *r,
		   G_GNUC_UNUSED GnmStyle const *os,
		   G_GNUC_UNUSED GnmStyle const *ns)
{
	SheetCompare *state = user;
	GtkTreeIter iter;
	GnmRangeRef loc_old, loc_new;

	setup_section (state,
		       &state->has_style_section,
		       &state->style_section_iter,
		       SEC_STYLE);

	gnm_cellref_init (&loc_old.a, state->old_sheet,
			  r->start.col, r->start.row,
			  FALSE);
	gnm_cellref_init (&loc_old.b, state->old_sheet,
			  r->end.col, r->end.row,
			  FALSE);
	loc_new = loc_old;
	loc_new.a.sheet = loc_new.b.sheet = state->new_sheet;

	gtk_tree_store_insert (state->results, &iter,
			       &state->style_section_iter,
			       -1);
	gtk_tree_store_set (state->results, &iter,
			    ITEM_SECTION, SEC_STYLE,
			    ITEM_DIRECTION, DIR_QUIET,
			    ITEM_OLD_LOC, &loc_old,
			    ITEM_NEW_LOC, &loc_new,
			    -1);
}

static const GnmDiffActions dsc_actions = {
	.sheet_start = dsc_sheet_start,
	.sheet_end = dsc_sheet_end,
	.cell_changed = dsc_cell_changed,
	.style_changed = dsc_style_changed,
};

static void
cb_compare_clicked (G_GNUC_UNUSED GtkWidget *ignore,
		    SheetCompare *state)
{
	GtkTreeView *tv = state->results_view;
	GtkTreeStore *ts = gtk_tree_store_new
		(NUM_COLUMNS,
		 G_TYPE_INT, // Enum, really
		 G_TYPE_INT, // Enum, really
		 gnm_rangeref_get_type (),
		 gnm_rangeref_get_type ());
	GtkWidget *w;
	Sheet *sheet_A, *sheet_B;

	if (gtk_tree_view_get_n_columns (tv) == 0) {
		GtkTreeViewColumn *tvc;
		GtkCellRenderer *cr;

		tvc = gtk_tree_view_column_new ();
		cr = gtk_cell_renderer_text_new ();
		gtk_tree_view_column_set_title (tvc, _("Description"));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr, section_renderer_func, NULL, NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);
		gtk_tree_view_append_column (tv, tvc);

		tvc = gtk_tree_view_column_new ();
		cr = gtk_cell_renderer_text_new ();
		gtk_tree_view_column_set_title (tvc, _("Location"));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr, location_renderer_func, NULL, NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);
		gtk_tree_view_append_column (tv, tvc);

		tvc = gtk_tree_view_column_new ();
		cr = gtk_cell_renderer_text_new ();
		gtk_tree_view_column_set_title (tvc, _("Old"));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr, oldnew_renderer_func,
			 GUINT_TO_POINTER (FALSE), NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);
		gtk_tree_view_append_column (tv, tvc);

		tvc = gtk_tree_view_column_new ();
		cr = gtk_cell_renderer_text_new ();
		gtk_tree_view_column_set_title (tvc, _("New"));
		gtk_tree_view_column_set_cell_data_func
			(tvc, cr, oldnew_renderer_func,
			 GUINT_TO_POINTER (TRUE), NULL);
		gtk_tree_view_column_pack_start (tvc, cr, TRUE);
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
	PangoLayout *layout;
	int height, width;

	g_return_if_fail (wbcg != NULL);

	wb = wb_control_get_workbook (GNM_WBC (wbcg));

	gui = gnm_gtk_builder_load ("sheet-compare.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, SHEET_COMPARE_KEY))
		return;

	layout = gtk_widget_create_pango_layout (GTK_WIDGET (wbcg_toplevel (wbcg)), "Mg19");
	pango_layout_get_pixel_size (layout, &width, &height);
	g_object_unref (layout);

	g_object_set_data (G_OBJECT (wb), SHEET_COMPARE_KEY, (gpointer) gui);
	state = g_new0 (SheetCompare, 1);
	state->gui = gui;
	state->wbcg = wbcg;
	state->dialog = go_gtk_builder_get_widget (gui, "sheet-compare-dialog");
	state->notebook = go_gtk_builder_get_widget (gui, "notebook");
	state->cancel_btn = go_gtk_builder_get_widget (gui, "cancel_button");
	state->compare_btn = go_gtk_builder_get_widget (gui, "compare_button");
	state->results_window = go_gtk_builder_get_widget (gui, "results_window");
	state->results_view = GTK_TREE_VIEW (go_gtk_builder_get_widget (gui, "results_treeview"));

	gtk_widget_set_size_request (state->results_window,
				     width / 4 * 40,
				     height * 10);

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

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-consolidate.c : implementation of the consolidation dialog.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
 * Copyright (C) Andreas J. Guelzow <aguelzow@taliesin.ca>
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

#include <commands.h>
#include <consolidate.h>
#include <func.h>
#include <gui-util.h>
#include <ranges.h>
#include <sheet-view.h>
#include <selection.h>
#include <widgets/gnumeric-expr-entry.h>
#include <widgets/gnumeric-cell-renderer-expr-entry.h>
#include <workbook-edit.h>

#include <string.h>

enum {
	SOURCE_COLUMN,
	PIXMAP_COLUMN,
	IS_EDITABLE_COLUMN,
	NUM_COLMNS
};

typedef struct {
	WorkbookControlGUI *wbcg;
	SheetView	   *sv;
	Sheet              *sheet;
	GladeXML           *glade_gui;
	GtkWidget          *warning_dialog;

	struct {
		GtkDialog       *dialog;

		GtkOptionMenu     *function;
		GtkOptionMenu     *put;

		GnmExprEntry	  *destination;

		GtkTreeView       *source_view;
		GtkTreeModel      *source_areas;
		GnumericCellRendererExprEntry *cellrenderer;
		GdkPixbuf         *pixmap;

		GtkButton         *clear;
		GtkButton         *delete;

		GtkCheckButton    *labels_row;
		GtkCheckButton    *labels_col;
		GtkCheckButton    *labels_copy;

		GtkButton         *btn_ok;
		GtkButton         *btn_cancel;
	} gui;

	int                        areas_index;     /* Select index in sources clist */
	char                      *construct_error; /* If set an error occurred in construct_consolidate */
} ConsolidateState;

static void dialog_set_button_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
					   ConsolidateState *state);

/**
 * adjust_source_areas
 * 
 * ensures that we have exactly 2 empty rows
 *
 *
 **/
static void
adjust_source_areas (ConsolidateState *state)
{
	int i = 0;
	int cnt_empty = 2;
	GtkTreeIter      iter;
	
	if (gtk_tree_model_get_iter_first 
	    (state->gui.source_areas, &iter)) {
		do {
			char *source;

			gtk_tree_model_get (state->gui.source_areas, 
					    &iter,
					    SOURCE_COLUMN, &source,
					    -1);
			if (strlen(source) == 0)
				cnt_empty--;
			g_free (source);
		} while (gtk_tree_model_iter_next 
			 (state->gui.source_areas,&iter));
	}
	for (i = 0; i < cnt_empty; i++) {
		gtk_list_store_append (GTK_LIST_STORE(state->gui.source_areas),
				       &iter);
		gtk_list_store_set (GTK_LIST_STORE(state->gui.source_areas),
				    &iter,
				    IS_EDITABLE_COLUMN,	TRUE,
				    SOURCE_COLUMN, "",
				    PIXMAP_COLUMN, state->gui.pixmap,
				    -1);
	}
	dialog_set_button_sensitivity (NULL, state);
}

/**
 * construct_consolidate:
 *
 * Builts a new Consolidate structure from the
 * state of all the widgets in the dialog, this can
 * be used to actually "execute" a consolidation
 **/
static Consolidate *
construct_consolidate (ConsolidateState *state)
{
	Consolidate      *cs   = consolidate_new ();
	ConsolidateMode  mode = 0;
	const char       *func;
	Value            *range_value;
	GtkTreeIter      iter;

	switch (gtk_option_menu_get_history (state->gui.function)) {
	case 0 : func = "SUM"; break;
	case 1 : func = "MIN"; break;
	case 2 : func = "MAX"; break;
	case 3 : func = "AVERAGE"; break;
	case 4 : func = "COUNT"; break;
	case 5 : func = "PRODUCT"; break;
	case 6 : func = "STDEV"; break;
	case 7 : func = "STDEVP"; break;
	case 8 : func = "VAR"; break;
	case 9 : func = "VARP"; break;
	default :
		func = NULL;
		g_warning ("Unknown function index!");
	}

	consolidate_set_function (cs, gnm_func_lookup (func, NULL));

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_row)))
		mode |= CONSOLIDATE_COL_LABELS;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_col)))
		mode |= CONSOLIDATE_ROW_LABELS;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_copy)))
		mode |= CONSOLIDATE_COPY_LABELS;
	if (gtk_option_menu_get_history (state->gui.put) == 0)
		mode |= CONSOLIDATE_PUT_VALUES;

	consolidate_set_mode (cs, mode);

	range_value = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->gui.destination), state->sheet);
	g_return_val_if_fail (range_value != NULL, NULL);

	if (!consolidate_set_destination (cs, range_value)) {
		g_warning ("Error while setting destination! This should not happen");
		consolidate_free (cs);
		return NULL;
	}

	g_return_val_if_fail (gtk_tree_model_iter_n_children
			      (state->gui.source_areas,
			       NULL)> 2, NULL);

	gtk_tree_model_get_iter_first 
		(state->gui.source_areas, &iter);

	do {
		char *source;

		gtk_tree_model_get (state->gui.source_areas, 
				    &iter,
				    SOURCE_COLUMN, &source,
				    -1);
		if (strlen(source) != 0) {
			range_value = global_range_parse (state->sheet, source);
			
			if (range_value == NULL) {
				state->construct_error = g_strdup_printf (
					_("Specification %s "
					  "does not define a region"),
					source);
				g_free (source);
				consolidate_free (cs);
				return NULL;
			}
			if (!consolidate_add_source (cs, range_value)) {
				state->construct_error = g_strdup_printf (
					_("Source region %s overlaps "
					  "with the destination region"),
					source);
				g_free (source);
				consolidate_free (cs);
				return NULL;
			}
		}
		g_free (source);
	} while (gtk_tree_model_iter_next 
		 (state->gui.source_areas,&iter));

	return cs;
}

/***************************************************************************************/

/**
 * dialog_set_button_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
dialog_set_button_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
			       ConsolidateState *state)
{
	gboolean ready = FALSE;

	ready = gnm_expr_entry_is_cell_ref (state->gui.destination, state->sheet, TRUE)
		&& (gtk_tree_model_iter_n_children
		    (state->gui.source_areas, NULL)> 2);
	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.btn_ok), ready);
	return;
}

static void
cb_selection_changed (G_GNUC_UNUSED GtkTreeSelection *ignored,
		      ConsolidateState *state)
{
	GtkTreeIter  iter;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->gui.source_view);

	gtk_widget_set_sensitive (GTK_WIDGET(state->gui.delete), 
				  gtk_tree_selection_get_selected (selection, NULL, &iter));
}


static void
cb_source_edited (G_GNUC_UNUSED GtkCellRendererText *cell,
	gchar               *path_string,
	gchar               *new_text,
        ConsolidateState        *state)
{
	GtkTreeIter iter;
	GtkTreePath *path;

	path = gtk_tree_path_new_from_string (path_string);

	gtk_tree_model_get_iter (state->gui.source_areas, &iter, path);
	gtk_list_store_set (GTK_LIST_STORE(state->gui.source_areas), 
			    &iter, SOURCE_COLUMN, new_text, -1);

	gtk_tree_path_free (path);
	adjust_source_areas (state);
}

static void
cb_dialog_destroy (ConsolidateState *state)
{
	wbcg_edit_detach_guru (state->wbcg);

	g_object_unref (G_OBJECT (state->glade_gui));
	state->glade_gui = NULL;
	g_object_unref (G_OBJECT (state->gui.pixmap));
	state->gui.pixmap = NULL;

	if (state->construct_error) {
		g_warning ("The construct error was not freed, this should not happen!");
		g_free (state->construct_error);
	}
	g_free (state);
}

static void
cb_dialog_clicked (GtkWidget *widget, ConsolidateState *state)
{
	if (state->gui.cellrenderer->entry)
		gnumeric_cell_renderer_expr_entry_editing_done (
			GTK_CELL_EDITABLE (state->gui.cellrenderer->entry),
			state->gui.cellrenderer);

	if (widget == GTK_WIDGET (state->gui.btn_ok)) {
		Consolidate *cs;

		if (state->warning_dialog != NULL)
			gtk_widget_destroy (state->warning_dialog);

		cs = construct_consolidate (state);

		/*
		 * If something went wrong consolidate_construct
		 * return NULL and sets the state->construct_error to
		 * a suitable error message
		 */
		if (cs == NULL) {
			gnumeric_notice_nonmodal (GTK_WINDOW (state->gui.dialog),
						  &state->warning_dialog,
						  GTK_MESSAGE_ERROR,
						  state->construct_error);
			g_free (state->construct_error);
			state->construct_error = NULL;

			return;
		}

		cmd_consolidate (WORKBOOK_CONTROL (state->wbcg), cs);
	}

	gtk_widget_destroy (GTK_WIDGET (state->gui.dialog));
}

static void
cb_source_changed (G_GNUC_UNUSED GtkEntry *ignored,
		   ConsolidateState *state)
{
	g_return_if_fail (state != NULL);

}

static void
cb_clear_clicked (G_GNUC_UNUSED GtkButton *button,
		  ConsolidateState *state)
{
	g_return_if_fail (state != NULL);

	if (state->gui.cellrenderer->entry)
		gnumeric_cell_renderer_expr_entry_editing_done (
			GTK_CELL_EDITABLE (state->gui.cellrenderer->entry),
			state->gui.cellrenderer);

	gtk_list_store_clear (GTK_LIST_STORE(state->gui.source_areas));
	adjust_source_areas (state);	

	dialog_set_button_sensitivity (NULL, state);
}

static void
cb_delete_clicked (G_GNUC_UNUSED GtkButton *button,
		   ConsolidateState *state)
{
	GtkTreeIter sel_iter;
	GtkTreeSelection  *selection = 
		gtk_tree_view_get_selection (state->gui.source_view);

	if (state->gui.cellrenderer->entry)
		gnumeric_cell_renderer_expr_entry_editing_done (
			GTK_CELL_EDITABLE (state->gui.cellrenderer->entry),
			state->gui.cellrenderer);
	if (!gtk_tree_selection_get_selected (selection, NULL, &sel_iter))
		return;
	gtk_list_store_remove (GTK_LIST_STORE(state->gui.source_areas),
			       &sel_iter);
	adjust_source_areas (state);

	dialog_set_button_sensitivity (NULL, state);
}

static void
cb_labels_toggled (G_GNUC_UNUSED GtkCheckButton *button,
		   ConsolidateState *state)
{
	gboolean copy_labels =
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_row)) ||
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_col));

	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.labels_copy), copy_labels);
	if (!copy_labels)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->gui.labels_copy), FALSE);
}

/***************************************************************************************/

static void
connect_signal_labels_toggled (ConsolidateState *state, GtkCheckButton *button)
{
	g_signal_connect (G_OBJECT (button),
		"toggled",
		G_CALLBACK (cb_labels_toggled), state);
}

static void
connect_signal_btn_clicked (ConsolidateState *state, GtkButton *button)
{
	g_signal_connect (G_OBJECT (button),
		"clicked",
		G_CALLBACK (cb_dialog_clicked), state);
}

static void
setup_widgets (ConsolidateState *state, GladeXML *glade_gui)
{
	GnmExprEntryFlags flags;
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	GtkCellRenderer *renderer;

	state->gui.dialog      = GTK_DIALOG        (glade_xml_get_widget (glade_gui, "dialog"));

	state->gui.function    = GTK_OPTION_MENU     (glade_xml_get_widget (glade_gui, "function"));
	state->gui.put         = GTK_OPTION_MENU     (glade_xml_get_widget (glade_gui, "put"));

	state->gui.destination = gnm_expr_entry_new (state->wbcg, TRUE);

/* Begin: Source Areas View*/
	state->gui.source_view = GTK_TREE_VIEW (glade_xml_get_widget 
						(glade_gui, 
						 "source_treeview"));
	state->gui.source_areas = GTK_TREE_MODEL(gtk_list_store_new 
						 (NUM_COLMNS, 
						  G_TYPE_STRING, 
						  GDK_TYPE_PIXBUF,
						  G_TYPE_INT));
	gtk_tree_view_set_model (state->gui.source_view, 
				 state->gui.source_areas);
	
	selection = gtk_tree_view_get_selection 
			(state->gui.source_view );
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	
	renderer = gnumeric_cell_renderer_expr_entry_new (state->wbcg);
	state->gui.cellrenderer = 
		GNUMERIC_CELL_RENDERER_EXPR_ENTRY (renderer);
	column = gtk_tree_view_column_new_with_attributes
		("", renderer,
		 "text", SOURCE_COLUMN,
		 "editable", IS_EDITABLE_COLUMN,
		 NULL);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_source_edited), state);
	/* Note, for GTK 2.4 we should */
	/* uncomment the next line and remove the following */
/* 		gtk_tree_view_column_set_expand (column, TRUE); */
	gtk_tree_view_column_set_min_width (column, 200);
	gtk_tree_view_append_column (state->gui.source_view, column);
	column = gtk_tree_view_column_new_with_attributes 
		("", gtk_cell_renderer_pixbuf_new (), 
		 "pixbuf", PIXMAP_COLUMN, NULL);
	gtk_tree_view_append_column (state->gui.source_view, column);
/* End: Source Areas View*/

	state->gui.clear       = GTK_BUTTON          (glade_xml_get_widget (glade_gui, "clear"));
	state->gui.delete      = GTK_BUTTON          (glade_xml_get_widget (glade_gui, "delete"));

	state->gui.labels_row  = GTK_CHECK_BUTTON (glade_xml_get_widget (glade_gui, "labels_row"));
	state->gui.labels_col  = GTK_CHECK_BUTTON (glade_xml_get_widget (glade_gui, "labels_col"));
	state->gui.labels_copy = GTK_CHECK_BUTTON (glade_xml_get_widget (glade_gui, "labels_copy"));

	state->gui.btn_ok     = GTK_BUTTON  (glade_xml_get_widget (glade_gui, "btn_ok"));
	state->gui.btn_cancel = GTK_BUTTON  (glade_xml_get_widget (glade_gui, "btn_cancel"));

	gtk_table_attach (GTK_TABLE (glade_xml_get_widget (glade_gui, "table1")),
			  GTK_WIDGET (state->gui.destination),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (GTK_WIDGET (state->gui.destination));

	flags = GNM_EE_SINGLE_RANGE;
	gnm_expr_entry_set_flags (state->gui.destination, flags, flags);

	gnumeric_editable_enters (GTK_WINDOW (state->gui.dialog),
				  GTK_WIDGET (state->gui.destination));

	cb_selection_changed (NULL, state);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_selection_changed), state);
	g_signal_connect (G_OBJECT (state->gui.destination),
		"changed",
		G_CALLBACK (dialog_set_button_sensitivity), state);
	g_signal_connect (G_OBJECT (state->gui.clear),
		"clicked",
		G_CALLBACK (cb_clear_clicked), state);
	g_signal_connect (G_OBJECT (state->gui.delete),
		"clicked",
		G_CALLBACK (cb_delete_clicked), state);

	connect_signal_labels_toggled (state, state->gui.labels_row);
	connect_signal_labels_toggled (state, state->gui.labels_col);
	connect_signal_labels_toggled (state, state->gui.labels_copy);

	connect_signal_btn_clicked (state, state->gui.btn_ok);
	connect_signal_btn_clicked (state, state->gui.btn_cancel);

	/* FIXME: that's not the proper help location */
	gnumeric_init_help_button (
		glade_xml_get_widget (glade_gui, "btn_help"),
		"data-menu.html");
}

static gboolean
add_source_area (SheetView *sv, Range const *r, gpointer closure)
{
	ConsolidateState *state = closure;
	char *range_name = global_range_name (sv_sheet (sv), r);
	GtkTreeIter      iter;
	
	gtk_list_store_prepend (GTK_LIST_STORE(state->gui.source_areas),
				&iter);
	gtk_list_store_set (GTK_LIST_STORE(state->gui.source_areas),
			    &iter,
			    IS_EDITABLE_COLUMN,	TRUE,
			    SOURCE_COLUMN, range_name,
			    PIXMAP_COLUMN, state->gui.pixmap,
			    -1);
	g_free (range_name);

	return TRUE;
}

void
dialog_consolidate (WorkbookControlGUI *wbcg)
{
	GladeXML *glade_gui;
	ConsolidateState *state;
	Range const *r = NULL;

	g_return_if_fail (wbcg != NULL);

	glade_gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg),
		"consolidate.glade", NULL, NULL);
        if (glade_gui == NULL)
                return;

	/* Primary static initialization */
	state = g_new0 (ConsolidateState, 1);
	state->wbcg        = wbcg;
	state->sv	   = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->sheet	   = sv_sheet (state->sv);
	state->glade_gui   = glade_gui;
	state->warning_dialog = NULL;
	state->areas_index = -1;

	setup_widgets (state, glade_gui);
	state->gui.pixmap =  gtk_widget_render_icon 
		(GTK_WIDGET(state->gui.dialog),
		 "Gnumeric_ExprEntry",
		 GTK_ICON_SIZE_LARGE_TOOLBAR,
		 "Gnumeric-Consolidate-Dialog");

	/* Dynamic initialization */
	cb_source_changed (NULL, state);
	cb_labels_toggled (state->gui.labels_row, state);

	/*
	 * When there are non-singleton selections add them all to the
	 * source range list for convenience
	 */
	if ((r = selection_first_range (state->sv, NULL, NULL)) != NULL 
	    && !range_is_singleton (r))
		selection_foreach_range (state->sv, TRUE, &add_source_area, state);

	adjust_source_areas (state);

	gtk_widget_grab_focus   (GTK_WIDGET (state->gui.function));
	gtk_widget_grab_default (GTK_WIDGET (state->gui.btn_ok));

	dialog_set_button_sensitivity(NULL, state);

	/* a candidate for merging into attach guru */
	g_object_set_data_full (G_OBJECT (state->gui.dialog),
		"state", state, (GDestroyNotify) cb_dialog_destroy);
	wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->gui.dialog));
	gnumeric_non_modal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->gui.dialog));
	gtk_widget_show (GTK_WIDGET (state->gui.dialog));
}

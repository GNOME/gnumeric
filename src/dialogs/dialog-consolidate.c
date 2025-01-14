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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>
#include <dialogs/tool-dialogs.h>

#include <commands.h>
#include <consolidate.h>
#include <func.h>
#include <gui-util.h>
#include <ranges.h>
#include <value.h>
#include <sheet-view.h>
#include <selection.h>
#include <widgets/gnm-expr-entry.h>
#include <widgets/gnm-cell-renderer-expr-entry.h>
#include <widgets/gnm-dao.h>
#include <wbc-gtk.h>
#include <dialogs/dao-gui-utils.h>
#include <tools/dao.h>

#include <string.h>


#define CONSOLIDATE_KEY            "consolidate-dialog"

enum {
	SOURCE_COLUMN,
	PIXMAP_COLUMN,
	IS_EDITABLE_COLUMN,
	NUM_COLUMNS
};

typedef struct {
	GnmGenericToolState base;

	GtkComboBox     *function;

	GtkTreeView       *source_view;
	GtkTreeModel      *source_areas;
	GnmCellRendererExprEntry *cellrenderer;
	GdkPixbuf         *pixmap;

	GtkButton         *clear;
	GtkButton         *delete;

	GtkCheckButton    *labels_row;
	GtkCheckButton    *labels_col;
	GtkCheckButton    *labels_copy;

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
	    (state->source_areas, &iter)) {
		do {
			char *source;

			gtk_tree_model_get (state->source_areas,
					    &iter,
					    SOURCE_COLUMN, &source,
					    -1);
			if (*source == 0)
				cnt_empty--;
			g_free (source);
		} while (gtk_tree_model_iter_next
			 (state->source_areas,&iter));
	}

	for (i = 0; i < cnt_empty; i++) {
		gtk_list_store_append (GTK_LIST_STORE(state->source_areas),
				       &iter);
		gtk_list_store_set (GTK_LIST_STORE(state->source_areas),
				    &iter,
				    IS_EDITABLE_COLUMN,	TRUE,
				    SOURCE_COLUMN, "",
				    PIXMAP_COLUMN, state->pixmap,
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
static GnmConsolidate *
construct_consolidate (ConsolidateState *state, data_analysis_output_t  *dao)
{
	GnmConsolidate      *cs   = gnm_consolidate_new ();
	GnmConsolidateMode  mode = 0;
	char       const *func;
	GnmValue         *range_value;
	GtkTreeIter      iter;
	gboolean         has_iter;

	switch (gtk_combo_box_get_active (state->function)) {
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

	gnm_consolidate_set_function (cs, gnm_func_lookup (func, NULL));

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->labels_row)))
		mode |= CONSOLIDATE_COL_LABELS;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->labels_col)))
		mode |= CONSOLIDATE_ROW_LABELS;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->labels_copy)))
		mode |= CONSOLIDATE_COPY_LABELS;
	if (!dao_put_formulas (dao))
		mode |= CONSOLIDATE_PUT_VALUES;

	gnm_consolidate_set_mode (cs, mode);

	g_return_val_if_fail (gtk_tree_model_iter_n_children
			      (state->source_areas,
			       NULL)> 2, NULL);

	has_iter = gtk_tree_model_get_iter_first (state->source_areas,
						  &iter);
	g_return_val_if_fail (has_iter, NULL);
	do {
		char *source;

		gtk_tree_model_get (state->source_areas,
				    &iter,
				    SOURCE_COLUMN, &source,
				    -1);
		if (*source) {
			range_value = value_new_cellrange_str (state->base.sheet, source);

			if (range_value == NULL) {
				state->construct_error = g_strdup_printf (
					_("Specification %s "
					  "does not define a region"),
					source);
				g_free (source);
				gnm_consolidate_free (cs, FALSE);
				return NULL;
			}
			if (!gnm_consolidate_add_source (cs, range_value)) {
				state->construct_error = g_strdup_printf (
					_("Source region %s overlaps "
					  "with the destination region"),
					source);
				g_free (source);
				gnm_consolidate_free (cs, FALSE);
				return NULL;
			}
		}
		g_free (source);
	} while (gtk_tree_model_iter_next
		 (state->source_areas,&iter));

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
	gboolean ready;

	ready = gnm_dao_is_ready (GNM_DAO (state->base.gdao))
		&& (gtk_tree_model_iter_n_children
		    (state->source_areas, NULL)> 2);
	gtk_widget_set_sensitive (GTK_WIDGET (state->base.ok_button), ready);
	return;
}

static void
cb_selection_changed (G_GNUC_UNUSED GtkTreeSelection *ignored,
		      ConsolidateState *state)
{
	GtkTreeIter  iter;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (state->source_view);

	gtk_widget_set_sensitive (GTK_WIDGET(state->delete),
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

	if (gtk_tree_model_get_iter (state->source_areas, &iter, path))
		gtk_list_store_set (GTK_LIST_STORE(state->source_areas),
				    &iter, SOURCE_COLUMN, new_text, -1);
	else
		g_warning ("Did not get a valid iterator");

	gtk_tree_path_free (path);
	adjust_source_areas (state);
}

static void
cb_dialog_destroy (ConsolidateState *state)
{
	if (state->pixmap != NULL)
		g_object_unref (state->pixmap);
	if (state->construct_error != NULL) {
		g_warning ("The construct error was not freed, this should not happen!");
		g_free (state->construct_error);
	}
}

static void
cb_consolidate_ok_clicked (GtkWidget *button, ConsolidateState *state)
{
	GnmConsolidate *cs;
	data_analysis_output_t  *dao;

	if (state->cellrenderer->entry)
		gnm_cell_renderer_expr_entry_editing_done (
			GTK_CELL_EDITABLE (state->cellrenderer->entry),
			state->cellrenderer);

	if (state->base.warning_dialog != NULL)
		gtk_widget_destroy (state->base.warning_dialog);

	dao  = parse_output ((GnmGenericToolState *)state, NULL);
	cs = construct_consolidate (state, dao);

	/*
	 * If something went wrong consolidate_construct
	 * return %NULL and sets the state->construct_error to
	 * a suitable error message
	 */
	if (cs == NULL) {
		go_gtk_notice_nonmodal_dialog (GTK_WINDOW (state->base.dialog),
					       &state->base.warning_dialog,
					       GTK_MESSAGE_ERROR,
					       "%s", state->construct_error);
		g_free (state->construct_error);
		g_free (dao);
		state->construct_error = NULL;

		return;
	}

	if (gnm_consolidate_check_destination (cs, dao)) {
		if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
					state->base.sheet,
					dao, cs, gnm_tool_consolidate_engine,
					FALSE) &&
		    (button == state->base.ok_button))
			gtk_widget_destroy (state->base.dialog);
	} else {
		go_gtk_notice_nonmodal_dialog (GTK_WINDOW (state->base.dialog),
					  &state->base.warning_dialog,
					  GTK_MESSAGE_ERROR,
					  _("The output range overlaps "
					    "with the input ranges."));
		g_free (dao);
		gnm_consolidate_free (cs, FALSE);
	}
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

	if (state->cellrenderer->entry)
		gnm_cell_renderer_expr_entry_editing_done (
			GTK_CELL_EDITABLE (state->cellrenderer->entry),
			state->cellrenderer);

	gtk_list_store_clear (GTK_LIST_STORE(state->source_areas));
	adjust_source_areas (state);

	dialog_set_button_sensitivity (NULL, state);
}

static void
cb_delete_clicked (G_GNUC_UNUSED GtkButton *button,
		   ConsolidateState *state)
{
	GtkTreeIter sel_iter;
	GtkTreeSelection  *selection =
		gtk_tree_view_get_selection (state->source_view);

	if (state->cellrenderer->entry)
		gnm_cell_renderer_expr_entry_editing_done (
			GTK_CELL_EDITABLE (state->cellrenderer->entry),
			state->cellrenderer);
	if (!gtk_tree_selection_get_selected (selection, NULL, &sel_iter))
		return;
	gtk_list_store_remove (GTK_LIST_STORE(state->source_areas),
			       &sel_iter);
	adjust_source_areas (state);

	dialog_set_button_sensitivity (NULL, state);
}

static void
cb_labels_toggled (G_GNUC_UNUSED GtkCheckButton *button,
		   ConsolidateState *state)
{
	gboolean copy_labels =
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->labels_row)) ||
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->labels_col));

	gtk_widget_set_sensitive (GTK_WIDGET (state->labels_copy), copy_labels);
	if (!copy_labels)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->labels_copy), FALSE);
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
setup_widgets (ConsolidateState *state, GtkBuilder *gui)
{
	GtkTreeViewColumn *column;
	GtkTreeSelection  *selection;
	GtkCellRenderer *renderer;

	state->function    = go_gtk_builder_combo_box_init_text (gui, "function");
	gtk_combo_box_set_active (state->function, 0);

/* Begin: Source Areas View*/
	state->source_view = GTK_TREE_VIEW (go_gtk_builder_get_widget
						(gui,
						 "source_treeview"));
	state->source_areas = GTK_TREE_MODEL(gtk_list_store_new
						 (NUM_COLUMNS,
						  G_TYPE_STRING,
						  GDK_TYPE_PIXBUF,
						  G_TYPE_INT));
	gtk_tree_view_set_model (state->source_view,
				 state->source_areas);
	g_object_unref (state->source_areas);

	selection = gtk_tree_view_get_selection
			(state->source_view );
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

	renderer = gnm_cell_renderer_expr_entry_new (state->base.wbcg);
	state->cellrenderer =
		GNM_CELL_RENDERER_EXPR_ENTRY (renderer);
	column = gtk_tree_view_column_new_with_attributes
		("", renderer,
		 "text", SOURCE_COLUMN,
		 "editable", IS_EDITABLE_COLUMN,
		 NULL);
	g_signal_connect (G_OBJECT (renderer), "edited",
			  G_CALLBACK (cb_source_edited), state);
		gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (state->source_view, column);
	column = gtk_tree_view_column_new_with_attributes
		("", gtk_cell_renderer_pixbuf_new (),
		 "pixbuf", PIXMAP_COLUMN, NULL);
	gtk_tree_view_append_column (state->source_view, column);
/* End: Source Areas View*/

	state->clear       = GTK_BUTTON          (go_gtk_builder_get_widget (gui, "clear"));
	state->delete      = GTK_BUTTON          (go_gtk_builder_get_widget (gui, "delete"));

	state->labels_row  = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "labels_row"));
	state->labels_col  = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "labels_col"));
	state->labels_copy = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "labels_copy"));

	cb_selection_changed (NULL, state);
	g_signal_connect (selection,
		"changed",
		G_CALLBACK (cb_selection_changed), state);
	g_signal_connect (G_OBJECT (state->clear),
		"clicked",
		G_CALLBACK (cb_clear_clicked), state);
	g_signal_connect (G_OBJECT (state->delete),
		"clicked",
		G_CALLBACK (cb_delete_clicked), state);

	connect_signal_labels_toggled (state, state->labels_row);
	connect_signal_labels_toggled (state, state->labels_col);
	connect_signal_labels_toggled (state, state->labels_copy);

}

static gboolean
add_source_area (SheetView *sv, GnmRange const *r, gpointer closure)
{
	ConsolidateState *state = closure;
	char             *range_name;
	GtkTreeIter       iter;

	if (range_is_singleton (r))
		return TRUE;

	range_name = global_range_name (sv_sheet (sv), r);

	gtk_list_store_prepend (GTK_LIST_STORE(state->source_areas),
				&iter);
	gtk_list_store_set (GTK_LIST_STORE(state->source_areas),
			    &iter,
			    IS_EDITABLE_COLUMN,	TRUE,
			    SOURCE_COLUMN, range_name,
			    PIXMAP_COLUMN, state->pixmap,
			    -1);
	g_free (range_name);

	return TRUE;
}

/**
 * dialog_consolidate_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static void
dialog_consolidate_tool_init (ConsolidateState *state)
{
	state->areas_index = -1;

	setup_widgets (state, state->base.gui);
	state->pixmap = go_gtk_widget_render_icon_pixbuf
		(GTK_WIDGET (state->base.dialog),
		 "gnumeric-exprentry",
		 GTK_ICON_SIZE_LARGE_TOOLBAR);

	/* Dynamic initialization */
	cb_source_changed (NULL, state);
	cb_labels_toggled (state->labels_row, state);

	/*
	 * When there are non-singleton selections add them all to the
	 * source range list for convenience
	 */
	sv_selection_foreach (state->base.sv, &add_source_area, state);

	adjust_source_areas (state);
	dialog_set_button_sensitivity (NULL, state);
	state->base.state_destroy = (state_destroy_t)cb_dialog_destroy;
}

void
dialog_consolidate (WBCGtk *wbcg)
{
	ConsolidateState *state;
	SheetView *sv;
	Sheet *sheet;

	g_return_if_fail (wbcg != NULL);
	sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));
	sheet = sv_sheet (sv);

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, CONSOLIDATE_KEY)) {
		return;
	}

	/* Primary static initialization */
	state = g_new0 (ConsolidateState, 1);

	if (dialog_tool_init ((GnmGenericToolState *)state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_CONSOLIDATE,
			      "res:ui/consolidate.ui", "Consolidate",
			      _("Could not create the Consolidate dialog."),
			      CONSOLIDATE_KEY,
			      G_CALLBACK (cb_consolidate_ok_clicked),
			      NULL,
			      G_CALLBACK (dialog_set_button_sensitivity),
			      0))
		return;

	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	dialog_consolidate_tool_init (state);
	gtk_widget_show (GTK_WIDGET (state->base.dialog));
}

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * dialog-cell-format-cond.c:  Implements a dialog to format cells.
 *
 * (c) Copyright 2010-2011 Andreas J. Guelzow <aguelzow@pyrshep.ca>
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
  **/

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"
#include <dead-kittens.h>

#include <sheet.h>
#include <sheet-view.h>
#include <sheet-merge.h>
#include <sheet-style.h>
#include <gui-util.h>
#include <selection.h>
#include <ranges.h>
#include <cell.h>
#include <expr.h>
#include <value.h>
#include <position.h>
#include <mstyle.h>
#include <application.h>
#include <validation.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <commands.h>
#include <mathfunc.h>
#include <style-conditions.h>

#include <gtk/gtk.h>

#include <string.h>

#define CELL_FORMAT_KEY "cell-format-cond-dialog"
#define CELL_FORMAT_DEF_KEY "cell-format-cond-def-dialog"

typedef struct _CFormatState {
	GtkBuilder	*gui;
	WBCGtk	        *wbcg;
	GtkDialog	*dialog;
	GtkWidget	*close_button;

	Sheet		*sheet;
	SheetView	*sv;
	unsigned int	 conflicts;
	gboolean         homogeneous;
	GnmStyle	*style;

	GtkButton       *add;
	GtkButton       *remove;
	GtkButton       *clear;
	GtkButton       *expand;
	GtkButton       *edit;
	GtkLabel        *label;
	GtkTreeView     *treeview;
	GtkTreeStore    *model;
	GtkTreeSelection *selection;

	struct {
		GOUndo *undo;
		GOUndo *redo;
		int size;
		GnmStyle *new_style;
		GnmStyle *old_style;
	} action;
} CFormatState;

typedef struct _CFormatChooseState {
	CFormatState    *cf_state;
	GtkBuilder	*gui;
	GtkDialog	*dialog;
	GtkWidget	*cancel_button;
	GtkWidget	*ok_button;
	GtkWidget	*new_button;
	GtkWidget	*combo;
	GtkListStore    *typestore;
	GnmStyle        *style;
	GtkWidget       *style_label;
} CFormatChooseState;

enum {
	CONDITIONS_RANGE,
	CONDITIONS_COND,
	CONDITIONS_NUM_COLUMNS
};



/*****************************************************************************/
static void c_fmt_dialog_load (CFormatState *state);
static void c_fmt_dialog_apply_add_choice (CFormatState *state, GnmStyleCond *cond);
static void c_fmt_dialog_update_buttons (CFormatState *state);

/*****************************************************************************/

/* Handler for destroy */
static void
cb_c_fmt_dialog_chooser_destroy (CFormatChooseState *state)
{
	if (state->style)
		gnm_style_unref (state->style);
	g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static void
cb_dialog_chooser_destroy (GtkDialog *dialog)
{
	g_object_set_data (G_OBJECT (dialog), "state", NULL);
}

static void
c_fmt_dialog_set_sensitive (CFormatChooseState *state)
{
	gtk_widget_set_sensitive (state->ok_button, state->style != NULL);
}

void     
dialog_cell_format_style_added (gpointer closure, GnmStyle *style)
{
	CFormatChooseState *state = closure;
	
	if (state->style)
		gnm_style_unref (state->style);
	state->style = style;
	gtk_label_set_text (GTK_LABEL (state->style_label), 
			    style ? _("(defined)") : _("undefined"));
	c_fmt_dialog_set_sensitive (state);
}

static void
cb_c_fmt_dialog_chooser_new_button (G_GNUC_UNUSED GtkWidget *btn, CFormatChooseState *state)
{
	dialog_cell_format_select_style (state->cf_state->wbcg, 1 << FD_BACKGROUND, 
					 GTK_WINDOW (state->dialog), state);
}

static void
cb_c_fmt_dialog_chooser_buttons (GtkWidget *btn, CFormatChooseState *state)
{
	if (btn == state->ok_button) {
		GnmStyleCond *cond = g_new0(GnmStyleCond, 1);
		GtkTreeIter iter;

		cond->overlay = gnm_style_new ();
		if (state->style) {
			gnm_style_merge_element (cond->overlay, state->style, 
						 MSTYLE_COLOR_BACK);
			gnm_style_merge_element (cond->overlay, state->style, 
						 MSTYLE_COLOR_PATTERN);
			gnm_style_merge_element (cond->overlay, state->style, 
						 MSTYLE_PATTERN);
		}
		if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (state->combo), &iter))
			gtk_tree_model_get (GTK_TREE_MODEL (state->typestore),
					    &iter,
					    1, &cond->op,
					    -1);
		else
			cond->op = GNM_STYLE_COND_CONTAINS_ERR;

		c_fmt_dialog_apply_add_choice (state->cf_state, cond);
		g_free (cond);
	}
	gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

static void	
c_fmt_dialog_chooser_load_combo (CFormatChooseState *state)
{
	static struct {
		char const *label;
		gint type;
	} cond_types[] = {
		{ N_("Cell contains an error value."), GNM_STYLE_COND_CONTAINS_ERR},
		{ N_("Cell does not contain an error value."), GNM_STYLE_COND_NOT_CONTAINS_ERR},
		{ N_("Cell contains whitespace."), GNM_STYLE_COND_CONTAINS_BLANKS},
		{ N_("Cell does not contain whitespace."), GNM_STYLE_COND_NOT_CONTAINS_BLANKS}
	};
	guint i;
	GtkCellRenderer  *cell;
	GtkTreeIter iter;

	for (i = 0; i < G_N_ELEMENTS (cond_types); i++)
		gtk_list_store_insert_with_values (state->typestore,
						   NULL, G_MAXINT,
                                                   0, _(cond_types[i].label),
						   1, cond_types[i].type,
						   -1);
	cell = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(state->combo), cell, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(state->combo), cell, "text", 0, NULL);
	if (gtk_tree_model_get_iter_first
	    (GTK_TREE_MODEL (state->typestore), &iter))
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (state->combo), &iter);

}

static void	
c_fmt_dialog_chooser (CFormatState *cf_state)
{
	GtkBuilder     *gui;
	CFormatChooseState  *state;
	GtkWidget *dialog;

	g_return_if_fail (cf_state != NULL);

	gui = gnm_gtk_builder_new ("cell-format-cond-def.ui", NULL, 
				   GO_CMD_CONTEXT (cf_state->wbcg));
        if (gui == NULL)
                return;

	/* Initialize */
	state = g_new (CFormatChooseState, 1);
	state->gui	= gui;
	state->cf_state = cf_state;
	state->style = NULL;

	dialog = go_gtk_builder_get_widget (state->gui, "style-condition-def");
	g_return_if_fail (dialog != NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Style Condition"));

	/* Initialize */
	state->dialog	   = GTK_DIALOG (dialog);

	state->cancel_button = go_gtk_builder_get_widget (state->gui, "cancel-button");
	state->ok_button = go_gtk_builder_get_widget (state->gui, "ok-button");
	state->new_button = go_gtk_builder_get_widget (state->gui, "new-button");
	state->combo = go_gtk_builder_get_widget (state->gui, "condition-combo");
	state->typestore = GTK_LIST_STORE (gtk_combo_box_get_model 
					   (GTK_COMBO_BOX (state->combo)));
	c_fmt_dialog_chooser_load_combo (state);
	
	state->style_label = go_gtk_builder_get_widget (state->gui, "style-label");
	gtk_label_set_text (GTK_LABEL (state->style_label), _("(undefined)"));

	gnumeric_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help-button"),
		GNUMERIC_HELP_LINK_CELL_FORMAT_COND);
	c_fmt_dialog_set_sensitive (state);

	gnumeric_restore_window_geometry (GTK_WINDOW (state->dialog),
					  CELL_FORMAT_DEF_KEY);

	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_chooser_buttons), state);
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_chooser_buttons), state);
	g_signal_connect (G_OBJECT (state->new_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_chooser_new_button), state);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_c_fmt_dialog_chooser_destroy);
	g_signal_connect (G_OBJECT (dialog), "destroy",
			  G_CALLBACK (cb_dialog_chooser_destroy), NULL);
	go_gtk_window_set_transient (GTK_WINDOW (state->cf_state->dialog), 
				     GTK_WINDOW (state->dialog));
	gtk_window_set_modal (GTK_WINDOW (state->dialog), TRUE);
	gtk_widget_show (GTK_WIDGET (state->dialog));
}



/*****************************************************************************/
/*****************************************************************************/
static gboolean
c_fmt_dialog_condition_setter (SheetView *sv, GnmRange const *range, CFormatState *state)
{
	GnmSheetRange *sr = g_new (GnmSheetRange, 1);
	sr->range = *range;
	sr->sheet = sv->sheet;
	state->action.redo = go_undo_combine 
		(state->action.redo,
		 sheet_apply_style_undo (sr, state->action.new_style));
	sr = g_new (GnmSheetRange, 1);
	sr->range = *range;
	sr->sheet = sv->sheet;
	state->action.undo = go_undo_combine 
		(sheet_apply_style_undo (sr, state->action.old_style),
		 state->action.undo);
	state->action.size++;
	return TRUE;
}

static gboolean
c_fmt_dialog_condition_setter_tiled (SheetView *sv, GnmRange const *range, CFormatState *state)
{
	GnmStyleList *l, *list = sheet_style_collect_conditions (state->sheet, range);
	for (l = list; l != NULL; l = l->next) {
		GnmStyleConditions *old_cond;
		GnmStyleRegion const *sr = l->data;
		GnmRange r  = *((GnmRange *) l->data);

		r.start.row += range->start.row;
		r.end.row += range->start.row;
		r.start.col += range->start.col;
		r.end.col += range->start.col;
		state->action.old_style = gnm_style_new ();
		old_cond = gnm_style_get_conditions (sr->style);
		gnm_style_set_conditions (state->action.old_style, 
					  g_object_ref (G_OBJECT (old_cond)));
		c_fmt_dialog_condition_setter (state->sv, &r, state);
		gnm_style_unref (state->action.old_style);
		state->action.old_style = NULL;
	}
	style_list_free (list);
	return TRUE;
}

static void
c_fmt_dialog_set_conditions (CFormatState *state, char const *cmd_label)
{
	GnmStyleConditions *old_cond;

	state->action.undo = NULL;
	state->action.redo = NULL;
	state->action.size = 0;

	if (state->homogeneous) {
		state->action.old_style = gnm_style_new ();
		old_cond = gnm_style_get_conditions (state->style);
		gnm_style_set_conditions (state->action.old_style, 
					  old_cond ? g_object_ref (G_OBJECT (old_cond)) : NULL);
		
		sv_selection_foreach (state->sv, 
				      (GnmSelectionFunc)c_fmt_dialog_condition_setter, 
				      state);
	} else {
		sv_selection_foreach (state->sv, 
				      (GnmSelectionFunc)c_fmt_dialog_condition_setter_tiled, 
				      state);
	}
	cmd_generic_with_size (WORKBOOK_CONTROL (state->wbcg), cmd_label,
			       state->action.size, state->action.undo, state->action.redo);

	state->action.undo = NULL;
	state->action.redo = NULL;
	if (state->action.old_style) {
		gnm_style_unref (state->action.old_style);
		state->action.old_style = NULL;
	}
}

static void
c_fmt_dialog_apply_add_choice (CFormatState *state, GnmStyleCond *cond)
{
	if (cond != NULL) {
		GnmStyleConditions *sc;
		sc = gnm_style_conditions_dup (gnm_style_get_conditions (state->style));
		if (sc == NULL)
			sc = gnm_style_conditions_new ();
		gnm_style_conditions_insert (sc, cond, -1);
		state->action.new_style = gnm_style_new ();
		gnm_style_set_conditions (state->action.new_style, sc);
	
		c_fmt_dialog_set_conditions (state, _("Set conditional formatting"));

		gnm_style_unref (state->action.new_style);
		state->action.new_style = NULL;

		c_fmt_dialog_load (state);
	}	
}

static void
cb_c_fmt_dialog_add_clicked (G_GNUC_UNUSED GtkButton *button, CFormatState *state)
{

	c_fmt_dialog_chooser (state);
}

static void
cb_c_fmt_dialog_remove_clicked (G_GNUC_UNUSED GtkButton *button, CFormatState *state)
{
	c_fmt_dialog_load (state);
}

static void
cb_c_fmt_dialog_clear_clicked (G_GNUC_UNUSED GtkButton *button, CFormatState *state)
{
	state->action.new_style = gnm_style_new ();
	gnm_style_set_conditions (state->action.new_style, NULL);
	
	c_fmt_dialog_set_conditions (state, _("Clear conditional formatting"));

	gnm_style_unref (state->action.new_style);
	state->action.new_style = NULL;

	c_fmt_dialog_load (state);
}

static void
cb_c_fmt_dialog_expand_clicked (G_GNUC_UNUSED GtkButton *button, CFormatState *state)
{
	c_fmt_dialog_load (state);
}

static void
cb_c_fmt_dialog_edit_clicked (G_GNUC_UNUSED GtkButton *button, CFormatState *state)
{
	c_fmt_dialog_load (state);
}


static void
c_fmt_dialog_conditions_page_load_cond_single_f (CFormatState *state,
					       GnmExprTop const *texpr, GtkTreeIter *iter1)
{
	char *formula;
	GnmParsePos pp;
	GtkTreeIter iter2;

	gtk_tree_store_append (state->model, &iter2, iter1);

	parse_pos_init (&pp, wb_control_get_workbook (WORKBOOK_CONTROL (state->wbcg)),
			state->sheet, 0, 0);

	formula = gnm_expr_top_as_string (texpr, &pp, gnm_conventions_default);
	gtk_tree_store_set (state->model, &iter2, CONDITIONS_RANGE, NULL,
			    CONDITIONS_COND, formula, -1);
	g_free (formula);
}


static void
c_fmt_dialog_conditions_page_load_cond_double_f (CFormatState *state,
					       GnmStyleCond const *cond, GtkTreeIter *iter1)
{
	c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], iter1);
	c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[1], iter1);
}

static void
c_fmt_dialog_conditions_page_load_cond (CFormatState *state, GnmStyleCond const *cond,
				      GtkTreeIter *iter)
{
	GtkTreeIter iter1;

	gtk_tree_store_append (state->model, &iter1, iter);

	switch (cond->op) {
	case GNM_STYLE_COND_BETWEEN:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is between these "
				      "two values, a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_double_f (state, cond, &iter1);
		break;
	case GNM_STYLE_COND_NOT_BETWEEN:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is not between these"
				      " two values, a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_double_f (state, cond, &iter1);
		break;
	case GNM_STYLE_COND_EQUAL:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is equal to this value"
				      ", a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_NOT_EQUAL:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is not equal to this value"
				      ", a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_GT:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is > this value, a "
				      "special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_LT:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is < this value, a "
				      "special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_GTE:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is \xe2\x89\xa7 this "
				      "value, a special style is used."), -1);

		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_LTE:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content is \xe2\x89\xa6 this "
				      "value, a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;

	case GNM_STYLE_COND_CUSTOM:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If this formula evaluates to TRUE, a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_CONTAINS_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content contains this string"
				      ", a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content does not contain this string"
				      ", a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_BEGINS_WITH_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content begins with this string"
				      ", a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_NOT_BEGINS_WITH_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content does not begin with this string,"
				      " a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_ENDS_WITH_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content ends with this string"
				      ", a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_NOT_ENDS_WITH_STR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content does not end  "
				      "with this string, a special style is used."), -1);
		c_fmt_dialog_conditions_page_load_cond_single_f (state, cond->texpr[0], &iter1);
		break;
	case GNM_STYLE_COND_CONTAINS_ERR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell contains an error "
				      "value, a special style is used."), -1);
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_ERR:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell does not contain an error value"
				      ", a special style is used."), -1);
		break;
	case GNM_STYLE_COND_CONTAINS_BLANKS:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content "
				      "contains blanks, a special style is used."), -1);
		break;
	case GNM_STYLE_COND_NOT_CONTAINS_BLANKS:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("If the cell content does not contain blanks"
				      ", a special style is used."), -1);
		break;
	default:
		gtk_tree_store_set (state->model, &iter1, CONDITIONS_RANGE, NULL,
				    CONDITIONS_COND,
				    _("This is an unknown condition type."), -1);
		return;
	}
}

static void
c_fmt_dialog_conditions_page_load_conditions (GnmStyle *style, char const *range, CFormatState *state)
{
	GnmStyleConditions const *sc;
	GArray const *conds;
	guint i;
	GtkTreeIter iter1, *iter;

	if (range == NULL)
		iter = NULL;
	else {
		iter = &iter1;
		gtk_tree_store_append (state->model, iter, NULL);
		gtk_tree_store_set (state->model, iter, CONDITIONS_RANGE, range,
				    CONDITIONS_COND, NULL, -1);
	}


	if (gnm_style_is_element_set (style, MSTYLE_CONDITIONS) &&
	    NULL != (sc = gnm_style_get_conditions (style)) &&
	    NULL != (conds = gnm_style_conditions_details (sc)))
		for (i = 0 ; i < conds->len ; i++)
			c_fmt_dialog_conditions_page_load_cond
				(state, &g_array_index (conds,
							GnmStyleCond,
							i), iter);

}

static gboolean
c_fmt_dialog_condition_collector (SheetView *sv, GnmRange const *range, gpointer user_data)
{
	CFormatState *state = user_data;
	GnmStyleList *l, *list = sheet_style_collect_conditions (state->sheet, range);

	for (l = list; l != NULL; l = l->next) {
		GnmStyleRegion const *sr = l->data;
		GnmRange r  = *((GnmRange *) l->data);
		r.start.row += range->start.row;
		r.end.row += range->start.row;
		r.start.col += range->start.col;
		r.end.col += range->start.col;
		c_fmt_dialog_conditions_page_load_conditions
			(sr->style, range_as_string (&r), state);
	}

	style_list_free (list);
	return TRUE;

}

static gboolean
c_fmt_dialog_selection_type (SheetView *sv,
			   GnmRange const *range,
			   gpointer user_data)
{
	GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX] = {NULL};
	CFormatState *state = user_data;
	GSList *merged = gnm_sheet_merge_get_overlap (sv->sheet, range);
	GnmRange r = *range;
	gboolean allow_multi =
		merged == NULL ||
		merged->next != NULL ||
		!range_equal ((GnmRange *)merged->data, range);
	g_slist_free (merged);

	/* allow_multi == FALSE && !is_singleton (range) means that we are in
	 * an merge cell, use only the top left */
	if (!allow_multi) {
		if (r.start.col != r.end.col)
	   			r.end.col = r.start.col;
		if (range->start.row != range->end.row)
				r.end.row = r.start.row;
	}

	state->conflicts = sheet_style_find_conflicts (state->sheet, &r,
		&(state->style), borders);

	return TRUE;
}

static void
c_fmt_dialog_load (CFormatState *state)
{
	gtk_tree_store_clear (state->model);
	if (state->style)
		gnm_style_unref (state->style);
	state->style = NULL;

	(void) sv_selection_foreach (state->sv,
		c_fmt_dialog_selection_type, state);

	state->homogeneous = !(state->conflicts & (1 << MSTYLE_CONDITIONS));

	if (state->homogeneous) {
		gtk_label_set_markup (state->label,
				      _("The selection is homogeneous with "
					 "respect to conditions."));
		if (state->style != NULL)
			c_fmt_dialog_conditions_page_load_conditions
				(state->style, NULL, state);
		gtk_tree_view_expand_all (state->treeview);
	} else {
		gtk_label_set_markup (state->label,
				      _("The selection is <b>not</b> "
					 "homogeneous "
					 "with respect to conditions!"));
		(void) sv_selection_foreach (state->sv,
					     c_fmt_dialog_condition_collector, state);
	}
	c_fmt_dialog_update_buttons(state);
}

static void
c_fmt_dialog_update_buttons (CFormatState *state)
{
	GtkTreeIter iter;
	gboolean not_empty, selected;

	not_empty = gtk_tree_model_get_iter_first 
		(GTK_TREE_MODEL (state->model), &iter);
	selected = gtk_tree_selection_get_selected 
		(state->selection, NULL, NULL);
	
	gtk_widget_set_sensitive (GTK_WIDGET (state->clear), not_empty);

	gtk_widget_set_sensitive (GTK_WIDGET (state->add), state->homogeneous);
	gtk_widget_set_sensitive (GTK_WIDGET (state->remove),
				  state->homogeneous && selected);
	gtk_widget_set_sensitive (GTK_WIDGET (state->expand),
				  (!state->homogeneous) && selected);
	gtk_widget_set_sensitive (GTK_WIDGET (state->edit),
				  state->homogeneous && selected);
}

static void
cb_selection_changed (GtkTreeSelection *treeselection, CFormatState *state)
{
	c_fmt_dialog_update_buttons (state);
}

static gboolean
cb_can_select (G_GNUC_UNUSED GtkTreeSelection *selection,
	       G_GNUC_UNUSED GtkTreeModel *model,
	       GtkTreePath *path,
	       gboolean path_currently_selected,
	       G_GNUC_UNUSED CFormatState *state)
{
	if (path_currently_selected)
		return TRUE;

	return (gtk_tree_path_get_depth (path) == 1);
}

static gboolean
cb_c_format_dialog_range (SheetView *sv, GnmRange const *range, GString *str)
{
	g_string_append (str, range_as_string (range));
	g_string_append (str, ", ");
	return TRUE;
}

static void
c_fmt_dialog_init_conditions_page (CFormatState *state)
{
	GtkTreeViewColumn * column;
	GtkCellRenderer *renderer;
	GtkLabel *hl;
	GString *str;

	g_return_if_fail (state != NULL);

	state->add = GTK_BUTTON (go_gtk_builder_get_widget (state->gui,
								     "conditions_add"));
	gtk_widget_set_sensitive (GTK_WIDGET (state->add), FALSE);
	state->remove = GTK_BUTTON (go_gtk_builder_get_widget (state->gui,
								     "conditions_remove"));
	gtk_widget_set_sensitive (GTK_WIDGET (state->remove), FALSE);
	state->clear = GTK_BUTTON (go_gtk_builder_get_widget (state->gui,
								     "conditions_clear"));
	gtk_widget_set_sensitive (GTK_WIDGET (state->clear), FALSE);
	state->expand = GTK_BUTTON (go_gtk_builder_get_widget (state->gui,
								     "conditions_expand"));
	gtk_widget_set_sensitive (GTK_WIDGET (state->expand), FALSE);
	state->edit = GTK_BUTTON (go_gtk_builder_get_widget (state->gui,
								     "conditions_edit"));
	gtk_widget_set_sensitive (GTK_WIDGET (state->edit), FALSE);

	state->model = gtk_tree_store_new (CONDITIONS_NUM_COLUMNS,
						      G_TYPE_STRING,
						      G_TYPE_STRING);
	state->treeview = GTK_TREE_VIEW (go_gtk_builder_get_widget 
					 (state->gui, "conditions_treeview"));
	gtk_tree_view_set_fixed_height_mode (state->treeview, FALSE);
	gtk_tree_view_set_model (state->treeview, GTK_TREE_MODEL (state->model));
	g_object_unref (state->model);
	state->selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (state->selection, GTK_SELECTION_SINGLE);
	gtk_tree_selection_set_select_function (state->selection,
						(GtkTreeSelectionFunc) cb_can_select,
						state, NULL);
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes
		("Range", renderer, "text", CONDITIONS_RANGE, NULL);
	gtk_tree_view_insert_column (state->treeview, column, -1);
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes
		("Conditions", renderer, "text", CONDITIONS_COND, NULL);
	gtk_tree_view_insert_column (state->treeview, column, -1);
	gtk_tree_view_set_expander_column (state->treeview, column);

	state->label = GTK_LABEL (go_gtk_builder_get_widget (state->gui,
								   "conditions_label"));
	hl = GTK_LABEL (go_gtk_builder_get_widget (state->gui, "header-label"));
	gtk_label_set_ellipsize (hl, PANGO_ELLIPSIZE_END);
	str = g_string_new (_("Editing conditional formatting: "));
	sv_selection_foreach (state->sv, 
			      (GnmSelectionFunc)cb_c_format_dialog_range, 
			      str);
	g_string_truncate (str, str->len -2);
	gtk_label_set_text(hl, str->str);
	g_string_free (str, TRUE);

	c_fmt_dialog_load (state);

	g_signal_connect (G_OBJECT (state->selection), "changed",
			  G_CALLBACK (cb_selection_changed), state);
	g_signal_connect (G_OBJECT (state->add), "clicked",
			  G_CALLBACK (cb_c_fmt_dialog_add_clicked), state);
	g_signal_connect (G_OBJECT (state->remove), "clicked",
			  G_CALLBACK (cb_c_fmt_dialog_remove_clicked), state);
	g_signal_connect (G_OBJECT (state->clear), "clicked",
			  G_CALLBACK (cb_c_fmt_dialog_clear_clicked), state);
	g_signal_connect (G_OBJECT (state->expand), "clicked",
			  G_CALLBACK (cb_c_fmt_dialog_expand_clicked), state);
	g_signal_connect (G_OBJECT (state->edit), "clicked",
			  G_CALLBACK (cb_c_fmt_dialog_edit_clicked), state);
	
	gtk_widget_hide (GTK_WIDGET (state->remove)); 
	gtk_widget_hide (GTK_WIDGET (state->expand)); 
	gtk_widget_hide (GTK_WIDGET (state->edit)); 
}

/*****************************************************************************/

/* button handlers */
static void
cb_c_fmt_dialog_dialog_buttons (G_GNUC_UNUSED GtkWidget *btn, CFormatState *state)
{
		gtk_widget_destroy (GTK_WIDGET (state->dialog));
}

/* Handler for destroy */
static void
cb_c_fmt_dialog_dialog_destroy (CFormatState *state)
{
	if (state->style)
		gnm_style_unref (state->style);
	g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static void
cb_dialog_destroy (GtkDialog *dialog)
{
	g_object_set_data (G_OBJECT (dialog), "state", NULL);
}


void
dialog_cell_format_cond (WBCGtk *wbcg)
{
	GtkBuilder     *gui;
	CFormatState  *state;
	GtkWidget *dialog;

	g_return_if_fail (wbcg != NULL);

	gui = gnm_gtk_builder_new ("cell-format-cond.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	/* Initialize */
	state = g_new (CFormatState, 1);
	state->wbcg	= wbcg;
	state->gui	= gui;
	state->sv	= wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->sheet	= sv_sheet (state->sv);
	state->style		= NULL;

	dialog = go_gtk_builder_get_widget (state->gui, "CellFormat");
	g_return_if_fail (dialog != NULL);

	gtk_window_set_title (GTK_WINDOW (dialog), _("Conditional Cell Formatting"));

	/* Initialize */
	state->dialog	   = GTK_DIALOG (dialog);

	c_fmt_dialog_init_conditions_page (state);

	gnumeric_init_help_button (
		go_gtk_builder_get_widget (state->gui, "helpbutton"),
		GNUMERIC_HELP_LINK_CELL_FORMAT_COND);

	state->close_button = go_gtk_builder_get_widget (state->gui, "closebutton");
	g_signal_connect (G_OBJECT (state->close_button),
		"clicked",
		G_CALLBACK (cb_c_fmt_dialog_dialog_buttons), state);

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (dialog), state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	/* a candidate for merging into attach guru */
	wbc_gtk_attach_guru (state->wbcg, GTK_WIDGET (state->dialog));
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_c_fmt_dialog_dialog_destroy);
	g_signal_connect (G_OBJECT (dialog), "destroy",
			  G_CALLBACK (cb_dialog_destroy), NULL);

	gnumeric_restore_window_geometry (GTK_WINDOW (state->dialog),
					  CELL_FORMAT_KEY);

	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));

	gtk_widget_show (GTK_WIDGET (state->dialog));
}

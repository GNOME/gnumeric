/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-goal-seek.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
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

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <cell.h>
#include <sheet.h>
#include <expr.h>
#include <dependent.h>
#include <format.h>
#include <value.h>
#include <mstyle.h>
#include <ranges.h>
#include <number-match.h>
#include <parse-util.h>
#include <workbook.h>
#include <workbook-control.h>
#include <workbook-edit.h>
#include <workbook-view.h>
#include <goal-seek.h>
#include <mathfunc.h>
#include <widgets/gnumeric-expr-entry.h>

#include <math.h>

#define MAX_CELL_NAME_LEN  20
#define GOALSEEK_KEY            "goal-seek-dialog"

typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
	GnumericExprEntry *set_cell_entry;
	GnumericExprEntry *change_cell_entry;
	GtkWidget *to_value_entry;
	GtkWidget *at_least_entry;
	GtkWidget *at_most_entry;
	GtkWidget *close_button;
	GtkWidget *cancel_button;
	GtkWidget *apply_button;
	GtkWidget *target_value_label;
	GtkWidget *current_value_label;
	GtkWidget *solution_label;
	GtkWidget *result_label;
	GtkWidget *result_frame;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	gnm_float target_value;
	gnm_float xmin;
	gnm_float xmax;
	Cell *set_cell;
	Cell *change_cell;
	Cell *old_cell;
	Value *old_value;
	GtkWidget *warning_dialog;
} GoalSeekState;


typedef struct {
	Cell *xcell, *ycell;
	gnm_float ytarget;
} GoalEvalData;

static GoalSeekStatus
goal_seek_eval (gnm_float x, gnm_float *y, void *vevaldata)
{
	GoalEvalData *evaldata = vevaldata;

	cell_set_value (evaldata->xcell, value_new_float (x));
	cell_queue_recalc (evaldata->xcell);
	workbook_recalc (evaldata->xcell->base.sheet->workbook);

	if (evaldata->ycell->value) {
	        *y = value_get_as_float (evaldata->ycell->value) - evaldata->ytarget;
		if (finitegnum (*y))
			return GOAL_SEEK_OK;
	}

	return GOAL_SEEK_ERROR;
}


static GoalSeekStatus
gnumeric_goal_seek (GoalSeekState *state)
{
	GoalSeekData seekdata;
	GoalEvalData evaldata;
	GoalSeekStatus status;
	gboolean hadold;
	gnm_float oldx;

	goal_seek_initialise (&seekdata);
	seekdata.xmin = state->xmin;
	seekdata.xmax = state->xmax;

	evaldata.xcell = state->change_cell;
	evaldata.ycell = state->set_cell;
	evaldata.ytarget = state->target_value;

	hadold = !VALUE_IS_EMPTY_OR_ERROR (state->change_cell->value);
	oldx = hadold ? value_get_as_float (state->change_cell->value) : 0;

	/* PLAN A: Newton's iterative method from initial or midpoint.  */
	{
		gnm_float x0;

		if (hadold)
			x0 = oldx;
		else
			x0 = (seekdata.xmin + seekdata.xmax) / 2;

		status = goal_seek_newton (goal_seek_eval, NULL,
					   &seekdata, &evaldata,
					   x0);
		if (status == GOAL_SEEK_OK)
			goto DONE;
	}

	/* PLAN B: Trawl uniformly.  */
	if (!seekdata.havexpos || !seekdata.havexneg) {
		status = goal_seek_trawl_uniformly (goal_seek_eval,
						    &seekdata, &evaldata,
						    seekdata.xmin, seekdata.xmax,
						    100);
		if (status == GOAL_SEEK_OK)
			goto DONE;
	}

	/* PLAN C: Trawl normally from middle.  */
	if (!seekdata.havexpos || !seekdata.havexneg) {
		gnm_float sigma, mu;
		int i;

		sigma = seekdata.xmax - seekdata.xmin;
		mu = (seekdata.xmax + seekdata.xmin) / 2;

		for (i = 0; i < 5; i++) {
			sigma /= 10;
			status = goal_seek_trawl_normally (goal_seek_eval,
							   &seekdata, &evaldata,
							   mu, sigma, 30);
			if (status == GOAL_SEEK_OK)
				goto DONE;
		}
	}

	/* PLAN D: Trawl normally from left.  */
	if (!seekdata.havexpos || !seekdata.havexneg) {
		gnm_float sigma, mu;
		int i;

		sigma = seekdata.xmax - seekdata.xmin;
		mu = seekdata.xmin;

		for (i = 0; i < 5; i++) {
			sigma /= 10;
			status = goal_seek_trawl_normally (goal_seek_eval,
							   &seekdata, &evaldata,
							   mu, sigma, 20);
			if (status == GOAL_SEEK_OK)
				goto DONE;
		}
	}

	/* PLAN E: Trawl normally from right.  */
	if (!seekdata.havexpos || !seekdata.havexneg) {
		gnm_float sigma, mu;
		int i;

		sigma = seekdata.xmax - seekdata.xmin;
		mu = seekdata.xmax;

		for (i = 0; i < 5; i++) {
			sigma /= 10;
			status = goal_seek_trawl_normally (goal_seek_eval,
							   &seekdata, &evaldata,
							   mu, sigma, 20);
			if (status == GOAL_SEEK_OK)
				goto DONE;
		}
	}

	/* PLAN F: Newton iteration with uniform net of starting points.  */
	if (!seekdata.havexpos || !seekdata.havexneg) {
		int i;
		const int N = 10;

		for (i = 1; i <= N; i++) {
			gnm_float x0 =	seekdata.xmin +
				(seekdata.xmax - seekdata.xmin) / (N + 1) * i;

			status = goal_seek_newton (goal_seek_eval, NULL,
						   &seekdata, &evaldata,
						   x0);
			if (status == GOAL_SEEK_OK)
				goto DONE;
		}
	}

	/* PLAN Z: Bisection.  */
	{
		status = goal_seek_bisection (goal_seek_eval,
					      &seekdata, &evaldata);
		if (status == GOAL_SEEK_OK)
			goto DONE;
	}

 DONE:
	if (status == GOAL_SEEK_OK) {
		gnm_float yroot;
		(void) goal_seek_eval (seekdata.root, &yroot, &evaldata);
	} else if (hadold) {
		gnm_float ydummy;
		(void) goal_seek_eval (oldx, &ydummy, &evaldata);
	}

	sheet_cell_calc_span (state->change_cell, SPANCALC_RENDER);
	sheet_flag_status_update_cell (state->change_cell);

	return status;
}

/**
 * dialog_destroy:
 * @window:
 * @focus_widget:
 * @state:
 *
 * Destroy the dialog and associated data structures.
 *
 **/
static gboolean
dialog_destroy (GtkObject *w, GoalSeekState  *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->old_value != NULL) {
		value_release (state->old_value);
		state->old_value = NULL;
	}

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	wbcg_edit_finish (state->wbcg, FALSE);

	state->dialog = NULL;

	g_free (state);

	return FALSE;
}

/**
 * cb_dialog_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			  GoalSeekState *state)
{
	if ((state->old_cell != NULL) && (state->old_value != NULL)) {
		sheet_cell_set_value (state->old_cell, state->old_value);
		workbook_recalc (state->old_cell->base.sheet->workbook);
		state->old_value = NULL;
	}
	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * cb_dialog_close_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_close_clicked (G_GNUC_UNUSED GtkWidget *button,
			 GoalSeekState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * cb_dialog_apply_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_apply_clicked (G_GNUC_UNUSED GtkWidget *button,
			 GoalSeekState *state)
{
	char *status_str;
	GoalSeekStatus status;
  	gnm_float max_range_val = 1e24;
	Value *target;
	RangeRef const *r;
	StyleFormat *format;

	if (state->warning_dialog != NULL)
		gtk_widget_destroy (state->warning_dialog);

	/* set up source */
	target = gnm_expr_entry_parse_as_value (state->set_cell_entry, 
						state->sheet);
	if (target == NULL) {
		gnumeric_notice_nonmodal (GTK_WINDOW(state->dialog),
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR,
					  _("You should introduce a valid cell "
					    "name in 'Set Cell:'!"));
		gnm_expr_entry_grab_focus (state->set_cell_entry, TRUE);
		return;
	}
	r = &target->v_range.cell;
	state->set_cell = sheet_cell_get (r->a.sheet, r->a.col, r->a.row);
	value_release (target);
	if (state->set_cell == NULL || !cell_has_expr (state->set_cell)) {
		gnumeric_notice_nonmodal (GTK_WINDOW(state->dialog),
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR,
					  _("The cell named in 'Set Cell:' "
					    "must contain a formula!"));
		gnm_expr_entry_grab_focus (state->set_cell_entry, TRUE);
		return;
	}

	/* set up source */
	target = gnm_expr_entry_parse_as_value (state->change_cell_entry, 
						state->sheet);
	if (target == NULL) {
		gnumeric_notice_nonmodal (GTK_WINDOW(state->dialog),
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR,
					  _("You should introduce a valid cell "
					    "name in 'By Changing Cell:'!"));
		gnm_expr_entry_grab_focus (state->change_cell_entry, TRUE);
		return;
	}

	r = &target->v_range.cell;
	state->change_cell = sheet_cell_fetch (r->a.sheet, r->a.col, r->a.row);
	value_release (target);
	if (cell_has_expr (state->change_cell)) {
		gnumeric_notice_nonmodal (GTK_WINDOW(state->dialog),
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR,
					  _("The cell named in 'By changing cell' "
					    "must not contain a formula."));
		gnm_expr_entry_grab_focus (state->change_cell_entry, TRUE);
		return;
	}


	format = mstyle_get_format (cell_get_mstyle (state->set_cell));
	if (entry_to_float_with_format (GTK_ENTRY(state->to_value_entry), 
					&state->target_value, TRUE, format)){
		gnumeric_notice_nonmodal (GTK_WINDOW(state->dialog),
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR,
					  _("The value given in 'To Value:' "
					    "is not valid."));
		focus_on_entry (GTK_ENTRY(state->to_value_entry));
		return;
	}

	format = mstyle_get_format (cell_get_mstyle (state->change_cell));
	if (entry_to_float_with_format (GTK_ENTRY(state->at_least_entry), 
					 &state->xmin, TRUE, format)) {
		state->xmin = -max_range_val;
		gtk_entry_set_text (GTK_ENTRY (state->at_least_entry), "");
	}

	if (entry_to_float_with_format (GTK_ENTRY(state->at_most_entry), &state->xmax, 
					TRUE, format)) {
  		state->xmax = +max_range_val;
		gtk_entry_set_text (GTK_ENTRY (state->at_most_entry), "");
	}

	if ((state->old_cell != NULL) && (state->old_value != NULL)) {
		sheet_cell_set_value (state->old_cell, state->old_value);
		workbook_recalc (state->old_cell->base.sheet->workbook);
		state->old_value = NULL;
	}
	state->old_cell = state->change_cell;
	state->old_value = state->change_cell->value ?
		value_duplicate (state->change_cell->value) : NULL;

	status = gnumeric_goal_seek (state);

	switch (status) {
	case GOAL_SEEK_OK: {
		const char *actual_str;
		const char *solution_str;
		StyleFormat *format = style_format_new_XL ("General", FALSE);
		Value *error_value = value_new_float (state->target_value -
						      value_get_as_float (state->set_cell->value));
  		char *target_str = format_value (format, error_value, NULL, 0,
						 workbook_date_conv (state->wb));
		gtk_label_set_text (GTK_LABEL (state->target_value_label), target_str);
		g_free (target_str);
		value_release (error_value);
		style_format_unref (format);

		status_str =
			g_strdup_printf (_("Goal seeking with cell %s found a solution."),
					 cell_name (state->set_cell));
		gtk_label_set_text (GTK_LABEL (state->result_label), status_str);
		g_free (status_str);

		/* FIXME?  Do a format?  */
		actual_str = state->set_cell->value
			? value_peek_string (state->set_cell->value)
			: "";
		gtk_label_set_text (GTK_LABEL (state->current_value_label), actual_str);

		solution_str = state->change_cell->value
			? value_peek_string (state->change_cell->value)
			: "";
		gtk_label_set_text (GTK_LABEL (state->solution_label), solution_str);

		break;
	}

	default:
		status_str =
			g_strdup_printf (_("Goal seeking with cell %s did not find a solution."),
					 cell_name (state->set_cell));
		gtk_label_set_text (GTK_LABEL (state->result_label), status_str);
		g_free (status_str);
		gtk_label_set_text (GTK_LABEL (state->current_value_label), "");
		gtk_label_set_text (GTK_LABEL (state->solution_label), "");
		gtk_label_set_text (GTK_LABEL (state->target_value_label), "");
		break;
	}
	gtk_widget_show (state->result_frame);
	return;
}

/**
 * dialog_realized:
 * @widget
 * @state:
 *
 *
 *
 **/
static void
dialog_realized (G_GNUC_UNUSED GtkWidget *dialog,
		 GoalSeekState *state)
{
	gtk_widget_hide (state->result_frame);
}

/**
 * dialog_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_init (GoalSeekState *state)
{
	GtkTable *table;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "goalseek.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "GoalSeek");
        if (state->dialog == NULL)
                return TRUE;

	state->close_button     = glade_xml_get_widget (state->gui, "closebutton");
	g_signal_connect (G_OBJECT (state->close_button),
		"clicked",
		G_CALLBACK (cb_dialog_close_clicked), state);

	state->cancel_button  = glade_xml_get_widget (state->gui, "cancelbutton");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_dialog_cancel_clicked), state);
	state->apply_button     = glade_xml_get_widget (state->gui, "applybutton");
	g_signal_connect (G_OBJECT (state->apply_button),
		"clicked",
		G_CALLBACK (cb_dialog_apply_clicked), state);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "helpbutton"),
		"goal-seek.html");

	state->to_value_entry = glade_xml_get_widget (state->gui, "to_value_entry");
	state->at_least_entry = glade_xml_get_widget (state->gui, "at_least-entry");
	state->at_most_entry = glade_xml_get_widget (state->gui, "at_most-entry");
	state->target_value_label = glade_xml_get_widget (state->gui, "target-value");
	gtk_label_set_justify (GTK_LABEL (state->target_value_label), GTK_JUSTIFY_RIGHT);
	state->current_value_label = glade_xml_get_widget (state->gui, "current-value");
	gtk_label_set_justify (GTK_LABEL (state->current_value_label), GTK_JUSTIFY_RIGHT);
	state->solution_label = glade_xml_get_widget (state->gui, "solution");
	gtk_label_set_justify (GTK_LABEL (state->solution_label), GTK_JUSTIFY_RIGHT);

	state->result_label = glade_xml_get_widget (state->gui, "result-label");
	state->result_frame = glade_xml_get_widget (state->gui, "result-frame");

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "goal-table"));
	state->set_cell_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->set_cell_entry,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL | 
				  GNM_EE_ABS_ROW | GNM_EE_ABS_COL,
		GNM_EE_MASK);
        gnm_expr_entry_set_scg (state->set_cell_entry, wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->set_cell_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->set_cell_entry));
	gtk_widget_show (GTK_WIDGET (state->set_cell_entry));

	state->change_cell_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->change_cell_entry,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL | 
				  GNM_EE_ABS_ROW | GNM_EE_ABS_COL,
		GNM_EE_MASK);
	gnm_expr_entry_set_scg (state->change_cell_entry, wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->change_cell_entry),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->change_cell_entry));
	gtk_widget_show (GTK_WIDGET (state->change_cell_entry));


	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	g_signal_connect (G_OBJECT (state->dialog),
		"realize",
		G_CALLBACK (dialog_realized), state);
	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (dialog_destroy), state);

	state->old_value = NULL;
	state->old_cell = NULL;

	gnm_expr_entry_grab_focus (state->set_cell_entry, FALSE);

	return FALSE;
}

/**
 * dialog_goal_seek:
 * @wbcg:
 * @sheet:
 *
 * Create the dialog (guru).
 *
 **/
void
dialog_goal_seek (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        GoalSeekState *state;

	if (wbcg == NULL) {
		return;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, GOALSEEK_KEY))
		return;

	state = g_new (GoalSeekState, 1);
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->warning_dialog = NULL;

	if (dialog_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Goal-Seek dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       GOALSEEK_KEY);

	gtk_widget_show (state->dialog);

        return;
}

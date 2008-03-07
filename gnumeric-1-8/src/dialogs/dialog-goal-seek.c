/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-goal-seek.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   Morten Welinder (terra@gnome.org)
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <cell.h>
#include <sheet.h>
#include <expr.h>
#include <commands.h>
#include <dependent.h>
#include <gnm-format.h>
#include <value.h>
#include <mstyle.h>
#include <ranges.h>
#include <number-match.h>
#include <parse-util.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <goal-seek.h>
#include <mathfunc.h>
#include <widgets/gnumeric-expr-entry.h>
#include <selection.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktable.h>

#include <math.h>
#include <string.h>

static const gnm_float max_range_val = GNM_const(1e24);

#define MAX_CELL_NAME_LEN  20
#define GOALSEEK_KEY            "goal-seek-dialog"

typedef struct {
	GladeXML  *gui;
	GtkWidget *dialog;
	GnmExprEntry *set_cell_entry;
	GnmExprEntry *change_cell_entry;
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
	GtkWidget *result_table;
	Sheet	  *sheet;
	Workbook  *wb;
	WBCGtk  *wbcg;
	gnm_float target_value;
	gnm_float xmin;
	gnm_float xmax;
	GnmCell *set_cell;
	GnmCell *change_cell;
	GnmCell *old_cell;
	GnmValue *old_value;
	GtkWidget *warning_dialog;
	gboolean cancelled;
} GoalSeekState;


typedef struct {
	GoalSeekState	*state;
	GnmCell *xcell, *ycell;
	gnm_float	ytarget;
	gboolean	update_ui;
} GoalEvalData;

static GoalSeekStatus
goal_seek_eval (gnm_float x, gnm_float *y, void *vevaldata)
{
	GoalEvalData const *evaldata = vevaldata;
	GnmValue *v = value_new_float (x);

	if (evaldata->update_ui) {
		sheet_cell_set_value (evaldata->xcell, v);
	} else {
		gnm_cell_set_value (evaldata->xcell, v);
		cell_queue_recalc (evaldata->xcell);
	}
	workbook_recalc (evaldata->state->wb);

	if (evaldata->ycell->value) {
		*y = value_get_as_float (evaldata->ycell->value) - evaldata->ytarget;
		if (gnm_finite (*y))
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

	goal_seek_initialize (&seekdata);
	seekdata.xmin = state->xmin;
	seekdata.xmax = state->xmax;

	evaldata.xcell = state->change_cell;
	evaldata.ycell = state->set_cell;
	evaldata.ytarget = state->target_value;
	evaldata.update_ui = FALSE;
	evaldata.state = state;

	hadold = !VALUE_IS_EMPTY_OR_ERROR (state->change_cell->value);
	oldx = hadold ? value_get_as_float (state->change_cell->value) : 0;

	/* PLAN A: Newton's iterative method from initial or midpoint.  */
	{
		gnm_float x0;

		if (hadold && oldx >= seekdata.xmin && oldx <= seekdata.xmax)
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

		sigma = MIN (seekdata.xmax - seekdata.xmin, 1e6);
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

		sigma = MIN (seekdata.xmax - seekdata.xmin, 1e6);
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

		sigma = MIN (seekdata.xmax - seekdata.xmin, 1e6);
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
	evaldata.update_ui = TRUE;
	if (status == GOAL_SEEK_OK) {
		gnm_float yroot;
		(void) goal_seek_eval (seekdata.root, &yroot, &evaldata);
	} else if (hadold) {
		gnm_float ydummy;
		(void) goal_seek_eval (oldx, &ydummy, &evaldata);
	}

	return status;
}

static void
cb_dialog_destroy (GoalSeekState *state)
{
	if (!state->cancelled 
	    && state->old_value != NULL 
	    && state->old_cell != NULL) {
		cmd_goal_seek (WORKBOOK_CONTROL(state->wbcg),
			       state->old_cell, state->old_value, NULL);
		state->old_value = NULL;
	}

	if (state->old_value != NULL)
		value_release (state->old_value);
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));

	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);
	g_free (state);
}

static void
cb_dialog_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			  GoalSeekState *state)
{
	state->cancelled = TRUE;

	if ((state->old_cell != NULL) && (state->old_value != NULL)) {
		sheet_cell_set_value (state->old_cell, state->old_value);
		workbook_recalc (state->wb);
		state->old_value = NULL;
	}
	gtk_widget_destroy (state->dialog);
}

static void
cb_dialog_close_clicked (G_GNUC_UNUSED GtkWidget *button,
			 GoalSeekState *state)
{
	gtk_widget_destroy (state->dialog);
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
	GnmValue *target;
	GnmRangeRef const *r;
	GOFormat *format;

	if (state->warning_dialog != NULL)
		gtk_widget_destroy (state->warning_dialog);

	/* set up source */
	target = gnm_expr_entry_parse_as_value (state->set_cell_entry,
						state->sheet);
	if (target == NULL) {
		go_gtk_notice_nonmodal_dialog (GTK_WINDOW(state->dialog),
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
	if (state->set_cell == NULL || !gnm_cell_has_expr (state->set_cell)) {
		go_gtk_notice_nonmodal_dialog (GTK_WINDOW(state->dialog),
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
		go_gtk_notice_nonmodal_dialog (GTK_WINDOW(state->dialog),
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
	if (gnm_cell_has_expr (state->change_cell)) {
		go_gtk_notice_nonmodal_dialog (GTK_WINDOW(state->dialog),
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR,
					  _("The cell named in 'By changing cell' "
					    "must not contain a formula."));
		gnm_expr_entry_grab_focus (state->change_cell_entry, TRUE);
		return;
	}


	format = gnm_style_get_format (gnm_cell_get_style (state->set_cell));
	if (entry_to_float_with_format (GTK_ENTRY(state->to_value_entry),
					&state->target_value, TRUE, format)){
		go_gtk_notice_nonmodal_dialog (GTK_WINDOW(state->dialog),
					  &(state->warning_dialog),
					  GTK_MESSAGE_ERROR,
					  _("The value given in 'To Value:' "
					    "is not valid."));
		focus_on_entry (GTK_ENTRY(state->to_value_entry));
		return;
	}

	format = gnm_style_get_format (gnm_cell_get_style (state->change_cell));
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
		workbook_recalc (state->wb);
		state->old_value = NULL;
	}
	state->old_cell = state->change_cell;
	state->old_value = state->change_cell->value ?
		value_dup (state->change_cell->value) : NULL;

	status = gnumeric_goal_seek (state);

	switch (status) {
	case GOAL_SEEK_OK: {
		const char *actual_str;
		const char *solution_str;
		GOFormat *format = go_format_general ();
		GnmValue *error_value = value_new_float (state->target_value -
						      value_get_as_float (state->set_cell->value));
		char *target_str = format_value (format, error_value, NULL, -1,
						 workbook_date_conv (state->wb));
		gtk_label_set_text (GTK_LABEL (state->target_value_label), target_str);
		g_free (target_str);
		value_release (error_value);

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
	state->cancelled = FALSE;
	
	gtk_widget_show (state->result_table);
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
	gtk_widget_hide (state->result_table);
}

/**
 * dialog_preload_selection:
 * @state:
 * @entry
 *
 *
 **/
static void
dialog_preload_selection (GoalSeekState *state, GnmExprEntry *entry)
{
	GnmRange const *sel;

	sel = selection_first_range 
		(wb_control_cur_sheet_view 
		 (WORKBOOK_CONTROL (state->wbcg)), NULL, NULL);
	if (sel)
		gnm_expr_entry_load_from_range (entry,
						state->sheet, sel);
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
		GNUMERIC_HELP_LINK_GOAL_SEEK);

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
	state->result_table = glade_xml_get_widget (state->gui, "result-table");

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "goal-table"));
	state->set_cell_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->set_cell_entry,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL | GNM_EE_FORCE_ABS_REF,
		GNM_EE_MASK);
	gtk_table_attach (table, GTK_WIDGET (state->set_cell_entry),
			  2, 3, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->set_cell_entry));
	dialog_preload_selection (state, state->set_cell_entry);
	gtk_widget_show (GTK_WIDGET (state->set_cell_entry));

	state->change_cell_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gnm_expr_entry_set_flags (state->change_cell_entry,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL | GNM_EE_FORCE_ABS_REF,
		GNM_EE_MASK);
	gtk_table_attach (table, GTK_WIDGET (state->change_cell_entry),
			  2, 3, 3, 4,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->change_cell_entry));
	dialog_preload_selection (state, state->change_cell_entry);
	gtk_widget_show (GTK_WIDGET (state->change_cell_entry));


	g_signal_connect (G_OBJECT (state->dialog),
		"realize",
		G_CALLBACK (dialog_realized), state);

	state->old_value = NULL;
	state->old_cell = NULL;

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) cb_dialog_destroy);

	gnm_expr_entry_grab_focus (state->set_cell_entry, FALSE);

	return FALSE;
}

/*
 * We need a horizontal strip of 5 cells containing:
 *
 * 0. Formula cell.
 * 1: X value cell.
 * 2: Y target value.
 * 3: Min value.
 * 4: Max value.
 */
static void
dialog_goal_seek_test (Sheet *sheet, const GnmRange *range)
{
	GoalSeekState state;
	GnmCell *cell;
	int r, c;
	GoalSeekStatus status;

	g_return_if_fail (range->start.row == range->end.row);
	g_return_if_fail (range->start.col + 4 == range->end.col);

	memset (&state, 0, sizeof (state));
	r = range->start.row;
	c = range->start.col;

	state.wb = sheet->workbook;
	state.sheet = sheet;

	state.set_cell = sheet_cell_fetch (sheet, c + 0, r);
	state.change_cell = sheet_cell_fetch (sheet, c + 1, r);
	state.old_value = state.change_cell->value
		? value_dup (state.change_cell->value)
		: NULL;

	cell = sheet_cell_fetch (sheet, c + 2, r);
	state.target_value = value_get_as_float (cell->value);

	cell = sheet_cell_fetch (sheet, c + 3, r);
	state.xmin = VALUE_IS_EMPTY (cell->value)
		? -max_range_val
		: value_get_as_float (cell->value);

	cell = sheet_cell_fetch (sheet, c + 4, r);
	state.xmax = VALUE_IS_EMPTY (cell->value)
		? max_range_val
		: value_get_as_float (cell->value);

	status = gnumeric_goal_seek (&state);
	if (status == GOAL_SEEK_OK) {
		/* Nothing */
	} else {
		sheet_cell_set_value (state.change_cell,
				      value_new_error_VALUE (NULL));
	}

	if (state.old_value)
		value_release (state.old_value);
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
dialog_goal_seek (WBCGtk *wbcg, Sheet *sheet)
{
        GoalSeekState *state;
	GladeXML *gui;

	g_return_if_fail (IS_SHEET (sheet));

	/* Testing hook.  */
	if (wbcg == NULL) {
		GnmRangeRef *range =
			g_object_get_data (G_OBJECT (sheet), "ssconvert-goal-seek");
		if (range) {
			Sheet *start_sheet, *end_sheet;
			GnmEvalPos ep;
			GnmRange r;

			gnm_rangeref_normalize (range,
						eval_pos_init_sheet (&ep, sheet),
						&start_sheet, &end_sheet,
						&r);
			g_return_if_fail (start_sheet == sheet);

			dialog_goal_seek_test (sheet, &r);
			return;
		}
	}

	g_return_if_fail (wbcg != NULL);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, GOALSEEK_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"goalseek.glade", NULL, NULL);
        if (gui == NULL)
                return;

	state = g_new (GoalSeekState, 1);
	state->wbcg  = wbcg;
	state->wb    = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->gui   = gui;
	state->warning_dialog = NULL;
	state->cancelled = TRUE;

	if (dialog_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the Goal-Seek dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       GOALSEEK_KEY);

	gtk_widget_show (state->dialog);
}

/*
 * dialog-goal-seek.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   Morten Welinder (terra@diku.dk)
 */

#include <config.h>
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-help.h>
#include <math.h>
#include "gnumeric.h"
#include "gui-util.h"
#include "dialogs.h"
#include "cell.h"
#include "sheet.h"
#include "expr.h"
#include "eval.h"
#include "format.h"
#include "value.h"
#include "mstyle.h"
#include "number-match.h"
#include "parse-util.h"
#include "workbook.h"
#include "workbook-control.h"
#include "workbook-edit.h"
#include "workbook-view.h"
#include "utils-dialog.h"
#include "goal-seek.h"
#include "mathfunc.h"
#include "widgets/gnumeric-expr-entry.h"
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

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
	GtkWidget *help_button;
	GtkWidget *target_value_label;
	GtkWidget *current_value_label;
	GtkWidget *solution_label;
	GtkWidget *result_label;
	GtkWidget *result_frame;
	char *helpfile;
	Sheet	  *sheet;
	Workbook  *wb;
	WorkbookControlGUI  *wbcg;
	int set_cell_col, set_cell_row;
	int change_cell_col, change_cell_row;
	gnum_float target_value;
	gnum_float xmin;
	gnum_float xmax;
	Cell *set_cell;
	Cell *change_cell;
	Cell *old_cell;
	Value *old_value;
} GoalSeekState;


static void
focus_on_entry (GtkWidget *entry)
{
	gtk_widget_grab_focus (entry);
	gtk_entry_set_position (GTK_ENTRY (entry), 0);
	gtk_entry_select_region (GTK_ENTRY (entry), 0,
				 GTK_ENTRY (entry)->text_length);
}


typedef struct {
	Cell *xcell, *ycell;
	gnum_float ytarget;
} GoalEvalData;


static GoalSeekStatus
goal_seek_eval (gnum_float x, gnum_float *y, void *vevaldata)
{
	GoalEvalData *evaldata = vevaldata;

	cell_set_value (evaldata->xcell, value_new_float (x), NULL);
	cell_queue_recalc (evaldata->xcell);
	workbook_recalc (evaldata->xcell->base.sheet->workbook);

	if (evaldata->ycell->value) {
	        *y = value_get_as_float (evaldata->ycell->value) - evaldata->ytarget;
		if (FINITE (*y))
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
	gnum_float oldx;

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
		gnum_float x0;

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
		gnum_float sigma, mu;
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
		gnum_float sigma, mu;
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
		gnum_float sigma, mu;
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
			gnum_float x0 =	seekdata.xmin +
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
		gnum_float yroot;
		(void) goal_seek_eval (seekdata.root, &yroot, &evaldata);
	} else if (hadold) {
		gnum_float ydummy;
		(void) goal_seek_eval (oldx, &ydummy, &evaldata);
	}

	sheet_cell_calc_span (state->change_cell, SPANCALC_RENDER);
	sheet_flag_status_update_cell (state->change_cell);

	return status;
}


/**
 * dialog_help_cb:
 * @button:
 * @state:
 *
 * Provide help.
 **/
static void
dialog_help_cb (GtkWidget *button, GoalSeekState *state)
{
	if (state->helpfile != NULL) {
		GnomeHelpMenuEntry help_ref;
		help_ref.name = "gnumeric";
		help_ref.path = state->helpfile;
		gnome_help_display (NULL, &help_ref);		
	}
	return;
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
		gtk_object_unref (GTK_OBJECT (state->gui));
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
cb_dialog_cancel_clicked (GtkWidget *button, GoalSeekState *state)
{
	if ((state->old_cell != NULL) && (state->old_value != NULL)) {
		sheet_cell_set_value (state->old_cell, state->old_value, NULL);
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
cb_dialog_close_clicked (GtkWidget *button, GoalSeekState *state)
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
cb_dialog_apply_clicked (GtkWidget *button, GoalSeekState *state)
{
	char *text;
	char *status_str;
	char *target_str;
	char *actual_str;
	char *solution_str;
	GoalSeekStatus	status;
	Value *value;
	StyleFormat *format;
	StyleFormat *target_value_format;
	StyleFormat *min_value_format;
	StyleFormat *max_value_format;
  	gnum_float  max_range_val = 1e24;    
	Value *error_value;


	text = gtk_entry_get_text (GTK_ENTRY (state->set_cell_entry));
	if (!parse_cell_name (text, &state->set_cell_col, &state->set_cell_row, TRUE, NULL)){
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell name in 'Set Cell:'!"));
		focus_on_entry (GTK_WIDGET (state->set_cell_entry));
		return;
	}

	state->set_cell = sheet_cell_get (state->sheet, state->set_cell_col, state->set_cell_row);
	if (state->set_cell == NULL || !cell_has_expr (state->set_cell)) {
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("The cell named in 'Set Cell:' must contain a formula!"));
		focus_on_entry (GTK_WIDGET (state->set_cell_entry));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (state->change_cell_entry));
	if (!parse_cell_name (text, &state->change_cell_col, &state->change_cell_row, TRUE, NULL)){
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell "
				   "name in 'By Changing Cell:'!"));
		focus_on_entry (GTK_WIDGET (state->change_cell_entry));
		return;
	}

	state->change_cell = sheet_cell_fetch (state->sheet, state->change_cell_col, state->change_cell_row);
	if (cell_has_expr (state->change_cell)) {
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("The cell named in 'By changing cell' "
				   "must not contain a formula."));
		focus_on_entry (GTK_WIDGET (state->change_cell_entry));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (state->to_value_entry));
	format = mstyle_get_format (cell_get_mstyle (state->set_cell));
	value = format_match_number (text, format, &target_value_format);
	if (format != NULL) 
		target_value_format = format;
	if (value == NULL){
		gnumeric_notice (state->wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("The value given in 'To Value:' "
				   "is not valid."));
		focus_on_entry (GTK_WIDGET (state->to_value_entry));		
		return;
	}
	state->target_value = value_get_as_float (value);
	target_str = format_value (target_value_format, value, NULL, 0);
	gtk_entry_set_text (GTK_ENTRY (state->to_value_entry), target_str);
	g_free (target_str);
	value_release (value);
	
	format = mstyle_get_format (cell_get_mstyle (state->change_cell));
	text = g_strdup (gtk_entry_get_text (GTK_ENTRY (state->at_least_entry)));
	g_strstrip (text);
	if (strlen (text) > 0) {
		value = format_match_number (text, format, &min_value_format);
		if (format != NULL) 
			min_value_format = format;
	} else {
		value = NULL;
	}
	g_free (text);
	if (value != NULL) {
		state->xmin = value_get_as_float (value);
		text = format_value (min_value_format, value, NULL, 0);
		gtk_entry_set_text (GTK_ENTRY (state->at_least_entry), text);
		g_free (text);	
		value_release (value);
	} else {
		state->xmin = - max_range_val;
		gtk_entry_set_text (GTK_ENTRY (state->at_least_entry), "");
	}

	text = g_strdup (gtk_entry_get_text (GTK_ENTRY (state->at_most_entry)));
	g_strstrip (text);
	if (strlen (text) > 0) {
		value = format_match_number (text, format, &max_value_format);
		if (format != NULL) 
			max_value_format = format;
	} else {
		value = NULL;
	}
	g_free (text);
	if (value != NULL) {
		state->xmax = value_get_as_float (value);
		text = format_value (max_value_format, value, NULL, 0);
		gtk_entry_set_text (GTK_ENTRY (state->at_most_entry), text);
		g_free (text);	
		value_release (value);
	} else {
  		state->xmax = max_range_val;
		gtk_entry_set_text (GTK_ENTRY (state->at_most_entry), "");
	}

	if ((state->old_cell != NULL) && (state->old_value != NULL)) {
		sheet_cell_set_value (state->old_cell, state->old_value, NULL);
		workbook_recalc (state->old_cell->base.sheet->workbook);
		state->old_value = NULL;
	}
	state->old_cell = state->change_cell;
	state->old_value = state->change_cell->value ? 
		value_duplicate (state->change_cell->value) : NULL;

	status = gnumeric_goal_seek (state);

	switch (status) {
	case GOAL_SEEK_OK:
		format = style_format_new_XL ("General", FALSE);
		error_value = value_new_float (state->target_value - 
					      value_get_as_float (state->set_cell->value));
  		target_str = format_value (format, error_value, NULL, 0);	 
		gtk_label_set_text (GTK_LABEL (state->target_value_label), target_str);
		g_free (target_str);
		value_release (error_value);
		style_format_unref (format);

		status_str =
			g_strdup_printf (_("Goal seeking with cell %s found a solution."),
					 cell_name (state->set_cell));
		gtk_label_set_text (GTK_LABEL (state->result_label), status_str);
		g_free (status_str);

		actual_str = cell_get_rendered_text (state->set_cell);
		gtk_label_set_text (GTK_LABEL (state->current_value_label), actual_str);
		g_free (actual_str);

		solution_str = cell_get_rendered_text (state->change_cell);
		gtk_label_set_text (GTK_LABEL (state->solution_label), solution_str);
		g_free (solution_str);

		break;
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
 * dialog_set_focus:
 * @window:
 * @focus_widget:
 * @state:
 *
 **/
static void
dialog_set_focus (GtkWidget *window, GtkWidget *focus_widget,
			GoalSeekState *state)
{
	if (IS_GNUMERIC_EXPR_ENTRY (focus_widget)) {
		wbcg_set_entry (state->wbcg,
				    GNUMERIC_EXPR_ENTRY (focus_widget));
		gnumeric_expr_entry_set_absolute (GNUMERIC_EXPR_ENTRY (focus_widget));
	} else
		wbcg_set_entry (state->wbcg, NULL);
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
dialog_realized (GtkWidget *dialog, GoalSeekState *state)
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
	gtk_signal_connect (GTK_OBJECT (state->close_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_close_clicked),
			    state);

	state->cancel_button  = glade_xml_get_widget (state->gui, "cancelbutton");
	gtk_signal_connect (GTK_OBJECT (state->cancel_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_cancel_clicked),
			    state);
	state->apply_button     = glade_xml_get_widget (state->gui, "applybutton");
	gtk_signal_connect (GTK_OBJECT (state->apply_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_dialog_apply_clicked), state);
	state->help_button     = glade_xml_get_widget (state->gui, "helpbutton");
	gtk_signal_connect (GTK_OBJECT (state->help_button), "clicked",
			    GTK_SIGNAL_FUNC (dialog_help_cb), state);
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
	state->set_cell_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->set_cell_entry,
                                      GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
                                      GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->set_cell_entry, wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->set_cell_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->set_cell_entry));
	gtk_widget_show (GTK_WIDGET (state->set_cell_entry));
	
	state->change_cell_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->change_cell_entry,
				       GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL, 
				       GNUM_EE_MASK);
	gnumeric_expr_entry_set_scg (state->change_cell_entry, 
				     wb_control_gui_cur_sheet (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->change_cell_entry),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->change_cell_entry));
	gtk_widget_show (GTK_WIDGET (state->change_cell_entry));				


	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (dialog_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "realize",
			    GTK_SIGNAL_FUNC (dialog_realized), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (dialog_destroy), state);

	state->old_value = NULL;
	state->old_cell = NULL;

	gtk_widget_grab_focus (GTK_WIDGET (state->set_cell_entry));

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
	state->helpfile = "goal-seek.html";

	if (dialog_init (state)) {
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("Could not create the Goal-Seek dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       GOALSEEK_KEY);

	gtk_widget_show (state->dialog);

        return;
}

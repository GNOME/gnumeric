/*
 * dialog-goal-seek.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <iivonen@iki.fi>
 *   Morten Welinder (terra@diku.dk)
 */


#include <config.h>
#include <gnome.h>
#include <math.h>
#include "gnumeric.h"
#include "gnumeric-util.h"
#include "dialogs.h"
#include "cell.h"
#include "expr.h"
#include "eval.h"
#include "value.h"
#include "parse-util.h"
#include "workbook.h"
#include "utils-dialog.h"
#include "goal-seek.h"
#include "mathfunc.h"
#ifdef HAVE_IEEEFP_H
#include <ieeefp.h>
#endif

#define MAX_CELL_NAME_LEN  20

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
	cell_content_changed (evaldata->xcell);
	workbook_recalc (evaldata->xcell->base.sheet->workbook);

	if (evaldata->ycell->value) {
	        *y = value_get_as_float (evaldata->ycell->value) - evaldata->ytarget;
		if (FINITE (*y))
			return GOAL_SEEK_OK;
	}

	return GOAL_SEEK_ERROR;
}


static GoalSeekStatus
gnumeric_goal_seek (WorkbookControlGUI *wbcg, Sheet *sheet,
		    Cell *set_cell, gnum_float target_value,
		    Cell *change_cell, gnum_float xmin, gnum_float xmax)
{
	GoalSeekData seekdata;
	GoalEvalData evaldata;
	GoalSeekStatus status;
	gboolean hadold;
	gnum_float oldx;

	goal_seek_initialise (&seekdata);
	seekdata.xmin = xmin;
	seekdata.xmax = xmax;

	evaldata.xcell = change_cell;
	evaldata.ycell = set_cell;
	evaldata.ytarget = target_value;

	hadold = (change_cell->value != NULL);
	oldx = hadold ? value_get_as_float (change_cell->value) : 0;

	/* PLAN A: Newton's iterative method.  */
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
	{
		status = goal_seek_trawl_uniformly (goal_seek_eval,
						    &seekdata, &evaldata,
						    seekdata.xmin, seekdata.xmax,
						    100);
		if (status == GOAL_SEEK_OK)
			goto DONE;
	}

	/* PLAN C: Trawl normally from middle.  */
	{
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
	{
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
	{
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

	sheet_cell_calc_span (change_cell, SPANCALC_RENDER);
	sheet_flag_status_update_cell (change_cell);
	sheet_redraw_cell (change_cell);

	return status;
}


static gboolean
dialog_found_solution (WorkbookControlGUI *wbcg,
		       Cell *set_cell, Cell *change_cell, gnum_float target_value)
{
        GtkWidget *dialog;
	GtkWidget *label_box;
	GtkWidget *status_label, *empty_label;
	GtkWidget *target_label, *actual_label, *solution_label;

	char *status_str, *target_str, *actual_str, *solution_str;
	int  selection;

	status_str =
		g_strdup_printf (_("Goal seeking with cell %s found a solution"),
				 cell_name (set_cell));
	target_str =
		g_strdup_printf (_("Target value:   %12.2f"),
				 (double)target_value);
	actual_str =
		g_strdup_printf (_("Current value:  %12.2f"),
				 (double)value_get_as_float (set_cell->value));
	solution_str =
		g_strdup_printf (_("Solution:       %12.2f"),
				 (double)value_get_as_float (change_cell->value));

	dialog = gnome_dialog_new (_("Goal Seek Report"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);

	gnome_dialog_set_default(GNOME_DIALOG(dialog), GNOME_OK);
	status_label = gtk_label_new (status_str);
	empty_label = gtk_label_new ("");
	target_label = gtk_label_new (target_str);
	actual_label = gtk_label_new (actual_str);
	solution_label = gtk_label_new (solution_str);

	gtk_misc_set_alignment (GTK_MISC (status_label), 0,0);
	gtk_misc_set_alignment (GTK_MISC (target_label), 0,0);
	gtk_misc_set_alignment (GTK_MISC (actual_label), 0,0);
	gtk_misc_set_alignment (GTK_MISC (solution_label), 0,0);

	label_box = gtk_vbox_new (FALSE, 0);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), status_label);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), empty_label);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), target_label);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), actual_label);
	gtk_box_pack_start_defaults (GTK_BOX (label_box), solution_label);

	gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
					      (dialog)->vbox), label_box);

	gtk_widget_show_all (GNOME_DIALOG (dialog)->vbox);
        selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));

	if (selection >= 0)
		gnome_dialog_close (GNOME_DIALOG (dialog));

	g_free (status_str);
	g_free (target_str);
	g_free (actual_str);
	g_free (solution_str);

	return (selection != 0);
}

void
dialog_goal_seek (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	static GtkWidget *dialog;
        static GtkWidget *set_entry;
	static GtkWidget *target_entry;
	static GtkWidget *change_entry;
	static GtkWidget *xmin_entry;
	static GtkWidget *xmax_entry;

	const char       *set_entry_str;
	int              selection;

	g_return_if_fail (sheet != NULL);
	g_return_if_fail (IS_SHEET (sheet));

	set_entry_str = cell_pos_name (&sheet->edit_pos);

	if (!dialog) {
		GtkWidget *set_label, *target_label, *change_label;
		GtkWidget *label_box, *entry_box, *box;
		GtkWidget *xmin_label, *xmax_label;

                dialog = gnome_dialog_new (_("Goal Seek..."),
                                           GNOME_STOCK_BUTTON_OK,
                                           GNOME_STOCK_BUTTON_CANCEL,
                                           NULL);
                gnome_dialog_close_hides (GNOME_DIALOG (dialog), TRUE);
		gnome_dialog_set_default (GNOME_DIALOG(dialog), GNOME_OK);

                set_entry = gnumeric_dialog_entry_new_with_max_length
			(GNOME_DIALOG (dialog), MAX_CELL_NAME_LEN);
                target_entry = gnumeric_dialog_entry_new_with_max_length
			(GNOME_DIALOG (dialog), MAX_CELL_NAME_LEN);
                change_entry = gnumeric_dialog_entry_new_with_max_length
			(GNOME_DIALOG (dialog), MAX_CELL_NAME_LEN);
                xmin_entry = gnumeric_dialog_entry_new_with_max_length
			(GNOME_DIALOG (dialog), MAX_CELL_NAME_LEN);
                xmax_entry = gnumeric_dialog_entry_new_with_max_length
			(GNOME_DIALOG (dialog), MAX_CELL_NAME_LEN);

		set_label    = gtk_label_new (_("Set cell:"));
		target_label = gtk_label_new (_("To value:"));
		change_label = gtk_label_new (_("By changing cell:"));
		xmin_label = gtk_label_new (_("To a value of at least [optional]:"));
		xmax_label = gtk_label_new (_("But no bigger than [optional]:"));

		gtk_misc_set_alignment (GTK_MISC (set_label), 0, 0);
		gtk_misc_set_alignment (GTK_MISC (target_label), 0, 0);
		gtk_misc_set_alignment (GTK_MISC (change_label), 0, 0);
		gtk_misc_set_alignment (GTK_MISC (xmin_label), 0, 0);
		gtk_misc_set_alignment (GTK_MISC (xmax_label), 0, 0);

                box = gtk_hbox_new (FALSE, 0);
                entry_box = gtk_vbox_new (FALSE, 0);
                label_box = gtk_vbox_new (FALSE, 0);

		gtk_box_pack_start_defaults (GTK_BOX (label_box), set_label);
                gtk_box_pack_start_defaults (GTK_BOX (entry_box), set_entry);
		gtk_box_pack_start_defaults (GTK_BOX (label_box), target_label);
                gtk_box_pack_start_defaults (GTK_BOX (entry_box), target_entry);
		gtk_box_pack_start_defaults (GTK_BOX (label_box), change_label);
                gtk_box_pack_start_defaults (GTK_BOX (entry_box), change_entry);
		gtk_box_pack_start_defaults (GTK_BOX (label_box), xmin_label);
                gtk_box_pack_start_defaults (GTK_BOX (entry_box), xmin_entry);
		gtk_box_pack_start_defaults (GTK_BOX (label_box), xmax_label);
                gtk_box_pack_start_defaults (GTK_BOX (entry_box), xmax_entry);

		gtk_box_pack_start_defaults (GTK_BOX (box), label_box);
		gtk_box_pack_start_defaults (GTK_BOX (box), entry_box);

                gtk_box_pack_start_defaults (GTK_BOX (GNOME_DIALOG
						      (dialog)->vbox), box);

		gtk_entry_set_text (GTK_ENTRY (set_entry), set_entry_str);
		focus_on_entry (set_entry);
                gtk_widget_show_all (box);
	} else {
		gtk_entry_set_text (GTK_ENTRY (set_entry), set_entry_str);
		focus_on_entry (set_entry);
	        gtk_widget_show (dialog);
	}

	gtk_widget_grab_focus (set_entry);

dialog_loop:
        selection = gnumeric_dialog_run (wbcg, GNOME_DIALOG (dialog));
	if (selection == 0) {
		Cell    *set_cell;
 	        int     set_cell_col, set_cell_row;

		Cell    *change_cell;
		int     change_cell_col, change_cell_row;

		Value   *old_value;
		char    *text;
		gnum_float target_value;
		gnum_float xmin, xmax;
		GoalSeekStatus status;

		/* Check that a cell entered in 'set cell' entry */
		text = gtk_entry_get_text (GTK_ENTRY (set_entry));
		if (!parse_cell_name (text, &set_cell_col, &set_cell_row, TRUE, NULL)){
	                gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("You should introduce a valid cell name in 'Set cell'"));
			focus_on_entry (set_entry);
			goto dialog_loop;
		}

		set_cell = sheet_cell_get (sheet, set_cell_col, set_cell_row);
		if (set_cell == NULL || !cell_has_expr (set_cell)) {
	                gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("The cell named in 'Set cell' must contain a formula"));
			focus_on_entry (set_entry);
			goto dialog_loop;
		}

		text = gtk_entry_get_text (GTK_ENTRY (target_entry));
		/* FIXME: Add float input parsing here */
		target_value = atof (text);

		/* Check that a cell entered in 'by changing cell' entry */
		text = gtk_entry_get_text (GTK_ENTRY (change_entry));
		if (!parse_cell_name (text, &change_cell_col, &change_cell_row, TRUE, NULL)){
	                gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("You should introduce a valid cell "
					   "name in 'By changing cell'"));
			focus_on_entry (change_entry);
			goto dialog_loop;
		}

		change_cell = sheet_cell_fetch (sheet, change_cell_col, change_cell_row);
		if (cell_has_expr (change_cell)) {
	                gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("The cell named in 'By changing cell' "
					   "must not contain a formula"));
			focus_on_entry (change_entry);
			goto dialog_loop;
		}

		text = gtk_entry_get_text (GTK_ENTRY (xmin_entry));
		/* FIXME: Add float input parsing here */
		if (*text)
			xmin = atof (text);
		else
			xmin = -1e6;

		text = gtk_entry_get_text (GTK_ENTRY (xmax_entry));
		/* FIXME: Add float input parsing here */
		if (*text)
			xmax = atof (text);
		else
			xmax = 1e6;

		old_value =
			change_cell->value
			? value_duplicate (change_cell->value)
			: NULL;

		status = gnumeric_goal_seek (wbcg, sheet,
					     set_cell, target_value,
					     change_cell, xmin, xmax);

		if (status == GOAL_SEEK_OK) {
		        gnome_dialog_close (GNOME_DIALOG (dialog));
			if (dialog_found_solution (wbcg, set_cell, change_cell, target_value)) {
			        /* Goal seek cancelled */
				if (old_value) {
					sheet_cell_set_value (change_cell, old_value, NULL);
					workbook_recalc (set_cell->base.sheet->workbook);
				}
				return;
			}
			if (old_value)
				value_release (old_value);
			return;
		} else {
	                gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("Goal seek did not find a solution!"));
		}
		if (old_value)
			value_release (old_value);
	}

	if (selection >= 0)
		gnome_dialog_close (GNOME_DIALOG (dialog));
}

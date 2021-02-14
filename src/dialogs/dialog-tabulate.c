/*
 * dialog-tabulate.c:
 *   Dialog for making tables of function dependcies.
 *
 * Author:
 *   COPYRIGHT (C) Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <widgets/gnm-expr-entry.h>
#include <tools/tabulate.h>
#include <wbc-gtk.h>
#include <ranges.h>
#include <value.h>
#include <sheet.h>
#include <mstyle.h>
#include <workbook.h>
#include <mathfunc.h>
#include <cell.h>
#include <commands.h>
#include <gnm-format.h>
#include <number-match.h>
#include <mstyle.h>
#include <style-border.h>
#include <sheet-style.h>
#include <style-color.h>

#include <string.h>

#define TABULATE_KEY "tabulate-dialog"

/* ------------------------------------------------------------------------- */

enum {
	COL_CELL = 0,
	COL_MIN,
	COL_MAX,
	COL_STEP
};

typedef struct {
	WBCGtk *wbcg;
	Sheet *sheet;

	GtkBuilder *gui;
	GtkDialog *dialog;

	GtkGrid *grid;
	GnmExprEntry *resultrangetext;
} DialogState;

static const char * const mode_group[] = {
	"mode_visual",
	"mode_coordinate",
	NULL
};

/* ------------------------------------------------------------------------- */

static void
non_model_dialog (WBCGtk *wbcg,
		  GtkDialog *dialog,
		  const char *key)
{
	gnm_keyed_dialog (wbcg, GTK_WINDOW (dialog), key);

	gtk_widget_show (GTK_WIDGET (dialog));
}

static GnmCell *
single_cell (Sheet *sheet, GnmExprEntry *gee)
{
	int col, row;
	gboolean issingle;
	GnmValue *v = gnm_expr_entry_parse_as_value (gee, sheet);

	if (!v) return NULL;

	col = v->v_range.cell.a.col;
	row = v->v_range.cell.a.row;
	issingle = (col == v->v_range.cell.b.col && row == v->v_range.cell.b.row);

	value_release (v);

	if (issingle)
		return sheet_cell_fetch (sheet, col, row);
	else
		return NULL;
}

static int
get_grid_float_entry (GtkGrid *g, int y, int x, GnmCell *cell, gnm_float *number,
		       GtkEntry **wp, gboolean with_default, gnm_float default_float)
{
	GOFormat const *format;
	GtkWidget *w = gtk_grid_get_child_at (g, x, y + 1);

	g_return_val_if_fail (GTK_IS_ENTRY (w), 3);

	*wp = GTK_ENTRY (w);
	format = gnm_cell_get_format (cell);

	return (with_default?
	        entry_to_float_with_format_default (*wp, number, TRUE, format,
	                                            default_float):
			entry_to_float_with_format (*wp, number, TRUE, format));
}

static void
cb_dialog_destroy (DialogState *dd)
{
	g_object_unref (dd->gui);
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}

static void
cancel_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GtkDialog *dialog = dd->dialog;
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
tabulate_ok_clicked (G_GNUC_UNUSED GtkWidget *widget, DialogState *dd)
{
	GtkDialog *dialog = dd->dialog;
	GnmCell *resultcell;
	int dims = 0;
	int row;
	gboolean with_coordinates;
	GnmTabulateInfo *data;
	/* we might get the 4 below from the positon of some of the widgets inside the grid */
	int nrows = 4;
	GnmCell **cells;
	gnm_float *minima, *maxima, *steps;

	cells = g_new (GnmCell *, nrows);
	minima = g_new (gnm_float, nrows);
	maxima = g_new (gnm_float, nrows);
	steps = g_new (gnm_float, nrows);

	for (row = 1; row < nrows; row++) {
		GtkEntry *e_w;
		GnmExprEntry *w = GNM_EXPR_ENTRY (gtk_grid_get_child_at (dd->grid, COL_CELL, row + 1));

		if (!w || gnm_expr_entry_is_blank (w))
			continue;

		cells[dims] = single_cell (dd->sheet, w);
		if (!cells[dims]) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("You should introduce a single valid cell as dependency cell"));
			gnm_expr_entry_grab_focus (GNM_EXPR_ENTRY (w), TRUE);
			goto error;
		}
		if (gnm_cell_has_expr (cells[dims])) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("The dependency cells should not contain an expression"));
			gnm_expr_entry_grab_focus (GNM_EXPR_ENTRY (w), TRUE);
			goto error;
		}

		if (get_grid_float_entry (dd->grid, row, COL_MIN, cells[dims],
					   &(minima[dims]), &e_w, FALSE, 0.0)) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("You should introduce a valid number as minimum"));
			focus_on_entry (e_w);
			goto error;
		}

		if (get_grid_float_entry (dd->grid, row, COL_MAX, cells[dims],
					   &(maxima[dims]), &e_w, FALSE, 0.0)) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("You should introduce a valid number as maximum"));
			focus_on_entry (e_w);
			goto error;
		}

		if (maxima[dims] < minima[dims]) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("The maximum value should be bigger than the minimum"));
			focus_on_entry (e_w);
			goto error;
		}

		if (get_grid_float_entry (dd->grid, row, COL_STEP, cells[dims],
					   &(steps[dims]), &e_w, TRUE, 1.0)) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("You should introduce a valid number as step size"));
			focus_on_entry (e_w);
			goto error;
		}

		if (steps[dims] <= 0) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("The step size should be positive"));
			focus_on_entry (e_w);
			goto error;
		}

		dims++;
	}

	if (dims == 0) {
		go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
				 GTK_MESSAGE_ERROR,
				 _("You should introduce one or more dependency cells"));
		goto error;
	}

	{
		resultcell = single_cell (dd->sheet, dd->resultrangetext);

		if (!resultcell) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("You should introduce a single valid cell as result cell"));
			gnm_expr_entry_grab_focus (dd->resultrangetext, TRUE);
			goto error;
		}

		if (!gnm_cell_has_expr (resultcell)) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("The target cell should contain an expression"));
			gnm_expr_entry_grab_focus (dd->resultrangetext, TRUE);
			goto error;
		}
	}

	{
		int i = gnm_gui_group_value (dd->gui, mode_group);
		with_coordinates = (i == -1) ? TRUE : (gboolean)i;
	}

	data = g_new (GnmTabulateInfo, 1);
	data->target = resultcell;
	data->dims = dims;
	data->cells = cells;
	data->minima = minima;
	data->maxima = maxima;
	data->steps = steps;
	data->with_coordinates = with_coordinates;

	if (!cmd_tabulate (GNM_WBC (dd->wbcg), data)) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	g_free (data);
 error:
	g_free (minima);
	g_free (maxima);
	g_free (steps);
	g_free (cells);
}

void
dialog_tabulate (WBCGtk *wbcg, Sheet *sheet)
{
	GtkBuilder *gui;
	GtkDialog *dialog;
	DialogState *dd;
	int i;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	if (gnm_dialog_raise_if_exists (wbcg, TABULATE_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/tabulate.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (go_gtk_builder_get_widget (gui, "tabulate_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->dialog = dialog;
	dd->sheet = sheet;

	dd->grid = GTK_GRID (go_gtk_builder_get_widget (gui, "main-grid"));
	/* we might get the 4 below from the positon of some of the widgets inside the grid */
	for (i = 1; i < 4; i++) {
		GnmExprEntry *ge = gnm_expr_entry_new (wbcg, TRUE);
		gnm_expr_entry_set_flags (ge,
			GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL,
			GNM_EE_MASK);

		gtk_grid_attach (dd->grid, GTK_WIDGET (ge), COL_CELL, i + 1, 1, 1);
		gtk_widget_set_margin_left (GTK_WIDGET (ge), 18);
		gtk_widget_show (GTK_WIDGET (ge));
	}

	dd->resultrangetext = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (dd->resultrangetext,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL,
		GNM_EE_MASK);
	gtk_grid_attach (dd->grid, GTK_WIDGET (dd->resultrangetext), 0, 6, 4, 1);
	gtk_widget_set_margin_left (GTK_WIDGET (dd->resultrangetext), 18);
	gtk_widget_show (GTK_WIDGET (dd->resultrangetext));

	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "ok_button")),
		"clicked",
		G_CALLBACK (tabulate_ok_clicked), dd);

	g_signal_connect (G_OBJECT (go_gtk_builder_get_widget (gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cancel_clicked), dd);
/* FIXME: Add correct helpfile address */
	gnm_init_help_button (
		go_gtk_builder_get_widget (gui, "help_button"),
		GNUMERIC_HELP_LINK_TABULATE);
	g_object_set_data_full (G_OBJECT (dialog),
		"state", dd, (GDestroyNotify) cb_dialog_destroy);

	gnm_dialog_setup_destroy_handlers (dialog, wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gtk_widget_show_all (gtk_dialog_get_content_area (dialog));
	wbc_gtk_attach_guru (wbcg, GTK_WIDGET (dialog));

	non_model_dialog (wbcg, dialog, TABULATE_KEY);
}


/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-tabulate.c:
 *   Dialog for making tables of function dependcies.
 *
 * Author:
 *   COPYRIGHT (C) Morten Welinder (terra@diku.dk)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"
#include <gui-util.h>
#include <glade/glade.h>
#include <widgets/gnumeric-expr-entry.h>
#include <workbook-edit.h>
#include "ranges.h"
#include "value.h"
#include "sheet.h"
#include "mstyle.h"
#include "workbook.h"
#include "mathfunc.h"
#include "cell.h"
#include "format.h"
#include "number-match.h"
#include "mstyle.h"
#include "style-border.h"
#include "sheet-style.h"
#include "style-color.h"

#define TABULATE_KEY "tabulate-dialog"

/* ------------------------------------------------------------------------- */

static Value *
tabulation_eval (Workbook *wb, int dims,
		 const gnum_float *x, Cell **xcells, Cell *ycell)
{
	int i;

	for (i = 0; i < dims; i++) {
		cell_set_value (xcells[i], value_new_float (x[i]));
		cell_queue_recalc (xcells[i]);
	}
	workbook_recalc (wb);

	return ycell->value
		? value_duplicate (ycell->value)
		: value_new_error (NULL, gnumeric_err_VALUE);
}

static StyleFormat const *
my_get_format (Cell const *cell)
{
	StyleFormat const *format = mstyle_get_format (cell_get_mstyle (cell));

	if (style_format_is_general (format) &&
	    cell->value != NULL && VALUE_FMT (cell->value) != NULL)
		return VALUE_FMT (cell->value);
	return format;
}

static void
do_tabulation (Workbook *wb,
	       Cell *target,
	       int dims,
	       Cell **cells,
	       const gnum_float *minima,
	       const gnum_float *maxima,
	       const gnum_float *steps,
	       gboolean with_coordinates)
{
	Sheet *sheet = NULL;
	gboolean sheetdim = (!with_coordinates && dims >= 3);
	StyleFormat const *targetformat = my_get_format (target);
	int row = 0;

	gnum_float *values = g_new (gnum_float, dims);
	int *index = g_new (int, dims);
	int *counts = g_new (int, dims);
	Sheet **sheets = NULL;
	StyleFormat const **formats = g_new (StyleFormat const *, dims);

	{
		int i;
		for (i = 0; i < dims; i++) {
			values[i] = minima[i];
			index[i] = 0;
			formats[i] = my_get_format (cells[i]);

			counts[i] = 1 + gnumeric_fake_floor ((maxima[i] - minima[i]) / steps[i]);
			/* Silently truncate at the edges.  */
			if (!with_coordinates && i == 0 && counts[i] > SHEET_MAX_COLS - 1) {
				counts[i] = SHEET_MAX_COLS - 1;
			} else if (!with_coordinates && i == 1 && counts[i] > SHEET_MAX_ROWS - 1) {
				counts[i] = SHEET_MAX_ROWS - 1;
			}
		}
	}

	if (sheetdim) {
		int dim = 2;
		gnum_float val = minima[dim];
		StyleFormat const *sf = my_get_format (cells[dim]);
		int i;

		sheets = g_new (Sheet *, counts[dim]);
		for (i = 0; i < counts[dim]; i++) {
			Value *v = value_new_float (val);
			char *base_name = format_value (sf, v, NULL, -1);
			char *unique_name =
				workbook_sheet_get_free_name (wb,
							      base_name,
							      FALSE, FALSE);

			g_free (base_name);
			value_release (v);
			sheet = sheets[i] = sheet_new (wb, unique_name);
			g_free (unique_name);
			workbook_sheet_attach (wb, sheet, NULL);

			val += steps[dim];
		}
	} else {
		char *unique_name =
			workbook_sheet_get_free_name (wb,
						      _("Tabulation"),
						      FALSE, FALSE);
	        sheet = sheet_new (wb, unique_name);
		g_free (unique_name);
		workbook_sheet_attach (wb, sheet, NULL);
	}

	while (1) {
		Value *v;
		Cell *cell;
		int dim;

		if (with_coordinates) {
			int i;

			for (i = 0; i < dims; i++) {
				Value *v = value_new_float (values[i]);
				value_set_fmt (v, formats[i]);
				sheet_cell_set_value (
					sheet_cell_fetch (sheet, i, row), v);
			}

			cell = sheet_cell_fetch (sheet, dims, row);
		} else {
			Sheet *thissheet = sheetdim ? sheets[index[2]] : sheet;
			int row = (dims >= 1 ? index[0] + 1 : 1);
			int col = (dims >= 2 ? index[1] + 1 : 1);

			/* Fill-in top header.  */
			if (row == 1 && dims >= 2) {
				Value *v = value_new_float (values[1]);
				value_set_fmt (v, formats[1]);
				sheet_cell_set_value (
					sheet_cell_fetch (sheet, col, 0), v);
			}

			/* Fill-in left header.  */
			if (col == 1 && dims >= 1) {
				Value *v = value_new_float (values[0]);
				value_set_fmt (v, formats[0]);
				sheet_cell_set_value (
					sheet_cell_fetch (sheet, 0, row), v);
			}

			/* Make a horizon line on top between header and table.  */
			if (row == 1 && col == 1) {
				MStyle *mstyle = mstyle_new ();
				Range range;
				StyleBorder *border;

				range.start.col = 0;
				range.start.row = 0;
				range.end.col   = (dims >= 2 ? counts[1] : 1);
				range.end.row   = 0;

				border = style_border_fetch (STYLE_BORDER_MEDIUM,
							     style_color_black (),
							     STYLE_BORDER_HORIZONTAL);

				mstyle_set_border (mstyle, MSTYLE_BORDER_BOTTOM, border);
				sheet_style_apply_range (sheet, &range, mstyle);
			}

			/* Make a vertical line on left between header and table.  */
			if (row == 1 && col == 1) {
				MStyle *mstyle = mstyle_new ();
				Range range;
				StyleBorder *border;

				range.start.col = 0;
				range.start.row = 0;
				range.end.col   = 0;
				range.end.row   = counts[0];;

				border = style_border_fetch (STYLE_BORDER_MEDIUM,
							     style_color_black (),
							     STYLE_BORDER_VERTICAL);

				mstyle_set_border (mstyle, MSTYLE_BORDER_RIGHT, border);
				sheet_style_apply_range (sheet, &range, mstyle);
			}

			cell = sheet_cell_fetch (thissheet, col, row);
		}

		v = tabulation_eval (wb, dims, values, cells, target);
		value_set_fmt (v, targetformat);
		sheet_cell_set_value (cell, v);

		if (with_coordinates) {
			row++;
			if (row >= SHEET_MAX_ROWS)
				break;
		}

		for (dim = dims - 1; dim >= 0; dim--) {
			values[dim] += steps[dim];
			index[dim]++;

			if (index[dim] == counts[dim]) {
				index[dim] = 0;
				values[dim] = minima[dim];
			} else
				break;
		}

		if (dim < 0)
			break;
	}

	g_free (values);
	g_free (index);
	g_free (counts);
	g_free (sheets);
	g_free (formats);
}

/* ------------------------------------------------------------------------- */

enum {
	COL_CELL = 0,
	COL_MIN,
	COL_MAX,
	COL_STEP
};

typedef struct {
	WorkbookControlGUI *wbcg;
	Sheet *sheet;

	GladeXML *gui;
	GtkDialog *dialog;

	GtkTable *source_table;
	GnumericExprEntry *resultrangetext;

} DialogState;

static const char *mode_group[] = {
	"mode_visual",
	"mode_coordinate",
	0
};

/* ------------------------------------------------------------------------- */

static void
free_state (DialogState *dd)
{
	g_object_unref (G_OBJECT (dd->gui));
	memset (dd, 0, sizeof (*dd));
	g_free (dd);
}

static void
non_model_dialog (WorkbookControlGUI *wbcg,
		  GtkDialog *dialog,
		  const char *key)
{
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), key);

	gtk_widget_show (GTK_WIDGET (dialog));
}

static Cell *
single_cell (Sheet *sheet, GnumericExprEntry *gee)
{
	int col, row;
	gboolean issingle;
	Value *v = gnm_expr_entry_parse_as_value (gee, sheet);

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

static GnumericExprEntry *
get_table_expr_entry (GtkTable *t, int y, int x)
{
	GList *l;

	for (l = t->children; l; l = l->next) {
		GtkTableChild *child = l->data;
		if (child->left_attach == x && child->top_attach == y && 
		    IS_GNUMERIC_EXPR_ENTRY (child->widget)) {
			return GNUMERIC_EXPR_ENTRY (child->widget);
		}
	}

	return NULL;
}

static int
get_table_float_entry (GtkTable *t, int y, int x, Cell *cell, gnum_float *number,
		       GtkEntry **wp, gboolean with_default, gnum_float default_float)
{
	GList *l;
	StyleFormat *format;

	*wp = NULL;
	for (l = t->children; l; l = l->next) {
		GtkTableChild *child = l->data;
		if (child->left_attach == x && child->top_attach == y && 
		    GTK_IS_ENTRY (child->widget)) {
			*wp = GTK_ENTRY (child->widget);
			format = mstyle_get_format (cell_get_mstyle (cell));
			return (with_default ? 
				entry_to_float_with_format_default (GTK_ENTRY (child->widget), number,
							   TRUE, format, default_float) :  
				entry_to_float_with_format (GTK_ENTRY (child->widget), number,
							   TRUE, format));
		} 
	}
	return 3;
}

static void
dialog_destroy (__attribute__((unused)) GtkWidget *widget, DialogState *dd)
{
	wbcg_edit_detach_guru (dd->wbcg);
	free_state (dd);
}

static void
cancel_clicked (__attribute__((unused)) GtkWidget *widget, DialogState *dd)
{
	GtkDialog *dialog = dd->dialog;
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
tabulate_ok_clicked (__attribute__((unused)) GtkWidget *widget, DialogState *dd)
{
	GtkDialog *dialog = dd->dialog;
	Cell *resultcell;
	int dims = 0;
	int row;
	gboolean with_coordinates;

	Cell **cells = g_new (Cell *, dd->source_table->nrows);
	gnum_float *minima = g_new (gnum_float, dd->source_table->nrows);
	gnum_float *maxima = g_new (gnum_float, dd->source_table->nrows);
	gnum_float *steps = g_new (gnum_float, dd->source_table->nrows);

	for (row = 1; row < dd->source_table->nrows; row++) {
		GtkEntry *e_w;
		GnumericExprEntry *w = get_table_expr_entry (dd->source_table, row, COL_CELL);

		if (!w || gnm_expr_entry_is_blank (w))
			continue;

		cells[dims] = single_cell (dd->sheet, w);
		if (!cells[dims]) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("You should introduce a single valid cell as dependency cell"));
			gnm_expr_entry_grab_focus (GNUMERIC_EXPR_ENTRY (w), TRUE);
			goto error;
		}
		if (cell_has_expr (cells[dims])) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("The dependency cells should not contain an expression"));
			gnm_expr_entry_grab_focus (GNUMERIC_EXPR_ENTRY (w), TRUE);
			goto error;
		}

		if (get_table_float_entry (dd->source_table, row, COL_MIN, cells[dims], 
					   &(minima[dims]), &e_w, FALSE, 0.0)) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("You should introduce a valid number as minimum"));
			focus_on_entry (e_w);
			goto error;
		}

		if (get_table_float_entry (dd->source_table, row, COL_MAX, cells[dims], 
					   &(maxima[dims]), &e_w, FALSE, 0.0)) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("You should introduce a valid number as maximum"));
			focus_on_entry (e_w);
			goto error;
		}

		if (maxima[dims] < minima[dims]) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("The maximum value should be bigger than the minimum"));
			focus_on_entry (e_w);
			goto error;
		}

		if (get_table_float_entry (dd->source_table, row, COL_STEP, cells[dims], 
					   &(steps[dims]), &e_w, TRUE, 1.0)) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("You should introduce a valid number as step size"));
			focus_on_entry (e_w);
			goto error;
		}

		if (steps[dims] <= 0) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("The step size should be positive"));
			focus_on_entry (e_w);
			goto error;
		}

		dims++;
	}

	if (dims == 0) {
		gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
				 _("You should introduce one or more dependency cells"));
		goto error;
	}

	{
		resultcell = single_cell (dd->sheet, dd->resultrangetext);

		if (!resultcell) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("You should introduce a single valid cell as result cell"));
			gnm_expr_entry_grab_focus (dd->resultrangetext, TRUE);
			goto error;
		}

		if (!cell_has_expr (resultcell)) {
			gnumeric_notice (dd->wbcg, GTK_MESSAGE_ERROR,
					 _("The target cell should contain an expression"));
			gnm_expr_entry_grab_focus (dd->resultrangetext, TRUE);
			goto error;
		}
	}

	{
		int i = gnumeric_glade_group_value (dd->gui, mode_group);
		with_coordinates = (i == -1) ? TRUE : (gboolean)i;
	}

	do_tabulation (dd->sheet->workbook,
		       resultcell,
		       dims,
		       cells, minima, maxima, steps,
		       with_coordinates);

	gtk_widget_destroy (GTK_WIDGET (dialog));

 error:
	g_free (minima);
	g_free (maxima);
	g_free (steps);
	g_free (cells);
}

void
dialog_tabulate (WorkbookControlGUI *wbcg, Sheet *sheet)
{
	GladeXML *gui;
	GtkDialog *dialog;
	DialogState *dd;
	int i;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbcg_edit_has_guru (wbcg))
		return;

	if (gnumeric_dialog_raise_if_exists (wbcg, TABULATE_KEY))
		return;

	gui = gnumeric_glade_xml_new (wbcg, "tabulate.glade");
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "tabulate_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->dialog = dialog;
	dd->sheet = sheet;

	gtk_window_set_policy (GTK_WINDOW (dialog), FALSE, TRUE, FALSE);

	dd->source_table = GTK_TABLE (glade_xml_get_widget (gui, "source_table"));
	for (i = 1; i < dd->source_table->nrows; i++) {
		GnumericExprEntry *ge = gnumeric_expr_entry_new (wbcg, TRUE);
		gnm_expr_entry_set_flags (ge,
					       GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL,
					       GNUM_EE_MASK);

		gtk_table_attach (dd->source_table,
				  GTK_WIDGET (ge),
				  COL_CELL, COL_CELL + 1,
				  i, i + 1,
				  GTK_FILL, GTK_FILL,
				  0, 0);
		gnm_expr_entry_set_scg (ge, wbcg_cur_scg (wbcg));
		gtk_widget_show (GTK_WIDGET (ge));
	}

	dd->resultrangetext = gnumeric_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (dd->resultrangetext,
				       GNUM_EE_SINGLE_RANGE | GNUM_EE_SHEET_OPTIONAL,
				       GNUM_EE_MASK);
	gtk_box_pack_start (GTK_BOX (glade_xml_get_widget (gui, "result_hbox")),
			    GTK_WIDGET (dd->resultrangetext),
			    TRUE, TRUE, 0);
	gnm_expr_entry_set_scg (dd->resultrangetext, wbcg_cur_scg (wbcg));
	gtk_widget_show (GTK_WIDGET (dd->resultrangetext));

	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "ok_button")),
		"clicked",
		G_CALLBACK (tabulate_ok_clicked), dd);

	g_signal_connect (G_OBJECT (glade_xml_get_widget (gui, "cancel_button")),
		"clicked",
		G_CALLBACK (cancel_clicked), dd);
/* FIXME: Add correct helpfile address */
	gnumeric_init_help_button (
		glade_xml_get_widget (gui, "help_button"),
		"fill-tabulate.html");

	g_signal_connect (G_OBJECT (dialog),
		"destroy",
		G_CALLBACK (dialog_destroy), dd);

	gtk_widget_show_all (dialog->vbox);
	wbcg_edit_attach_guru (wbcg, GTK_WIDGET (dialog));

	non_model_dialog (wbcg, dialog, TABULATE_KEY);
}

/* ------------------------------------------------------------------------- */

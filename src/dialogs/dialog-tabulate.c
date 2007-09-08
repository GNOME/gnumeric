/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-tabulate.c:
 *   Dialog for making tables of function dependcies.
 *
 * Author:
 *   COPYRIGHT (C) Morten Welinder (terra@gnome.org)
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <gui-util.h>
#include <glade/glade.h>
#include <widgets/gnumeric-expr-entry.h>
#include <tools/tabulate.h>
#include <wbc-gtk.h>
#include "ranges.h"
#include "value.h"
#include "sheet.h"
#include "mstyle.h"
#include "workbook.h"
#include "mathfunc.h"
#include "cell.h"
#include "commands.h"
#include "gnm-format.h"
#include "number-match.h"
#include "mstyle.h"
#include "style-border.h"
#include "sheet-style.h"
#include "style-color.h"

#include <gtk/gtktable.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtkradiobutton.h>
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

	GladeXML *gui;
	GtkDialog *dialog;

	GtkTable *source_table;
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
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (dialog), key);

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

static GnmExprEntry *
get_table_expr_entry (GtkTable *t, int y, int x)
{
	GList *l;

	for (l = t->children; l; l = l->next) {
		GtkTableChild *child = l->data;
		if (child->left_attach == x && child->top_attach == y &&
		    IS_GNM_EXPR_ENTRY (child->widget)) {
			return GNM_EXPR_ENTRY (child->widget);
		}
	}

	return NULL;
}

static int
get_table_float_entry (GtkTable *t, int y, int x, GnmCell *cell, gnm_float *number,
		       GtkEntry **wp, gboolean with_default, gnm_float default_float)
{
	GList *l;
	GOFormat *format;

	*wp = NULL;
	for (l = t->children; l; l = l->next) {
		GtkTableChild *child = l->data;
		if (child->left_attach == x && child->top_attach == y &&
		    GTK_IS_ENTRY (child->widget)) {
			*wp = GTK_ENTRY (child->widget);
			format = gnm_style_get_format (gnm_cell_get_style (cell));
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
cb_dialog_destroy (DialogState *dd)
{
	g_object_unref (G_OBJECT (dd->gui));
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

	GnmCell **cells = g_new (GnmCell *, dd->source_table->nrows);
	gnm_float *minima = g_new (gnm_float, dd->source_table->nrows);
	gnm_float *maxima = g_new (gnm_float, dd->source_table->nrows);
	gnm_float *steps = g_new (gnm_float, dd->source_table->nrows);

	for (row = 1; row < dd->source_table->nrows; row++) {
		GtkEntry *e_w;
		GnmExprEntry *w = get_table_expr_entry (dd->source_table, row, COL_CELL);

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

		if (get_table_float_entry (dd->source_table, row, COL_MIN, cells[dims],
					   &(minima[dims]), &e_w, FALSE, 0.0)) {
			go_gtk_notice_dialog (GTK_WINDOW (dd->dialog),
					 GTK_MESSAGE_ERROR,
					 _("You should introduce a valid number as minimum"));
			focus_on_entry (e_w);
			goto error;
		}

		if (get_table_float_entry (dd->source_table, row, COL_MAX, cells[dims],
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

		if (get_table_float_entry (dd->source_table, row, COL_STEP, cells[dims],
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
		int i = gnumeric_glade_group_value (dd->gui, mode_group);
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

	if (!cmd_tabulate (WORKBOOK_CONTROL (dd->wbcg), data)) { 
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
	GladeXML *gui;
	GtkDialog *dialog;
	DialogState *dd;
	int i;

	g_return_if_fail (wbcg != NULL);

	/* Only one guru per workbook. */
	if (wbc_gtk_get_guru (wbcg))
		return;

	if (gnumeric_dialog_raise_if_exists (wbcg, TABULATE_KEY))
		return;
	gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"tabulate.glade", NULL, NULL);
        if (gui == NULL)
                return;

	dialog = GTK_DIALOG (glade_xml_get_widget (gui, "tabulate_dialog"));

	dd = g_new (DialogState, 1);
	dd->wbcg = wbcg;
	dd->gui = gui;
	dd->dialog = dialog;
	dd->sheet = sheet;

	g_object_set (G_OBJECT (dialog),
		"allow-shrink",	FALSE,
		"allow-grow",	TRUE,
		NULL);

	dd->source_table = GTK_TABLE (glade_xml_get_widget (gui, "source_table"));
	for (i = 1; i < dd->source_table->nrows; i++) {
		GnmExprEntry *ge = gnm_expr_entry_new (wbcg, TRUE);
		gnm_expr_entry_set_flags (ge,
			GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL,
			GNM_EE_MASK);

		gtk_table_attach (dd->source_table,
				  GTK_WIDGET (ge),
				  COL_CELL, COL_CELL + 1,
				  i, i + 1,
				  GTK_FILL, GTK_FILL,
				  0, 0);
		gtk_widget_show (GTK_WIDGET (ge));
	}

	dd->resultrangetext = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (dd->resultrangetext,
		GNM_EE_SINGLE_RANGE | GNM_EE_SHEET_OPTIONAL,
		GNM_EE_MASK);
	gtk_box_pack_start (GTK_BOX (glade_xml_get_widget (gui, "result_hbox")),
			    GTK_WIDGET (dd->resultrangetext),
			    TRUE, TRUE, 0);
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
		GNUMERIC_HELP_LINK_TABULATE);
	g_object_set_data_full (G_OBJECT (dialog),
		"state", dd, (GDestroyNotify) cb_dialog_destroy);

	gnm_dialog_setup_destroy_handlers (dialog, wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gtk_widget_show_all (dialog->vbox);
	wbc_gtk_attach_guru (wbcg, GTK_WIDGET (dialog));

	non_model_dialog (wbcg, dialog, TABULATE_KEY);
}

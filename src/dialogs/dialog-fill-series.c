/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-fill-series.c: Fill according to a linear or exponential serie.
 *
 * Authors: Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
 *          Andreas J. Guelzow (aguelzow@taliesin.ca)
 *
 * Copyright (C) 2003  Jukka-Pekka Iivonen (jiivonen@hutcs.cs.hut.fi)
 * Copyright (C) Andreas J. Guelzow (aguelzow@taliesin.ca)
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
#include <selection.h>
#include <workbook.h>
#include <sheet.h>
#include <sheet-view.h>
#include <commands.h>
#include <ranges.h>
#include <cmd-edit.h>
#include <workbook-edit.h>
#include <command-context.h>

#include <dao-gui-utils.h>

#include <glade/glade.h>
#include "fill-series.h"

#define INSERT_CELL_DIALOG_KEY "insert-cells-dialog"

typedef struct {
	WorkbookControlGUI *wbcg;
	GtkWidget          *dialog;
	GtkWidget          *ok_button;
	GtkWidget          *cancel_button;
	Range const        *sel;
	Sheet              *sheet;
	GladeXML           *gui;
} FillSeriesState;

static gboolean
fill_series_destroy (GtkObject *w, FillSeriesState *state)
{
	g_return_val_if_fail (w != NULL, FALSE);
	g_return_val_if_fail (state != NULL, FALSE);

	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);

	return FALSE;
}

static void
cb_insert_cell_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
			   FillSeriesState *state)
{
	GtkWidget       *radio, *entry;
	int             cols, rows;

	fill_series_t           fs;
	data_analysis_output_t  dao;


	/* Read the `Series in' radio buttons. */
	radio = glade_xml_get_widget (state->gui, "series_in_rows");
	g_return_if_fail (radio != NULL);

	fs.series_in_rows = ! gtk_radio_group_get_selected
	        (GTK_RADIO_BUTTON (radio)->group);

	/* Read the `Type' radio buttons. */
	radio = glade_xml_get_widget (state->gui, "type_linear");
	g_return_if_fail (radio != NULL);

	fs.type = gtk_radio_group_get_selected
	        (GTK_RADIO_BUTTON (radio)->group);

	/* Read the `Date unit' radio buttons. */
	radio = glade_xml_get_widget (state->gui, "unit_day");
	g_return_if_fail (radio != NULL);

	fs.date_unit = gtk_radio_group_get_selected
	        (GTK_RADIO_BUTTON (radio)->group);


	cols   = state->sel->end.col - state->sel->start.col + 1;
	rows   = state->sel->end.row - state->sel->start.row + 1;
	fs.sel = state->sel;


	/* Read the `Step' value. */
	entry = glade_xml_get_widget (state->gui, "step_entry");
	g_return_if_fail (entry != NULL);
	fs.is_step_set = ! entry_to_float (GTK_ENTRY (entry), &fs.step_value,
					   TRUE);
	if (! fs.is_step_set && cols == 1 && rows == 1)
		goto out;  /* Cannot do series. */

	/* Read the `Stop' value. */
	entry = glade_xml_get_widget (state->gui, "stop_entry");
	g_return_if_fail (entry != NULL);
	fs.is_stop_set = ! entry_to_float (GTK_ENTRY (entry), &fs.stop_value,
					   TRUE);
	if (! fs.is_stop_set && cols == 1 && rows == 1)
		goto out;  /* Cannot do series. */

	fill_series (WORKBOOK_CONTROL (state->wbcg), &dao, state->sheet, &fs);
 out:
	gtk_widget_destroy (state->dialog);
}

static void
cb_fill_series_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			       FillSeriesState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_type_button_clicked (G_GNUC_UNUSED GtkWidget *button,
			FillSeriesState *state)
{
	GtkWidget          *frame, *radio;
	fill_series_type_t type;


	/* Read the `Type' radio buttons. */
	radio = glade_xml_get_widget (state->gui, "type_linear");
	g_return_if_fail (radio != NULL);

	type = gtk_radio_group_get_selected (GTK_RADIO_BUTTON (radio)->group);

	frame = glade_xml_get_widget (state->gui, "frame_date_unit");
	if (type == FillSeriesTypeDate)
		gtk_widget_set_sensitive (frame, TRUE);
	else
		gtk_widget_set_sensitive (frame, FALSE);
}

void
dialog_fill_series (WorkbookControlGUI *wbcg)
{
	FillSeriesState *state;
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView       *sv = wb_control_cur_sheet_view (wbc);
	GtkWidget       *radio;
	GladeXML	*gui;

	Range const *sel;

	g_return_if_fail (wbcg != NULL);

	if (!(sel = selection_first_range (sv, COMMAND_CONTEXT (wbc),
					   _("FillSeries"))))
		return;
	gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg),
		"fill-series.glade", NULL, NULL);
	if (gui == NULL)
		return;

	state = g_new (FillSeriesState, 1);
	state->wbcg  = wbcg;
	state->sel   = sel;
	state->sheet = sv_sheet (sv);
	state->gui   = gui;
	state->dialog = glade_xml_get_widget (state->gui, "Fill_series");
	if (state->dialog == NULL) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Fill Series "
				   "dialog."));
		g_free (state);
		return ;
	}

	state->ok_button = glade_xml_get_widget (state->gui, "okbutton");
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_insert_cell_ok_clicked), state);

	state->cancel_button = glade_xml_get_widget (state->gui,
						     "cancelbutton");
	g_signal_connect (G_OBJECT (state->cancel_button),
		"clicked",
		G_CALLBACK (cb_fill_series_cancel_clicked), state);

	gnumeric_init_help_button
		(glade_xml_get_widget (state->gui, "helpbutton"),
		 "fill-series.html");

	g_signal_connect (G_OBJECT (state->dialog),
			  "destroy",
			  G_CALLBACK (fill_series_destroy), state);

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       INSERT_CELL_DIALOG_KEY);

	/* Set the sensitivity of Unit day. */
	radio = glade_xml_get_widget (state->gui, "type_date");
	g_return_if_fail (radio != NULL);
	g_signal_connect (G_OBJECT (radio), "clicked",
			  G_CALLBACK (cb_type_button_clicked), state);

	radio = glade_xml_get_widget (state->gui, "frame_date_unit");
	g_return_if_fail (radio != NULL);
	gtk_widget_set_sensitive (radio, FALSE);

	gtk_widget_show (state->dialog);
}

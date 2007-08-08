/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-shuffle.c:
 *
 * Authors:
 *        Jukka-Pekka Iivonen <iivonen@iki.fi>
 *        Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000-2003 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2002 by Andreas J. Guelzow <aguelzow@taliesin.ca>
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

#include <sheet.h>
#include <cell.h>
#include <ranges.h>
#include <gui-util.h>
#include <tool-dialogs.h>
#include <dao-gui-utils.h>
#include <value.h>
#include <wbc-gtk.h>
#include <commands.h>
#include <selection.h>

#include <glade/glade.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktogglebutton.h>
#include <widgets/gnumeric-expr-entry.h>
#include "data-shuffling.h"


#define SHUFFLE_KEY         "shuffle-dialog"

typedef GenericToolState ShuffleState;

static char const * const shuffle_by[] = {
	"shuffle_cols",
	"shuffle_rows",
	"shuffle_area",
	NULL
};

/**
 * shuffle_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
shuffle_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
			       ShuffleState *state)
{
        GnmValue *input_range = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The input range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
	} else {
		value_release (input_range);
		gtk_label_set_text (GTK_LABEL (state->warning), "");
		gtk_widget_set_sensitive (state->ok_button, TRUE);
	}
}

/**
 * shuffle_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the data_shuffling.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
shuffle_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button, ShuffleState *state)
{
	data_analysis_output_t  *dao;
	data_shuffling_t        *ds;
	WorkbookControl         *wbc;
	GnmValue                *input;
	int                     type;

	/* This is free'ed by cmd_data_shuffle_finalize. */
	dao = g_new (data_analysis_output_t, 1);

	input = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);

	type = gnumeric_glade_group_value (state->gui, shuffle_by);

	ds = data_shuffling (WORKBOOK_CONTROL (state->wbcg), dao,
			     state->sheet, input, type);

	wbc = WORKBOOK_CONTROL (state->wbcg);
	cmd_data_shuffle (wbc, ds, state->sheet);

	value_release (input);
	gtk_widget_destroy (state->dialog);
}

/**
 * dialog_shuffle:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
void
dialog_shuffle (WBCGtk *wbcg)
{
        ShuffleState    *state;
	WorkbookControl *wbc;
	GtkWidget *w;
	char const *type;
	GnmRange const *r;

	g_return_if_fail (wbcg != NULL);

	wbc = WORKBOOK_CONTROL (wbcg);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, SHUFFLE_KEY))
		return;

	state = g_new (ShuffleState, 1);

	if (dialog_tool_init (state, wbcg, wb_control_cur_sheet (wbc),
			      GNUMERIC_HELP_LINK_DATA_SHUFFLE,
			      "shuffle.glade", "Shuffling",
			      _("Could not create the Data Shuffling dialog."),
			      SHUFFLE_KEY,
			      G_CALLBACK (shuffle_ok_clicked_cb), NULL,
			      G_CALLBACK (shuffle_update_sensitivity_cb),
			      0))
		return;

	shuffle_update_sensitivity_cb (NULL, state);
	state->gdao = NULL;
	tool_load_selection ((GenericToolState *)state, FALSE);

	r = selection_first_range (state->sv, NULL, NULL);
	if (range_width (r) == 1)
		type = "shuffle_cols";
	else if (range_height (r) == 1)
		type = "shuffle_rows";
	else
		type = "shuffle_area";
	w = glade_xml_get_widget (state->gui, type);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);

	gtk_widget_show (state->dialog);

        return;
}

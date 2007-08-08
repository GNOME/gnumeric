/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-advanced-filter.c:
 *
 * Authors:
 *        Jukka-Pekka Iivonen <iivonen@iki.fi>
 *        Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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

#include <glade/glade.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktogglebutton.h>
#include <widgets/gnumeric-expr-entry.h>
#include <widgets/gnm-dao.h>
#include "filter.h"


#define ADVANCED_FILTER_KEY         "advanced-filter-dialog"

typedef GenericToolState AdvancedFilterState;

/**
 * advanced_filter_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity
 **/
static void
advanced_filter_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				       AdvancedFilterState *state)
{
        GnmValue *input_range    = NULL;
        GnmValue *criteria_range = NULL;

        input_range = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The list range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	} else
		value_release (input_range);

	criteria_range =  gnm_expr_entry_parse_as_value
		(state->input_entry_2, state->sheet);
	if (criteria_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The criteria range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	} else
		value_release (criteria_range);

	if (!gnm_dao_is_ready (GNM_DAO (state->gdao))) {
		gtk_label_set_text (GTK_LABEL (state->warning),
				    _("The output range is invalid."));
		gtk_widget_set_sensitive (state->ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->warning), "");
	gtk_widget_set_sensitive (state->ok_button, TRUE);
	return;
}

/**
 * advanced_filter_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the advanced_filter.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
advanced_filter_ok_clicked_cb (G_GNUC_UNUSED GtkWidget *button,
			       AdvancedFilterState *state)
{
	data_analysis_output_t  dao;
	GnmValue                   *input;
	GnmValue                   *criteria;
	char                    *text;
	GtkWidget               *w;
	int                     err = 0;
	gboolean                unique;

	input = gnm_expr_entry_parse_as_value (
		GNM_EXPR_ENTRY (state->input_entry), state->sheet);

	criteria = gnm_expr_entry_parse_as_value
		(state->input_entry_2, state->sheet);

        parse_output ((GenericToolState *) state, &dao);

	w = glade_xml_get_widget (state->gui, "unique-button");
	unique = (1 == gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)));

	err = advanced_filter (WORKBOOK_CONTROL (state->wbcg),
			       &dao, input, criteria, unique);

	value_release (input);
	value_release (criteria);

	switch (err) {
	case OK:
		gtk_widget_destroy (state->dialog);
		break;
	case ERR_INVALID_FIELD:
		error_in_entry ((GenericToolState *) state,
				GTK_WIDGET (state->input_entry_2),
				_("The given criteria are invalid."));
		break;
	case NO_RECORDS_FOUND:
		go_gtk_notice_nonmodal_dialog ((GtkWindow *) state->dialog,
					  &(state->warning_dialog),
					  GTK_MESSAGE_INFO,
					  _("No matching records were found."));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: "
					  "%d."), err);
		error_in_entry ((GenericToolState *) state,
				GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
	}
	return;
}

/**
 * dialog_advanced_filter:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
void
dialog_advanced_filter (WBCGtk *wbcg)
{
        AdvancedFilterState *state;
	WorkbookControl *wbc;

	g_return_if_fail (wbcg != NULL);

	wbc = WORKBOOK_CONTROL (wbcg);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, ADVANCED_FILTER_KEY))
		return;

	state = g_new (AdvancedFilterState, 1);

	if (dialog_tool_init (state, wbcg, wb_control_cur_sheet (wbc),
			      GNUMERIC_HELP_LINK_ADVANCED_FILTER,
			      "advanced-filter.glade", "Filter",
			      _("Could not create the Advanced Filter dialog."),
			      ADVANCED_FILTER_KEY,
			      G_CALLBACK (advanced_filter_ok_clicked_cb), NULL,
			      G_CALLBACK (advanced_filter_update_sensitivity_cb),
			      0))
		return;

	gnm_dao_set_inplace (GNM_DAO (state->gdao), _("Filter _in-place"));
	gnm_dao_set_put (GNM_DAO (state->gdao), FALSE, FALSE);
	advanced_filter_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

        return;
}

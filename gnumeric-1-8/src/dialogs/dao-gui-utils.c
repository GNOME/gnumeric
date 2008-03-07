/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dao-gui-utils.c:
 *
 * Authors:
 *   Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *   Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2001, 2002 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
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
#include "dao-gui-utils.h"

#include "value.h"
#include "gui-util.h"
#include "selection.h"
#include <widgets/gnm-dao.h>

#include <gtk/gtktogglebutton.h>
#include <gtk/gtknotebook.h>
#include <gtk/gtktable.h>
#include <gtk/gtkbox.h>

char const * const output_group[] = {
	"newsheet-button",
	"newworkbook-button",
	"outputrange-button",
	"inplace-button",          /* used only in advanced filter  */
	NULL
};

/**
 * dialog__tool_preset_to_range:
 * @state:
 *
 * Load selection into range and switch to output range
 *
 **/

void
dialog_tool_preset_to_range (GenericToolState *state)
{
	GnmRange const *sel;
	GtkWidget *w;

	g_return_if_fail (state != NULL);
	g_return_if_fail (state->gdao != NULL);

	sel = selection_first_range (state->sv, NULL, NULL);
	gnm_dao_load_range (GNM_DAO (state->gdao), sel);
	gnm_dao_focus_output_range (GNM_DAO (state->gdao));

	w = glade_xml_get_widget (state->gui, "notebook1");
	g_return_if_fail (w && GTK_IS_NOTEBOOK (w));
	gtk_notebook_set_current_page (GTK_NOTEBOOK(w), 0);
}


/**
 * dialog_tool_init_outputs:
 * @state:
 * @sensitivity_cb:
 *
 * Setup the standard output information
 *
 **/
void
dialog_tool_init_outputs (GenericToolState *state, GtkSignalFunc sensitivity_cb)
{
	GtkWidget *dao_box;

	dao_box = glade_xml_get_widget (state->gui, "dao");

	if (dao_box == NULL) {
		state->gdao = NULL;
		return;
	}

	state->gdao = gnm_dao_new (state->wbcg, NULL);
	gtk_box_pack_start (GTK_BOX (dao_box), state->gdao,
			    TRUE, TRUE, 0);
	gtk_widget_show (state->gdao);
	g_signal_connect_after (G_OBJECT (state->gdao),
				"readiness-changed",
				G_CALLBACK (sensitivity_cb), state);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->gdao));

	return;
}

/**
 * parse_output:
 *
 * @state:
 * @dao:
 *
 * fill dao with information from the standard output section of a dialog
 */
data_analysis_output_t *
parse_output (GenericToolState *state, data_analysis_output_t *dao)
{
	data_analysis_output_t *this_dao = dao;

	gnm_dao_get_data (GNM_DAO (state->gdao), &this_dao);
	if (this_dao->type == InPlaceOutput) {
		GnmValue *output_range
			= gnm_expr_entry_parse_as_value (
				state->input_entry, state->sheet);
		dao_load_from_value (this_dao, output_range);
		value_release (output_range);
	}
	return this_dao;
}

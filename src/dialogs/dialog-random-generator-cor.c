/*
 * dialog-random-generator-cor.c:
 *
 * Authors:
 *  Andreas J. Guelzow  <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2009  Andreas J. Guelzow  <aguelzow@pyrshep.ca>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>
#include <dialogs/tool-dialogs.h>
#include <tools/random-generator-cor.h>

#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <gnm-format.h>
#include <dialogs/dao-gui-utils.h>
#include <sheet.h>
#include <expr.h>
#include <number-match.h>
#include <ranges.h>
#include <selection.h>
#include <value.h>
#include <commands.h>

#include <widgets/gnm-expr-entry.h>
#include <widgets/gnm-dao.h>

#include <string.h>


/**********************************************/
/*  Generic guru items */
/**********************************************/


#define RANDOM_COR_KEY            "analysistools-random-cor-dialog"

static char const * const matrix_group[] = {
	"cov-button",
	"cholesky-button",
	NULL
};

typedef struct {
	GnmGenericToolState base;
	GtkWidget *count_entry;
} RandomCorToolState;


/**********************************************/
/*  Begin of random tool code */
/**********************************************/


/**
 * random_cor_tool_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are the standard input (one range) and output items.
 **/
static void
random_cor_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				       RandomCorToolState *state)
{
        GnmValue *input_range;
	gint height, width, count;

	input_range = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry), state->base.sheet);
	if (input_range == NULL) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The matrix range is not valid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	height = input_range->v_range.cell.b.row - input_range->v_range.cell.a.row;
	width  = input_range->v_range.cell.b.col - input_range->v_range.cell.a.col;
	value_release (input_range);

	if (height != width || height == 0) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The matrix must be symmetric positive-definite."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

        if (!gnm_dao_is_ready (GNM_DAO (state->base.gdao))) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The output specification "
				      "is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	if (entry_to_int (GTK_ENTRY (state->count_entry), &count, FALSE) != 0 ||
	    count <= 0) {
		gtk_label_set_text (GTK_LABEL (state->base.warning),
				    _("The number of random numbers requested is invalid."));
		gtk_widget_set_sensitive (state->base.ok_button, FALSE);
		return;
	}

	gtk_label_set_text (GTK_LABEL (state->base.warning), "");
	gtk_widget_set_sensitive (state->base.ok_button, TRUE);
}

/**
 * random_cor_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the appropriate tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
random_cor_tool_ok_clicked_cb (GtkWidget *button, RandomCorToolState *state)
{
	data_analysis_output_t  *dao;
	tools_data_random_cor_t  *data;

	data = g_new0 (tools_data_random_cor_t, 1);

	dao  = parse_output ((GnmGenericToolState *)state, NULL);
	(void)entry_to_int (GTK_ENTRY (state->count_entry), &data->count, FALSE);
	data->matrix = gnm_expr_entry_parse_as_value
		(GNM_EXPR_ENTRY (state->base.input_entry),
		 state->base.sheet);

	data->variables = data->matrix->v_range.cell.b.row -
		data->matrix->v_range.cell.a.row + 1;

	data->matrix_type = gnm_gui_group_value
		(state->base.gui, matrix_group);


	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, data, tool_random_cor_engine, TRUE) &&
	    (button == state->base.ok_button))
		gtk_widget_destroy (state->base.dialog);
}


/**
 * dialog_random_cor_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static void
dialog_random_cor_tool_init (RandomCorToolState *state)
{
	state->count_entry = go_gtk_builder_get_widget (state->base.gui, "count_entry");
	int_to_entry (GTK_ENTRY (state->count_entry), 2);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->count_entry));
	g_signal_connect_after (G_OBJECT (state->count_entry),
				"changed",
				G_CALLBACK (random_cor_tool_update_sensitivity_cb), state);
}


/**
 * dialog_random_cor_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_random_cor_tool (WBCGtk *wbcg, Sheet *sheet)
{
        RandomCorToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, RANDOM_COR_KEY)) {
		return 0;
	}

	state = g_new (RandomCorToolState, 1);

	if (dialog_tool_init ((GnmGenericToolState *)state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_RANDOM_GENERATOR_COR,
			      "res:ui/random-generation-cor.ui", "CorRandom",
			      _("Could not create the Correlated Random Tool dialog."),
			      RANDOM_COR_KEY,
			      G_CALLBACK (random_cor_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (random_cor_tool_update_sensitivity_cb),
			      0))
		return 0;


	gnm_dao_set_put (GNM_DAO (state->base.gdao), TRUE, TRUE);
	dialog_random_cor_tool_init (state);

	tool_load_selection ((GnmGenericToolState *)state, TRUE);

	gtk_widget_show_all (state->base.dialog);

        return 0;
}

/*
 * dialog-paste-special.c: The dialog for selecting non-standard
 *    behaviors when pasting.
 *
 * Author:
 *  MIguel de Icaza (miguel@gnu.org)
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

#include <wbc-gtk.h>
#include <gui-util.h>
#include <clipboard.h>
#include <selection.h>
#include <cmd-edit.h>


static char const * const paste_type_group[] = {
	"paste-type-all",
	"paste-type-content",
	"paste-type-as-value",
	"paste-type-formats",
	"paste-type-comments",
	NULL
};
static const struct {
	gboolean permit_cell_ops;
	int paste_enum;
} paste_type_group_props[] = {
	{TRUE, PASTE_ALL_CELL},
	{TRUE, PASTE_CONTENTS},
	{TRUE, PASTE_AS_VALUES},
	{FALSE, PASTE_FORMATS},
	{FALSE, PASTE_COMMENTS},
};
static char const * const cell_operation_group[] = {
	"cell-operation-none",
	"cell-operation-add",
	"cell-operation-subtract",
	"cell-operation-multiply",
	"cell-operation-divide",
	NULL
};
static const struct {
	int paste_enum;
} cell_operation_props[] = {
	{0},
	{PASTE_OPER_ADD},
	{PASTE_OPER_SUB},
	{PASTE_OPER_MULT},
	{PASTE_OPER_DIV},
};
static char const * const region_operation_group[] = {
	"region-operation-none",
	"region-operation-transpose",
	"region-operation-flip-h",
	"region-operation-flip-v",
	NULL
};
static const struct {
	int paste_enum;
} region_operation_props[] = {
	{0},
	{PASTE_TRANSPOSE},
	{PASTE_FLIP_H},
	{PASTE_FLIP_V},
};

typedef struct {
	GtkBuilder *gui;
	GtkWidget *dialog;
	GtkWidget *ok_button;
	GtkWidget *cancel_button;
	GtkWidget *link_button;
	GtkWidget *help_button;
	char const *help_link;
	Sheet	  *sheet;
	SheetView *sv;
	Workbook  *wb;
	WBCGtk  *wbcg;
} PasteSpecialState;

#define GNM_PASTE_SPECIAL_KEY	"gnm-paste-special"

/* The "Paste Link" button should be grayed-out, unless type "All" is
   selected, cell operation "None" is selected,
   region operation "None" is selected, and "Skip
   Blanks" is not selected.  */
static void
paste_link_set_sensitive (PasteSpecialState *state)
{
	gboolean sensitive =
		(!gtk_toggle_button_get_active
		 (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui,"skip-blanks")))
		 && 0 == gnm_gui_group_value (state->gui, paste_type_group)
		 && 0 == gnm_gui_group_value (state->gui, cell_operation_group)
		 && 0 == gnm_gui_group_value (state->gui, region_operation_group));
	gtk_widget_set_sensitive (state->link_button, sensitive);
}

static void
skip_blanks_set_sensitive (PasteSpecialState *state)
{
	GtkWidget *button = go_gtk_builder_get_widget (state->gui,"skip-blanks");
	gboolean sensitive =
		(3 > gnm_gui_group_value (state->gui, paste_type_group)
		 && 0 == gnm_gui_group_value (state->gui, cell_operation_group));
	gtk_widget_set_sensitive (button, sensitive);
}

static void
dont_change_formulae_set_sensitive (PasteSpecialState *state)
{
	GtkWidget *button = go_gtk_builder_get_widget (state->gui,"dont-change-formulae");
	gboolean sensitive =
		(2 > gnm_gui_group_value (state->gui, paste_type_group)
		 && 0 == gnm_gui_group_value (state->gui, cell_operation_group));
	gtk_widget_set_sensitive (button, sensitive);
}

static void
cb_destroy (PasteSpecialState *state)
{
	if (state->gui != NULL)
		g_object_unref (state->gui);
	wbcg_edit_finish (state->wbcg, WBC_EDIT_REJECT, NULL);
	g_free (state);
}

static void
dialog_paste_special_type_toggled_cb (GtkWidget *button, PasteSpecialState *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		int i = gnm_gui_group_value (state->gui, paste_type_group);
		char const * const *group;
		gboolean permit_cell_ops = paste_type_group_props[i].permit_cell_ops;

		for (group = cell_operation_group; *group != NULL; group++)
			gtk_widget_set_sensitive (go_gtk_builder_get_widget (state->gui,*group),
						  permit_cell_ops);
		paste_link_set_sensitive (state);
		skip_blanks_set_sensitive (state);
		dont_change_formulae_set_sensitive (state);
	}
}

static void
dialog_paste_special_cell_op_toggled_cb (GtkWidget *button, PasteSpecialState *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		paste_link_set_sensitive (state);
		skip_blanks_set_sensitive (state);
		dont_change_formulae_set_sensitive (state);
	}
}

static void
dialog_paste_special_region_op_toggled_cb (GtkWidget *button, PasteSpecialState *state)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		paste_link_set_sensitive (state);
	}
}
static void
dialog_paste_special_skip_blanks_toggled_cb (GtkWidget *button, PasteSpecialState *state)
{
		paste_link_set_sensitive (state);
}

static void
cb_tool_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
			PasteSpecialState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_tool_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
			PasteSpecialState *state)
{
	int result;
	int paste_type = gnm_gui_group_value (state->gui, paste_type_group);
	int region_op_type = gnm_gui_group_value (state->gui, region_operation_group);

	result = paste_type_group_props[paste_type].paste_enum
		| region_operation_props[region_op_type].paste_enum;

	if (paste_type_group_props[paste_type].permit_cell_ops) {
		int cell_op_type = gnm_gui_group_value (state->gui, cell_operation_group);
		result |= cell_operation_props[cell_op_type].paste_enum;
	}

	if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui,"skip-blanks"))))
		result |= PASTE_SKIP_BLANKS;
	if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui,"dont-change-formulae"))))
		result |= PASTE_EXPR_LOCAL_RELOCATE;

	if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui,"row-heights"))))
		result |= PASTE_ROW_HEIGHTS;
	if (gtk_toggle_button_get_active
	    (GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui,"column-widths"))))
		result |= PASTE_COLUMN_WIDTHS;

	cmd_paste_to_selection (GNM_WBC (state->wbcg), state->sv, result);
	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_tool_paste_link_clicked (G_GNUC_UNUSED GtkWidget *button,
			PasteSpecialState *state)
{
	cmd_paste_to_selection (GNM_WBC (state->wbcg), state->sv, PASTE_LINK);
	gtk_widget_destroy (state->dialog);
	return;
}

void
dialog_paste_special (WBCGtk *wbcg)
{
	PasteSpecialState *state;
	GtkBuilder *gui;
	char const * const *group;

	if (gnm_dialog_raise_if_exists (wbcg, GNM_PASTE_SPECIAL_KEY))
		return;
	gui = gnm_gtk_builder_load ("res:ui/paste-special.ui", NULL, GO_CMD_CONTEXT (wbcg));
	if (gui == NULL)
		return;

	state = g_new0 (PasteSpecialState, 1);
	state->wbcg   = wbcg;
	state->gui    = gui;
	state->dialog =  go_gtk_builder_get_widget (state->gui, "paste-special");
	state->sheet = wbcg_cur_sheet (wbcg);
	state->sv = wb_control_cur_sheet_view (GNM_WBC (wbcg));

	g_return_if_fail (state->dialog != NULL);

	state->link_button = go_gtk_builder_get_widget (state->gui,"paste-link_button");
	g_signal_connect (G_OBJECT (state->link_button),
			  "clicked",
			  G_CALLBACK (cb_tool_paste_link_clicked), state);
	state->help_button = go_gtk_builder_get_widget (state->gui, "help_button");
	gnm_init_help_button (state->help_button, GNUMERIC_HELP_LINK_PASTE_SPECIAL);
	state->cancel_button = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (state->cancel_button),
			  "clicked",
			  G_CALLBACK (cb_tool_cancel_clicked), state);
	state->ok_button = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (state->ok_button),
			  "clicked",
			  G_CALLBACK (cb_tool_ok_clicked), state);


	for (group = paste_type_group; *group != NULL; group++)
		g_signal_connect_after (go_gtk_builder_get_widget (state->gui,*group),
					"toggled",
					G_CALLBACK (dialog_paste_special_type_toggled_cb), state);
	for (group = cell_operation_group; *group != NULL; group++)
		g_signal_connect_after (go_gtk_builder_get_widget (state->gui,*group),
					"toggled",
					G_CALLBACK (dialog_paste_special_cell_op_toggled_cb), state);
	for (group = region_operation_group; *group != NULL; group++)
		g_signal_connect_after (go_gtk_builder_get_widget (state->gui,*group),
					"toggled",
					G_CALLBACK (dialog_paste_special_region_op_toggled_cb), state);
	g_signal_connect_after (go_gtk_builder_get_widget (state->gui, "skip-blanks"),
				"toggled",
				G_CALLBACK (dialog_paste_special_skip_blanks_toggled_cb), state);
	paste_link_set_sensitive (state);

	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui,"column-widths")),
		 sv_is_full_colrow_selected (state->sv, TRUE, -1));
	gtk_toggle_button_set_active
		(GTK_TOGGLE_BUTTON (go_gtk_builder_get_widget (state->gui,"row-heights")),
		 sv_is_full_colrow_selected (state->sv, FALSE, -1));

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog), wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	gnm_keyed_dialog (wbcg, GTK_WINDOW (state->dialog),
			       GNM_PASTE_SPECIAL_KEY);

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
				"state", state,
				(GDestroyNotify) cb_destroy);

	gtk_widget_show (state->dialog);
}

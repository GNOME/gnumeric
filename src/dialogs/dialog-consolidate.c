/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-consolidate.c : implementation of the consolidation dialog.
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
 * Copyright (C) Andreas J. Guelzow <aguelzow@taliesin.ca>
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

#include <commands.h>
#include <consolidate.h>
#include <func.h>
#include <gui-util.h>
#include <ranges.h>
#include <sheet-view.h>
#include <selection.h>
#include <widgets/gnumeric-expr-entry.h>
#include <workbook-edit.h>

typedef struct {
	WorkbookControlGUI *wbcg;
	SheetView	   *sv;
	Sheet              *sheet;
	GladeXML           *glade_gui;
	GtkWidget          *warning_dialog;

	struct {
		GtkDialog       *dialog;

		GtkOptionMenu     *function;
		GtkOptionMenu     *put;
		GnumericExprEntry *source;

		GnumericExprEntry *destination;
		GtkButton         *add;
		GtkCList          *areas;
		GtkButton         *clear;
		GtkButton         *delete;

		GtkCheckButton    *labels_row;
		GtkCheckButton    *labels_col;
		GtkCheckButton    *labels_copy;

		GtkButton         *btn_ok;
		GtkButton         *btn_cancel;
	} gui;

	int                        areas_index;     /* Select index in sources clist */
	char                      *construct_error; /* If set an error occurred in construct_consolidate */
} ConsolidateState;

/**
 * construct_consolidate:
 *
 * Builts a new Consolidate structure from the
 * state of all the widgets in the dialog, this can
 * be used to actually "execute" a consolidation
 **/
static Consolidate *
construct_consolidate (ConsolidateState *state)
{
	Consolidate      *cs   = consolidate_new ();
	ConsolidateMode  mode = 0;
	const char       *func;
	Value            *range_value;
	int              i;

	switch (gtk_option_menu_get_history (state->gui.function)) {
	case 0 : func = "SUM"; break;
	case 1 : func = "MIN"; break;
	case 2 : func = "MAX"; break;
	case 3 : func = "AVERAGE"; break;
	case 4 : func = "COUNT"; break;
	case 5 : func = "PRODUCT"; break;
	case 6 : func = "STDEV"; break;
	case 7 : func = "STDEVP"; break;
	case 8 : func = "VAR"; break;
	case 9 : func = "VARP"; break;
	default :
		func = NULL;
		g_warning ("Unknown function index!");
	}

	consolidate_set_function (cs, gnm_func_lookup (func, NULL));

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_row)))
		mode |= CONSOLIDATE_COL_LABELS;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_col)))
		mode |= CONSOLIDATE_ROW_LABELS;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_copy)))
		mode |= CONSOLIDATE_COPY_LABELS;
	if (gtk_option_menu_get_history (state->gui.put) == 0)
		mode |= CONSOLIDATE_PUT_VALUES;

	consolidate_set_mode (cs, mode);

	range_value = gnm_expr_entry_parse_as_value
		(GNUMERIC_EXPR_ENTRY (state->gui.destination), state->sheet);
	g_return_val_if_fail (range_value != NULL, NULL);

	if (!consolidate_set_destination (cs, range_value)) {
		g_warning ("Error while setting destination! This should not happen");
		consolidate_free (cs);
		return NULL;
	}

	g_return_val_if_fail (state->gui.areas->rows > 0, NULL);

	for (i = 0; i < state->gui.areas->rows; i++) {
		char *tmp[1];

		gtk_clist_get_text (state->gui.areas, i, 0, tmp);
		range_value = global_range_parse (state->sheet, tmp[0]);
		g_return_val_if_fail (range_value != NULL, NULL);

		if (!consolidate_add_source (cs, range_value)) {
			state->construct_error = g_strdup_printf (
				_("Source region %s overlaps with the destination region"),
				tmp[0]);
			consolidate_free (cs);
			return NULL;
		}
	}

	return cs;
}

/***************************************************************************************/

/**
 * dialog_set_button_sensitivity:
 * @dummy:
 * @state:
 *
 **/
static void
dialog_set_button_sensitivity (G_GNUC_UNUSED GtkWidget *dummy,
			       ConsolidateState *state)
{
	gboolean ready = FALSE;

	ready = gnm_expr_entry_is_cell_ref (state->gui.destination, state->sheet, TRUE)
		&& (state->gui.areas->rows > 0);
	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.btn_ok), ready);
	return;
}

static void
cb_dialog_destroy (ConsolidateState *state)
{
	wbcg_edit_detach_guru (state->wbcg);

	g_object_unref (G_OBJECT (state->glade_gui));

	if (state->construct_error) {
		g_warning ("The construct error was not freed, this should not happen!");
		g_free (state->construct_error);
	}
	g_free (state);
}

static void
cb_dialog_clicked (GtkWidget *widget, ConsolidateState *state)
{
	if (widget == GTK_WIDGET (state->gui.btn_ok)) {
		Consolidate *cs;

		if (state->warning_dialog != NULL)
			gtk_widget_destroy (state->warning_dialog);

		cs = construct_consolidate (state);

		/*
		 * If something went wrong consolidate_construct
		 * return NULL and sets the state->construct_error to
		 * a suitable error message
		 */
		if (cs == NULL) {
			gnumeric_notice_nonmodal (GTK_WINDOW (state->gui.dialog),
						  &state->warning_dialog,
						  GTK_MESSAGE_ERROR,
						  state->construct_error);
			g_free (state->construct_error);
			state->construct_error = NULL;

			return;
		}

		cmd_consolidate (WORKBOOK_CONTROL (state->wbcg), cs);
	}

	gtk_widget_destroy (GTK_WIDGET (state->gui.dialog));
}

static void
cb_areas_select_row (G_GNUC_UNUSED GtkCList *clist,
		     int row,
		     G_GNUC_UNUSED int column,
		     G_GNUC_UNUSED GdkEventButton *event,
		     ConsolidateState *state)
{
	g_return_if_fail (state != NULL);

	state->areas_index = row;
	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.delete), TRUE);
}

static void
cb_source_changed (G_GNUC_UNUSED GtkEntry *ignored,
		   ConsolidateState *state)
{
	g_return_if_fail (state != NULL);

	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.add),
				  gnm_expr_entry_is_cell_ref (state->gui.source, 
							      state->sheet, TRUE));
}

static void
cb_add_clicked (G_GNUC_UNUSED GtkButton *button,
		ConsolidateState *state)
{
	char *text[1];
	int i, exists = -1;

	g_return_if_fail (state != NULL);

	/*
	 * Add the newly added range, we have to make sure
	 * that the range doesn't already exist, in such cases we
	 * simply select the existing entry
	 */

	text[0] = gnm_expr_entry_global_range_name (state->gui.source, state->sheet);
	g_return_if_fail (text != NULL);

	for (i = 0; i < state->gui.areas->rows; i++) {
		char *t[1];

		gtk_clist_get_text (state->gui.areas, i, 0, t);
		if (strcmp (t[0], text[0]) == 0)
			exists = i;
	}

	if (exists == -1) {
		exists = gtk_clist_append (state->gui.areas, text);
		gtk_widget_grab_focus (GTK_WIDGET (state->gui.source));
	}

	gtk_clist_select_row (state->gui.areas, exists, 0);
	gtk_clist_moveto (state->gui.areas, exists, 0, 0.5, 0.5);

	g_free (text[0]);

	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.clear), TRUE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.delete), TRUE);

	dialog_set_button_sensitivity (NULL, state);
}

static void
cb_clear_clicked (G_GNUC_UNUSED GtkButton *button,
		  ConsolidateState *state)
{
	g_return_if_fail (state != NULL);

	gtk_clist_clear (state->gui.areas);

	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.clear), FALSE);
	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.delete), FALSE);

	dialog_set_button_sensitivity (NULL, state);
}

static void
cb_delete_clicked (G_GNUC_UNUSED GtkButton *button,
		   ConsolidateState *state)
{
	g_return_if_fail (state != NULL);

	if (state->areas_index == -1)
		return;

	gtk_clist_remove (state->gui.areas, state->areas_index);

	if (state->gui.areas->rows < 1) {
		gtk_widget_set_sensitive (GTK_WIDGET (state->gui.clear), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (state->gui.delete), FALSE);
		state->areas_index = -1;
	} else
		gtk_clist_select_row (state->gui.areas, state->gui.areas->rows - 1, 0);

	dialog_set_button_sensitivity (NULL, state);
}

static void
cb_labels_toggled (G_GNUC_UNUSED GtkCheckButton *button,
		   ConsolidateState *state)
{
	gboolean copy_labels =
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_row)) ||
		gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->gui.labels_col));

	gtk_widget_set_sensitive (GTK_WIDGET (state->gui.labels_copy), copy_labels);
	if (!copy_labels)
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->gui.labels_copy), FALSE);
}

/***************************************************************************************/

static void
connect_signal_labels_toggled (ConsolidateState *state, GtkCheckButton *button)
{
	g_signal_connect (G_OBJECT (button),
		"toggled",
		G_CALLBACK (cb_labels_toggled), state);
}

static void
connect_signal_btn_clicked (ConsolidateState *state, GtkButton *button)
{
	g_signal_connect (G_OBJECT (button),
		"clicked",
		G_CALLBACK (cb_dialog_clicked), state);
}

static void
setup_widgets (ConsolidateState *state, GladeXML *glade_gui)
{
	GnumericExprEntryFlags flags;

	state->gui.dialog      = GTK_DIALOG        (glade_xml_get_widget (glade_gui, "dialog"));

	state->gui.function    = GTK_OPTION_MENU     (glade_xml_get_widget (glade_gui, "function"));
	state->gui.put         = GTK_OPTION_MENU     (glade_xml_get_widget (glade_gui, "put"));

	state->gui.destination = gnumeric_expr_entry_new (state->wbcg, TRUE);
	state->gui.source      = gnumeric_expr_entry_new (state->wbcg, TRUE);

	state->gui.add         = GTK_BUTTON          (glade_xml_get_widget (glade_gui, "add"));
	state->gui.areas       = GTK_CLIST           (glade_xml_get_widget (glade_gui, "areas"));
	state->gui.clear       = GTK_BUTTON          (glade_xml_get_widget (glade_gui, "clear"));
	state->gui.delete      = GTK_BUTTON          (glade_xml_get_widget (glade_gui, "delete"));

	state->gui.labels_row  = GTK_CHECK_BUTTON (glade_xml_get_widget (glade_gui, "labels_row"));
	state->gui.labels_col  = GTK_CHECK_BUTTON (glade_xml_get_widget (glade_gui, "labels_col"));
	state->gui.labels_copy = GTK_CHECK_BUTTON (glade_xml_get_widget (glade_gui, "labels_copy"));

	state->gui.btn_ok     = GTK_BUTTON  (glade_xml_get_widget (glade_gui, "btn_ok"));
	state->gui.btn_cancel = GTK_BUTTON  (glade_xml_get_widget (glade_gui, "btn_cancel"));

	gtk_table_attach (GTK_TABLE (glade_xml_get_widget (glade_gui, "table1")),
			  GTK_WIDGET (state->gui.destination),
			  1, 2, 2, 3,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_box_pack_start (GTK_BOX (glade_xml_get_widget (glade_gui, "hbox2")),
			    GTK_WIDGET (state->gui.source),
			    TRUE, TRUE, 0);
	gtk_widget_show (GTK_WIDGET (state->gui.destination));
	gtk_widget_show (GTK_WIDGET (state->gui.source));

	gnm_expr_entry_set_scg (state->gui.destination, wbcg_cur_scg (state->wbcg));
	gnm_expr_entry_set_scg (state->gui.source, wbcg_cur_scg (state->wbcg));

	flags = GNM_EE_SINGLE_RANGE;
	gnm_expr_entry_set_flags (state->gui.destination, flags, flags);
	gnm_expr_entry_set_flags (state->gui.source, flags, flags);

	gnumeric_editable_enters (GTK_WINDOW (state->gui.dialog),
				  GTK_WIDGET (state->gui.destination));
 	gnumeric_editable_enters (GTK_WINDOW (state->gui.dialog),
				  GTK_WIDGET (state->gui.source));

	g_signal_connect (G_OBJECT (state->gui.destination),
		"changed",
		G_CALLBACK (dialog_set_button_sensitivity), state);
	g_signal_connect (G_OBJECT (state->gui.areas),
		"select_row",
		G_CALLBACK (cb_areas_select_row), state);
	g_signal_connect (G_OBJECT (state->gui.source),
		"changed",
		G_CALLBACK (cb_source_changed), state);
	g_signal_connect (G_OBJECT (state->gui.add),
		"clicked",
		G_CALLBACK (cb_add_clicked), state);
	g_signal_connect (G_OBJECT (state->gui.clear),
		"clicked",
		G_CALLBACK (cb_clear_clicked), state);
	g_signal_connect (G_OBJECT (state->gui.delete),
		"clicked",
		G_CALLBACK (cb_delete_clicked), state);

	connect_signal_labels_toggled (state, state->gui.labels_row);
	connect_signal_labels_toggled (state, state->gui.labels_col);
	connect_signal_labels_toggled (state, state->gui.labels_copy);

	connect_signal_btn_clicked (state, state->gui.btn_ok);
	connect_signal_btn_clicked (state, state->gui.btn_cancel);

	/* FIXME: that's not the proper help location */
	gnumeric_init_help_button (
		glade_xml_get_widget (glade_gui, "btn_help"),
		"data-menu.html");
}

static gboolean
cb_add_source_area (SheetView *sv, Range const *range, gpointer user_data)
{
	ConsolidateState *state = user_data;
	char const *name = global_range_name (sv_sheet (sv), range);
	char const *t[2];

	t[0] = name;
	t[1] = NULL;

	gtk_clist_append (state->gui.areas, (char **) t);
	return TRUE;
}

void
dialog_consolidate (WorkbookControlGUI *wbcg)
{
	GladeXML *glade_gui;
	ConsolidateState *state;
	Range const *r = NULL;

	g_return_if_fail (wbcg != NULL);

	glade_gui = gnm_glade_xml_new (COMMAND_CONTEXT (wbcg),
		"consolidate.glade", NULL, NULL);
        if (glade_gui == NULL)
                return;

	/* Primary static initialization */
	state = g_new0 (ConsolidateState, 1);
	state->wbcg        = wbcg;
	state->sv	   = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));
	state->sheet	   = sv_sheet (state->sv);
	state->glade_gui   = glade_gui;
	state->warning_dialog = NULL;
	state->areas_index = -1;

	setup_widgets (state, glade_gui);

	/* Dynamic initialization */
	cb_source_changed (NULL, state);
	cb_clear_clicked  (state->gui.clear, state);
	cb_labels_toggled (state->gui.labels_row, state);

	/*
	 * When there are non-singleton selections add them all to the
	 * source range list for convenience
	 */
	if ((r = selection_first_range (state->sv, NULL, NULL)) != NULL && !range_is_singleton (r)) {
		selection_foreach_range (state->sv, TRUE, cb_add_source_area, state);
		gtk_clist_select_row (state->gui.areas, state->gui.areas->rows - 1, 0);
		gtk_clist_moveto (state->gui.areas, state->gui.areas->rows - 1, 0, 0.5, 0.5);
		gtk_widget_set_sensitive (GTK_WIDGET (state->gui.clear), TRUE);
	}

	gtk_widget_grab_focus   (GTK_WIDGET (state->gui.function));
	gtk_widget_grab_default (GTK_WIDGET (state->gui.btn_ok));

	/* a candidate for merging into attach guru */
	g_object_set_data_full (G_OBJECT (state->gui.dialog),
		"state", state, (GDestroyNotify) cb_dialog_destroy);
	wbcg_edit_attach_guru (state->wbcg, GTK_WIDGET (state->gui.dialog));
	gnumeric_non_modal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->gui.dialog));
	gtk_widget_show (GTK_WIDGET (state->gui.dialog));
}

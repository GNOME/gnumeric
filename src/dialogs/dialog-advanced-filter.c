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
 **/
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <sheet.h>
#include <cell.h>
#include <ranges.h>
#include <func-util.h>
#include <gui-util.h>
#include <analysis-tools.h>
#include <value.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <widgets/gnumeric-expr-entry.h>

#define OK               0
#define N_COLUMNS_ERROR  1
#define ERR_INVALID_FIELD  2

#define ADVANCED_FILTER_KEY         "advanced-filter-dialog"

typedef struct {
	GENERIC_TOOL_STATE
} AdvancedFilterState;

static void
free_rows (GSList *row_list)
{
	GSList *list;

	for (list = row_list; list != NULL; list = list->next)
	        g_free (list->data);
	g_slist_free (row_list);
}


static void
filter (data_analysis_output_t *dao, Sheet *sheet, GSList *rows, 
	gint input_col_b, gint input_col_e, gint input_row_b, gint input_row_e)
{
        Cell *cell;
	int  i, r=0;

	if (dao->type == InPlaceOutput) {
		colrow_set_visibility (sheet, FALSE,
						    FALSE, input_row_b+1, input_row_e);
		while (rows != NULL) {
			gint *row = (gint *) rows->data;
			colrow_set_visibility (sheet, FALSE,
						    TRUE, *row, *row);
			rows = rows->next;
		}
		sheet_redraw_all (sheet, TRUE);
/* FIXME: what happens if we just have hidden the selection? */	
	
	} else {
		for (i=input_col_b; i<=input_col_e; i++) {
			cell = sheet_cell_get (sheet, i, input_row_b);
			if (cell == NULL)
				set_cell (dao, i - input_col_b, r, NULL);
			else {
				Value *value = value_duplicate (cell->value);
				set_cell_value (dao, i - input_col_b, r, value);
			}
		}
		++r;
		
		while (rows != NULL) {
			gint *row = (gint *) rows->data;
			for (i=input_col_b; i<=input_col_e; i++) {
				cell = sheet_cell_get (sheet, i, *row);
				if (cell == NULL)
					set_cell (dao, i - input_col_b, r, NULL);
				else {
					Value *value = value_duplicate (cell->value);
					set_cell_value (dao, i - input_col_b, r, value);
				}
			}
			++r;
			rows = rows->next;
		}
	} 
}

/* Filter tool.
 */
static gint
advanced_filter (WorkbookControl *wbc,
		 data_analysis_output_t   *dao,
		 Value *database, Value *criteria,
		 gboolean unique_only_flag)
{
        GSList *crit, *rows;
	EvalPos ep = {{0, 0}, wb_control_cur_sheet (wbc)};

	crit = parse_database_criteria (&ep, database, criteria);

	if (crit == NULL)
		return ERR_INVALID_FIELD;

	rows = find_rows_that_match (database->v_range.cell.a.sheet, 
				     database->v_range.cell.a.col,
				     database->v_range.cell.a.row + 1,
				     database->v_range.cell.b.col,
				     database->v_range.cell.b.row,
				     crit, unique_only_flag);

	prepare_output (wbc, dao, "Filtered");

	filter (dao, database->v_range.cell.a.sheet, rows, database->v_range.cell.a.col, 
		database->v_range.cell.b.col, database->v_range.cell.a.row, 
		database->v_range.cell.b.row);

	free_criterias (crit);
	free_rows (rows);

	autofit_columns (dao, 0, database->v_range.cell.b.col - database->v_range.cell.a.col);

	return OK;
}

/**
 * advanced_filter_update_sensitivity_cb:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity 
 **/
static void
advanced_filter_update_sensitivity_cb (GtkWidget *dummy, AdvancedFilterState *state)
{
	gboolean ready  = TRUE;
	gboolean input_ready  = FALSE;
	gboolean crit_ready  = FALSE;
	gboolean output_ready  = FALSE;

        Value *output_range = NULL;
        Value *input_range = NULL;
        Value *criteria_range = NULL;

	int i;

        input_range = gnumeric_expr_entry_parse_to_value (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);
	criteria_range =  gnumeric_expr_entry_parse_to_value
		(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);

	i = gnumeric_glade_group_value (state->gui, output_group);
	if (i == 2)
		output_range = gnumeric_expr_entry_parse_to_value
			(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);

	input_ready = (input_range != NULL);
	crit_ready = (criteria_range != NULL);
	output_ready =  ((i != 2) || (output_range != NULL));

        if (input_range != NULL) value_release (input_range);
        if (criteria_range != NULL) value_release (criteria_range);
        if (output_range != NULL) value_release (output_range);

	ready = ready && input_ready && crit_ready && output_ready;
	gtk_widget_set_sensitive (state->ok_button, ready);
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
advanced_filter_ok_clicked_cb (GtkWidget *button, AdvancedFilterState *state)
{
	data_analysis_output_t  dao;
	Value  *input;
	Value  *criteria;
	char   *text;
	GtkWidget *w;
	int err = 0;
	gboolean unique;

	input = gnumeric_expr_entry_parse_to_value (
		GNUMERIC_EXPR_ENTRY (state->input_entry), state->sheet);

	criteria = gnumeric_expr_entry_parse_to_value
		(GNUMERIC_EXPR_ENTRY (state->input_entry_2), state->sheet);

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
		error_in_entry ((GenericToolState *) state, GTK_WIDGET (state->input_entry_2), 
				_("The given criteria are invalid."));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		error_in_entry ((GenericToolState *) state, GTK_WIDGET (state->input_entry), text);
		g_free (text);
		break;
	}
	return;
}

/**
 * dialog_histogram_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_advanced_filter_init (AdvancedFilterState *state)
{
	GtkTable *table;
	GtkWidget *widget;
	gint key;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "advanced-filter.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "Filter");
        if (state->dialog == NULL)
                return TRUE;

	state->accel = gtk_accel_group_new ();

	dialog_tool_init_buttons ((GenericToolState *) state, 
				  GTK_SIGNAL_FUNC (advanced_filter_ok_clicked_cb));

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "input-table"));
	state->input_entry = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->input_entry, 0, GNUM_EE_MASK);
        gnumeric_expr_entry_set_scg (state->input_entry, wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->input_entry),
			  1, 2, 0, 1,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gtk_signal_connect_after (GTK_OBJECT (state->input_entry), "changed",
				  GTK_SIGNAL_FUNC (advanced_filter_update_sensitivity_cb), 
				  state);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->input_entry));
	widget = glade_xml_get_widget (state->gui, "var1-label");
	key = gtk_label_parse_uline (GTK_LABEL (widget), state->input_var1_str);
	if (key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (GTK_WIDGET (state->input_entry),
					    "grab_focus",
					    state->accel, key,
					    GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (state->input_entry));

	state->input_entry_2 = GNUMERIC_EXPR_ENTRY (gnumeric_expr_entry_new (state->wbcg));
	gnumeric_expr_entry_set_flags (state->input_entry_2, GNUM_EE_SINGLE_RANGE, GNUM_EE_MASK);
	gnumeric_expr_entry_set_scg (state->input_entry_2,
				     wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, GTK_WIDGET (state->input_entry_2),
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_EDITABLE (state->input_entry_2));
	gtk_signal_connect_after (GTK_OBJECT (state->input_entry_2), "changed",
				  GTK_SIGNAL_FUNC (advanced_filter_update_sensitivity_cb), 
				  state);
	widget = glade_xml_get_widget (state->gui, "var2-label");
	key = gtk_label_parse_uline (GTK_LABEL (widget), state->input_var2_str);
	if (key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (GTK_WIDGET (state->input_entry_2),
					    "grab_focus",
					    state->accel, key,
					    GDK_MOD1_MASK, 0);
	gtk_widget_show (GTK_WIDGET (state->input_entry_2));

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "set-focus",
			    GTK_SIGNAL_FUNC (tool_set_focus), state);
	gtk_signal_connect (GTK_OBJECT (state->dialog), "destroy",
			    GTK_SIGNAL_FUNC (tool_destroy), state);

	dialog_tool_init_outputs ((GenericToolState *) state, GTK_SIGNAL_FUNC 
				  (advanced_filter_update_sensitivity_cb));

	gtk_window_add_accel_group (GTK_WINDOW (state->dialog),
				    state->accel);

	return FALSE;
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
dialog_advanced_filter (WorkbookControlGUI *wbcg)
{
        AdvancedFilterState *state;
	WorkbookControl *wbc;

	g_return_if_fail (wbcg != NULL);

	wbc = WORKBOOK_CONTROL (wbcg);

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, ADVANCED_FILTER_KEY))
		return;

	state = g_new (AdvancedFilterState, 1);
	(*(ToolType *)state) = TOOL_ADVANCED_FILTER;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = wb_control_cur_sheet (wbc);
	state->warning_dialog = NULL;
	state->help_link = "filters.html";
	state->input_var1_str = _("_List Range:");
	state->input_var2_str = _("Criteria _Range:");

	if (dialog_advanced_filter_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Advanced Filter dialog."));
		g_free (state);
		return;
	}

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       ADVANCED_FILTER_KEY);

	advanced_filter_update_sensitivity_cb (NULL, state);
	tool_load_selection ((GenericToolState *)state, TRUE);

        return;
}

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-advanced-filter.c:
 *
 * Author:
 *        Jukka-Pekka Iivonen <iivonen@iki.fi>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>

#define OK               0
#define N_COLUMNS_ERROR  1

static gboolean unique_only_flag;

static void
unique_only_toggled (GtkWidget *widget, gpointer ignored)
{
        unique_only_flag = GTK_TOGGLE_BUTTON (widget)->active;
}

static void
free_rows (GSList *row_list)
{
	GSList *list;

	for (list = row_list; list != NULL; list = list->next)
	        g_free (list->data);
	g_slist_free (row_list);
}


static void
filter (data_analysis_output_t *dao,
	Sheet *sheet, GSList *rows, gint input_col_b, gint input_col_e)
{
        Cell *cell;
	int  i, r=0;

        while (rows != NULL) {
	        gint *row = (gint *) rows->data;
	        for (i=input_col_b; i<=input_col_e; i++) {
		        cell = sheet_cell_get (sheet, i, *row);
			if (cell == NULL)
			        set_cell (dao, i-input_col_b, r, "");
			else
			        set_cell (dao, i-input_col_b, r,
					  cell_get_entered_text (cell));
		}
		++r;
	        rows = rows->next;
	}
}

/* Filter tool.
 */
static gint
advanced_filter (WorkbookControl *wbc,
		 data_analysis_output_t   *dao,
		 gint     input_col_b,    gint input_row_b,
		 gint     input_col_e,    gint input_row_e,
		 gint     criteria_col_b, gint criteria_row_b,
		 gint     criteria_col_e, gint criteria_row_e,
		 gboolean unique_only_flag)
{
        GSList *criteria, *rows;
	Sheet  *sheet;
        gint   cols;

	cols = input_col_e - input_col_b;
	if (cols != criteria_col_e - criteria_col_b)
	        return N_COLUMNS_ERROR;

	sheet = wb_control_cur_sheet (wbc);
	criteria = parse_criteria_range (sheet, criteria_col_b,
					 criteria_row_b, criteria_col_e,
					 criteria_row_e, NULL);

	rows = find_rows_that_match (sheet, input_col_b,
				     input_row_b, input_col_e, input_row_e,
				     criteria, unique_only_flag);

	filter (dao, sheet, rows, input_col_b, cols);

	free_criterias (criteria);
	free_rows (rows);

	return OK;
}


typedef enum {
        InPlace, CopyTo, NewSheet, NewWorkbook
} filter_type_t;

typedef struct {
        filter_type_t type;
        GtkWidget     *copy_to;
} filter_t;

static void
in_place_toggled (GtkWidget *widget, filter_t *filter)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        filter->type = InPlace;
		gtk_widget_set_sensitive (filter->copy_to, FALSE);
	}
}

static void
copy_to_toggled (GtkWidget *widget, filter_t *filter)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        filter->type = CopyTo;
		gtk_widget_set_sensitive (filter->copy_to, TRUE);
	}
}

static void
new_sheet_toggled (GtkWidget *widget, filter_t *filter)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        filter->type = NewSheet;
		gtk_widget_set_sensitive (filter->copy_to, FALSE);
	}
}

static void
new_workbook_toggled (GtkWidget *widget, filter_t *filter)
{
        if (GTK_TOGGLE_BUTTON (widget)->active) {
	        filter->type = NewWorkbook;
		gtk_widget_set_sensitive (filter->copy_to, FALSE);
	}
}

static void
dialog_help_cb (GtkWidget *ignored, gchar *link)
{
	gnumeric_help_display (link);
}

void
dialog_advanced_filter (WorkbookControlGUI *wbcg)
{
        data_analysis_output_t dao;
	filter_t               f;

	GladeXML  *gui;
	GtkWidget *dia;
	GtkWidget *list_range;
	GtkWidget *criteria_range;
	GtkWidget *copy_to;
	GtkWidget *unique_only;
	GtkWidget *radiobutton;
	GtkWidget *helpbutton;
	Sheet     *sheet;
	gint      v, error_flag;
	Range	  list, crit;
	gchar const *text;

	f.type = InPlace;

	gui = gnumeric_glade_xml_new (wbcg,  "advanced-filter.glade");
	if (gui == NULL )
		return;

	dia = glade_xml_get_widget (gui, "AdvancedFilter");
	if (!dia) {
		printf ("Corrupt file advanced-filter.glade\n");
		return;
	}

	list_range = glade_xml_get_widget (gui, "entry1");
	criteria_range = glade_xml_get_widget (gui, "entry2");
	f.copy_to = copy_to = glade_xml_get_widget (gui, "entry3");
	unique_only = glade_xml_get_widget (gui, "checkbutton1");

	radiobutton = glade_xml_get_widget (gui, "radiobutton2");
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (in_place_toggled),
			    &f);
	radiobutton = glade_xml_get_widget (gui, "radiobutton3");
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (copy_to_toggled),
			    &f);
	radiobutton = glade_xml_get_widget (gui, "radiobutton4");
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (new_sheet_toggled),
			    &f);
	radiobutton = glade_xml_get_widget (gui, "radiobutton5");
	gtk_signal_connect (GTK_OBJECT (radiobutton),   "toggled",
			    GTK_SIGNAL_FUNC (new_workbook_toggled),
			    &f);
	helpbutton = glade_xml_get_widget (gui, "helpbutton");
	gtk_signal_connect (GTK_OBJECT (helpbutton), "clicked",
			    GTK_SIGNAL_FUNC (dialog_help_cb), "filters.html");

        if (unique_only_flag)
	        gtk_toggle_button_set_active ((GtkToggleButton *)
					      unique_only, unique_only_flag);

	gtk_signal_connect (GTK_OBJECT (unique_only), "toggled",
			    GTK_SIGNAL_FUNC (unique_only_toggled), NULL);

	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (list_range));
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (criteria_range));
	gnome_dialog_editable_enters (GNOME_DIALOG (dia),
				      GTK_EDITABLE (copy_to));
	gtk_widget_grab_focus (list_range);
	gtk_widget_set_sensitive (copy_to, FALSE);
loop:
	v = gnumeric_dialog_run (wbcg, GTK_DIALOG (dia));

	sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));

	if (v == 2)
	        goto loop;

	if (v == -1 || v == 1){
	        /* Canceled */
		if (v != -1)
			gtk_object_destroy (GTK_OBJECT (dia));
		g_object_unref (G_OBJECT (gui));
		return;
	}

	text = gtk_entry_get_text (GTK_ENTRY (list_range));
	if (!parse_range (text, &list)) {
 	        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell names "
				   "in 'List Range:'"));
		gtk_widget_grab_focus (list_range);
		gtk_entry_set_position (GTK_ENTRY (list_range), 0);
		gtk_entry_select_region (GTK_ENTRY (list_range), 0,
					 GTK_ENTRY (list_range)->text_length);
		goto loop;
	}

	text = gtk_entry_get_text (GTK_ENTRY (criteria_range));
	error_flag = parse_range (text, &crit);
	if (! error_flag) {
 	        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce a valid cell names "
				   "in 'Criteria Range:'"));
		gtk_widget_grab_focus (criteria_range);
		gtk_entry_set_position (GTK_ENTRY (criteria_range), 0);
		gtk_entry_select_region (GTK_ENTRY (criteria_range), 0,
					 GTK_ENTRY (criteria_range)->text_length);
		goto loop;
	}

	if (f.type == CopyTo) {
		Range r;
	        text = gtk_entry_get_text (GTK_ENTRY (copy_to));
		if (!parse_range (text, &r)) {
		        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
					 _("You should introduce a valid "
					   "cell range in 'Copy To:'"));
			gtk_widget_grab_focus (copy_to);
			gtk_entry_set_position (GTK_ENTRY (copy_to), 0);
			gtk_entry_select_region (GTK_ENTRY (copy_to), 0,
						 GTK_ENTRY (copy_to)->text_length);
			goto loop;
		}
		dao.start_col = r.start.col;
		dao.start_row = r.end.row;
		dao.cols = range_width (&r);
		dao.rows = range_height (&r);
	}

	switch (f.type) {
	case InPlace:
	        dao.type = RangeOutput;
		dao.start_col = list.start.col;
		dao.start_row = list.start.row;
		dao.cols = range_width (&list);
		dao.rows = range_height (&list);
		dao.sheet = sheet;
	        break;
	case CopyTo:
	        dao.type = RangeOutput;
		dao.sheet = sheet;
	        break;
	case NewSheet:
	        dao.type = NewSheetOutput;
	        break;
	case NewWorkbook:
	        dao.type = NewWorkbookOutput;
	        break;
	}
	prepare_output (WORKBOOK_CONTROL (wbcg), &dao, "Filtered");
	error_flag = advanced_filter (WORKBOOK_CONTROL (wbcg),
				      &dao,
				      list.start.col, list.start.row,
				      list.end.col, list.end.row,
				      crit.start.col, crit.start.row,
				      crit.end.col, crit.end.row,
				      unique_only_flag);
	switch (error_flag) {
	case OK: break;

	case N_COLUMNS_ERROR:
 	        gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 _("You should introduce the same number of "
				   "columns in the `List Range' and in "
				   "`Criteria Range:'"));
		gtk_widget_grab_focus (list_range);
		gtk_entry_set_position (GTK_ENTRY (list_range), 0);
		gtk_entry_select_region (GTK_ENTRY (list_range), 0,
					 GTK_ENTRY (list_range)->text_length);
		goto loop;
	}

	if (v != -1) {
	        g_object_unref (G_OBJECT (gui));
		gtk_object_destroy (GTK_OBJECT (dia));
	}
}

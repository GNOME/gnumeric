/*
 * dialog-stf-format-page.c : Controls the widgets on the format page of the dialog
 *
 * Copyright 2001 Almer S. Tigelaar <almer@gnome.org>
 * Copyright 2003 Morten Welinder <terra@gnome.org>
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

#undef GTK_DISABLE_DEPRECATED
#warning "This file uses GTK_DISABLE_DEPRECATED"
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialog-stf.h"
#include <format.h>
#include <formats.h>
#include <workbook.h>
#include <gui-util.h>

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/**                                                                                         * main_page_trim_toggled:                                                                  * @button: the toggle button the event handler is attached to                              * @data: mother struct                                                                     *                                                                                          **/
static void
format_page_trim_menu_deactivate (G_GNUC_UNUSED GtkMenu *menu,
				  DruidPageData_t *data)
{
	int trimtype = gtk_option_menu_get_history (data->format.format_trim);

	switch (trimtype) {
	case -1:
	case 0 : data->trim = (TRIM_TYPE_LEFT | TRIM_TYPE_RIGHT);
		break;
	case 1 : data->trim = TRIM_TYPE_NEVER;
		break;
	case 2 : data->trim = TRIM_TYPE_LEFT;
		break;
	case 3 : data->trim = TRIM_TYPE_RIGHT;
		break;
	default : g_warning ("Unknown trim type selected (%d)", trimtype);
	}
}


static void
cb_col_clicked (GtkTreeViewColumn *column, gpointer _i)
{
	int i = GPOINTER_TO_INT (_i);
	DruidPageData_t *pagedata =
		g_object_get_data (G_OBJECT (column), "pagedata");

	gtk_clist_select_row (pagedata->format.format_collist, i, 0);

	/* FIXME: warp focus away from the header.  */
}


/**
 * format_page_update_preview
 * @pagedata : mother struct
 *
 * Will simply utilize the preview rendering functions to update
 * the preview
 *
 * returns : nothing
 **/
static void
format_page_update_preview (DruidPageData_t *pagedata)
{
	RenderData_t *renderdata = pagedata->format.format_run_renderdata;
	GPtrArray *lines;
	unsigned int ui;
	int i;

	stf_preview_colformats_clear (renderdata);
	for (ui = 0; ui < pagedata->format.format_run_list->len; ui++) {
		StyleFormat *sf = g_ptr_array_index (pagedata->format.format_run_list, ui);
		stf_preview_colformats_add (renderdata, sf);
	}

	lines = stf_parse_general (pagedata->format.format_run_parseoptions, pagedata->cur);

	stf_preview_render (renderdata, lines);

	for (i = 0; i < renderdata->colcount; i++) {
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, i);

		g_object_set_data (G_OBJECT (column), "pagedata", pagedata);
		g_object_set (G_OBJECT (column), "clickable", TRUE, NULL);
		g_signal_connect (G_OBJECT (column),
				  "clicked",
				  G_CALLBACK (cb_col_clicked),
				  GINT_TO_POINTER (i));
	}
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/


/**
 * format_page_collist_select_row
 * @clist : GtkCList which emitted the signal
 * @row : row the user selected
 * @column : column the user selected
 * @event : some info on the button the user clicked (unused)
 * @data : Dialog "mother" record
 *
 * this will simply set the gtkentry data->format.format_format's text to the (char*) format associated
 * with @row (@row is actually the column in the @data->src->sheet *confusing*)
 *
 * returns : nothing
 **/
static void
format_page_collist_select_row (G_GNUC_UNUSED GtkCList *clist,
				int row, G_GNUC_UNUSED int column,
				G_GNUC_UNUSED GdkEventButton *event,
				DruidPageData_t *data)
{
	StyleFormat const *colformat = g_ptr_array_index (data->format.format_run_list, row);
	char *fmt;

	if (!colformat)
		return;

	gnumeric_clist_moveto (data->format.format_collist, row);

	if (data->format.format_run_manual_change) {
		data->format.format_run_manual_change = FALSE;
		return;
	}

	data->format.format_run_index = row;
	fmt = style_format_as_XL (colformat, TRUE);
	gtk_entry_set_text (data->format.format_format, fmt);
	g_free (fmt);
}

/**
 * format_page_sublist_select_row
 * @clist : GtkCList which emitted the signal
 * @row : row the user selected
 * @column : column the user selected
 * @event : some info on the button the user clicked (unused)
 * @data : Dialog "mother" record
 *
 * If the user selects a different format from @clist, the caption of data->format.format_format will
 * change to the entry in the @clist the user selected
 *
 * returns : nothing
 **/
static void
format_page_sublist_select_row (GtkCList *clist, int row, int column,
				G_GNUC_UNUSED GdkEventButton *event,
				DruidPageData_t *data)
{
	char *t[1];

	/* User did not select, it was done in the code with gtk_clist_select_row */
	if (data->format.format_run_manual_change) {
		data->format.format_run_manual_change = FALSE;
		return;
	}

	/* WEIRD THING : when scrolling with keys it will give the right row, but always -1 as column,
	   because we have only one column, always set "column" to 0 for now */
	column = 0;

	gtk_clist_get_text (clist, row, column, t);

	data->format.format_run_sublist_select = FALSE;
	if (strcmp (t[0], _("Custom")) != 0)
		gtk_entry_set_text (data->format.format_format, t[0]);
	data->format.format_run_sublist_select = TRUE;
}

/**
 * format_page_format_changed
 * @entry : GtkEntry which emitted the signal
 * @data : Dialog "mother" record
 *
 * Updates the selected column on the sheet with the new
 * format the user choose/entered.
 *
 * returns : nothing
 **/
static void
format_page_format_changed (GtkEntry *entry, DruidPageData_t *data)
{
	if (data->format.format_run_index >= 0) {
		int i, found;
		char *t[1];
		char *new_fmt = gtk_editable_get_chars (GTK_EDITABLE (entry), 0, -1);
		StyleFormat *sf;

		sf = g_ptr_array_index (data->format.format_run_list, data->format.format_run_index);
		style_format_unref (sf);

		g_ptr_array_index (data->format.format_run_list, data->format.format_run_index) =
			style_format_new_XL (new_fmt, TRUE);

		gtk_clist_set_text (data->format.format_collist, data->format.format_run_index, 1, new_fmt);

		gtk_clist_set_column_width (data->format.format_collist,
					    1,
					    gtk_clist_optimal_column_width (data->format.format_collist, 1));

		if (data->format.format_run_sublist_select) {
			found = 0;
			for (i = 0; i < GTK_CLIST (data->format.format_sublist)->rows; i++) {
				gtk_clist_get_text (data->format.format_sublist, i, 0, t);
				if (strcmp (t[0], new_fmt)==0) {
					found = i;
					break;
				}
			}

			data->format.format_run_manual_change = TRUE;
			gtk_clist_select_row (data->format.format_sublist, found, 0);
			gnumeric_clist_moveto (data->format.format_sublist, found);
		}

		g_free (new_fmt);
	}

	format_page_update_preview (data);
}

/*************************************************************************************************
 * FORMAT EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_format_page_prepare
 * @page : format page
 * @druid : gnome druid hosting @page
 * @data : mother struct
 *
 * This will prepare the widgets on the format page before
 * the page gets displayed
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_prepare (G_GNUC_UNUSED GnomeDruidPage *page,
				G_GNUC_UNUSED GnomeDruid *druid,
				DruidPageData_t *data)
{
	int i;

	format_page_update_preview (data);

	/* If necessary add new items (non-visual) */
	while ((int)data->format.format_run_list->len < data->format.format_run_renderdata->colcount) {
		g_ptr_array_add (data->format.format_run_list,
				 style_format_new_XL (cell_formats[0][0], FALSE));
	}

	/* Add new items visual */
	gtk_clist_clear (data->format.format_collist);
	for (i = 0; i < data->format.format_run_renderdata->colcount; i++) {
		StyleFormat *sf = g_ptr_array_index (data->format.format_run_list, i);
		char *t[2];

		t[0] = g_strdup_printf (_(COLUMN_CAPTION), i + 1);
		t[1] = style_format_as_XL (sf, TRUE);
		gtk_clist_append (data->format.format_collist, t);
		g_free (t[1]);
		g_free (t[0]);
	}
	gtk_clist_columns_autosize (data->format.format_collist);

	data->format.format_run_manual_change = TRUE;
	gtk_clist_select_row (data->format.format_collist, 0, 0);
	gnumeric_clist_moveto (data->format.format_collist, 0);

	data->format.format_run_index = 0;

	{
		StyleFormat const *sf = g_ptr_array_index (data->format.format_run_list, 0);
		char *fmt;

		fmt = style_format_as_XL (sf, TRUE);
		gtk_entry_set_text (data->format.format_format, fmt);
		g_free (fmt);
	}
}

/**
 * stf_dialog_format_page_cleanup
 * @pagedata : mother struct
 *
 * This should be called when the druid has finished to clean up resources
 * used. In this case the format_run_list data pointers and the format_run_list
 * itself will be freed
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_cleanup (DruidPageData_t *pagedata)
{
	stf_preview_free (pagedata->format.format_run_renderdata);
}

/**
 * stf_dialog_format_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the format Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	char const * const * const * mainiterator = cell_formats;
	char const * const * subiterator;
	char *temp[1];
	int rownumber;
	GtkMenu *menu;

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

        /* Create/get object and fill information struct */
	pagedata->format.format_collist       = GTK_CLIST (glade_xml_get_widget (gui, "format_collist"));
	pagedata->format.format_sublist       = GTK_CLIST (glade_xml_get_widget (gui, "format_sublist"));
	pagedata->format.format_sublistholder = GTK_SCROLLED_WINDOW (glade_xml_get_widget (gui, "format_sublistholder"));
	pagedata->format.format_format        = GTK_ENTRY (glade_xml_get_widget (gui, "format_format"));

	pagedata->format.format_data_container = glade_xml_get_widget (gui, "format_data_container");
	pagedata->format.format_trim   = GTK_OPTION_MENU  (glade_xml_get_widget (gui, "format_trim"));
	
	/* Set properties */
	pagedata->format.format_run_renderdata    = stf_preview_new (pagedata->format.format_data_container,
							  workbook_date_conv (wb_control_workbook (WORKBOOK_CONTROL (pagedata->wbcg))));
	pagedata->format.format_run_list          = g_ptr_array_new ();
	pagedata->format.format_run_index         = -1;
	pagedata->format.format_run_manual_change = FALSE;
	pagedata->format.format_run_sublist_select = TRUE;
	pagedata->format.format_run_parseoptions  = NULL; /*  stf_parse_options_new (); */

        gtk_clist_column_titles_passive (pagedata->format.format_sublist);

	rownumber = 0;
	temp[0] = _("Custom");
	gtk_clist_append (pagedata->format.format_sublist, temp);
	while (*mainiterator) {
		subiterator = *mainiterator;
		while (*subiterator) {
			temp[0] = (char*) *subiterator;
			gtk_clist_append (pagedata->format.format_sublist, temp);
			subiterator++;
			rownumber++;
		}
		mainiterator++;
	}

	gtk_clist_set_column_justification (pagedata->format.format_collist, 0, GTK_JUSTIFY_RIGHT);

	/* Connect signals */
	g_signal_connect (G_OBJECT (pagedata->format.format_format),
			  "changed",
			  G_CALLBACK (format_page_format_changed), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format.format_collist),
			  "select_row",
			  G_CALLBACK (format_page_collist_select_row), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format.format_sublist),
			  "select_row",
			  G_CALLBACK (format_page_sublist_select_row), pagedata);

	menu = (GtkMenu *) gtk_option_menu_get_menu (pagedata->format.format_trim);
        g_signal_connect (G_OBJECT (menu),
			  "deactivate",
			  G_CALLBACK (format_page_trim_menu_deactivate), pagedata);
	format_page_trim_menu_deactivate (menu, pagedata);
}

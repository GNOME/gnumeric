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
				  StfDialogData *data)
{
	StfTrimType_t trim;
	int trimtype = gtk_option_menu_get_history (data->format.format_trim);

	switch (trimtype) {
	case -1:
	case 0:
		trim = TRIM_TYPE_LEFT | TRIM_TYPE_RIGHT;
		break;
	default:
		g_warning ("Unknown trim type selected (%d)", trimtype);
		/* Fall through.  */
	case 1:
		trim = TRIM_TYPE_NEVER;
		break;
	case 2:
		trim = TRIM_TYPE_LEFT;
		break;
	case 3:
		trim = TRIM_TYPE_RIGHT;
		break;
	}

	stf_parse_options_set_trim_spaces (data->parseoptions, trim);
}

static void
activate_column (StfDialogData *pagedata, int i)
{
	StyleFormat *colformat;
	GtkCellRenderer *cell;
	RenderData_t *renderdata = pagedata->format.renderdata;

	cell = stf_preview_get_cell_renderer (renderdata,
					      pagedata->format.index);
	if (cell) {
		g_object_set (G_OBJECT (cell),
			      "background", NULL,
			      NULL);
	}

	pagedata->format.index = i;
	cell = stf_preview_get_cell_renderer (renderdata,
					      pagedata->format.index);
	if (cell) {
		g_object_set (G_OBJECT (cell),
			      "background", "lightgrey",
			      NULL);
		gtk_widget_queue_draw (GTK_WIDGET (renderdata->tree_view));
	}

	/* FIXME: warp focus away from the header.  */

	colformat = g_ptr_array_index (pagedata->format.formats, pagedata->format.index);
	if (colformat)
		number_format_selector_set_style_format (pagedata->format.format_selector,
							 colformat);
}


static void cb_col_check_clicked (GtkToggleButton *togglebutton,
                                            gpointer _i)
{
	int i = GPOINTER_TO_INT (_i);
	StfDialogData *pagedata =
		g_object_get_data (G_OBJECT (togglebutton), "pagedata");
	gboolean active = gtk_toggle_button_get_active (togglebutton);
	
	if (pagedata->format.col_import_array[i] == active)
	     return;
	if (!active) {
	     pagedata->format.col_import_array[i] = FALSE;
	     pagedata->format.col_import_count--;
	} else {
	     if (pagedata->format.col_import_count < SHEET_MAX_COLS) {
		  pagedata->format.col_import_array[i] = TRUE;
		  pagedata->format.col_import_count++;
	     } else {
		  char *msg = g_strdup_printf 
		       (_("A maximum of %d columns can be imported."), 
			SHEET_MAX_COLS);
		  gtk_toggle_button_set_active (togglebutton, FALSE);
		  gnumeric_notice (pagedata->wbcg, GTK_MESSAGE_WARNING, msg);
		  g_free (msg);
	     }
	}
	return;
}

static void
cb_col_clicked (GtkTreeViewColumn *column, gpointer _i)
{
	int i = GPOINTER_TO_INT (_i);
	StfDialogData *pagedata =
		g_object_get_data (G_OBJECT (column), "pagedata");

	activate_column (pagedata, i);
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
format_page_update_preview (StfDialogData *pagedata)
{
	RenderData_t *renderdata = pagedata->format.renderdata;
	unsigned int ui;
	int i;

	stf_preview_colformats_clear (renderdata);
	for (ui = 0; ui < pagedata->format.formats->len; ui++) {
		StyleFormat *sf = g_ptr_array_index (pagedata->format.formats, ui);
		stf_preview_colformats_add (renderdata, sf);
	}

	stf_preview_set_lines (renderdata,
			       stf_parse_general (pagedata->parseoptions,
						  pagedata->cur,
						  pagedata->cur_end));

	g_free (pagedata->format.col_import_array);
	pagedata->format.col_import_array = g_new0(gboolean, renderdata->colcount);
	pagedata->format.col_import_count = 0;	

	for (i = 0; i < renderdata->colcount; i++) {
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, i);
		GtkWidget *box = gtk_hbox_new (FALSE,5);
		GtkWidget *check = gtk_check_button_new ();
		char * label_text = g_strdup_printf (_("Column %d"), i+1);
		GtkWidget *label = gtk_label_new (label_text);
		
		g_free (label_text);
		
		gtk_box_pack_start (GTK_BOX(box), check, FALSE, FALSE, 0);
		
		pagedata->format.col_import_array[i] = (i < SHEET_MAX_COLS);
		
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(check),
					      pagedata->
					      format.col_import_array[i]);
		if ( pagedata->format.col_import_array[i])
		     pagedata->format.col_import_count++;
		
		gtk_tooltips_set_tip (gtk_tooltips_new(), check,
				      _("If this checkbox is selected, the "
					"column will be imported into "
					"Gnumeric."),
				      _("At most 256 columns can be imported "
					"at one time."));
		g_object_set_data (G_OBJECT (check), "pagedata", pagedata);
		gtk_box_pack_start (GTK_BOX(box), label, TRUE, TRUE, 0);
		gtk_widget_show_all (box);
		
		
		gtk_tree_view_column_set_widget (column, box);
		g_object_set_data (G_OBJECT (column), "pagedata", pagedata);
		g_object_set (G_OBJECT (column), "clickable", TRUE, NULL);
		g_signal_connect (G_OBJECT (column),
				  "clicked",
				  G_CALLBACK (cb_col_clicked),
				  GINT_TO_POINTER (i));
		g_signal_connect (G_OBJECT (check),
				  "toggled",
				  G_CALLBACK (cb_col_check_clicked),
				  GINT_TO_POINTER (i));
	}
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

static void
locale_changed_cb (LocaleSelector *ls, char const *new_locale,
		      StfDialogData *pagedata)
{
	g_free (pagedata->locale);
	pagedata->locale = g_strdup (new_locale);

	number_format_selector_set_locale (pagedata->format.format_selector,
					   new_locale);
}


/**
 * cb_number_format_changed
 * @widget : NumberFormatSelector which emitted the signal
 * @fmt : current selected format
 * @data : Dialog "mother" record
 *
 * Updates the selected column on the sheet with the new
 * format the user choose/entered.
 *
 * returns : nothing
 **/
static void
cb_number_format_changed (G_GNUC_UNUSED GtkWidget *widget,
			  const char *fmt,
			  StfDialogData *data)
{
	if (data->format.index >= 0) {
		StyleFormat *sf;

		sf = g_ptr_array_index (data->format.formats, data->format.index);
		style_format_unref (sf);

		g_ptr_array_index (data->format.formats, data->format.index) =
			style_format_new_XL (fmt, FALSE);
	}

	format_page_update_preview (data);
}

/**
 * stf_dialog_format_page_prepare
 * @data : mother struct
 *
 * This will prepare the widgets on the format page before
 * the page gets displayed
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_prepare (StfDialogData *data)
{
	StyleFormat *sf;

	format_page_update_preview (data);

	/* If necessary add new items (non-visual) */
	while ((int)data->format.formats->len < data->format.renderdata->colcount) {
		g_ptr_array_add (data->format.formats,
				 style_format_new_XL (cell_formats[0][0], FALSE));
	}

	data->format.manual_change = TRUE;
	activate_column (data, 0);

	sf = g_ptr_array_index (data->format.formats, 0);
	number_format_selector_set_style_format (data->format.format_selector,
						 sf);
}

/*************************************************************************************************
 * FORMAT EXPORTED FUNCTIONS
 *************************************************************************************************/

void
stf_dialog_format_page_cleanup (StfDialogData *pagedata)
{
	GPtrArray *formats = pagedata->format.formats;
	if (formats) {
		unsigned int ui;
		for (ui = 0; ui < formats->len; ui++) {
			StyleFormat *sf = g_ptr_array_index (formats, ui);
			style_format_unref (sf);
		}
		g_ptr_array_free (formats, TRUE);
	}

	stf_preview_free (pagedata->format.renderdata);
	g_free (pagedata->format.col_import_array);
	pagedata->format.col_import_array = NULL;
	pagedata->format.col_import_count = 0;	
}

void
stf_dialog_format_page_init (GladeXML *gui, StfDialogData *pagedata)
{
	GtkMenu *menu;
	GtkWidget * format_hbox;

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

        /* Create/get object and fill information struct */
	pagedata->format.col_import_array = NULL;
	pagedata->format.col_import_count = 0;
	
	pagedata->format.format_selector      = NUMBER_FORMAT_SELECTOR( number_format_selector_new ());

	pagedata->format.format_data_container = glade_xml_get_widget (gui, "format_data_container");
	pagedata->format.format_trim   = GTK_OPTION_MENU  (glade_xml_get_widget (gui, "format_trim"));

	format_hbox = glade_xml_get_widget (gui, "format_hbox");
	gtk_box_pack_end_defaults (GTK_BOX (format_hbox), GTK_WIDGET (pagedata->format.format_selector));
	gtk_widget_show (GTK_WIDGET (pagedata->format.format_selector));

	pagedata->format.locale_selector =
		LOCALE_SELECTOR (locale_selector_new ());
	gtk_table_attach (
		GTK_TABLE (glade_xml_get_widget (gui, "locale_table")),
		GTK_WIDGET (pagedata->format.locale_selector),
		3, 4, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gtk_widget_show_all (GTK_WIDGET (pagedata->format.locale_selector));
	gtk_widget_set_sensitive
		(GTK_WIDGET (pagedata->format.locale_selector),
		 !pagedata->fixed_locale);

	/* Set properties */
	pagedata->format.renderdata =
		stf_preview_new (pagedata->format.format_data_container,
				 workbook_date_conv (wb_control_workbook (WORKBOOK_CONTROL (pagedata->wbcg))));
	pagedata->format.formats          = g_ptr_array_new ();
	pagedata->format.index         = -1;
	pagedata->format.manual_change = FALSE;

	/* Connect signals */
	g_signal_connect (G_OBJECT (pagedata->format.format_selector),
			  "number_format_changed",
			  G_CALLBACK (cb_number_format_changed),
			  pagedata);
	g_signal_connect (G_OBJECT (pagedata->format.locale_selector),
			  "locale_changed",
			  G_CALLBACK (locale_changed_cb), pagedata);

	menu = (GtkMenu *) gtk_option_menu_get_menu (pagedata->format.format_trim);
        g_signal_connect (G_OBJECT (menu),
			  "deactivate",
			  G_CALLBACK (format_page_trim_menu_deactivate), pagedata);
	format_page_trim_menu_deactivate (menu, pagedata);
}

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

static void
locale_changed_cb (LocaleSelector *ls, char const *new_enc,
		      DruidPageData_t *pagedata)
{
	const char *name = locale_selector_get_locale_name (ls, new_enc);
#warning Fixme: implement locale changes
	g_warning ("Locale changes (%s) have not been implemented", 
			   name ? name : new_enc);
}


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
	StyleFormat *colformat = g_ptr_array_index (data->format.formats, row);

	if (!colformat)
		return;

	gnumeric_clist_moveto (data->format.format_collist, row);

	if (data->format.manual_change) {
		data->format.manual_change = FALSE;
		return;
	}

	data->format.index = row;
	number_format_selector_set_style_format (data->format.format_selector,
						 colformat);
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
			      char * fmt,
			      DruidPageData_t *data)
{
	if (data->format.index >= 0) {

		StyleFormat *sf;

		sf = g_ptr_array_index (data->format.formats, data->format.index);
		style_format_unref (sf);

		g_ptr_array_index (data->format.formats, data->format.index) = 
			style_format_new_XL (fmt, FALSE);

		gtk_clist_set_text (data->format.format_collist, 
				    data->format.index, 
				    1, fmt);

		gtk_clist_set_column_width 
			(data->format.format_collist,
			 1,
			 gtk_clist_optimal_column_width (data->format.format_collist, 1));
	}

	format_page_update_preview (data);
}

/**
 * format_page_prepare
 * @page : format page
 * @druid : gnome druid hosting @page
 * @data : mother struct
 *
 * This will prepare the widgets on the format page before
 * the page gets displayed
 *
 * returns : nothing
 **/
static void
format_page_prepare (G_GNUC_UNUSED GnomeDruidPage *page,
		     G_GNUC_UNUSED GnomeDruid *druid,
		     DruidPageData_t *data)
{
	int i;
	StyleFormat *sf;

	format_page_update_preview (data);

	/* If necessary add new items (non-visual) */
	while ((int)data->format.formats->len < data->format.renderdata->colcount) {
		g_ptr_array_add (data->format.formats,
				 style_format_new_XL (cell_formats[0][0], FALSE));
	}

	/* Add new items visual */
	gtk_clist_clear (data->format.format_collist);
	for (i = 0; i < data->format.renderdata->colcount; i++) {
		StyleFormat *sf = g_ptr_array_index (data->format.formats, i);
		char *t[2];

		t[0] = g_strdup_printf (_(COLUMN_CAPTION), i + 1);
		t[1] = style_format_as_XL (sf, TRUE);
		
		gtk_clist_append (data->format.format_collist, t);
		
		g_free (t[1]);
		g_free (t[0]);
	}
	gtk_clist_columns_autosize (data->format.format_collist);

	data->format.manual_change = TRUE;
	gtk_clist_select_row (data->format.format_collist, 0, 0);
	gnumeric_clist_moveto (data->format.format_collist, 0);

	data->format.index = 0;

	sf = g_ptr_array_index (data->format.formats, 0);
	number_format_selector_set_style_format (data->format.format_selector,
						 sf);
}

/*************************************************************************************************
 * FORMAT EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_format_page_cleanup
 * @pagedata : mother struct
 *
 * This should be called when the druid has finished to clean up resources
 * used. In this case the formats data pointers and the formats
 * itself will be freed
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_cleanup (DruidPageData_t *pagedata)
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
	GtkMenu *menu;
	GtkWidget * format_hbox;

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

        /* Create/get object and fill information struct */
	pagedata->format.format_collist       = GTK_CLIST (glade_xml_get_widget (gui, "format_collist"));
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

	gtk_clist_set_column_justification (pagedata->format.format_collist,
					    0, 
					    GTK_JUSTIFY_RIGHT);

	/* Connect signals */
	g_signal_connect (G_OBJECT (pagedata->format.format_selector),
			  "number_format_changed",
			  G_CALLBACK (cb_number_format_changed), 
			  pagedata);
	g_signal_connect (G_OBJECT (pagedata->format.format_collist),
			  "select_row",
			  G_CALLBACK (format_page_collist_select_row), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format.locale_selector),
			  "locale_changed",
			  G_CALLBACK (locale_changed_cb), pagedata);

	menu = (GtkMenu *) gtk_option_menu_get_menu (pagedata->format.format_trim);
        g_signal_connect (G_OBJECT (menu),
			  "deactivate",
			  G_CALLBACK (format_page_trim_menu_deactivate), pagedata);
	format_page_trim_menu_deactivate (menu, pagedata);

	g_signal_connect (G_OBJECT (pagedata->format_page),
		"prepare",
		G_CALLBACK (format_page_prepare), pagedata);
}

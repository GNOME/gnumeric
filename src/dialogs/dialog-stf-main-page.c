/*
 * dialog-stf-main-page.c : controls the widget on the main page of the druid
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
#include <gui-util.h>
#include <workbook.h>

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

static void
main_page_update_preview (DruidPageData_t *pagedata)
{
	RenderData_t *renderdata = pagedata->main.renderdata;

	stf_preview_set_lines (renderdata,
			       stf_parse_lines (pagedata->data, TRUE));
	stf_preview_render (renderdata);
}


/**
 * main_page_set_spin_button_adjustment
 * @spinbutton : the spinbutton to adjust
 * @min : the minimum number the user may enter into the spinbutton
 * @max : the maximum number the user may enter into the spinbutton
 *
 * returns : nothing
 **/
static void
main_page_set_spin_button_adjustment (GtkSpinButton* spinbutton, int min, int max)
{
	GtkAdjustment *spinadjust;

	spinadjust = gtk_spin_button_get_adjustment (spinbutton);
	spinadjust->lower = min;
	spinadjust->upper = max;
	gtk_spin_button_set_adjustment (spinbutton, spinadjust);
}

/**
 * main_page_import_range_changed
 * @data : mother struct
 *
 * Updates the "number of lines to import" label on the main page of the druid
 *
 * returns : nothing
 **/
static void
main_page_import_range_changed (DruidPageData_t *data)
{
	RenderData_t *renderdata = data->main.renderdata;
	int startrow, stoprow;
	char *linescaption;

	startrow = gtk_spin_button_get_value_as_int (data->main.main_startrow);
	stoprow  = gtk_spin_button_get_value_as_int (data->main.main_stoprow);

	if (stoprow > (int)renderdata->lines->len) {
	     stoprow = renderdata->lines->len;
	     gtk_spin_button_set_value (data->main.main_stoprow, stoprow);
	}

	if (startrow > stoprow) {
	     startrow = stoprow;
	     gtk_spin_button_set_value (data->main.main_startrow, startrow);
	}

	main_page_set_spin_button_adjustment (data->main.main_startrow, 1, stoprow);
	main_page_set_spin_button_adjustment (data->main.main_stoprow, startrow, renderdata->lines->len);

	data->importlines = (stoprow - startrow) + 1;
	linescaption = g_strdup_printf (_("%d of %d lines to import"),
					data->importlines,
					renderdata->lines->len);
	gtk_label_set_text (data->main.main_lines, linescaption);
	g_free (linescaption);
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * main_page_startrow_changed
 * @button : the spinbutton the event handler is attached to
 * @data : mother struct
 *
 * This function will adjust the amount of displayed text to
 * reflect the number of lines the user wants to import
 *
 * returns : nothing
 **/
static void
main_page_startrow_changed (GtkSpinButton* button, DruidPageData_t *data)
{
	const char *cur = data->data;
	int startrow;

	startrow = gtk_spin_button_get_value_as_int (button) - 1;

	for (; startrow != 0; startrow--) {
		while (*cur != '\n' && *cur != 0)
			cur++;
		if (*cur == 0) break;
		cur++;
	}
	data->cur = cur;

	main_page_import_range_changed (data);
}

/**
 * main_page_startrow_changed
 * @button : the spinbutton the event handler is attached to
 * @data : mother struct
 *
 * returns : nothing
 **/
static void
main_page_stoprow_changed (G_GNUC_UNUSED GtkSpinButton* button,
			   DruidPageData_t *data)
{
	main_page_import_range_changed (data);
}

static void
main_page_stringindicator_change (G_GNUC_UNUSED GtkWidget *widget,
			DruidPageData_t *data)
{
	StfParseOptions_t *parseoptions = data->csv.parseoptions;
	char *textfieldtext;
	gunichar str_ind;

	textfieldtext = gtk_editable_get_chars (GTK_EDITABLE (data->main.main_textfield), 0, -1);
	str_ind = g_utf8_get_char (textfieldtext);
	if (str_ind != '\0')
	     stf_parse_options_csv_set_stringindicator (parseoptions,
							str_ind);
	g_free (textfieldtext);

	stf_parse_options_csv_set_indicator_2x_is_single  (parseoptions,
							   gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main.main_2x_indicator)));

	main_page_stoprow_changed (NULL, data);
}

static void
main_page_source_format_toggled (G_GNUC_UNUSED GtkWidget *widget,
				 DruidPageData_t *data)
{
     if (gtk_toggle_button_get_active
	 (GTK_TOGGLE_BUTTON (data->main.main_separated))) {
	  gtk_widget_set_sensitive
	       (GTK_WIDGET (data->main.main_2x_indicator), TRUE);
	  gtk_widget_set_sensitive
	       (GTK_WIDGET (data->main.main_textindicator), TRUE);
	  gtk_widget_set_sensitive
	       (GTK_WIDGET (data->main.main_textfield), TRUE);
     } else {
	  gtk_widget_set_sensitive
	       (GTK_WIDGET (data->main.main_2x_indicator), FALSE);
	  gtk_widget_set_sensitive
	       (GTK_WIDGET (data->main.main_textindicator), FALSE);
	  gtk_widget_set_sensitive
	       (GTK_WIDGET (data->main.main_textfield), FALSE);
     }
     main_page_stoprow_changed (NULL, data);
}

/**
 * main_page_prepare
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
main_page_prepare (G_GNUC_UNUSED GnomeDruidPage *page,
		   G_GNUC_UNUSED GnomeDruid *druid,
		   DruidPageData_t *pagedata)
{
	main_page_update_preview (pagedata);
}


/*************************************************************************************************
 * MAIN EXPORTED FUNCTIONS
 *************************************************************************************************/

void
stf_dialog_main_page_cleanup (DruidPageData_t *pagedata)
{
	stf_preview_free (pagedata->main.renderdata);
}

/**
 * stf_dialog_main_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the Main Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
stf_dialog_main_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	RenderData_t *renderdata;
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;

	pagedata->main.main_separated = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "main_separated"));
	pagedata->main.main_fixed     = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "main_fixed"));
	pagedata->main.main_startrow  = GTK_SPIN_BUTTON  (glade_xml_get_widget (gui, "main_startrow"));
	pagedata->main.main_stoprow   = GTK_SPIN_BUTTON  (glade_xml_get_widget (gui, "main_stoprow"));
	pagedata->main.main_lines     = GTK_LABEL        (glade_xml_get_widget (gui, "main_lines"));
	pagedata->main.main_data_container =              glade_xml_get_widget (gui, "main_data_container");
	pagedata->main.main_2x_indicator  = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "main_2x_indicator"));
	pagedata->main.main_textindicator = GTK_COMBO    (glade_xml_get_widget (gui, "main_textindicator"));
	pagedata->main.main_textfield     = GTK_ENTRY    (glade_xml_get_widget (gui, "main_textfield"));

	renderdata = pagedata->main.renderdata = stf_preview_new
		(pagedata->main.main_data_container,
		 NULL);
	renderdata->ignore_formats = TRUE;

	main_page_update_preview (pagedata);

	column = stf_preview_get_column (renderdata, 0);
	cell = stf_preview_get_cell_renderer (renderdata, 0);
	gtk_tree_view_column_set_title (column, _("Line"));
	g_object_set (G_OBJECT (cell),
		      "xalign", 1.0,
		      "style", PANGO_STYLE_ITALIC,
		      "background", "lightgrey",
		      NULL);

	column = stf_preview_get_column (renderdata, 1);
	cell = stf_preview_get_cell_renderer (renderdata, 1);
	gtk_tree_view_column_set_title (column, _("Text"));
	g_object_set (G_OBJECT (cell),
		      "family", "monospace",
		      NULL);

	/* Set properties */
	main_page_set_spin_button_adjustment (pagedata->main.main_startrow, 1, renderdata->lines->len);
	main_page_set_spin_button_adjustment (pagedata->main.main_stoprow, 1, renderdata->lines->len);
	gtk_spin_button_set_value (pagedata->main.main_stoprow, renderdata->lines->len);

	{
		GtkFrame *main_frame = GTK_FRAME (glade_xml_get_widget (gui, "main_frame"));
		char *base = g_path_get_basename (pagedata->filename);
		char *label = g_strdup_printf (_("Data (from %s)"), base);
		gtk_frame_set_label (main_frame, label);
		g_free (label);
		g_free (base);
	}

	/* Connect signals */
	g_signal_connect (G_OBJECT (pagedata->main.main_startrow),
		"changed",
		G_CALLBACK (main_page_startrow_changed), pagedata);
	g_signal_connect (G_OBJECT (pagedata->main.main_stoprow),
		"changed",
		G_CALLBACK (main_page_stoprow_changed), pagedata);
	g_signal_connect (G_OBJECT (pagedata->main.main_2x_indicator),
		"toggled",
		G_CALLBACK (main_page_stringindicator_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->main.main_textfield),
		"changed",
		G_CALLBACK (main_page_stringindicator_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->main.main_separated),
		"toggled",
		G_CALLBACK (main_page_source_format_toggled), pagedata);

	g_signal_connect (G_OBJECT (pagedata->main_page),
		"prepare",
		G_CALLBACK (main_page_prepare), pagedata);

	main_page_startrow_changed (pagedata->main.main_startrow, pagedata);
}

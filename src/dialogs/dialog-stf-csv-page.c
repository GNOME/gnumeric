/*
 * dialog-stf.c : Controls the widget on the CSV (Comma Separated Value) page of the druid
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
#include <gnumeric.h>
#include "dialog-stf.h"
#include "format.h"

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * csv_page_global_change
 * @widget : the widget which emitted the signal
 * @data : mother struct
 *
 * This will update the preview based on the state of
 * the widgets on the csv page
 *
 * returns : nothing
 **/
static void
csv_page_global_change (G_GNUC_UNUSED GtkWidget *widget,
			DruidPageData_t *data)
{
	StfParseOptions_t *parseoptions = data->csv.csv_run_parseoptions;
	GSList *sepstr;
	GString *sepc = g_string_new (NULL);

	sepstr = NULL;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_custom))) {
		char *csvcustomtext = gtk_editable_get_chars (GTK_EDITABLE (data->csv.csv_customseparator), 0, -1);

		if (strcmp (csvcustomtext, "") != 0)
			sepstr = g_slist_append (sepstr, csvcustomtext);
		else
			g_free (csvcustomtext);
	}

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_tab)))
		g_string_append_c (sepc, '\t');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_colon)))
		g_string_append_c (sepc, ':');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_comma)))
		g_string_append_c (sepc, ',');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_space)))
		g_string_append_c (sepc, ' ');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_semicolon)))
		g_string_append_c (sepc, ';');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_pipe)))
		g_string_append_c (sepc, '|');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_slash)))
		g_string_append_c (sepc, '/');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_hyphen)))
		g_string_append_c (sepc, '-');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_bang)))
		g_string_append_c (sepc, '!');

	stf_parse_options_csv_set_separators (parseoptions,
					      strcmp (sepc->str, "") == 0 ? NULL : sepc->str,
					      sepstr);
	g_string_free (sepc, TRUE);
	if (sepstr) {
		GSList *l;
		for (l = sepstr; l != NULL; l = l->next)
			g_free ((char *) l->data);
		g_slist_free (sepstr);
	}

	stf_parse_options_csv_set_duplicates (parseoptions,
					      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_duplicates)));

	stf_parse_general_free (data->lines);
	data->lines = stf_parse_general (parseoptions, data->cur);

	stf_preview_render (data->csv.csv_run_renderdata);
}

/**
 * csv_page_custom_toggled
 * @button : the Checkbutton that emitted the signal
 * @data : a mother struct
 *
 * This will nicely activate the @data->csv_info->csv_customseparator widget
 * so the user can enter text into it.
 * It will also gray out this widget if the @button is not selected.
 *
 * returns : nothing
 **/
static void
csv_page_custom_toggled (GtkCheckButton *button, DruidPageData_t *data)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		gtk_widget_set_sensitive   (GTK_WIDGET (data->csv.csv_customseparator), TRUE);
		gtk_widget_grab_focus      (GTK_WIDGET (data->csv.csv_customseparator));
		gtk_editable_select_region (GTK_EDITABLE (data->csv.csv_customseparator), 0, -1);

	} else {
		gtk_widget_set_sensitive (GTK_WIDGET (data->csv.csv_customseparator), FALSE);
		gtk_editable_select_region (GTK_EDITABLE (data->csv.csv_customseparator), 0, 0); /* If we don't use this the selection will remain blue */
	}

	csv_page_global_change (NULL, data);
}

/**
 * stf_dialog_csv_page_prepare
 * @page : The druidpage that emitted the signal
 * @druid : The gnomedruid that houses @page
 * @data : mother struct
 *
 * Will prepare the csv page
 *
 * returns : nothing
 **/
static void
csv_page_prepare (G_GNUC_UNUSED GnomeDruidPage *page,
		  G_GNUC_UNUSED GnomeDruid *druid,
		  DruidPageData_t *pagedata)
{
	stf_parse_options_set_trim_spaces (pagedata->csv.csv_run_parseoptions, pagedata->trim);

	if (format_get_arg_sep () == ',')
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_comma), TRUE);
	else
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_semicolon), TRUE);

#if 0
	stf_preview_set_startrow (pagedata->csv.csv_run_renderdata, GTK_RANGE (pagedata->csv.csv_scroll)->adjustment->value);
#endif

	/* Calling this routine will also automatically call global change which updates the preview too */
	csv_page_custom_toggled (pagedata->csv.csv_custom, pagedata);
}

/*************************************************************************************************
 * CSV EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_csv_page_cleanup
 * @pagedata : mother struct
 *
 * Will cleanup csv page run-time data
 *
 * returns : nothing
 **/
void
stf_dialog_csv_page_cleanup (DruidPageData_t *pagedata)
{
	if (pagedata->csv.csv_run_parseoptions) {
		stf_parse_options_free (pagedata->csv.csv_run_parseoptions);
		pagedata->csv.csv_run_parseoptions = NULL;
	}

	stf_preview_free (pagedata->csv.csv_run_renderdata);
	pagedata->csv.csv_run_renderdata = NULL;
}

/**
 * stf_dialog_csv_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the CSV Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
stf_dialog_csv_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

	/* Create/get object and fill information struct */
	pagedata->csv.csv_tab             = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_tab"));
	pagedata->csv.csv_colon           = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_colon"));
	pagedata->csv.csv_comma           = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_comma"));
	pagedata->csv.csv_space           = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_space"));
	pagedata->csv.csv_semicolon       = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_semicolon"));
	pagedata->csv.csv_pipe            = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_pipe"));
	pagedata->csv.csv_slash           = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_slash"));
	pagedata->csv.csv_hyphen          = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_hyphen"));
	pagedata->csv.csv_bang            = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_bang"));
	pagedata->csv.csv_custom          = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_custom"));
	pagedata->csv.csv_customseparator = GTK_ENTRY        (glade_xml_get_widget (gui, "csv_customseparator"));

	pagedata->csv.csv_duplicates    = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_duplicates"));
	pagedata->csv.csv_data_container  =                   glade_xml_get_widget (gui, "csv_data_container");

	/* Set properties */
	pagedata->csv.csv_run_renderdata    =
		stf_preview_new (pagedata->csv.csv_data_container,
				 &pagedata->lines,
				 NULL);
	pagedata->csv.csv_run_parseoptions  = stf_parse_options_new ();

	stf_parse_options_set_type  (pagedata->csv.csv_run_parseoptions, PARSE_TYPE_CSV);

	/* Connect signals */
	g_signal_connect (G_OBJECT (pagedata->csv.csv_tab),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_colon),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_comma),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_space),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_semicolon),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_pipe),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_slash),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_hyphen),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_bang),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_custom),
		"toggled",
		G_CALLBACK (csv_page_custom_toggled), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_customseparator),
		"changed",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_duplicates),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);

	g_signal_connect (G_OBJECT (pagedata->csv_page),
		"prepare",
		G_CALLBACK (csv_page_prepare), pagedata);
}

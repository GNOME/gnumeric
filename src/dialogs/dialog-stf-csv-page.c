/*
 * dialog-stf.c : Controls the widget on the CSV (Comma Separated Value) page of the druid
 *
 * Copyright (C) Almer S. Tigelaar <almer@gnome.org>
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
csv_page_global_change (__attribute__((unused)) GtkWidget *widget,
			DruidPageData_t *data)
{
	CsvInfo_t *info = data->csv_info;
	StfParseOptions_t *parseoptions = info->csv_run_parseoptions;
	GList *list;
	GSList *sepstr;
	GString *sepc = g_string_new ("");
	char *textfieldtext;
	int i;

	sepstr = NULL;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_custom))) {
		char *csvcustomtext = gtk_editable_get_chars (GTK_EDITABLE (info->csv_customseparator), 0, -1);

		if (strcmp (csvcustomtext, "") != 0)
			sepstr = g_slist_append (sepstr, csvcustomtext);
		else
			g_free (csvcustomtext);
	}

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_tab)))
		g_string_append_c (sepc, '\t');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_colon)))
		g_string_append_c (sepc, ':');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_comma)))
		g_string_append_c (sepc, ',');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_space)))
		g_string_append_c (sepc, ' ');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_semicolon)))
		g_string_append_c (sepc, ';');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_pipe)))
		g_string_append_c (sepc, '|');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_slash)))
		g_string_append_c (sepc, '/');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_hyphen)))
		g_string_append_c (sepc, '-');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_bang)))
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

	textfieldtext = gtk_editable_get_chars (GTK_EDITABLE (info->csv_textfield), 0, -1);
	stf_parse_options_csv_set_stringindicator (parseoptions, textfieldtext[0]);
	g_free (textfieldtext);

	stf_parse_options_csv_set_indicator_2x_is_single  (parseoptions,
							   gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_2x_indicator)));

	stf_parse_options_csv_set_duplicates (parseoptions,
					      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_duplicates)));

	data->colcount = stf_parse_get_colcount (parseoptions, data->cur);
	stf_preview_colwidths_clear (info->csv_run_renderdata);
	for (i = 0; i < data->colcount + 1; i++)
		stf_preview_colwidths_add (info->csv_run_renderdata, stf_parse_get_colwidth (parseoptions, data->cur, i));

	list = stf_parse_general (parseoptions, data->cur);

	stf_preview_render (info->csv_run_renderdata, list,
			    info->csv_run_displayrows,
			    data->colcount);
}

/**
 * csv_page_scroll_value_changed
 * @adjustment : The gtkadjustment that emitted the signal
 * @data : a mother struct
 *
 * This signal responds to changes in the scrollbar and
 * will force a redraw of the preview
 *
 * returns : nothing
 **/
static void
csv_page_scroll_value_changed (GtkAdjustment *adjustment, DruidPageData_t *data)
{
	CsvInfo_t *info = data->csv_info;

	stf_preview_set_startrow (info->csv_run_renderdata, adjustment->value);
	csv_page_global_change (NULL, data);
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
	CsvInfo_t *info = data->csv_info;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button))) {
		gtk_widget_set_sensitive   (GTK_WIDGET (info->csv_customseparator), TRUE);
		gtk_widget_grab_focus      (GTK_WIDGET (info->csv_customseparator));
		gtk_editable_select_region (GTK_EDITABLE (info->csv_customseparator), 0, -1);

	}
	else {
		gtk_widget_set_sensitive (GTK_WIDGET (info->csv_customseparator), FALSE);
		gtk_editable_select_region (GTK_EDITABLE (info->csv_customseparator), 0, 0); /* If we don't use this the selection will remain blue */
	}

	csv_page_global_change (NULL, data);
}

/*************************************************************************************************
 * CSV EXPORTED FUNCTIONS
 *************************************************************************************************/

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
void
stf_dialog_csv_page_prepare (__attribute__((unused)) GnomeDruidPage *page,
			     __attribute__((unused)) GnomeDruid *druid,
			     DruidPageData_t *pagedata)
{
	CsvInfo_t *info = pagedata->csv_info;

	stf_parse_options_set_trim_spaces (info->csv_run_parseoptions, pagedata->trim);
	pagedata->colcount = stf_parse_get_colcount (info->csv_run_parseoptions, pagedata->cur);

	/*
	 * This piece of code is here to limit the number of rows we display
	 * when previewing
	 */
	{
		int rowcount = stf_parse_get_rowcount (info->csv_run_parseoptions, pagedata->cur) + 1;

		if (rowcount > LINE_DISPLAY_LIMIT) {
			GTK_RANGE (info->csv_scroll)->adjustment->upper = LINE_DISPLAY_LIMIT;
			stf_parse_options_set_lines_to_parse (info->csv_run_parseoptions, LINE_DISPLAY_LIMIT);
		} else {
			GTK_RANGE (info->csv_scroll)->adjustment->upper = pagedata->importlines;
			stf_parse_options_set_lines_to_parse (info->csv_run_parseoptions, pagedata->importlines);
		}
	}

	gtk_adjustment_changed (GTK_RANGE (info->csv_scroll)->adjustment);
	stf_preview_set_startrow (info->csv_run_renderdata, GTK_RANGE (info->csv_scroll)->adjustment->value);

	/* Calling this routine will also automatically call global change which updates the preview too */
	csv_page_custom_toggled (info->csv_custom, pagedata);
}

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
	CsvInfo_t *info = pagedata->csv_info;

	if (info->csv_run_parseoptions) {
		stf_parse_options_free (info->csv_run_parseoptions);
		info->csv_run_parseoptions = NULL;
	}

	stf_preview_free (info->csv_run_renderdata);
	info->csv_run_renderdata = NULL;
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
	CsvInfo_t *info;

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);
	g_return_if_fail (pagedata->csv_info != NULL);

	info = pagedata->csv_info;

	/* Create/get object and fill information struct */
	info->csv_tab             = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_tab"));
	info->csv_colon           = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_colon"));
	info->csv_comma           = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_comma"));
	info->csv_space           = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_space"));
	info->csv_semicolon       = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_semicolon"));
	info->csv_pipe            = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_pipe"));
	info->csv_slash           = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_slash"));
	info->csv_hyphen          = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_hyphen"));
	info->csv_bang            = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_bang"));
	info->csv_custom          = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_custom"));
	info->csv_customseparator = GTK_ENTRY        (glade_xml_get_widget (gui, "csv_customseparator"));

	info->csv_2x_indicator  = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_2x_indicator"));
	info->csv_duplicates    = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_duplicates"));
	info->csv_textindicator = GTK_COMBO        (glade_xml_get_widget (gui, "csv_textindicator"));
	info->csv_textfield     = GTK_ENTRY        (glade_xml_get_widget (gui, "csv_textfield"));

	info->csv_canvas = GNOME_CANVAS   (glade_xml_get_widget (gui, "csv_canvas"));
	info->csv_scroll = GTK_VSCROLLBAR (glade_xml_get_widget (gui, "csv_scroll"));

	/* Set properties */
	info->csv_run_renderdata    = stf_preview_new (info->csv_canvas, FALSE);
	info->csv_run_parseoptions  = stf_parse_options_new ();
	info->csv_run_displayrows   = stf_preview_get_displayed_rowcount (info->csv_run_renderdata);

	stf_parse_options_set_type  (info->csv_run_parseoptions, PARSE_TYPE_CSV);

	/* Connect signals */
	g_signal_connect (G_OBJECT (info->csv_tab),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_colon),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_comma),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_space),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_semicolon),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_pipe),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_slash),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_hyphen),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_bang),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_custom),
		"toggled",
		G_CALLBACK (csv_page_custom_toggled), pagedata);
	g_signal_connect (G_OBJECT (info->csv_customseparator),
		"changed",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_2x_indicator),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_duplicates),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (info->csv_textfield),
		"changed",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (GTK_RANGE (info->csv_scroll)->adjustment),
		"value_changed",
		G_CALLBACK (csv_page_scroll_value_changed), pagedata);
}

/*
 * dialog-stf.c : Controls the widget on the CSV (Comma Separated Value) page.
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <dialogs/dialog-stf.h>
#include <gnm-format.h>
#include <goffice/goffice.h>
#include <string.h>

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * csv_page_global_change
 * @widget: the widget which emitted the signal
 * @data: mother struct
 *
 * This will update the preview based on the state of
 * the widgets on the csv page
 **/
static void
csv_page_global_change (G_GNUC_UNUSED GtkWidget *widget,
			StfDialogData *pagedata)
{
	StfParseOptions_t *parseoptions = pagedata->parseoptions;
	RenderData_t *renderdata = pagedata->csv.renderdata;
	GSList *sepstr;
	GString *sepc = g_string_new (NULL);
	GStringChunk *lines_chunk;
	GPtrArray *lines;
	StfTrimType_t trim;

	sepstr = NULL;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_custom))) {
		char *csvcustomtext = gtk_editable_get_chars (GTK_EDITABLE (pagedata->csv.csv_customseparator), 0, -1);

		if (strcmp (csvcustomtext, "") != 0)
			sepstr = g_slist_append (sepstr, csvcustomtext);
		else
			g_free (csvcustomtext);
	}

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_tab)))
		g_string_append_c (sepc, '\t');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_colon)))
		g_string_append_c (sepc, ':');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_comma)))
		g_string_append_c (sepc, ',');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_space)))
		g_string_append_c (sepc, ' ');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_semicolon)))
		g_string_append_c (sepc, ';');
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_hyphen)))
		g_string_append_c (sepc, '-');

	stf_parse_options_csv_set_separators (parseoptions,
					      strcmp (sepc->str, "") == 0 ? NULL : sepc->str,
					      sepstr);
	g_string_free (sepc, TRUE);
	g_slist_free_full (sepstr, g_free);

	stf_parse_options_csv_set_duplicates (parseoptions,
					      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_duplicates)));
	stf_parse_options_csv_set_trim_seps (parseoptions,
					     gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_trim_seps)));

	lines_chunk = g_string_chunk_new (100 * 1024);

	/* Don't trim on this page.  */
	trim = parseoptions->trim_spaces;
	stf_parse_options_set_trim_spaces (parseoptions, TRIM_TYPE_NEVER);
	lines = stf_parse_general (parseoptions, lines_chunk,
				   pagedata->cur, pagedata->cur_end);
	stf_parse_options_set_trim_spaces (parseoptions, trim);

	stf_preview_set_lines (renderdata, lines_chunk, lines);
}

/**
 * csv_page_custom_toggled
 * @button: the Checkbutton that emitted the signal
 * @data: a mother struct
 *
 * This will nicely activate the @data->csv_info->csv_customseparator widget
 * so the user can enter text into it.
 * It will also gray out this widget if the @button is not selected.
 **/
static void
csv_page_custom_toggled (GtkCheckButton *button, StfDialogData *data)
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


static void
csv_page_textindicator_change (G_GNUC_UNUSED GtkWidget *widget,
			       StfDialogData *data)
{
	char *textfieldtext = gtk_editable_get_chars (GTK_EDITABLE (data->csv.csv_textfield), 0, -1);
	gunichar str_ind = g_utf8_get_char (textfieldtext);

	stf_parse_options_csv_set_stringindicator (data->parseoptions, str_ind);
	g_free (textfieldtext);

	stf_parse_options_csv_set_indicator_2x_is_single  (data->parseoptions,
							   gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->csv.csv_2x_indicator)));
	csv_page_global_change (NULL, data);
}

static void
csv_page_parseoptions_to_gui (StfDialogData *pagedata)
{
	StfParseOptions_t *po = pagedata->parseoptions;

	{
		const char *sep;
		gboolean s_tab = FALSE;
		gboolean s_colon = FALSE;
		gboolean s_comma = FALSE;
		gboolean s_space = FALSE;
		gboolean s_semicolon = FALSE;
		gboolean s_hyphen = FALSE;

		if (po->sep.chr)
			for (sep = po->sep.chr; *sep; sep++) {
				switch (*sep) {
				case '\t': s_tab = TRUE; break;
				case ':': s_colon = TRUE; break;
				case ',': s_comma = TRUE; break;
				case ' ': s_space = TRUE; break;
				case ';': s_semicolon = TRUE; break;
				case '-': s_hyphen = TRUE; break;
				default: break;
				}
			}
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_tab), s_tab);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_colon), s_colon);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_comma), s_comma);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_space), s_space);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_semicolon), s_semicolon);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_hyphen), s_hyphen);
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_duplicates),
				      po->sep.duplicates);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_trim_seps),
				      po->trim_seps);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->csv.csv_2x_indicator),
				      po->indicator_2x_is_single);

	{
		char buffer[7];
		gint len;
		len = g_unichar_to_utf8 (po->stringindicator, buffer);
		buffer[len] = 0;
		gtk_combo_box_set_active_id
			(GTK_COMBO_BOX (pagedata->csv.csv_textindicator),
			 buffer);
	}
}


/**
 * stf_dialog_csv_page_prepare
 * @pagedata: mother struct
 *
 * Will prepare the csv page
 **/
void
stf_dialog_csv_page_prepare (StfDialogData *pagedata)
{
	csv_page_parseoptions_to_gui (pagedata);

	/* Calling this routine will also automatically call global change which updates the preview too */
	csv_page_custom_toggled (pagedata->csv.csv_custom, pagedata);
}

/*************************************************************************************************
 * CSV EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_csv_page_cleanup
 * @pagedata: mother struct
 *
 * Will cleanup csv page run-time data
 **/
void
stf_dialog_csv_page_cleanup (StfDialogData *pagedata)
{
	stf_preview_free (pagedata->csv.renderdata);
	pagedata->csv.renderdata = NULL;
}

void
stf_dialog_csv_page_init (GtkBuilder *gui, StfDialogData *pagedata)
{
	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

	/* Create/get object and fill information struct */
	pagedata->csv.csv_tab             = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_tab"));
	pagedata->csv.csv_colon           = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_colon"));
	pagedata->csv.csv_comma           = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_comma"));
	pagedata->csv.csv_space           = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_space"));
	pagedata->csv.csv_semicolon       = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_semicolon"));
	pagedata->csv.csv_hyphen          = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_hyphen"));
	pagedata->csv.csv_custom          = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_custom"));
	pagedata->csv.csv_customseparator = GTK_ENTRY        (go_gtk_builder_get_widget (gui, "csv_customseparator"));
	pagedata->csv.csv_2x_indicator    = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_2x_indicator"));

	pagedata->csv.csv_textindicator   = go_gtk_builder_get_widget (gui, "csv-textindicator");
	pagedata->csv.csv_textfield       = GTK_ENTRY    (gtk_bin_get_child (GTK_BIN (pagedata->csv.csv_textindicator)));

	pagedata->csv.csv_duplicates      = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_duplicates"));
	pagedata->csv.csv_trim_seps       = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "csv_trim_seps"));
	pagedata->csv.csv_data_container  =                   go_gtk_builder_get_widget (gui, "csv_data_container");

	/* Set properties */
	pagedata->csv.renderdata    =
		stf_preview_new (pagedata->csv.csv_data_container,
				 NULL);
	csv_page_parseoptions_to_gui (pagedata);

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
	g_signal_connect (G_OBJECT (pagedata->csv.csv_hyphen),
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
	g_signal_connect (G_OBJECT (pagedata->csv.csv_trim_seps),
		"toggled",
		G_CALLBACK (csv_page_global_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_2x_indicator),
		"toggled",
		G_CALLBACK (csv_page_textindicator_change), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv.csv_textfield),
		"changed",
		G_CALLBACK (csv_page_textindicator_change), pagedata);
}

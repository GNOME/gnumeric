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

static gboolean
main_page_set_encoding (DruidPageData_t *pagedata, const char *enc)
{
	char *utf8_data;
	gsize bytes_read = -1;
	gsize bytes_written = -1;
	GError *error = NULL;

	if (!enc) return FALSE;

	utf8_data = g_convert (pagedata->raw_data, -1,
			       "UTF-8", enc,
			       &bytes_read, &bytes_written, &error);
	if (error) {
		g_free (utf8_data);
		g_error_free (error);
		/* FIXME: What to do with error?  */
		return FALSE;
	}

	if (!charmap_selector_set_encoding (pagedata->main.charmap_selector, enc)) {
		g_free (utf8_data);
		return FALSE;
	}

	g_free (pagedata->utf8_data);
	pagedata->utf8_data = utf8_data;

	if (enc != pagedata->encoding) {
		g_free (pagedata->encoding);
		pagedata->encoding = g_strdup (enc);
	}

	return TRUE;
}


static void
main_page_update_preview (DruidPageData_t *pagedata)
{
	RenderData_t *renderdata = pagedata->main.renderdata;

	stf_preview_set_lines (renderdata,
			       stf_parse_lines (pagedata->parseoptions,
						pagedata->utf8_data, TRUE));
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

	data->cur = stf_parse_find_line (data->parseoptions, data->utf8_data, startrow - 1);
	data->cur_end = stf_parse_find_line (data->parseoptions, data->utf8_data, stoprow);

	linescaption = g_strdup_printf (_("%d of %d lines to import"),
					(stoprow - startrow) + 1,
					renderdata->lines->len);
	gtk_label_set_text (data->main.main_lines, linescaption);
	g_free (linescaption);
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

static void
encodings_changed_cb (CharmapSelector *cs, char const *new_charmap,
		      DruidPageData_t *pagedata)
{
	if (main_page_set_encoding (pagedata, new_charmap)) {
		main_page_import_range_changed (pagedata);
		main_page_update_preview (pagedata);
	} else {
		const char *name = charmap_selector_get_encoding_name (cs, new_charmap);
		char *msg = g_strdup_printf
			(_("The data is not valid in encoding %s; "
			   "please select another encoding."),
			 name ? name : new_charmap);
		gnumeric_notice (pagedata->wbcg, GTK_MESSAGE_ERROR, msg);
		g_free (msg);

		charmap_selector_set_encoding (pagedata->main.charmap_selector,
					       pagedata->encoding);
	}
}

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
	main_page_import_range_changed (data);
}

/**
 * main_page_stoprow_changed
 * @button : the spinbutton the event handler is attached to
 * @data : mother struct
 *
 * returns : nothing
 **/
static void
main_page_stoprow_changed (GtkSpinButton* button,
			   DruidPageData_t *data)
{
	main_page_import_range_changed (data);
}

static void
main_page_stringindicator_change (G_GNUC_UNUSED GtkWidget *widget,
				  DruidPageData_t *data)
{
	char *textfieldtext;
	gunichar str_ind;

	textfieldtext = gtk_editable_get_chars (GTK_EDITABLE (data->main.main_textfield), 0, -1);
	str_ind = g_utf8_get_char (textfieldtext);
	if (str_ind != '\0')
	     stf_parse_options_csv_set_stringindicator (data->parseoptions,
							str_ind);
	g_free (textfieldtext);

	stf_parse_options_csv_set_indicator_2x_is_single  (data->parseoptions,
							   gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main.main_2x_indicator)));
}

static void
main_page_source_format_toggled (G_GNUC_UNUSED GtkWidget *widget,
				 DruidPageData_t *data)
{
	gboolean active = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (data->main.main_separated));

	gtk_widget_set_sensitive (GTK_WIDGET (data->main.main_2x_indicator), active);
	gtk_widget_set_sensitive (GTK_WIDGET (data->main.main_textindicator), active);
	gtk_widget_set_sensitive (GTK_WIDGET (data->main.main_textfield), active);

	stf_parse_options_set_type (data->parseoptions,
				    active ? PARSE_TYPE_CSV : PARSE_TYPE_FIXED);
}

static void
cb_line_breaks (G_GNUC_UNUSED GtkWidget *widget,
		DruidPageData_t *data)
{
	gboolean to_end = (gtk_spin_button_get_value_as_int (data->main.main_stoprow) ==
			   (int)data->main.renderdata->lines->len);

	stf_parse_options_clear_line_terminator (data->parseoptions);
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main.line_break_unix)))
		stf_parse_options_add_line_terminator (data->parseoptions, "\n");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main.line_break_windows)))
		stf_parse_options_add_line_terminator (data->parseoptions, "\r\n");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main.line_break_mac)))
		stf_parse_options_add_line_terminator (data->parseoptions, "\r");

	main_page_update_preview (data);
	main_page_import_range_changed (data);

	/* If the selected area went all the way to the end, follow it there.  */
	if (to_end) {
		gtk_spin_button_set_value (data->main.main_stoprow,
					   data->main.renderdata->lines->len);
		main_page_import_range_changed (data);
	}
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
	main_page_source_format_toggled (NULL, pagedata);
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
	char const *locale_encoding;

	g_get_charset (&locale_encoding);

	pagedata->main.main_separated = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "main_separated"));
	pagedata->main.main_fixed     = GTK_RADIO_BUTTON (glade_xml_get_widget (gui, "main_fixed"));
	pagedata->main.main_startrow  = GTK_SPIN_BUTTON  (glade_xml_get_widget (gui, "main_startrow"));
	pagedata->main.main_stoprow   = GTK_SPIN_BUTTON  (glade_xml_get_widget (gui, "main_stoprow"));
	pagedata->main.main_lines     = GTK_LABEL        (glade_xml_get_widget (gui, "main_lines"));
	pagedata->main.main_data_container =              glade_xml_get_widget (gui, "main_data_container");
	pagedata->main.main_2x_indicator  = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "main_2x_indicator"));
	pagedata->main.main_textindicator = GTK_COMBO    (glade_xml_get_widget (gui, "main_textindicator"));
	pagedata->main.main_textfield     = GTK_ENTRY    (glade_xml_get_widget (gui, "main_textfield"));
	pagedata->main.line_break_unix    = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "line_break_unix"));
	pagedata->main.line_break_windows = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "line_break_windows"));
	pagedata->main.line_break_mac     = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "line_break_mac"));

	pagedata->main.charmap_selector = CHARMAP_SELECTOR (charmap_selector_new (CHARMAP_SELECTOR_TO_UTF8));
	if (!main_page_set_encoding (pagedata, pagedata->encoding) &&
	    !main_page_set_encoding (pagedata, locale_encoding) &&
	    !main_page_set_encoding (pagedata, "ASCII") &&
	    !main_page_set_encoding (pagedata, "ISO-8859-1") &&
	    !main_page_set_encoding (pagedata, "UTF-8"))
		g_warning ("This is not good -- failed to find a valid encoding of data!");
	gtk_container_add (GTK_CONTAINER (glade_xml_get_widget (gui, "encoding_hbox")),
			   GTK_WIDGET (pagedata->main.charmap_selector));
	gtk_widget_show_all (GTK_WIDGET (pagedata->main.charmap_selector));
	gtk_widget_set_sensitive (GTK_WIDGET (pagedata->main.charmap_selector),
				  !pagedata->fixed_encoding);

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
		char *label = g_strdup_printf (_("Data (from %s)"), pagedata->source);
		gtk_frame_set_label (main_frame, label);
		g_free (label);
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

	g_signal_connect (G_OBJECT (pagedata->main.line_break_unix),
			  "toggled",
			  G_CALLBACK (cb_line_breaks), pagedata);
	g_signal_connect (G_OBJECT (pagedata->main.line_break_windows),
			  "toggled",
			  G_CALLBACK (cb_line_breaks), pagedata);
	g_signal_connect (G_OBJECT (pagedata->main.line_break_mac),
			  "toggled",
			  G_CALLBACK (cb_line_breaks), pagedata);

	g_signal_connect (G_OBJECT (pagedata->main_page),
		"prepare",
		G_CALLBACK (main_page_prepare), pagedata);

	g_signal_connect (G_OBJECT (pagedata->main.charmap_selector),
			  "charmap_changed",
			  G_CALLBACK (encodings_changed_cb), pagedata);

	main_page_source_format_toggled (NULL, pagedata);
	main_page_import_range_changed (pagedata);
}

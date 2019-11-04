/*
 * dialog-stf-main-page.c : controls the widget on the main page
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include <dialogs/dialog-stf.h>
#include <gui-util.h>
#include <sheet.h>
#include <workbook.h>
#include <goffice/goffice.h>
#include <string.h>

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

static gboolean
main_page_set_encoding (StfDialogData *pagedata, const char *enc)
{
	char *utf8_data;
	gsize bytes_read = -1;
	gsize bytes_written = -1;
	GError *error = NULL;

	if (!enc) return FALSE;

	utf8_data = g_convert (pagedata->raw_data, pagedata->raw_data_len,
			       "UTF-8", enc,
			       &bytes_read, &bytes_written, &error);

	/*
	 * It _is_ possible to get NULL error, but not have valid UTF-8.
	 * Specifically observed with UTF-16BE.
	 */
	if (error || !g_utf8_validate (utf8_data, -1, NULL)) {
		g_free (utf8_data);
		if (error) {
			/* FIXME: What to do with error?  */
			g_error_free (error);
		}
		return FALSE;
	}


	if (!go_charmap_sel_set_encoding (pagedata->main.charmap_selector, enc)) {
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
main_page_update_preview (StfDialogData *pagedata)
{
	RenderData_t *renderdata = pagedata->main.renderdata;
	GStringChunk *lines_chunk = g_string_chunk_new (100 * 1024);
	GPtrArray *lines = stf_parse_lines (pagedata->parseoptions,
					    lines_chunk,
					    pagedata->utf8_data,
					    INT_MAX,
					    TRUE);
        unsigned int ui;

	pagedata->rowcount = lines->len;
	pagedata->longest_line = 0;
	for (ui = 0; ui < lines->len; ui++) {
		GPtrArray *line = g_ptr_array_index (lines, ui);
		int thislen = g_utf8_strlen (g_ptr_array_index (line, 1), -1);
		pagedata->longest_line = MAX (pagedata->longest_line, thislen);
	}

	stf_preview_set_lines (renderdata, lines_chunk, lines);
}


/**
 * main_page_set_spin_button_adjustment
 * @spinbutton: the spinbutton to adjust
 * @min: the minimum number the user may enter into the spinbutton
 * @max: the maximum number the user may enter into the spinbutton
 **/
static void
main_page_set_spin_button_adjustment (GtkSpinButton* spinbutton, int min, int max)
{
	GtkAdjustment *spinadjust;

	spinadjust = gtk_spin_button_get_adjustment (spinbutton);
	gtk_adjustment_set_lower (spinadjust, min);
	gtk_adjustment_set_upper (spinadjust, max);
}

/**
 * main_page_import_range_changed
 * @data: mother struct
 *
 * Updates the "number of lines to import" label on the main page
 **/
static void
main_page_import_range_changed (StfDialogData *data)
{
	RenderData_t *renderdata = data->main.renderdata;
	int startrow, stoprow, stoplimit;
	char *linescaption;

	g_return_if_fail (renderdata->lines != NULL);

	startrow = gtk_spin_button_get_value_as_int (data->main.main_startrow);
	stoprow  = gtk_spin_button_get_value_as_int (data->main.main_stoprow);

	stoprow = MAX (1, stoprow);
	startrow = MIN (stoprow, MAX (1, startrow));

	stoplimit = MIN ((int)renderdata->lines->len,
			 startrow + (GNM_MAX_ROWS - 1));
	stoprow = MIN (stoprow, stoplimit);

	gtk_spin_button_set_value (data->main.main_startrow, startrow);
	main_page_set_spin_button_adjustment (data->main.main_startrow, 1, stoprow);

	gtk_spin_button_set_value (data->main.main_stoprow, stoprow);
	main_page_set_spin_button_adjustment (data->main.main_stoprow, startrow, stoplimit);

	data->cur = stf_parse_find_line (data->parseoptions, data->utf8_data, startrow - 1);
	data->cur_end = stf_parse_find_line (data->parseoptions, data->utf8_data, stoprow);

	linescaption = g_strdup_printf (ngettext("%d of %d line to import",
						 "%d of %d lines to import",
						 renderdata->lines->len),
					(stoprow - startrow) + 1,
					renderdata->lines->len);
	gtk_label_set_text (data->main.main_lines, linescaption);
	g_free (linescaption);
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

static void
encodings_changed_cb (GOCharmapSel *cs, char const *new_charmap,
		      StfDialogData *pagedata)
{
	if (main_page_set_encoding (pagedata, new_charmap)) {
		main_page_update_preview (pagedata);
		main_page_import_range_changed (pagedata);
	} else {
		const char *name = go_charmap_sel_get_encoding_name (cs, new_charmap);
		char *msg = g_strdup_printf
			(_("The data is not valid in encoding %s; "
			   "please select another encoding."),
			 name ? name : new_charmap);
		go_gtk_notice_dialog (GTK_WINDOW (pagedata->dialog),
				      GTK_MESSAGE_ERROR,
				      "%s", msg);
		g_free (msg);

		go_charmap_sel_set_encoding (pagedata->main.charmap_selector,
					       pagedata->encoding);
	}
}

/**
 * main_page_startrow_changed
 * @button: the spinbutton the event handler is attached to
 * @data: mother struct
 *
 * This function will adjust the amount of displayed text to
 * reflect the number of lines the user wants to import
 **/
static void
main_page_startrow_changed (GtkSpinButton* button, StfDialogData *data)
{
	main_page_import_range_changed (data);
}

/**
 * main_page_stoprow_changed
 * @button: the spinbutton the event handler is attached to
 * @data: mother struct
 **/
static void
main_page_stoprow_changed (GtkSpinButton* button,
			   StfDialogData *data)
{
	main_page_import_range_changed (data);
}

static void
main_page_source_format_toggled (G_GNUC_UNUSED GtkWidget *widget,
				 StfDialogData *data)
{
	gboolean separated = gtk_toggle_button_get_active
		(GTK_TOGGLE_BUTTON (data->main.main_separated));

	stf_parse_options_set_type (data->parseoptions,
				    separated ? PARSE_TYPE_CSV : PARSE_TYPE_FIXED);
}

static void
cb_line_breaks (G_GNUC_UNUSED GtkWidget *widget,
		StfDialogData *data)
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
 * stf_dialog_main_page_prepare
 * @pagedata: mother struct
 *
 * This will prepare the widgets on the format page before
 * the page gets displayed
 **/
void
stf_dialog_main_page_prepare (StfDialogData *pagedata)
{
	main_page_source_format_toggled (NULL, pagedata);
	main_page_update_preview (pagedata);
}

static void
main_page_parseoptions_to_gui (StfDialogData *pagedata)
{
	StfParseOptions_t *po = pagedata->parseoptions;

#if 0
	go_charmap_sel_set_encoding (pagedata->main.charmap_selector, pagedata->encoding);
#endif

	switch (po->parsetype) {
	case PARSE_TYPE_CSV:
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->main.main_separated),
					      TRUE);
		break;
	case PARSE_TYPE_FIXED:
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pagedata->main.main_fixed),
					      TRUE);
		break;
	default:
		break;
	}

	{
		gboolean lb_unix = FALSE, lb_windows = FALSE, lb_mac = FALSE;
		GSList *l;

		for (l = po->terminator; l; l = l->next) {
			const char *term = l->data;
			if (strcmp (term, "\n") == 0)
				lb_unix = TRUE;
			else if (strcmp (term, "\r\n") == 0)
				lb_windows = TRUE;
			else if (strcmp (term, "\r") == 0)
				lb_mac = TRUE;
		}

		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (pagedata->main.line_break_unix),
			 lb_unix);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (pagedata->main.line_break_windows),
			 lb_windows);
		gtk_toggle_button_set_active
			(GTK_TOGGLE_BUTTON (pagedata->main.line_break_mac),
			 lb_mac);
	}
}

/*************************************************************************************************
 * MAIN EXPORTED FUNCTIONS
 *************************************************************************************************/

void
stf_dialog_main_page_cleanup (StfDialogData *pagedata)
{
	stf_preview_free (pagedata->main.renderdata);
}

void
stf_dialog_main_page_init (GtkBuilder *gui, StfDialogData *pagedata)
{
	RenderData_t *renderdata;
	GtkTreeViewColumn *column;
	const char *encoding_guess;

	encoding_guess = go_guess_encoding (pagedata->raw_data, pagedata->raw_data_len,
					    "ASCII",
					    NULL, NULL);

	pagedata->main.main_separated = GTK_RADIO_BUTTON (go_gtk_builder_get_widget (gui, "main_separated"));
	pagedata->main.main_fixed     = GTK_RADIO_BUTTON (go_gtk_builder_get_widget (gui, "main_fixed"));
	pagedata->main.main_startrow  = GTK_SPIN_BUTTON  (go_gtk_builder_get_widget (gui, "main_startrow"));
	pagedata->main.main_stoprow   = GTK_SPIN_BUTTON  (go_gtk_builder_get_widget (gui, "main_stoprow"));
	pagedata->main.main_lines     = GTK_LABEL        (go_gtk_builder_get_widget (gui, "main_lines"));
	pagedata->main.main_data_container =              go_gtk_builder_get_widget (gui, "main_data_container");
	pagedata->main.line_break_unix    = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "line_break_unix"));
	pagedata->main.line_break_windows = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "line_break_windows"));
	pagedata->main.line_break_mac     = GTK_CHECK_BUTTON (go_gtk_builder_get_widget (gui, "line_break_mac"));

	pagedata->main.charmap_selector = GO_CHARMAP_SEL (go_charmap_sel_new (GO_CHARMAP_SEL_TO_UTF8));
	if (!main_page_set_encoding (pagedata, pagedata->encoding) &&
	    !main_page_set_encoding (pagedata, encoding_guess)) {
		g_warning ("This is not good -- failed to find a valid encoding of data!");
		pagedata->raw_data_len = 0;
		main_page_set_encoding (pagedata, "ASCII");
	}
	gtk_grid_attach (GTK_GRID (go_gtk_builder_get_widget (gui, "format-grid")),
			   GTK_WIDGET (pagedata->main.charmap_selector), 1, 0, 1, 1);
	gtk_widget_show_all (GTK_WIDGET (pagedata->main.charmap_selector));
	gtk_widget_set_sensitive (GTK_WIDGET (pagedata->main.charmap_selector),
				  !pagedata->fixed_encoding);

	pagedata->parseoptions = stf_parse_options_guess (pagedata->utf8_data);
	main_page_parseoptions_to_gui (pagedata);

	renderdata = pagedata->main.renderdata = stf_preview_new
		(pagedata->main.main_data_container,
		 NULL);
	renderdata->ignore_formats = TRUE;

	main_page_update_preview (pagedata);

	column = stf_preview_get_column (renderdata, 0);
	if (column) {
		/* This probably cannot happen.  */
		GtkCellRenderer *cell = stf_preview_get_cell_renderer (renderdata, 0);
		gtk_tree_view_column_set_title (column, _("Line"));
		g_object_set (G_OBJECT (cell),
			      "xalign", 1.0,
			      "style", PANGO_STYLE_ITALIC,
			      "background", "lightgrey",
			      NULL);
	}

	column = stf_preview_get_column (renderdata, 1);
	if (column) {
		/* In case of an empty file, there will be no column.  */
		GtkCellRenderer *cell = stf_preview_get_cell_renderer (renderdata, 1);
		gtk_tree_view_column_set_title (column, _("Text"));
		g_object_set (G_OBJECT (cell),
			      "family", "monospace",
			      NULL);
	}

	/* Set properties */
	main_page_set_spin_button_adjustment (pagedata->main.main_startrow, 1, renderdata->lines->len);
	main_page_set_spin_button_adjustment (pagedata->main.main_stoprow, 1, renderdata->lines->len);
	gtk_spin_button_set_value (pagedata->main.main_stoprow, renderdata->lines->len);

	{
		GtkLabel *data_label = GTK_LABEL (gtk_builder_get_object (gui, "data-lbl"));
		char *label = g_strdup_printf (_("Data (from %s)"), pagedata->source);
		gtk_label_set_label (data_label, label);
		g_free (label);
	}

	/* Connect signals */
	g_signal_connect (G_OBJECT (pagedata->main.main_startrow),
		"value-changed",
		G_CALLBACK (main_page_startrow_changed), pagedata);
	g_signal_connect (G_OBJECT (pagedata->main.main_stoprow),
		"value-changed",
		G_CALLBACK (main_page_stoprow_changed), pagedata);
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

	g_signal_connect (G_OBJECT (pagedata->main.charmap_selector),
			  "charmap_changed",
			  G_CALLBACK (encodings_changed_cb), pagedata);

	main_page_source_format_toggled (NULL, pagedata);
	main_page_import_range_changed (pagedata);
}

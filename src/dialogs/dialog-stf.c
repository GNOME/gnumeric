/*
 * dialog-stf.c: implementation of the STF import dialog
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
#include <glib/gi18n.h>
#include <gnumeric.h>
#include "dialog-stf.h"

#include <format.h>
#include <command-context.h>
#include <gui-util.h>
#include <gdk/gdkkeysyms.h>
#include <sheet-style.h>
#include <mstyle.h>
#include <clipboard.h>
#include <gtk/gtkmain.h>

/**********************************************************************************************
 * DIALOG CONTROLLING CODE
 **********************************************************************************************/

/**
 * stf_dialog_set_initial_keyboard_focus
 * @pagedata: mother struct
 *
 * Sets keyboard focus to the an appropriate widget on the page.
 *
 * returns: nothing
 **/
static void
stf_dialog_set_initial_keyboard_focus (StfDialogData *pagedata)
{
	GtkWidget *focus_widget = NULL;

	switch (gtk_notebook_get_current_page (pagedata->notebook)) {
	case DPG_MAIN:
		focus_widget = GTK_WIDGET (pagedata->main.main_separated);
		break;
	case DPG_CSV:
		focus_widget = GTK_WIDGET (pagedata->csv.csv_space);
		break;
	case DPG_FIXED:
		focus_widget = GTK_WIDGET (pagedata->fixed.fixed_auto);
		break;
	case DPG_FORMAT:
		number_format_selector_set_focus (pagedata->format.format_selector);
		break;
	default:
		g_assert_not_reached ();
	}

	if (focus_widget)
		gtk_widget_grab_focus (focus_widget);
}

static void
frob_buttons (StfDialogData *pagedata)
{
	StfDialogPage pos =
		gtk_notebook_get_current_page (pagedata->notebook);

	if (pos == DPG_FORMAT) {
		gtk_widget_show (pagedata->finish_button);
		gtk_widget_hide (pagedata->next_button);
	} else {
		gtk_widget_hide (pagedata->finish_button);
		gtk_widget_show (pagedata->next_button);
	}
	gtk_widget_set_sensitive (pagedata->back_button, pos != DPG_MAIN);
}

static void
prepare_page (StfDialogData *data)
{
	switch (gtk_notebook_get_current_page (data->notebook)) {
	case DPG_MAIN:   stf_dialog_main_page_prepare (data); break;
	case DPG_CSV:    stf_dialog_csv_page_prepare (data); break;
	case DPG_FIXED:  stf_dialog_fixed_page_prepare (data); break;
	case DPG_FORMAT: stf_dialog_format_page_prepare (data); break;
	}
}



static void
next_clicked (G_GNUC_UNUSED GtkWidget *widget, StfDialogData *data)
{
	StfDialogPage newpos;

	switch (gtk_notebook_get_current_page (data->notebook)) {
	case DPG_MAIN:
		stf_preview_set_lines (data->main.renderdata, NULL, NULL);
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main.main_separated))) {
			newpos = DPG_CSV;
		} else {
			newpos = DPG_FIXED;
		}
		break;

	case DPG_CSV:
		stf_preview_set_lines (data->csv.renderdata, NULL, NULL);
		newpos = DPG_FORMAT;
		break;

        case DPG_FIXED:
		stf_preview_set_lines (data->fixed.renderdata, NULL, NULL);
		newpos = DPG_FORMAT;
		break;

	default:
		g_assert_not_reached ();
		return;
	}

	gtk_notebook_set_current_page (data->notebook, newpos);
	prepare_page (data);
	frob_buttons (data);
	stf_dialog_set_initial_keyboard_focus (data);
}

static void
back_clicked (G_GNUC_UNUSED GtkWidget *widget, StfDialogData *data)
{
	StfDialogPage newpos;

	switch (gtk_notebook_get_current_page (data->notebook)) {
	case DPG_FORMAT:
		stf_preview_set_lines (data->format.renderdata, NULL, NULL);
		if (data->parseoptions->parsetype == PARSE_TYPE_CSV)
			newpos = DPG_CSV;
		else
			newpos = DPG_FIXED;
		break;

	case DPG_FIXED:
		stf_preview_set_lines (data->fixed.renderdata, NULL, NULL);
		newpos = DPG_MAIN;
		break;

	case DPG_CSV:
		stf_preview_set_lines (data->csv.renderdata, NULL, NULL);
		newpos = DPG_MAIN;
		break;

	default:
		g_assert_not_reached ();
		return;
	}

	gtk_notebook_set_current_page (data->notebook, newpos);
	prepare_page (data);
	frob_buttons (data);
	stf_dialog_set_initial_keyboard_focus (data);
}


static void
cancel_clicked (G_GNUC_UNUSED GtkWidget *widget, StfDialogData *data)
{
	if (gnumeric_dialog_question_yes_no
	    (GTK_WINDOW (data->dialog),
	     _("Are you sure you want to cancel?"),
	     FALSE)) {
		data->canceled = TRUE;
		gtk_widget_destroy (GTK_WIDGET (data->dialog));
		gtk_main_quit ();
	}
}

static void
finish_clicked (G_GNUC_UNUSED GtkWidget *widget, StfDialogData *data)
{
	gtk_widget_destroy (GTK_WIDGET (data->dialog));
	gtk_main_quit ();
}

/**
 * stf_dialog_window_delete
 *
 * Stops the import and indicates the user has cancelled
 **/
static gboolean
stf_dialog_window_delete (G_GNUC_UNUSED GtkDialog *dialog,
			  G_GNUC_UNUSED GdkEventKey *event,
			  StfDialogData *data)
{
	data->canceled = TRUE;
	gtk_main_quit ();
	return TRUE;
}

/**
 * stf_dialog_check_escape
 *
 * Stops the import if the user pressed escape.
 *
 * returns: TRUE if we handled the keypress, FALSE if we pass it on.
 **/
static gint
stf_dialog_check_escape (G_GNUC_UNUSED GtkDialog *dialog,
			 GdkEventKey *event, StfDialogData *data)
{
	if (event->keyval == GDK_Escape) {
		data->canceled = TRUE;
		gtk_widget_destroy (GTK_WIDGET (data->dialog));
		gtk_main_quit ();
		return TRUE;
	} else
		return FALSE;
}

/**
 * stf_dialog_attach_page_signals
 * @gui: the glade gui of the dialog
 * @pagedata: mother struct
 *
 * Connects all signals to all pages and fills the mother struct
 * The page flow looks like:
 *
 * main_page /- csv_page   -\ format_page
 *           \- fixed_page -/
 *
 * returns: nothing
 **/
static void
stf_dialog_attach_page_signals (GladeXML *gui, StfDialogData *pagedata)
{
	frob_buttons (pagedata);
	/* Signals for individual pages */

	g_signal_connect (G_OBJECT (pagedata->next_button),
			  "clicked",
			  G_CALLBACK (next_clicked), pagedata);

	g_signal_connect (G_OBJECT (pagedata->back_button),
			  "clicked",
			  G_CALLBACK (back_clicked), pagedata);

	g_signal_connect (G_OBJECT (pagedata->cancel_button),
			  "clicked",
			  G_CALLBACK (cancel_clicked), pagedata);

	g_signal_connect (G_OBJECT (pagedata->finish_button),
			  "clicked",
			  G_CALLBACK (finish_clicked), pagedata);

	/* And for the surrounding dialog */
	g_signal_connect (G_OBJECT (pagedata->dialog),
		"key_press_event",
		G_CALLBACK (stf_dialog_check_escape), pagedata);
	g_signal_connect (G_OBJECT (pagedata->dialog),
		"delete_event",
		G_CALLBACK (stf_dialog_window_delete), pagedata);
}

/**
 * stf_dialog_editables_enter
 * @pagedata: mother struct
 *
 * Make <Ret> in text fields activate default.
 *
 * returns: nothing
 **/
static void
stf_dialog_editables_enter (StfDialogData *pagedata)
{
#if 0
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (&pagedata->main.main_startrow->entry));
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (&pagedata->main.main_stoprow->entry));
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (pagedata->csv.csv_customseparator));
	gnumeric_combo_enters
		(pagedata->window,
		 pagedata->csv.csv_textindicator);
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (&pagedata->fixed.fixed_colend->entry));
	number_format_selector_editable_enters
		(pagedata->format.format_selector,
	         pagedata->window);
#endif
}

/**
 * stf_dialog
 * @wbcg: a Commandcontext (can be NULL)
 * @source: name of the file we are importing (or data) in UTF-8
 * @data: the data itself
 *
 * This will start the import.
 * (NOTE: you have to free the DialogStfResult_t that this function returns yourself)
 *
 * returns: A DialogStfResult_t struct on success, NULL otherwise.
 **/
DialogStfResult_t*
stf_dialog (WorkbookControlGUI *wbcg,
	    const char *opt_encoding,
	    gboolean fixed_encoding,
	    const char *opt_locale,
	    gboolean fixed_locale,
	    const char *source,
	    const char *data,
	    int data_len)
{
	GladeXML *gui;
	DialogStfResult_t *dialogresult;
	StfDialogData pagedata;

	g_return_val_if_fail (opt_encoding != NULL || !fixed_encoding, NULL);
	g_return_val_if_fail (opt_locale != NULL || !fixed_locale, NULL);
	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (data != NULL, NULL);

	gui = gnm_glade_xml_new (GNM_CMD_CONTEXT (wbcg),
		"dialog-stf.glade", NULL, NULL);
	if (gui == NULL)
		return NULL;

	pagedata.canceled = FALSE;

	pagedata.encoding = g_strdup (opt_encoding);
	pagedata.fixed_encoding = fixed_encoding;
	pagedata.locale = g_strdup (opt_locale);
	pagedata.fixed_locale = fixed_locale;
	pagedata.wbcg = wbcg;
	pagedata.source = source;
	pagedata.raw_data = data;
	pagedata.raw_data_len = data_len < 0 ? (int)strlen (data) : data_len;
	pagedata.utf8_data = NULL;
	pagedata.cur = NULL;

	pagedata.dialog         = GTK_DIALOG  (glade_xml_get_widget (gui, "stf_dialog"));
	pagedata.notebook       = GTK_NOTEBOOK (glade_xml_get_widget (gui, "stf_notebook"));
	pagedata.next_button    = glade_xml_get_widget (gui, "forward_button");
	pagedata.back_button    = glade_xml_get_widget (gui, "back_button");
	pagedata.cancel_button    = glade_xml_get_widget (gui, "cancel_button");
	pagedata.help_button    = glade_xml_get_widget (gui, "help_button");
	pagedata.finish_button    = glade_xml_get_widget (gui, "finish_button");
	pagedata.parseoptions = NULL;

	stf_dialog_main_page_init   (gui, &pagedata);
	stf_dialog_csv_page_init    (gui, &pagedata);
	stf_dialog_fixed_page_init  (gui, &pagedata);
	stf_dialog_format_page_init (gui, &pagedata);

	stf_dialog_attach_page_signals (gui, &pagedata);

	stf_dialog_editables_enter (&pagedata);

	stf_dialog_set_initial_keyboard_focus (&pagedata);

	g_object_ref (pagedata.dialog);

	prepare_page (&pagedata);
	frob_buttons (&pagedata);

	gnumeric_set_transient (wbcg_toplevel (wbcg), GTK_WINDOW (pagedata.dialog));
	gtk_widget_show (GTK_WIDGET (pagedata.dialog));
	gtk_main ();

	if (pagedata.canceled) {
		dialogresult = NULL;
	} else {
		dialogresult = g_new (DialogStfResult_t, 1);

		dialogresult->text = pagedata.utf8_data;
		*((char *)pagedata.cur_end) = 0;
		if (dialogresult->text != pagedata.cur)
			strcpy (dialogresult->text, pagedata.cur);
		pagedata.cur = pagedata.utf8_data = NULL;

		dialogresult->encoding = pagedata.encoding;
		pagedata.encoding = NULL;

		dialogresult->rowcount = pagedata.rowcount;

		dialogresult->parseoptions = pagedata.parseoptions;
		pagedata.parseoptions = NULL;
		g_free (dialogresult->parseoptions->locale);
		dialogresult->parseoptions->locale = pagedata.locale;
		pagedata.locale = NULL;

		dialogresult->parseoptions->formats = pagedata.format.formats;
		pagedata.format.formats = NULL;
		dialogresult->parseoptions->col_import_array 
			=  pagedata.format.col_import_array;
		dialogresult->parseoptions->col_import_array_len 
			=  pagedata.format.col_import_array_len;
		pagedata.format.col_import_array = NULL;
		pagedata.format.col_import_count = 0;
		pagedata.format.col_import_array_len = 0;
	}

	stf_dialog_main_page_cleanup   (&pagedata);
	stf_dialog_csv_page_cleanup    (&pagedata);
	stf_dialog_fixed_page_cleanup  (&pagedata);
	stf_dialog_format_page_cleanup (&pagedata);

	gtk_widget_destroy (GTK_WIDGET (pagedata.dialog));
	g_object_unref (pagedata.dialog);
	g_object_unref (G_OBJECT (gui));
	g_free (pagedata.encoding);
	g_free (pagedata.locale);
	g_free (pagedata.utf8_data);
	if (pagedata.parseoptions)
		stf_parse_options_free (pagedata.parseoptions);

	return dialogresult;
}

/**
 * stf_dialog_result_free
 * @dialogresult: a dialogresult struct
 *
 * This routine will properly free the members of @dialogresult and
 * @dialogresult itself
 *
 * returns: nothing
 **/
void
stf_dialog_result_free (DialogStfResult_t *dialogresult)
{
	g_return_if_fail (dialogresult != NULL);

	stf_parse_options_free (dialogresult->parseoptions);

	g_free (dialogresult->text);
	g_free (dialogresult->encoding);

	g_free (dialogresult);
}

/**
 * stf_dialog_result_attach_formats_to_cr
 * @dialogresult: a dialogresult struct
 * @cr: a cell region
 *
 * Attach the formats of the dialogresult to the given cell region.
 *
 * returns: nothing
 **/
void    
stf_dialog_result_attach_formats_to_cr (DialogStfResult_t *dialogresult,
					GnmCellRegion *cr)
{
	unsigned int col, targetcol;
	
	g_return_if_fail (dialogresult != NULL);
	g_return_if_fail (cr != NULL);

	targetcol = 0;
	for (col = 0; col < dialogresult->parseoptions->formats->len; col++) {
		if (dialogresult->parseoptions->col_import_array[col]) {
			GnmFormat *sf = g_ptr_array_index 
				(dialogresult->parseoptions->formats, col);
			GnmStyleRegion *sr = g_new (GnmStyleRegion, 1);
			
			sr->range.start.col = targetcol;
			sr->range.start.row = 0;
			sr->range.end.col   = targetcol;
			sr->range.end.row   = dialogresult->rowcount - 1;
			sr->style = mstyle_new_default ();
			mstyle_set_format (sr->style, sf);
			targetcol++;
			
			cr->styles = g_slist_prepend (cr->styles, sr);
		}
	}
}


/*
 * dialog-stf.c : implementation of the STF import dialog
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
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialog-stf.h"

#include <format.h>
#include <command-context.h>
#include <gui-util.h>
#include <gdk/gdkkeysyms.h>

#define GLADE_FILE "dialog-stf.glade"

/**********************************************************************************************
 * UTILITY FUNCTIONS
 **********************************************************************************************/

/**
 * stf_dialog_set_scroll_region_and_prevent_center
 * @canvas : canvas where the @rectangle is located on
 * @rectangle : a rectangle on the @canvas which can be used for centering purposes
 * @height : the height of the region which is covered on the canvas
 * @width : the width of the region which is covered on the canvas
 *
 * This is merely a hack to prevent the canvas from centering on the text if the text
 * width and/or height are smaller than the width and/or height of the GnomeCanvas.
 * Warning 1 : Don't remove this, this is, it's necessary!!
 * Warning 2 : Be sure that the @canvas has both his width and height set to something other than 0
 *
 * returns : nothing
 **/
void
stf_dialog_set_scroll_region_and_prevent_center (GnomeCanvas *canvas, GnomeCanvasRect *rectangle, double width, double height)
{
	int canvaswidth, canvasheight;
	double rectwidth, rectheight;

	g_return_if_fail (canvas != NULL);
	g_return_if_fail (rectangle != NULL);

	gtk_object_get (GTK_OBJECT (canvas),
			"width", &canvaswidth,
			"height", &canvasheight,
			NULL);

	if (width < canvaswidth)
		rectwidth = canvaswidth;
	else
		rectwidth = width;

        if (height < canvasheight)
		rectheight = canvasheight;
	else
		rectheight = height;

	gnome_canvas_item_set (GNOME_CANVAS_ITEM (rectangle),
			       "x1", 0,         "y1", 0,
			       "x2", rectwidth,	"y2", rectheight,
			       NULL);

	gnome_canvas_set_scroll_region (canvas, 0, 0, rectwidth, rectheight);
}


/**********************************************************************************************
 * DIALOG CONTROLLING CODE
 **********************************************************************************************/

/**
 * stf_dialog_druid_page_cancel
 * @page : Active druid page
 * @druid : The parent Druid widget
 * @data : mother struct
 *
 * Presents the user with a nice cancel y/n dialognn
 *
 * returns : TRUE if the user actually wants to cancel, FALSE otherwise.
 **/
static gboolean
stf_dialog_druid_page_cancel (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data)
{
	return gnumeric_dialog_question_yes_no (data->wbcg,
		_("Are you sure you want to cancel?"), FALSE);
}

/**
 * stf_dialog_druid_position_to_page
 * @pagedata : mother struct
 * @pos : Position in the druid
 *
 * Will translate a DPG_* position into a pointer to the page.
 *
 * returns : A pointer to the GnomeDruidPage indicated by @pos
 **/
static GnomeDruidPage*
stf_dialog_druid_position_to_page (DruidPageData_t *pagedata, DruidPosition_t pos)
{
	switch (pos) {
	case DPG_MAIN   : return pagedata->main_page;
	case DPG_CSV    : return pagedata->csv_page;
	case DPG_FIXED  : return pagedata->fixed_page;
	case DPG_FORMAT : return pagedata->format_page;
	default :
		g_warning ("Unknown druid position");
		return NULL;
	}
}

/**
 * stf_dialog_set_initial_keyboard_focus
 * @pagedata : mother struct
 *
 * Sets keyboard focus to the an appropriate widget on the page.
 *
 * returns : nothing
 **/
static void
stf_dialog_set_initial_keyboard_focus (DruidPageData_t *pagedata)
{
	GtkWidget *focus_widget = NULL;

	switch (pagedata->position) {
	case DPG_MAIN   :
		focus_widget =
			GTK_WIDGET (pagedata->main_info->main_separated);
		break;
	case DPG_CSV    :
		focus_widget =
			GTK_WIDGET (pagedata->csv_info->csv_space);
		break;
	case DPG_FIXED  :
		focus_widget = GTK_WIDGET
			(&pagedata->fixed_info->fixed_colend->entry);
		break; /* ?? */
	case DPG_FORMAT :
		focus_widget =
			GTK_WIDGET (pagedata->format_info->format_format);
		break;
	default :
		g_warning ("Unknown druid position");
	}

	if (focus_widget)
		gtk_widget_grab_focus (focus_widget);
}

/**
 * stf_dialog_druid_page_next
 * @page : A druid page
 * @druid : The druid itself
 * @data : mother struct
 *
 * This function will determine and set the next page depending on choices
 * made in previous pages
 *
 * returns : always TRUE, because it always sets the new page manually
 **/
static gboolean
stf_dialog_druid_page_next (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data)
{
	DruidPosition_t newpos;
	GnomeDruidPage *nextpage;

	switch (data->position) {
	case DPG_MAIN   : {
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main_info->main_separated))) {
			newpos = DPG_CSV;
			data->parsetype = PARSE_TYPE_CSV;
		}
		else {
			newpos = DPG_FIXED;
			data->parsetype = PARSE_TYPE_FIXED;
		}
	} break;
	case DPG_CSV    : {
		newpos = DPG_FORMAT;

		data->format_info->format_run_parseoptions = data->csv_info->csv_run_parseoptions;
		data->format_info->format_run_source_hash  = data->csv_info->csv_run_renderdata;
	} break;
        case DPG_FIXED  : {
		newpos = DPG_FORMAT;

		/*
		 * Set trim type here, we never want trimming on
		 * the fixed width page of the druid because of
		 * columns getting mangled
		 */
		stf_parse_options_set_trim_spaces (data->fixed_info->fixed_run_parseoptions, data->trim);
		data->format_info->format_run_parseoptions = data->fixed_info->fixed_run_parseoptions;
		data->format_info->format_run_source_hash  = data->fixed_info->fixed_run_renderdata;
	} break;
	default :
		g_warning ("Page Cycle Error : Unknown page %d", data->position);
		return FALSE;
	}

        nextpage = stf_dialog_druid_position_to_page (data, newpos);
	if (!nextpage) {
		g_warning ("Page Cycle Error : Invalid page");
		return FALSE;
	}

	gnome_druid_set_page (druid, nextpage);
	data->position = newpos;

	stf_dialog_set_initial_keyboard_focus (data);
	if (newpos == DPG_FORMAT) {
		gnome_druid_set_show_finish (data->druid, TRUE);
		gtk_widget_grab_default (data->druid->finish);
	}

	return TRUE;
}

/**
 * stf_dialog_druid_page_previous
 * @page : a druid page
 * @druid : a druid
 * @data : mother struct
 *
 * Determines the previous page based on choices made earlier on
 *
 * returns : always TRUE, because it always cycles to the previous page manually
 **/
static gboolean
stf_dialog_druid_page_previous (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data)
{
	DruidPosition_t newpos;
	GnomeDruidPage *nextpage;

	switch (data->position) {
	case DPG_FORMAT : {
		if (data->parsetype == PARSE_TYPE_CSV)
			newpos = DPG_CSV;
		else
			newpos = DPG_FIXED;
	} break;
	case DPG_FIXED  : newpos = DPG_MAIN; break;
	case DPG_CSV    : newpos = DPG_MAIN; break;
	default :
		g_warning ("Page Cycle Error : Unknown page");
		return FALSE;
	}

	if (newpos == DPG_MAIN)
		gnome_druid_set_buttons_sensitive (druid, FALSE, TRUE, TRUE, TRUE);

        nextpage = stf_dialog_druid_position_to_page (data, newpos);
	if (!nextpage) {
		g_warning ("Page Cycle Error : Invalid page");
		return FALSE;
	}

	gnome_druid_set_page (data->druid, nextpage);
	data->position = newpos;

	stf_dialog_set_initial_keyboard_focus (data);
	if (newpos == DPG_MAIN)
		gnome_druid_set_buttons_sensitive (druid, FALSE, TRUE, TRUE, TRUE);
	else
		gtk_widget_grab_default (data->druid->next);
	return TRUE;
}

/**
 * stf_dialog_druid_cancel
 * @druid : a druid
 * @data : mother struct
 *
 * Stops the druid and indicates the user has cancelled
 *
 * returns : nothing
 **/
static void
stf_dialog_druid_cancel (GnomeDruid *druid, DruidPageData_t *data)
{
	data->canceled = TRUE;
	gtk_main_quit ();
}

/**
 * stf_dialog_check_escape
 * @druid : a druid
 * @event : the event
 *
 * Stops the druid if the user pressed escape.
 *
 * returns : TRUE if we handled the keypress, FALSE if we pass it on.
 **/
static gint
stf_dialog_check_escape (GnomeDruid *druid, GdkEventKey *event,
			 DruidPageData_t *data)
{
	if (event->keyval == GDK_Escape) {
		gtk_button_clicked (GTK_BUTTON (data->druid->cancel));
		return TRUE;
	} else
		return FALSE;
}

/**
 * stf_dialog_format_page_druid_finish
 * @druid : a druid
 * @page : a druidpage
 * @data : mother struct
 *
 * Stops the druid but does not set the cancel property of @data.
 * The main routine (stf_dialog()) will know that the druid has successfully
 * been completed.
 *
 * returns : nothing
 **/
static void
stf_dialog_druid_format_page_finish (GnomeDruid *druid, GnomeDruidPage *page, DruidPageData_t *data)
{
	gtk_main_quit ();
}

/**
 * stf_dialog_attach_page_signals
 * @gui : the glade gui of the dialog
 * @pagedata : mother struct
 *
 * Connects all signals to all pages and fills the mother struct
 * The page flow of the druid looks like :
 *
 * main_page /- csv_page   -\ format_page
 *           \- fixed_page -/
 *
 * returns : nothing
 **/
static void
stf_dialog_attach_page_signals (GladeXML *gui, DruidPageData_t *pagedata)
{
	pagedata->window     = GTK_WINDOW  (glade_xml_get_widget (gui, "window"));
	pagedata->druid      = GNOME_DRUID (glade_xml_get_widget (gui, "druid"));

	pagedata->main_page   = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "main_page"));
	pagedata->csv_page    = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "csv_page"));
	pagedata->fixed_page  = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "fixed_page"));
	pagedata->format_page = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "format_page"));
/*	pagedata->stop_page   = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "stop_page"));*/

	pagedata->position  = DPG_MAIN;
	gnome_druid_set_buttons_sensitive (pagedata->druid, FALSE, TRUE, TRUE, TRUE);

	/* Signals for individual pages */

	g_signal_connect (G_OBJECT (pagedata->main_page),
		"next",
		G_CALLBACK (stf_dialog_druid_page_next), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv_page),
		"next",
		G_CALLBACK (stf_dialog_druid_page_next), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed_page),
		"next",
		G_CALLBACK (stf_dialog_druid_page_next), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format_page),
		"next",
		G_CALLBACK (stf_dialog_druid_page_next), pagedata);

	g_signal_connect (G_OBJECT (pagedata->main_page),
		"back",
		G_CALLBACK (stf_dialog_druid_page_previous), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv_page),
		"back",
		G_CALLBACK (stf_dialog_druid_page_previous), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed_page),
		"back",
		G_CALLBACK (stf_dialog_druid_page_previous), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format_page),
		"back",
		G_CALLBACK (stf_dialog_druid_page_previous), pagedata);

	g_signal_connect (G_OBJECT (pagedata->main_page),
		"cancel",
		G_CALLBACK (stf_dialog_druid_page_cancel), pagedata);
	g_signal_connect (G_OBJECT (pagedata->csv_page),
		"cancel",
		G_CALLBACK (stf_dialog_druid_page_cancel), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed_page),
		"cancel",
		G_CALLBACK (stf_dialog_druid_page_cancel), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format_page),
		"cancel",
		G_CALLBACK (stf_dialog_druid_page_cancel), pagedata);

	g_signal_connect (G_OBJECT (pagedata->format_page),
		"finish",
		G_CALLBACK (stf_dialog_druid_format_page_finish), pagedata);

	g_signal_connect (G_OBJECT (pagedata->csv_page),
		"prepare",
		G_CALLBACK (stf_dialog_csv_page_prepare), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed_page),
		"prepare",
		G_CALLBACK (stf_dialog_fixed_page_prepare), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format_page),
		"prepare",
		G_CALLBACK (stf_dialog_format_page_prepare), pagedata);

	/* Signals for the druid itself */

	g_signal_connect (G_OBJECT (pagedata->druid),
		"cancel",
		G_CALLBACK (stf_dialog_druid_cancel), pagedata);

	/* And for the surrounding window */

	g_signal_connect (G_OBJECT (pagedata->window),
		"key_press_event",
		G_CALLBACK (stf_dialog_check_escape), pagedata);
}

/**
 * stf_dialog_editables_enter
 * @pagedata : mother struct
 *
 * Make <Ret> in text fields activate default.
 *
 * returns : nothing
 **/
static void
stf_dialog_editables_enter (DruidPageData_t *pagedata)
{
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (&pagedata->main_info->main_startrow->entry));
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (&pagedata->main_info->main_stoprow->entry));
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (pagedata->csv_info->csv_customseparator));
	gnumeric_combo_enters
		(pagedata->window,
		pagedata->csv_info->csv_textindicator);
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (&pagedata->fixed_info->fixed_colend->entry));
	gnumeric_editable_enters
		(pagedata->window,
		 GTK_WIDGET (pagedata->format_info->format_format));
}

/**
 * stf_dialog
 * @wbcg : a Commandcontext (can be NULL)
 * @filename : name of the file we are importing (or data)
 * @data : the data itself
 *
 * This will start the import druid.
 * (NOTE : you have to free the DialogStfResult_t that this function returns yourself)
 *
 * returns : A DialogStfResult_t struct on success, NULL otherwise.
 **/
DialogStfResult_t*
stf_dialog (WorkbookControlGUI *wbcg, const char *filename, const char *data)
{
	GladeXML *gui;
	DialogStfResult_t *dialogresult;
	DruidPageData_t pagedata;
	MainInfo_t main_info;
	CsvInfo_t csv_info;
	FixedInfo_t fixed_info;
	FormatInfo_t format_info;
	StfParseOptions_t *parseoptions;
	char* message;

	gui = gnumeric_glade_xml_new (NULL, GLADE_FILE);
	if (!gui) {

		message = g_strdup_printf (_("Missing %s file"), GLADE_FILE);

		if (wbcg)
			gnumeric_error_read (COMMAND_CONTEXT (wbcg), message);
		else
			g_warning (message);

		g_free (message);

		return NULL;
	}

	pagedata.canceled = FALSE;

	pagedata.wbcg	  = wbcg;
	pagedata.filename = filename;
	pagedata.data     = data;
	pagedata.cur      = data;

	/* Just to check out the number of lines.... */
	parseoptions      = stf_parse_options_new ();
	pagedata.lines    = stf_parse_get_rowcount (parseoptions, data);
	stf_parse_options_free (parseoptions);

	pagedata.main_info   = &main_info;
	pagedata.csv_info    = &csv_info;
	pagedata.fixed_info  = &fixed_info;
	pagedata.format_info = &format_info;

	stf_dialog_main_page_init   (gui, &pagedata);
	stf_dialog_csv_page_init    (gui, &pagedata);
	stf_dialog_fixed_page_init  (gui, &pagedata);
	stf_dialog_format_page_init (gui, &pagedata);

	stf_dialog_attach_page_signals (gui, &pagedata);

	stf_dialog_editables_enter (&pagedata);

	stf_dialog_set_initial_keyboard_focus (&pagedata);
	gtk_widget_grab_default (pagedata.druid->next);

	gnumeric_set_transient (wbcg, pagedata.window);
	gtk_widget_show (GTK_WIDGET (pagedata.window));
	gtk_main ();

	if (pagedata.canceled) {

		dialogresult = NULL;
	} else {

		dialogresult = g_new (DialogStfResult_t, 1);

		dialogresult->newstart = pagedata.cur;
		dialogresult->lines = pagedata.importlines;
		if (pagedata.parsetype == PARSE_TYPE_CSV) {
			dialogresult->parseoptions = csv_info.csv_run_parseoptions;
			csv_info.csv_run_parseoptions = NULL;
		} else {
			dialogresult->parseoptions = fixed_info.fixed_run_parseoptions;
			fixed_info.fixed_run_parseoptions= NULL;
		}

		dialogresult->formats = format_info.format_run_list;
	}

	/* Quick Note, if the parseoptions members of either the csv page or
	 * fixed page have been set to NULL when calling the cleanup function
	 * they will not attempt to free it, this will be done in stf_dialog_result_free
	 * instead
	 */
	stf_dialog_csv_page_cleanup    (&pagedata);
	stf_dialog_fixed_page_cleanup  (&pagedata);
	stf_dialog_format_page_cleanup (&pagedata);

	gtk_widget_destroy (GTK_WIDGET (pagedata.window));
	g_object_unref (G_OBJECT (gui));

	return dialogresult;
}

/**
 * stf_dialog_result_free
 * @dialogresult : a dialogresult struct
 *
 * This routine will properly free the members of @dialogresult and
 * @dialogresult itself
 *
 * returns : nothing
 **/
void
stf_dialog_result_free (DialogStfResult_t *dialogresult)
{
	GSList *iterator;

	g_return_if_fail (dialogresult != NULL);

	stf_parse_options_free (dialogresult->parseoptions);

	iterator = dialogresult->formats;
	while (iterator != NULL) {
		style_format_unref (iterator->data);
		iterator = g_slist_next (iterator);
	}
	g_slist_free (dialogresult->formats);

	g_free (dialogresult);
}

/*
 * dialog-stf.c : implementation of the STF import dialog
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
 *
 */

#include <config.h>
#include <gnome.h>

#include "command-context.h"
#include "dialog-stf.h"

#define GLADE_FILE "stf.glade"

/**
 * dialog_stf_druid_page_cancel
 * @page : Active druid page
 * @druid : The parent Druid widget
 * @data : mother struct 
 *
 * Presents the user with a nice cancel y/n dialog
 *
 * returns : TRUE if the user actually wants to cancel, FALSE otherwise.
 **/
static gboolean
dialog_stf_druid_page_cancel (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data)
{
	GtkWidget *dialog;
	int ret;
      
	dialog = gnome_question_dialog_parented (_("Are you sure you want to cancel?"), 
						 NULL,
						 NULL,
						 data->window);

	ret = gnome_dialog_run (GNOME_DIALOG (dialog));
					
	return (ret==1);
}

/** 
 * dialog_stf_druid_position_to_page
 * @pagedata : mother struct
 * @pos : Position in the druid
 *
 * Will translate a DPG_* position into a pointer to the page.
 *
 * returns : A pointer to the GnomeDruidPage indicated by @pos
 **/
static GnomeDruidPage*
dialog_stf_druid_position_to_page (DruidPageData_t *pagedata, DruidPosition_t pos) 
{
	switch (pos) {
	case DPG_MAIN   : return pagedata->main_page;
	case DPG_CSV    : return pagedata->csv_page;
	case DPG_FIXED  : return pagedata->fixed_page;
	case DPG_FORMAT : return pagedata->format_page;
	case DPG_STOP   : return pagedata->stop_page;
	default :
		g_warning ("Unknown druid position");
		return NULL;
	}
}

/**
 * dialog_stf_druid_page_next
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
dialog_stf_druid_page_next (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data)
{
	DruidPosition_t newpos;
	GnomeDruidPage *nextpage;

	switch (data->position) {
	case DPG_MAIN   : {
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main_info->main_separated)))
			newpos = DPG_CSV;
		else
			newpos = DPG_FIXED;
	} break;
	case DPG_CSV    : {
		newpos = DPG_FORMAT;
		stf_separated_parse_sheet (data->src, data->csv_info->csv_run_sepinfo);
	} break;
        case DPG_FIXED  : {
		newpos = DPG_FORMAT;
		stf_fixed_parse_sheet (data->src, data->fixed_info->fixed_run_fixinfo);
	} break;
	case DPG_FORMAT : newpos = DPG_STOP; break;
	default :
		g_warning ("Page Cycle Error : Unknown page %d", data->position);
		return FALSE;
	}

        nextpage = dialog_stf_druid_position_to_page (data, newpos);
	if (!nextpage) {
		g_warning ("Page Cycle Error : Invalid page");
		return FALSE;
	}

	gnome_druid_set_page (druid, nextpage);
	data->position = newpos;

	return TRUE;
}

/**
 * dialog_stf_druid_page_previous
 * @page : a druid page
 * @druid : a druid
 * @data : mother struct
 * 
 * Determines the previous page based on choices made earlier on
 *
 * returns : always TRUE, because it always cycles to the previous page manually
 **/
static gboolean
dialog_stf_druid_page_previous (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *data)
{
	DruidPosition_t newpos;
	GnomeDruidPage *nextpage;

	switch (data->position) {
	case DPG_STOP   : newpos = DPG_FORMAT; break;
	case DPG_FORMAT : {
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->main_info->main_separated)))
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
		gnome_druid_set_buttons_sensitive (druid, FALSE, TRUE, TRUE);
	
        nextpage = dialog_stf_druid_position_to_page (data, newpos);
	if (!nextpage) {
		g_warning ("Page Cycle Error : Invalid page");
		return FALSE;
	}
	
	gnome_druid_set_page (data->druid, nextpage);
	data->position = newpos;

	if (newpos == DPG_MAIN) 
		gnome_druid_set_buttons_sensitive (druid, FALSE, TRUE, TRUE);

	return TRUE;
}

/**
 * dialog_stf_druid_cancel
 * @druid : a druid
 * @data : mother struct
 * 
 * Stops the druid and indicates the user has cancelled
 *
 * returns : nothing
 **/
static void
dialog_stf_druid_cancel (GnomeDruid *druid, DruidPageData_t *data)
{
	data->canceled = TRUE;
	gtk_main_quit ();
}

/**
 * dialog_stf_stop_page_druid_finish
 * @druid : a druid
 * @page : a druidpage
 * @data : mother struct
 *
 * Stops the druid but does not set the cancel property of @data.
 * The main routine (dialog_stf()) will know that the druid has successfully
 * been completed.
 *
 * returns : nothing
 **/
static void
dialog_stf_druid_stop_page_finish (GnomeDruid *druid, GnomeDruidPage *page, DruidPageData_t *data)
{
	gtk_main_quit ();
}

/**
 * dialog_stf_attach_page_signals
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
dialog_stf_attach_page_signals (GladeXML *gui, DruidPageData_t *pagedata)
{
	pagedata->window     = GTK_WINDOW  (glade_xml_get_widget (gui, "window"));
	pagedata->druid      = GNOME_DRUID (glade_xml_get_widget (gui, "druid"));

	pagedata->main_page   = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "main_page"));
	pagedata->csv_page    = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "csv_page"));
	pagedata->fixed_page  = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "fixed_page"));
	pagedata->format_page = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "format_page"));
	pagedata->stop_page   = GNOME_DRUID_PAGE (glade_xml_get_widget (gui, "stop_page"));

	pagedata->position  = DPG_MAIN;
	gnome_druid_set_buttons_sensitive (pagedata->druid, FALSE, TRUE, TRUE);
	
	/* Signals for individual pages */

	gtk_signal_connect (GTK_OBJECT (pagedata->main_page), 
			    "next", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_next),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->csv_page), 
			    "next", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_next),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->fixed_page), 
			    "next", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_next),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->format_page), 
			    "next", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_next),
			    pagedata);		  

	gtk_signal_connect (GTK_OBJECT (pagedata->main_page), 
			    "back", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_previous),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->csv_page), 
			    "back", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_previous),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->fixed_page), 
			    "back", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_previous),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->format_page), 
			    "back", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_previous),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->stop_page), 
			    "back", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_previous),
			    pagedata);

	gtk_signal_connect (GTK_OBJECT (pagedata->main_page), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_cancel),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->csv_page), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_cancel),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->fixed_page), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_cancel),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->format_page), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_cancel),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->stop_page), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_page_cancel),
			    pagedata);

	gtk_signal_connect (GTK_OBJECT (pagedata->csv_page), 
			    "prepare", 
			    GTK_SIGNAL_FUNC (csv_page_prepare),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->fixed_page), 
			    "prepare", 
			    GTK_SIGNAL_FUNC (fixed_page_prepare),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (pagedata->format_page), 
			    "prepare", 
			    GTK_SIGNAL_FUNC (format_page_prepare),
			    pagedata);
			    
	gtk_signal_connect (GTK_OBJECT (pagedata->stop_page), 
			    "finish", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_stop_page_finish),
			    pagedata);
			    
	/* Signals for the druid itself */

	gtk_signal_connect (GTK_OBJECT (pagedata->druid), 
			    "cancel", 
			    GTK_SIGNAL_FUNC (dialog_stf_druid_cancel),
			    pagedata);
}

/**
 * dialog_stf
 * @src : a record containing information on the file to import
 *
 * This will start the import druid.
 *
 * returns : 0 on success, -1 otherwise.
 **/
int
dialog_stf (CommandContext *context, FileSource_t *src)
{
	GladeXML *gui;
	DruidPageData_t pagedata;
	MainInfo_t main_info;
	CsvInfo_t csv_info;
	FixedInfo_t fixed_info;
	FormatInfo_t format_info;
	char* message;

	glade_gnome_init();

	gui = glade_xml_new (GNUMERIC_GLADEDIR "/" GLADE_FILE, NULL);
	if (!gui) {
		message = g_strdup_printf (_("Missing %s file"), GLADE_FILE);
		gnumeric_error_read (context, message);
		g_free (message);
		return -1;
	}
	
	pagedata.canceled = FALSE;
	pagedata.src      = src;
	
	pagedata.main_info   = &main_info;
	pagedata.csv_info    = &csv_info;
	pagedata.fixed_info  = &fixed_info;
	pagedata.format_info = &format_info;

	main_page_init   (gui, &pagedata);
	csv_page_init    (gui, &pagedata);
	fixed_page_init  (gui, &pagedata);
	format_page_init (gui, &pagedata);

	dialog_stf_attach_page_signals (gui, &pagedata);

	gtk_widget_show (GTK_WIDGET (pagedata.window));
	gtk_main ();

	csv_page_cleanup    (&pagedata);
	fixed_page_cleanup  (&pagedata);
	format_page_cleanup (&pagedata);
	
	gtk_widget_destroy (GTK_WIDGET (pagedata.window));
	gtk_object_unref (GTK_OBJECT (gui));

	return -1;
}












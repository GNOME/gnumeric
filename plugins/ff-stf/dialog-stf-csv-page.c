/*
 * dialog-stf.c : Controls the widget on the CSV (Comma Separated Value) page of the druid
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
 *
 */

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>

#include "dialog-stf.h"

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * csv_page_global_change
 * @widget : the widget which emmited the signal
 * @data : mother struct
 *
 * This will update the preview based on the state of
 * the widgets on the csv page
 *
 * returns : nothing
 **/
static void
csv_page_global_change (GtkWidget *widget, DruidPageData_t *data)
{
	CsvInfo_t *info = data->csv_info;
	SeparatedInfo_t *sepinfo;
	char *textfieldtext;

	sepinfo = g_new0 (SeparatedInfo_t, 1);
	
	sepinfo->separator = 0;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_tab)))
		sepinfo->separator |= TST_TAB;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_colon)))
		sepinfo->separator |= TST_COLON;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_comma)))
		sepinfo->separator |= TST_COMMA;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_space)))
		sepinfo->separator |= TST_SPACE;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_custom))) {
		char *csvcustomtext = gtk_editable_get_chars (GTK_EDITABLE (info->csv_customseparator),
							      0,
							      -1);

		if (strcmp (csvcustomtext, "") != 0) {
			sepinfo->separator |= TST_CUSTOM;
			sepinfo->custom = csvcustomtext[0];
		}
	}
	
	textfieldtext = gtk_editable_get_chars (GTK_EDITABLE (info->csv_textfield), 0, -1);
	sepinfo->string = textfieldtext[0];
	sepinfo->duplicates = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (info->csv_duplicates));

	if (info->csv_run_sepinfo != NULL) {
		if (memcmp (info->csv_run_sepinfo, sepinfo, sizeof (SeparatedInfo_t)) != 0)
			sheet_destroy_contents (data->src->sheet);

		g_free (info->csv_run_sepinfo);
	}
	info->csv_run_sepinfo = sepinfo;

	/* If we haven't rendered before, parse one time *fully* */
	if (info->csv_run_renderdata->rowsrendered == 0 && data->src->lines != 0) {
		stf_separated_parse_sheet (data->src, sepinfo);
	
	} else {
		stf_separated_parse_sheet_partial (data->src,
						   sepinfo,
						   info->csv_run_renderdata->startrow - 1,
						   (info->csv_run_renderdata->startrow - 1) + info->csv_run_renderdata->rowsrendered);
	}
		   
	stf_preview_render (info->csv_run_renderdata);
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
 * @button : the Checkbutton that emmited the signal
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
 * csv_page_prepare
 * @page : The druidpage that emmitted the signal
 * @druid : The gnomedruid that houses @page
 * @data : mother struct
 *
 * Will prepare the csv page
 *
 * returns : nothing
 **/
void
csv_page_prepare (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *pagedata)
{
	CsvInfo_t *info = pagedata->csv_info;

	/*if (info->csv_run_scrollpos > pagedata->src->lines)
	  info->csv_run_scrollpos = pagedata->src->lines;*/

	sheet_destroy_contents (pagedata->src->sheet);	
	GTK_RANGE (info->csv_scroll)->adjustment->upper = pagedata->src->lines + 1;

	/* Calling this routine will also automatically call global change which updates the preview too */
	csv_page_custom_toggled (info->csv_custom, pagedata);
}

/**
 * csv_page_cleanup
 * @pagedata : mother struct
 *
 * Will cleanup csv page run-time data
 *
 * returns : nothing
 **/
void
csv_page_cleanup (DruidPageData_t *pagedata)
{
	CsvInfo_t *info = pagedata->csv_info;

	stf_preview_free (info->csv_run_renderdata);
	info->csv_run_renderdata = NULL;

	if (info->csv_run_sepinfo)
		g_free (info->csv_run_sepinfo);
}
 
/**
 * csv_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the CSV Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
csv_page_init (GladeXML *gui, DruidPageData_t *pagedata)
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
	info->csv_custom          = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_custom"));
	info->csv_customseparator = GTK_ENTRY        (glade_xml_get_widget (gui, "csv_customseparator"));

	info->csv_duplicates    = GTK_CHECK_BUTTON (glade_xml_get_widget (gui, "csv_duplicates"));
	info->csv_textindicator = GTK_COMBO        (glade_xml_get_widget (gui, "csv_textindicator"));
	info->csv_textfield     = GTK_ENTRY        (glade_xml_get_widget (gui, "csv_textfield"));

	info->csv_canvas = GNOME_CANVAS   (glade_xml_get_widget (gui, "csv_canvas"));
	info->csv_scroll = GTK_VSCROLLBAR (glade_xml_get_widget (gui, "csv_scroll"));
	
	/* Set properties */
	info->csv_run_renderdata = stf_preview_new (info->csv_canvas, pagedata->src, FALSE);
	info->csv_run_sepinfo    = NULL;
	
	/* Connect signals */
	gtk_signal_connect (GTK_OBJECT (info->csv_tab),
			    "toggled",
			    GTK_SIGNAL_FUNC (csv_page_global_change),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->csv_colon),
			    "toggled",
			    GTK_SIGNAL_FUNC (csv_page_global_change),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->csv_comma),
			    "toggled",
			    GTK_SIGNAL_FUNC (csv_page_global_change),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->csv_space),
			    "toggled",
			    GTK_SIGNAL_FUNC (csv_page_global_change),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->csv_custom),
			    "toggled",
			    GTK_SIGNAL_FUNC (csv_page_custom_toggled),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->csv_customseparator),
			    "changed",
			    GTK_SIGNAL_FUNC (csv_page_global_change),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->csv_duplicates),
			    "toggled",
			    GTK_SIGNAL_FUNC (csv_page_global_change),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->csv_textfield),
			    "changed",
			    GTK_SIGNAL_FUNC (csv_page_global_change),
			    pagedata);
	
	gtk_signal_connect (GTK_OBJECT (GTK_RANGE (info->csv_scroll)->adjustment),
			    "value_changed",
			    GTK_SIGNAL_FUNC (csv_page_scroll_value_changed),
			    pagedata);
}






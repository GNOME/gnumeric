/*
 * dialog-stf-export-format-page.c : controls the widgets on the format page
 *                                   of the export druid
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
 *
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialog-stf-export-private.h"

/*
 * Index of "Custom" in the separator optionmenu
 */
#define CUSTOM_INDEX 9

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * sheet_page_separator_menu_deactivate:
 * @shell: the gtkmenu associated with the option menu
 * @data: format page data struct
 *
 * Triggered when the menu of the separator option menu is closed
 **/
static void
sheet_page_separator_menu_deactivate (GtkMenuShell *shell, StfE_FormatPageData_t *data)
{
	if (gnumeric_option_menu_get_selected_index (data->format_separator) == CUSTOM_INDEX) {

		gtk_widget_set_sensitive (GTK_WIDGET (data->format_custom), TRUE);
		gtk_widget_grab_focus      (GTK_WIDGET (data->format_custom));
		gtk_editable_select_region (GTK_EDITABLE (data->format_custom), 0, -1);
	}
	else {

		gtk_widget_set_sensitive (GTK_WIDGET (data->format_custom), FALSE);
		gtk_editable_select_region (GTK_EDITABLE (data->format_custom), 0, 0); /* If we don't use this the selection will remain blue */
	}
}

/*************************************************************************************************
 * MAIN EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_export_dialog_format_page_init
 * @gui : The glade gui of the dialog
 *
 * This routine prepares/initializes all widgets on the format page of the
 * Druid.
 *
 * returns : formatpagedata struct
 **/
StfE_FormatPageData_t*
stf_export_dialog_format_page_init (GladeXML *gui)
{
	StfE_FormatPageData_t *data;
	GtkMenu *menu;

	g_return_val_if_fail (gui != NULL, NULL);

	data = g_new (StfE_FormatPageData_t, 1);

	/* Create/get object and fill information struct */

	data->format_termination = GTK_OPTION_MENU (glade_xml_get_widget (gui, "format_termination"));
	data->format_separator   = GTK_OPTION_MENU (glade_xml_get_widget (gui, "format_separator"));
	data->format_custom      = GTK_ENTRY       (glade_xml_get_widget (gui, "format_custom"));
	data->format_quote       = GTK_OPTION_MENU (glade_xml_get_widget (gui, "format_quote"));
	data->format_quotechar   = GTK_COMBO       (glade_xml_get_widget (gui, "format_quotechar"));

	menu = (GtkMenu *) gtk_option_menu_get_menu (data->format_separator);

	g_signal_connect (G_OBJECT (menu),
		"deactivate",
		G_CALLBACK (sheet_page_separator_menu_deactivate), data);

	return data;
}

/**
 * stf_export_dialog_sheet_page_result:
 * @data: format page data
 * @export_options: an export options struct
 *
 * Adjusts @export_options to reflect the options choosen on the
 * format page
 **/
void
stf_export_dialog_format_page_result (StfE_FormatPageData_t *data, StfExportOptions_t *export_options)
{
	StfTerminatorType_t terminator;
	StfQuotingMode_t quotingmode;
	char *text;
	char separator;

	g_return_if_fail (data != NULL);
	g_return_if_fail (export_options != NULL);

	switch (gnumeric_option_menu_get_selected_index (data->format_termination)) {
	case 0 : terminator = TERMINATOR_TYPE_LINEFEED; break;
	case 1 : terminator = TERMINATOR_TYPE_RETURN; break;
	case 2 : terminator = TERMINATOR_TYPE_RETURN_LINEFEED; break;
	default :
		terminator = TERMINATOR_TYPE_UNKNOWN;
		break;
	}

	stf_export_options_set_terminator_type (export_options, terminator);

	switch (gnumeric_option_menu_get_selected_index (data->format_quote)) {
	case 0 : quotingmode = QUOTING_MODE_AUTO; break;
	case 1 : quotingmode = QUOTING_MODE_ALWAYS; break;
	case 2 : quotingmode = QUOTING_MODE_NEVER; break;
	default :
		quotingmode = QUOTING_MODE_UNKNOWN;
		break;
	}

	stf_export_options_set_quoting_mode (export_options, quotingmode);

	text = gtk_editable_get_chars (GTK_EDITABLE (data->format_quotechar->entry), 0, -1);
	stf_export_options_set_quoting_char (export_options, text[0]);
	g_free (text);

	separator = '\0';
	switch (gnumeric_option_menu_get_selected_index (data->format_separator)) {
	case 0 : separator = ' '; break;
	case 1 : separator = '\t'; break;
	case 2 : separator = '!'; break;
	case 3 : separator = ':'; break;
	case 4 : separator = ','; break;
	case 5 : separator = '-'; break;
	case 6 : separator = '|'; break;
	case 7 : separator = ';'; break;
	case 8 : separator = '/'; break;
	case 9 : {
		text = gtk_editable_get_chars (GTK_EDITABLE (data->format_custom), 0, -1);
		separator = text[0];
		g_free (text);
		break;
	}
	default :
		g_warning ("Unknown separator");
		break;
	}

	stf_export_options_set_cell_separator (export_options, separator);
}

/**
 * stf_export_dialog_format_page_cleanup
 * @data : formatpagedata struct
 *
 * Will cleanup format page run-time data
 *
 * returns : nothing
 **/
void
stf_export_dialog_format_page_cleanup (StfE_FormatPageData_t *data)
{
	g_return_if_fail (data != NULL);

	g_free (data);
}

/*
 * dialog-stf-export-sheet-page.c : controls the widgets on the sheet page
 *                                  of the export druid
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
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
#include "dialog-stf-export-private.h"

#include <workbook.h>
#include <sheet.h>

#include <libgnome/gnome-i18n.h>

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/**
 * sheet_page_transfer_item:
 * @source: source gtkclist
 * @dest: destination gtklist
 * @source_index: index of item in @source to transfer
 *
 * Transfers item @source_index from the @source gtkclist to the @dest
 * gtkclist
 *
 * Return value: returns FALSE if the source list is empty, TRUE otherwise.
 **/
static gboolean
sheet_page_transfer_item (GtkCList *source, GtkCList *dest, int source_index)
{
	Sheet *sheet;
	char *t[1];
	int index;

	g_return_val_if_fail (source != NULL, FALSE);
	g_return_val_if_fail (dest != NULL, FALSE);

	gtk_clist_get_text (source, source_index, 0, t);
	sheet = gtk_clist_get_row_data (source, source_index);

	index = gtk_clist_append (dest, t);
	gtk_clist_set_row_data (dest, index, sheet);
	gtk_clist_select_row (dest, index, 0);

	gtk_clist_remove (source, source_index);

	if (source->rows < 1) {

		return FALSE;
	} else {

		gtk_clist_select_row (source, 0, 0);

		return TRUE;
	}
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * sheet_page_add_clicked:
 * @button: a gtkbutton
 * @data: sheet page data
 *
 * Handles the click signal for the add button
 **/
static void
sheet_page_add_clicked (GtkButton *button, StfE_SheetPageData_t *data)
{
	gboolean enable;

	g_return_if_fail (button != NULL);
	g_return_if_fail (data != NULL);

	if (data->sheet_run_avail_index == -1)
		return;

	enable = sheet_page_transfer_item (data->sheet_avail, data->sheet_export,
					   data->sheet_run_avail_index);

	gtk_widget_set_sensitive (GTK_WIDGET (button), enable);
	gtk_widget_set_sensitive (GTK_WIDGET (data->sheet_addall), enable);

	gtk_widget_set_sensitive (GTK_WIDGET (data->sheet_remove),
				  (data->sheet_export->rows > 0));
	gtk_widget_set_sensitive (GTK_WIDGET (data->sheet_removeall),
				  (data->sheet_export->rows > 0));
}

/**
 * sheet_page_remove_clicked:
 * @button: a gtkbutton
 * @data: sheet page data
 *
 * Handles the click signal for the remove button
 **/
static void
sheet_page_remove_clicked (GtkButton *button, StfE_SheetPageData_t *data)
{
	gboolean enable;

	g_return_if_fail (button != NULL);
	g_return_if_fail (data != NULL);

	if (data->sheet_run_export_index == -1)
		return;

	enable = sheet_page_transfer_item (data->sheet_export, data->sheet_avail,
					   data->sheet_run_export_index);

	gtk_widget_set_sensitive (GTK_WIDGET (button), enable);
	gtk_widget_set_sensitive (GTK_WIDGET (data->sheet_removeall), enable);

	gtk_widget_set_sensitive (GTK_WIDGET (data->sheet_add),
				  (data->sheet_avail->rows > 0));
	gtk_widget_set_sensitive (GTK_WIDGET (data->sheet_addall),
				  (data->sheet_avail->rows > 0));
}

/**
 * sheet_page_addall_clicked:
 * @button: gtkbutton
 * @data: sheet page data
 *
 * Will move all items from the avail list to the export list
 **/
static void
sheet_page_addall_clicked (GtkButton *button, StfE_SheetPageData_t *data)
{

	while (data->sheet_avail->rows > 0) {

		data->sheet_run_avail_index = 0;
		gtk_signal_emit_by_name (GTK_OBJECT (data->sheet_add),
					 "clicked",
					 data);
	}
}

/**
 * sheet_page_removeall_clicked:
 * @button: gtkbutton
 * @data: sheet page data
 *
 * Will move all items from the export list to the avail list
 **/
static void
sheet_page_removeall_clicked (GtkButton *button, StfE_SheetPageData_t *data)
{

	while (data->sheet_export->rows > 0) {

		data->sheet_run_export_index = 0;
		gtk_signal_emit_by_name (GTK_OBJECT (data->sheet_remove),
					 "clicked",
					 data);
	}
}

/**
 * sheet_page_up_clicked:
 * @button: gtkbutton
 * @data: sheet page data
 *
 * Will swap the selected item with the item right above it
 **/
static void
sheet_page_up_clicked (GtkButton *button, StfE_SheetPageData_t *data)
{
	if (data->sheet_run_export_index > 0) {

		gtk_clist_swap_rows (data->sheet_export,
				     data->sheet_run_export_index,
				     data->sheet_run_export_index - 1);

		gtk_clist_select_row (data->sheet_export,
				      data->sheet_run_export_index - 1, 0);
	}
}

/**
 * sheet_page_down_clicked:
 * @button: gtkbutton
 * @data: sheet page data
 *
 * Will swap the selected item with the item right below it
 **/
static void
sheet_page_down_clicked (GtkButton *button, StfE_SheetPageData_t *data)
{
	if (data->sheet_run_export_index + 1 < data->sheet_export->rows) {

		gtk_clist_swap_rows (data->sheet_export,
				     data->sheet_run_export_index,
				     data->sheet_run_export_index + 1);

		gtk_clist_select_row (data->sheet_export,
				      data->sheet_run_export_index + 1, 0);
	}
}

/**
 * sheet_page_avail_select_row:
 * @clist: a gtkclist
 * @row: the newly selected row
 * @column: the newly selected column
 * @event: the event
 * @data: sheet page data
 *
 * Signal emitted when the selected row changes
 **/
static void
sheet_page_avail_select_row (GtkCList *clist, int row, int column,
			     GdkEventButton *event, StfE_SheetPageData_t *data)
{
	g_return_if_fail (clist != NULL);
	g_return_if_fail (data != NULL);

	data->sheet_run_avail_index = row;
}

/**
 * sheet_page_export_select_row:
 * @clist: a gtkclist
 * @row: the newly selected row
 * @column: the newly selected column
 * @event: the event
 * @data: sheet page data
 *
 * Signal emitted when the selected row changes
 **/
static void
sheet_page_export_select_row (GtkCList *clist, int row, int column,
			      GdkEventButton *event, StfE_SheetPageData_t *data)
{
	g_return_if_fail (clist != NULL);
	g_return_if_fail (data != NULL);

	data->sheet_run_export_index = row;
}

/*************************************************************************************************
 * MAIN EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_export_dialog_sheet_page_init
 * @gui : The glade gui of the dialog
 *
 * This routine prepares/initializes all widgets on the sheet page of the
 * Druid.
 *
 * returns : sheetpagedata struct
 **/
StfE_SheetPageData_t*
stf_export_dialog_sheet_page_init (GladeXML *gui, Workbook *wb)
{
	StfE_SheetPageData_t *data;
	GList *sheets, *ptr;

	g_return_val_if_fail (gui != NULL, NULL);
	g_return_val_if_fail (wb != NULL, NULL);

	data = g_new (StfE_SheetPageData_t, 1);

	/* Create/get object and fill information struct */

	data->sheet_avail     = GTK_CLIST  (glade_xml_get_widget (gui, "sheet_avail"));
	data->sheet_export    = GTK_CLIST  (glade_xml_get_widget (gui, "sheet_export"));

	data->sheet_add       = GTK_BUTTON (glade_xml_get_widget (gui, "sheet_add"));
	data->sheet_remove    = GTK_BUTTON (glade_xml_get_widget (gui, "sheet_remove"));
	data->sheet_addall    = GTK_BUTTON (glade_xml_get_widget (gui, "sheet_addall"));
	data->sheet_removeall = GTK_BUTTON (glade_xml_get_widget (gui, "sheet_removeall"));
	data->sheet_up        = GTK_BUTTON (glade_xml_get_widget (gui, "sheet_up"));
	data->sheet_down      = GTK_BUTTON (glade_xml_get_widget (gui, "sheet_down"));

	/* Run-time stuff */

	data->sheet_run_avail_index = -1;
	data->sheet_run_export_index = -1;

	sheets = workbook_sheets (wb);
	for (ptr = sheets ; ptr != NULL ; ptr = ptr->next) {
		Sheet *sheet = ptr->data;
		char *t[1];
		int index;

		t[0] = sheet->name_quoted;
		index = gtk_clist_append (data->sheet_avail, t);
		gtk_clist_set_row_data (data->sheet_avail, index, sheet);
	}
	g_list_free (sheets);

	/* Connect signals */

	g_signal_connect (G_OBJECT (data->sheet_add),
		"clicked",
		G_CALLBACK (sheet_page_add_clicked), data);
	g_signal_connect (G_OBJECT (data->sheet_remove),
		"clicked",
		G_CALLBACK (sheet_page_remove_clicked), data);
	g_signal_connect (G_OBJECT (data->sheet_addall),
		"clicked",
		G_CALLBACK (sheet_page_addall_clicked), data);
	g_signal_connect (G_OBJECT (data->sheet_removeall),
		"clicked",
		G_CALLBACK (sheet_page_removeall_clicked), data);
	g_signal_connect (G_OBJECT (data->sheet_up),
		"clicked",
		G_CALLBACK (sheet_page_up_clicked), data);
	g_signal_connect (G_OBJECT (data->sheet_down),
		"clicked",
		G_CALLBACK (sheet_page_down_clicked), data);
	g_signal_connect (G_OBJECT (data->sheet_avail),
		"select_row",
		G_CALLBACK (sheet_page_avail_select_row), data);
	g_signal_connect (G_OBJECT (data->sheet_export),
		"select_row",
		G_CALLBACK (sheet_page_export_select_row), data);

	gtk_clist_select_row (data->sheet_avail, 0, 0);

	return data;
}

/**
 * stf_export_dialog_sheet_page_can_continue:
 * @window : The parent window, useful when dialog needs to be displayed
 * @data: sheet page data struct
 *
 * Can be used to query weather all condition on the
 * sheet page have been met.
 *
 * Return value: return TRUE if the user can continue to the next page,
 *               FALSE otherwise.
 **/
gboolean
stf_export_dialog_sheet_page_can_continue (GtkWidget *window, StfE_SheetPageData_t *data)
{
	if (data->sheet_export->rows < 1) {
		GtkWidget *dialog = gnome_error_dialog_parented (_("You need to select at least one sheet to export"),
								 GTK_WINDOW (window));

		gnome_dialog_run (GNOME_DIALOG (dialog));

		return FALSE;
	}
	else {
		return TRUE;
	}
}

/**
 * stf_export_dialog_sheet_page_result:
 * @data: sheet page data
 * @export_options: an export options struct
 *
 * Adjusts @export_options to reflect the options choosen on the
 * sheet page
 **/
void
stf_export_dialog_sheet_page_result (StfE_SheetPageData_t *data, StfExportOptions_t *export_options)
{
	int i;

	g_return_if_fail (data != NULL);
	g_return_if_fail (export_options != NULL);

	stf_export_options_sheet_list_clear (export_options);

	for (i = 0; i < data->sheet_export->rows; i++) {
		Sheet *sheet = gtk_clist_get_row_data (data->sheet_export, i);

		stf_export_options_sheet_list_add (export_options, sheet);
	}
}

/**
 * stf_export_dialog_sheet_page_cleanup
 * @data : sheetpagedata struct
 *
 * Will cleanup sheet page run-time data
 *
 * returns : nothing
 **/
void
stf_export_dialog_sheet_page_cleanup (StfE_SheetPageData_t *data)
{
	g_return_if_fail (data != NULL);

	g_free (data);
}

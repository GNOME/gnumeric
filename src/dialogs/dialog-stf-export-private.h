/*
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
#ifndef GNUMERIC_DIALOG_STF_EXPORT_PRIVATE_H
#define GNUMERIC_DIALOG_STF_EXPORT_PRIVATE_H

#include "gui-gnumeric.h"
#include "gui-util.h"
#include "stf-export.h"
#include <glade/glade.h>
#include <libgnomeui/gnome-druid.h>

#include "../widgets/widget-charmap-selector.h"

/* for the sheet page */
typedef struct {
	GtkCList  *sheet_avail;
	GtkCList  *sheet_export;

	GtkButton *sheet_add;
	GtkButton *sheet_remove;
	GtkButton *sheet_addall;
	GtkButton *sheet_removeall;
	GtkButton *sheet_up;
	GtkButton *sheet_down;

	/* Run-time members */
	int        sheet_run_avail_index;
	int        sheet_run_export_index;
} StfE_SheetPageData_t;

/* for the format page */
typedef struct {
	GtkOptionMenu 	*format_termination;
	GtkOptionMenu 	*format_separator;
	GtkEntry      	*format_custom;
	GtkOptionMenu 	*format_quote;
	GtkCombo      	*format_quotechar;
	CharmapSelector *format_charset;
} StfE_FormatPageData_t;

/* Global stuff */
typedef enum {
	DPG_SHEET,
	DPG_FORMAT
} StfE_DruidPosition_t;

typedef struct {
	StfE_DruidPosition_t   active_page;                /* The currently active page */

	WorkbookControlGUI    *wbcg;
	GtkWindow             *window;                     /* The main window */
	GnomeDruid            *druid;                      /* The gnome druid */
	GnomeDruidPage        *sheet_page, *format_page;   /* Rest of the pages */

	StfE_FormatPageData_t *format_page_data;
	StfE_SheetPageData_t  *sheet_page_data;

	gboolean               canceled;                   /* Indicates weather the user canceled */
} StfE_DruidData_t;

/*
 * INIT FUNCTIONS
 *
 * These are called when the Dialog is created and give
 * each page the opportunity to connect signal handlers and set the contents
 * of their Data_t record
 */
StfE_SheetPageData_t*  stf_export_dialog_sheet_page_init         (GladeXML *gui, Workbook *wb);
StfE_FormatPageData_t* stf_export_dialog_format_page_init        (GladeXML *gui);

/*
 * CAN CONTINUE FUNCTIONS
 *
 * These are called right before advancing to the next
 * page in the druid. If they return FALSE the druid will
 * not advance to the next page.
 * This is to make sure that certain condition on a page
 * are met.
 */
gboolean               stf_export_dialog_sheet_page_can_continue (GtkWidget *window, StfE_SheetPageData_t *data);

/*
 * RESULT FUNCTIONS
 *
 * Before the dialog is result these functions are called.
 * They should modify the StfExportOptions_t struct to reflect
 * the choices made by the user on the druidpage
 */
void                   stf_export_dialog_sheet_page_result       (StfE_SheetPageData_t *data, StfExportOptions_t *export_options);
void                   stf_export_dialog_format_page_result      (StfE_FormatPageData_t *data, StfExportOptions_t *export_options);

/*
 * CLEANUP FUNCTIONS
 *
 * Called right before the druid is destroyed
 */
void                   stf_export_dialog_sheet_page_cleanup      (StfE_SheetPageData_t *data);
void                   stf_export_dialog_format_page_cleanup     (StfE_FormatPageData_t *data);

#endif

/*
 * dialog-stf.c : Controls the widgets on the fixed page of the dialog (fixed-width page that is)
 *
 * Almer. S. Tigelaar <almer1@dds.nl>
 *
 */

#include <config.h>
#include <gnome.h>
#include <glade/glade.h>
#include <stdlib.h>

#include "dialog-stf.h"

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/**
 * fixed_page_update_preview
 * @pagedata : mother struct
 *
 * Will simply update the preview
 *
 * returns : nothing
 **/
static void
fixed_page_update_preview (DruidPageData_t *pagedata)
{
	FixedInfo_t *info = pagedata->fixed_info;
	ParseFixedInfo_t *fixinfo = pagedata->fixed_info->fixed_run_fixinfo;
	GArray* splitpos;
	char *t[2];
	int i, temp;

	if (fixinfo->splitpos) 
		g_array_free (fixinfo->splitpos, TRUE);
	
	splitpos = g_array_new (FALSE, FALSE, sizeof (int));
	for (i = 0; i < info->fixed_collist->rows; i++) {
		gtk_clist_get_text (info->fixed_collist, i, 1, t);
		temp = atoi (t[0]);
		g_array_append_val (splitpos, temp);
	}

	fixinfo->splitpos    = splitpos;
	fixinfo->splitposcnt = info->fixed_collist->rows;

	if (info->fixed_run_modified) {
		sheet_destroy_contents (pagedata->src->sheet);
		info->fixed_run_modified = FALSE;
	}

	if (info->fixed_run_renderdata->rowsrendered == 0 && pagedata->src->lines != 0) {
		stf_fixed_parse_sheet (pagedata->src, fixinfo);
	} else {
		stf_fixed_parse_sheet_partial (pagedata->src,
					       fixinfo,
					       info->fixed_run_renderdata->startrow - 1,
					       (info->fixed_run_renderdata->startrow - 1) + info->fixed_run_renderdata->rowsrendered);
	}
	
	stf_preview_render (info->fixed_run_renderdata); 
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * fixed_page_scroll_value_changed
 * @adjustment : The gtkadjustment that emitted the signal
 * @data : a mother struct
 *
 * This signal responds to changes in the scrollbar and
 * will force a redraw of the preview
 *
 * returns : nothing
 **/
static void
fixed_page_scroll_value_changed (GtkAdjustment *adjustment, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;

	stf_preview_set_startrow (info->fixed_run_renderdata, adjustment->value);
	fixed_page_update_preview (data);
}

/**
 * fixed_page_collist_select_row
 * @clist : the GtkClist that emitted the signal
 * @row : row the user clicked on
 * @column : column the user clicked on
 * @event : information on the buttons that were pressed
 * @data : mother struct
 *
 * This will update the widgets on the right side of the dialog to
 * reflect the new column selection
 *
 * returns : nothing
 **/
static void
fixed_page_collist_select_row (GtkCList *clist, int row, int column, GdkEventButton *event, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *t[2];

	if (info->fixed_run_manual) {
		info->fixed_run_manual = FALSE;
		return;
	}
	
	info->fixed_run_index = row;
	
	gtk_clist_get_text (clist, row, 1, t);
	gtk_spin_button_set_value (info->fixed_colend, atoi(t[0]));

	gtk_widget_set_sensitive (GTK_WIDGET (info->fixed_colend), !(row == clist->rows - 1));
}

/**
 * fixed_page_colend_changed
 * @button : the gtkspinbutton that emitted the signal
 * @data : a mother struct
 *
 * if the user changes the end of the current column the preview will be redrawn
 * and the @data->fixed_info->fixed_collist will be updated
 *
 * returns : nothing
 **/
static void
fixed_page_colend_changed (GtkSpinButton *button, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *t[1];

	if (info->fixed_run_index < 0 || (info->fixed_run_index == info->fixed_collist->rows - 1))
		return;
		
	t[0] = gtk_editable_get_chars (GTK_EDITABLE (button), 0, -1);
	gtk_clist_set_text (info->fixed_collist, info->fixed_run_index, 1, t[0]);
	g_free (t[0]);

	info->fixed_run_modified = TRUE;
	fixed_page_update_preview (data);
}

/**
 * fixed_page_add_clicked
 * @button : the GtkButton that emitted the signal
 * @data : the mother struct
 *
 * This will add a new column to the @data->fixed_info->fixed_collist
 *
 * returns : nothing
 **/
static void
fixed_page_add_clicked (GtkButton *button, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *tget[1], *tset[2];
	int colindex = info->fixed_collist->rows;

	if (colindex > 1) {
		gtk_clist_get_text (info->fixed_collist, colindex - 2, 1, tget);
		tget[0] = g_strdup_printf ("%d", atoi (tget[0]) + 1);
		gtk_clist_set_text (info->fixed_collist, colindex - 1, 1, tget[0]);
		g_free (tget[0]);
	}
	else {
		tget[0] = g_strdup ("1");
		gtk_clist_set_text (info->fixed_collist, colindex -1, 1, tget[0]);
	}
	
	tset[0] = g_strdup_printf ("%d", colindex);
	tset[1] = g_strdup_printf ("%d", -1);
	gtk_clist_append (info->fixed_collist, tset);
	g_free (tset[0]);
	g_free (tset[1]);

	gtk_clist_select_row (info->fixed_collist, info->fixed_collist->rows - 2, 0);
	
	info->fixed_run_modified = TRUE;
	fixed_page_update_preview (data);
}

/**
 * fixed_page_remove_clicked
 * @button : the GtkButton that emitted the signal
 * @data : the mother struct
 *
 * This will remove the selected item from the @data->fixed_info->fixed_collist
 *
 * returns : nothing
 **/
static void
fixed_page_remove_clicked (GtkButton *button, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;

	if (info->fixed_run_index < 0 || (info->fixed_run_index == info->fixed_collist->rows - 1))
		info->fixed_run_index--;
	
	gtk_clist_remove (info->fixed_collist, info->fixed_run_index);	
	info->fixed_run_modified = TRUE;

	gtk_clist_select_row (info->fixed_collist, info->fixed_run_index, 0);
		
	fixed_page_update_preview (data);
}

/*************************************************************************************************
 * FIXED EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * fixed_page_prepare
 * @page : The druidpage that emmitted the signal
 * @druid : The gnomedruid that houses @page
 * @data : mother struct
 *
 * Will prepare the fixed page
 *
 * returns : nothing
 **/
void
fixed_page_prepare (GnomeDruidPage *page, GnomeDruid *druid, DruidPageData_t *pagedata)
{
	FixedInfo_t *info = pagedata->fixed_info;

	sheet_destroy_contents (pagedata->src->sheet);
	GTK_RANGE (info->fixed_scroll)->adjustment->upper = pagedata->src->lines + 1;

	fixed_page_update_preview (pagedata);
}

/**
 * fixed_page_cleanup
 * @pagedata : mother struct
 *
 * Will cleanup fixed page run-time data
 *
 * returns : nothing
 **/
void
fixed_page_cleanup (DruidPageData_t *pagedata)
{
	FixedInfo_t *info = pagedata->fixed_info;

	stf_preview_free (info->fixed_run_renderdata);

	if (info->fixed_run_fixinfo->splitpos)
		g_array_free (info->fixed_run_fixinfo->splitpos, TRUE);
	g_free (info->fixed_run_fixinfo);
}

/**
 * fixed_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the fixed Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
fixed_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	FixedInfo_t *info;
	char *t[2];

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);
	g_return_if_fail (pagedata->fixed_info != NULL);

	info = pagedata->fixed_info;
		
        /* Create/get object and fill information struct */
	info->fixed_collist = GTK_CLIST       (glade_xml_get_widget (gui, "fixed_collist"));
	info->fixed_colend  = GTK_SPIN_BUTTON (glade_xml_get_widget (gui, "fixed_colend"));
	info->fixed_add     = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_add"));
	info->fixed_remove  = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_remove"));
	info->fixed_canvas  = GNOME_CANVAS    (glade_xml_get_widget (gui, "fixed_canvas"));
	info->fixed_scroll  = GTK_VSCROLLBAR  (glade_xml_get_widget (gui, "fixed_scroll"));

	/* Set properties */
	info->fixed_run_renderdata = stf_preview_new (info->fixed_canvas, pagedata->src, FALSE);
	info->fixed_run_fixinfo    = g_new0 (ParseFixedInfo_t, 1);
	info->fixed_run_manual     = FALSE;
	info->fixed_run_modified   = TRUE;
	info->fixed_run_index      = -1;
	
	t[0] = g_strdup ("0");
	t[1] = g_strdup ("-1");
	gtk_clist_append (info->fixed_collist, t);
	g_free (t[0]);
	g_free (t[1]);
	
	/* Connect signals */
	gtk_signal_connect (GTK_OBJECT (info->fixed_collist),
			    "select_row",
			    GTK_SIGNAL_FUNC (fixed_page_collist_select_row),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->fixed_colend),
			    "changed",
			    GTK_SIGNAL_FUNC (fixed_page_colend_changed),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->fixed_add),
			    "clicked",
			    GTK_SIGNAL_FUNC (fixed_page_add_clicked),
			    pagedata);
	gtk_signal_connect (GTK_OBJECT (info->fixed_remove),
			    "clicked",
			    GTK_SIGNAL_FUNC (fixed_page_remove_clicked),
			    pagedata);

	gtk_signal_connect (GTK_OBJECT (GTK_RANGE (info->fixed_scroll)->adjustment),
			    "value_changed",
			    GTK_SIGNAL_FUNC (fixed_page_scroll_value_changed),
			    pagedata);
}





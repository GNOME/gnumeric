/*
 * dialog-stf-fixed-page.c : Controls the widgets on the fixed page of the dialog (fixed-width page that is)
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

#undef GTK_DISABLE_DEPRECATED
#warning "This file uses GTK_DISABLE_DEPRECATED"
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialog-stf.h"
#include <gui-util.h>

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

/**
 * fixed_page_autodiscover:
 * @pagedata: a mother struct
 *
 * Use the STF's autodiscovery function and put the
 * result in the fixed_collist
 **/
static void
fixed_page_autodiscover (DruidPageData_t *pagedata)
{
	guint i = 1;
	char *tset[2];

	stf_parse_options_fixed_autodiscover (pagedata->fixed.fixed_run_parseoptions, pagedata->importlines, (char *) pagedata->cur);

	gtk_clist_clear (pagedata->fixed.fixed_collist);
	while (i < pagedata->fixed.fixed_run_parseoptions->splitpositions->len) {
		tset[0] = g_strdup_printf ("%d", i - 1);
		tset[1] = g_strdup_printf ("%d", g_array_index (pagedata->fixed.fixed_run_parseoptions->splitpositions,
								int,
								i));
		gtk_clist_append (pagedata->fixed.fixed_collist, tset);

		g_free (tset[0]);
		g_free (tset[1]);

		i++;
	}

	tset[0] = g_strdup_printf ("%d", i - 1);
	tset[1] = g_strdup_printf ("%d", -1);

	gtk_clist_append (pagedata->fixed.fixed_collist, tset);

	g_free (tset[0]);
	g_free (tset[1]);

	/*
	 * If there are no splitpositions than apparantly
	 * no columns where found
	 */

	if (pagedata->fixed.fixed_run_parseoptions->splitpositions->len < 1) {
		GtkWidget *dialog = gtk_message_dialog_new (NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			_("Autodiscovery did not find any columns in the text. Try manually"));
		gnumeric_dialog_run (pagedata->wbcg, GTK_DIALOG (dialog));
	}
}


static void fixed_page_update_preview (DruidPageData_t *pagedata);

static gint
cb_col_event (GtkWidget *button,
	      GdkEvent  *event,
	      gpointer   _col)
{
	int col = GPOINTER_TO_INT (_col);

	if (event->type == GDK_BUTTON_PRESS) {
		DruidPageData_t *data = g_object_get_data (G_OBJECT (button), "fixed-data");
		GdkEventButton *bevent = (GdkEventButton *)event;

		if (bevent->button == 1) {
			/* Split column.  */
			GtkCellRenderer *cell =	stf_preview_get_cell_renderer
				(data->fixed.fixed_run_renderdata, col);
			char *row[2];
			int i, charindex, colstart, colend;
			PangoLayout *layout;
			PangoFontDescription *font_desc;
			int dx, width;

			if (col == 0)
				colstart = 0;
			else {
				gtk_clist_get_text (data->fixed.fixed_collist, col - 1, 1, row);
				colstart = atoi (row[0]);
			}

			gtk_clist_get_text (data->fixed.fixed_collist, col, 1, row);
			colend = atoi (row[0]);

			dx = (int)bevent->x - (GTK_BIN (button)->child->allocation.x - button->allocation.x);

			layout = gtk_widget_create_pango_layout (button, "x");

			g_object_get (G_OBJECT (cell), "font_desc", &font_desc, NULL);
			pango_layout_set_font_description (layout, font_desc);
			pango_layout_get_pixel_size (layout, &width, NULL);
			if (width < 1) width = 1;
			charindex = colstart + (dx + width / 2) / width;
			g_object_unref (layout);
			pango_font_description_free (font_desc);

#if 0
			g_print ("Start at %d; end at %d; charindex=%d\n", colstart, colend, charindex);
#endif

			if (charindex <= colstart || (colend != -1 && charindex >= colend))
				return TRUE;

			row[0] = g_strdup_printf ("%d", col + 1);
			row[1] = g_strdup_printf ("%d", charindex);
			gtk_clist_insert (data->fixed.fixed_collist, col, row);
			g_free (row[0]);
			g_free (row[1]);

			/* Patch the following column numbers in the list.  */
			for (i = col; i < GTK_CLIST (data->fixed.fixed_collist)->rows; i++) {
				char *text = g_strdup_printf ("%d", i);
				gtk_clist_set_text (data->fixed.fixed_collist, i, 0, text);
				g_free (text);
			}

			fixed_page_update_preview (data);
			return TRUE;
		}

		if (bevent->button == 3) {
			/* Remove column.  */
			gtk_clist_select_row (data->fixed.fixed_collist, col, 0);
			gnumeric_clist_moveto (data->fixed.fixed_collist, col);

			gtk_signal_emit_by_name (GTK_OBJECT (data->fixed.fixed_remove),
						 "clicked",
						 data);
			return TRUE;
		}
	}
	    
	return FALSE;
}


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
	StfParseOptions_t *parseoptions = pagedata->fixed.fixed_run_parseoptions;
	RenderData_t *renderdata = pagedata->fixed.fixed_run_renderdata;
	GPtrArray *lines;
	char *t[2];
	int i, temp;

	stf_parse_options_fixed_splitpositions_clear (parseoptions);
	for (i = 0; i < GTK_CLIST (pagedata->fixed.fixed_collist)->rows; i++) {
		gtk_clist_get_text (pagedata->fixed.fixed_collist, i, 1, t);
		temp = atoi (t[0]);
		stf_parse_options_fixed_splitpositions_add (parseoptions, temp);
	}

	lines = stf_parse_general (parseoptions, pagedata->cur);

	stf_preview_render (renderdata, lines);

	for (i = 0; i < renderdata->colcount; i++) {
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, i);
		GtkCellRenderer *cell =
			stf_preview_get_cell_renderer (renderdata, i);

		g_object_set (G_OBJECT (cell),
			      "family", "monospace",
			      NULL);

		g_object_set_data (G_OBJECT (column->button), "fixed-data", pagedata);
		g_object_set (G_OBJECT (column), "clickable", TRUE, NULL);
		g_signal_connect (column->button, "event",
				  G_CALLBACK (cb_col_event),
				  GINT_TO_POINTER (i));
	}
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

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
fixed_page_collist_select_row (GtkCList *clist, int row,
			       G_GNUC_UNUSED int column,
			       G_GNUC_UNUSED GdkEventButton *event,
			       DruidPageData_t *data)
{
	char *t[2];

	if (data->fixed.fixed_run_manual) {
		data->fixed.fixed_run_manual = FALSE;
		return;
	}

	data->fixed.fixed_run_index = row;

	gtk_clist_get_text (clist, row, 1, t);
	gtk_spin_button_set_value (data->fixed.fixed_colend, atoi (t[0]));

	gtk_widget_set_sensitive (GTK_WIDGET (data->fixed.fixed_colend), !(row == clist->rows - 1));
}

/**
 * fixed_page_colend_changed
 * @button : the gtkspinbutton that emitted the signal
 * @data : a mother struct
 *
 * if the user changes the end of the current column the preview will be redrawn
 * and the @data->fixed_data->fixed.fixed_collist will be updated
 *
 * returns : nothing
 **/
static void
fixed_page_colend_changed (GtkSpinButton *button, DruidPageData_t *data)
{
	char *text;

	if (data->fixed.fixed_run_index < 0 || (data->fixed.fixed_run_index == GTK_CLIST (data->fixed.fixed_collist)->rows - 1))
		return;

	text = gtk_editable_get_chars (GTK_EDITABLE (button), 0, -1);
	gtk_clist_set_text (data->fixed.fixed_collist, data->fixed.fixed_run_index, 1, text);
	g_free (text);

	fixed_page_update_preview (data);
}

/**
 * fixed_page_add_clicked
 * @button : the GtkButton that emitted the signal
 * @data : the mother struct
 *
 * This will add a new column to the @data->fixed_data->fixed.fixed_collist
 *
 * returns : nothing
 **/
static void
fixed_page_add_clicked (G_GNUC_UNUSED GtkButton *button,
			DruidPageData_t *data)
{
	char *tget[1], *tset[2];
	int colindex = GTK_CLIST (data->fixed.fixed_collist)->rows;

	if (colindex > 1) {
		gtk_clist_get_text (data->fixed.fixed_collist, colindex - 2, 1, tget);
		tget[0] = g_strdup_printf ("%d", atoi (tget[0]) + 1);
		gtk_clist_set_text (data->fixed.fixed_collist, colindex - 1, 1, tget[0]);
		g_free (tget[0]);
	} else {
		tget[0] = g_strdup ("1");
		gtk_clist_set_text (data->fixed.fixed_collist, colindex - 1, 1, tget[0]);
		g_free (tget[0]);
	}

	tset[0] = g_strdup_printf ("%d", colindex);
	tset[1] = g_strdup_printf ("%d", -1);
	gtk_clist_append (data->fixed.fixed_collist, tset);
	g_free (tset[0]);
	g_free (tset[1]);

	gtk_clist_select_row (data->fixed.fixed_collist, GTK_CLIST (data->fixed.fixed_collist)->rows - 2, 0);
	gnumeric_clist_moveto (data->fixed.fixed_collist, GTK_CLIST (data->fixed.fixed_collist)->rows - 2);

	fixed_page_update_preview (data);
}

/**
 * fixed_page_remove_clicked
 * @button : the GtkButton that emitted the signal
 * @data : the mother struct
 *
 * This will remove the selected item from the @data->fixed_data->fixed.fixed_collist
 *
 * returns : nothing
 **/
static void
fixed_page_remove_clicked (G_GNUC_UNUSED GtkButton *button,
			   DruidPageData_t *data)
{
	int i;

	if (data->fixed.fixed_run_index < 0 || (data->fixed.fixed_run_index == GTK_CLIST (data->fixed.fixed_collist)->rows - 1))
		data->fixed.fixed_run_index--;

	gtk_clist_remove (data->fixed.fixed_collist, data->fixed.fixed_run_index);

	for (i = data->fixed.fixed_run_index; i < GTK_CLIST (data->fixed.fixed_collist)->rows; i++) {
		char *text = g_strdup_printf ("%d", i);

		gtk_clist_set_text (data->fixed.fixed_collist, i, 0, text);
		g_free (text);
	}

	gtk_clist_select_row (data->fixed.fixed_collist, data->fixed.fixed_run_index, 0);
	gnumeric_clist_moveto (data->fixed.fixed_collist, data->fixed.fixed_run_index);

	fixed_page_update_preview (data);
}

/**
 * fixed_page_clear_clicked:
 * @button: GtkButton
 * @data: mother struct
 *
 * Will clear all entries in fixed_collist
 **/
static void
fixed_page_clear_clicked (G_GNUC_UNUSED GtkButton *button,
			  DruidPageData_t *data)
{
	char *tset[2];

	gtk_clist_clear (data->fixed.fixed_collist);

	tset[0] = g_strdup ("0");
	tset[1] = g_strdup ("-1");

	gtk_clist_append (data->fixed.fixed_collist, tset);

	g_free (tset[0]);
	g_free (tset[1]);

	fixed_page_update_preview (data);
}

/**
 * fixed_page_auto_clicked:
 * @button: GtkButton
 * @data: mother struct
 *
 * Will try to automatically recognize columns in the
 * text.
 **/
static void
fixed_page_auto_clicked (G_GNUC_UNUSED GtkButton *button,
			 DruidPageData_t *data)
{
	fixed_page_autodiscover (data);

	fixed_page_update_preview (data);
}

/*************************************************************************************************
 * FIXED EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_fixed_page_prepare
 * @page : The druidpage that emitted the signal
 * @druid : The gnomedruid that houses @page
 * @data : mother struct
 *
 * Will prepare the fixed page
 *
 * returns : nothing
 **/
void
stf_dialog_fixed_page_prepare (G_GNUC_UNUSED GnomeDruidPage *page,
			       G_GNUC_UNUSED GnomeDruid *druid,
			       DruidPageData_t *pagedata)
{
	GtkAdjustment *spinadjust;

	stf_parse_options_set_trim_spaces (pagedata->fixed.fixed_run_parseoptions, TRIM_TYPE_NEVER);

#if 0
	stf_preview_set_startrow (pagedata->fixed.fixed_run_renderdata, GTK_RANGE (pagedata->fixed.fixed_scroll)->adjustment->value);
#endif

	spinadjust = gtk_spin_button_get_adjustment (pagedata->fixed.fixed_colend);
	spinadjust->lower = 1;
	spinadjust->upper = stf_parse_get_longest_row_width (pagedata->fixed.fixed_run_parseoptions, pagedata->cur);
	gtk_spin_button_set_adjustment (pagedata->fixed.fixed_colend, spinadjust);

	fixed_page_update_preview (pagedata);
}

/**
 * stf_dialog_fixed_page_cleanup
 * @pagedata : mother struct
 *
 * Will cleanup fixed page run-time data
 *
 * returns : nothing
 **/
void
stf_dialog_fixed_page_cleanup (DruidPageData_t *pagedata)
{
	stf_preview_free (pagedata->fixed.fixed_run_renderdata);

	if (pagedata->fixed.fixed_run_parseoptions) {
		stf_parse_options_free (pagedata->fixed.fixed_run_parseoptions);
		pagedata->fixed.fixed_run_parseoptions = NULL;
	}
}

/**
 * stf_dialog_fixed_page_init
 * @gui : The glade gui of the dialog
 * @pagedata : pagedata mother struct passed to signal handlers etc.
 *
 * This routine prepares/initializes all widgets on the fixed Page of the
 * Druid.
 *
 * returns : nothing
 **/
void
stf_dialog_fixed_page_init (GladeXML *gui, DruidPageData_t *pagedata)
{
	char *t[2];

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

        /* Create/get object and fill information struct */
	pagedata->fixed.fixed_collist = GTK_CLIST       (glade_xml_get_widget (gui, "fixed_collist"));
	pagedata->fixed.fixed_colend  = GTK_SPIN_BUTTON (glade_xml_get_widget (gui, "fixed_colend"));
	pagedata->fixed.fixed_add     = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_add"));
	pagedata->fixed.fixed_remove  = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_remove"));
	pagedata->fixed.fixed_clear   = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_clear"));
	pagedata->fixed.fixed_auto    = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_auto"));
	pagedata->fixed.fixed_data_container =          (glade_xml_get_widget (gui, "fixed_data_container"));

	/* Set properties */
	pagedata->fixed.fixed_run_renderdata    = stf_preview_new (pagedata->fixed.fixed_data_container, NULL);
	pagedata->fixed.fixed_run_parseoptions  = stf_parse_options_new ();
	pagedata->fixed.fixed_run_manual        = FALSE;
	pagedata->fixed.fixed_run_index         = -1;
	pagedata->fixed.fixed_run_mousedown     = FALSE;
	pagedata->fixed.fixed_run_xorigin       = 0;

	stf_parse_options_set_type  (pagedata->fixed.fixed_run_parseoptions, PARSE_TYPE_FIXED);

	gtk_clist_column_titles_passive (pagedata->fixed.fixed_collist);

	t[0] = g_strdup ("0");
	t[1] = g_strdup ("-1");
	gtk_clist_append (pagedata->fixed.fixed_collist, t);
	g_free (t[0]);
	g_free (t[1]);

	/* Connect signals */
	g_signal_connect (G_OBJECT (pagedata->fixed.fixed_collist),
		"select_row",
		G_CALLBACK (fixed_page_collist_select_row), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed.fixed_colend),
		"changed",
		G_CALLBACK (fixed_page_colend_changed), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed.fixed_add),
		"clicked",
		G_CALLBACK (fixed_page_add_clicked), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed.fixed_remove),
		"clicked",
		G_CALLBACK (fixed_page_remove_clicked), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed.fixed_clear),
		"clicked",
		G_CALLBACK (fixed_page_clear_clicked), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed.fixed_auto),
		"clicked",
		G_CALLBACK (fixed_page_auto_clicked), pagedata);
}

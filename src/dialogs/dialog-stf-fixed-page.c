/*
 * dialog-stf-fixed-page.c : Controls the widgets on the fixed page of the dialog (fixed-width page that is)
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
	FixedInfo_t *info = pagedata->fixed_info;
	guint i = 1;
	char *tset[2];

	stf_parse_options_fixed_autodiscover (info->fixed_run_parseoptions, pagedata->importlines, (char *) pagedata->cur);

	gtk_clist_clear (info->fixed_collist);

	while (i < info->fixed_run_parseoptions->splitpositions->len) {

		tset[0] = g_strdup_printf ("%d", i - 1);
		tset[1] = g_strdup_printf ("%d", g_array_index (info->fixed_run_parseoptions->splitpositions,
								int,
								i));
		gtk_clist_append (info->fixed_collist, tset);

		g_free (tset[0]);
		g_free (tset[1]);

		i++;
	}

	tset[0] = g_strdup_printf ("%d", i - 1);
	tset[1] = g_strdup_printf ("%d", -1);

	gtk_clist_append (info->fixed_collist, tset);

	g_free (tset[0]);
	g_free (tset[1]);

	/*
	 * If there are no splitpositions than apparantly
	 * no columns where found
	 */

	if (info->fixed_run_parseoptions->splitpositions->len < 1) {
		GtkWidget *dialog = gtk_message_dialog_new (NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			_("Autodiscovery did not find any columns in the text. Try manually"));
		gnumeric_dialog_run (pagedata->wbcg, GTK_DIALOG (dialog));
	}
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
	FixedInfo_t *info = pagedata->fixed_info;
	StfParseOptions_t *parseoptions = pagedata->fixed_info->fixed_run_parseoptions;
	GList *list;
	char *t[2];
	int i, temp;

	stf_parse_options_fixed_splitpositions_clear (parseoptions);
	for (i = 0; i < info->fixed_collist->rows; i++) {
		gtk_clist_get_text (info->fixed_collist, i, 1, t);
		temp = atoi (t[0]);
		stf_parse_options_fixed_splitpositions_add (parseoptions, temp);
	}

	pagedata->colcount = stf_parse_get_colcount (parseoptions, pagedata->cur);

	stf_preview_colwidths_clear (info->fixed_run_renderdata);
	for (i = 0; i < pagedata->colcount + 1; i++)
		stf_preview_colwidths_add (info->fixed_run_renderdata, stf_parse_get_colwidth (parseoptions, pagedata->cur, i));

	list = stf_parse_general (parseoptions, pagedata->cur);

	stf_preview_render (info->fixed_run_renderdata, list,
			    info->fixed_run_displayrows,
			    pagedata->colcount);
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
 * fixed_page_canvas_motion_notify_event
 * @canvas : The gnome canvas that emitted the signal
 * @event : a gdk motion event struct
 * @data : a mother struct
 *
 * This event handles the changing of the mouse cursor and
 * re-sizing of columns
 *
 * returns : always TRUE
 **/
static gboolean
fixed_page_canvas_motion_notify_event (GnomeCanvas *canvas, GdkEventMotion *event, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	GdkCursor *cursor;
	double worldx, worldy;
	int column;

	gnome_canvas_window_to_world (canvas, event->x, event->y, &worldx, &worldy);

	column = stf_preview_get_column_border_at_x (info->fixed_run_renderdata, worldx);

	if (column != -1 || info->fixed_run_mousedown) {

		cursor = gdk_cursor_new (GDK_SB_H_DOUBLE_ARROW);
		gdk_window_set_cursor (canvas->layout.bin_window, cursor);
		gdk_cursor_destroy (cursor);

		/* This is were the actual resizing is done, now we simply wait till
		 * the user moves the mouse "width in pixels of a char" pixels
		 * after that we reset the x_position and adjust the column end and
		 * wait till the same thing happens again
		 */
		if (info->fixed_run_mousedown) {
			char *t[1];
			double diff;
			int min, max;
			int colend, chars;

			diff = worldx - info->fixed_run_xorigin;
			chars = diff / info->fixed_run_renderdata->charwidth;

			if (chars != 0) {

				info->fixed_run_xorigin = worldx;

				gtk_clist_get_text (info->fixed_collist, info->fixed_run_column, 1, t);

				colend = atoi (t[0]);

				if (info->fixed_run_column > 0) {

					gtk_clist_get_text (info->fixed_collist, info->fixed_run_column - 1, 1, t);
					min = atoi (t[0]) + 1;
				} else
					min = 1;

				if (info->fixed_run_column < info->fixed_collist->rows - 2) {

					gtk_clist_get_text (info->fixed_collist, info->fixed_run_column + 1, 1, t);
					max = atoi (t[0]) - 1;
				} else {
					GtkAdjustment *spinadjust = gtk_spin_button_get_adjustment (info->fixed_colend);

					max = spinadjust->upper;
				}

				colend += chars;

				if (colend < min)
					colend = min;

				if (colend > max)
					colend = max;

				gtk_clist_select_row (info->fixed_collist, info->fixed_run_column, 0);
				gnumeric_clist_moveto (info->fixed_collist, info->fixed_run_column);

				gtk_spin_button_set_value (info->fixed_colend, colend);

				fixed_page_update_preview (data);
			}
		}
	} else {

		cursor = gdk_cursor_new (GDK_HAND2);
		gdk_window_set_cursor (canvas->layout.bin_window, cursor);
		gdk_cursor_destroy (cursor);
	}

	return TRUE;
}

/**
 * fixed_page_canvas_motion_notify_event
 * @canvas : The gnome canvas that emitted the signal
 * @event : a gdk event button struct
 * @data : a mother struct
 *
 * This handles single clicking/drag start events :
 * 1) start of re-sizing of existing columns
 * 2) removal of existing columns
 *
 * returns : always TRUE
 **/
static gboolean
fixed_page_canvas_button_press_event (GnomeCanvas *canvas, GdkEventButton *event, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	double worldx, worldy;
	int column;

	gnome_canvas_window_to_world (canvas, event->x, event->y, &worldx, &worldy);

	column = stf_preview_get_column_border_at_x (info->fixed_run_renderdata, worldx);

	switch (event->button) {
	case 1 : { /* Left button -> Resize */

		if (column != -1) {

			info->fixed_run_xorigin   = worldx;
			info->fixed_run_column    = column;
			info->fixed_run_mousedown = TRUE;
		}
	} break;
	case 3 : { /* Right button -> Remove */

		if (column != -1) {

			gtk_clist_select_row (info->fixed_collist, column, 0);
			gnumeric_clist_moveto (info->fixed_collist, column);

			gtk_signal_emit_by_name (GTK_OBJECT (info->fixed_remove),
						 "clicked",
						 data);
		}
	} break;
	}

	return TRUE;
}

/**
 * fixed_page_canvas_motion_notify_event
 * @canvas : The gnome canvas that emitted the signal
 * @event : a gdk event button struct
 * @data : a mother struct
 *
 * This signal handler actually handles creating new columns
 *
 * returns : always TRUE
 **/
static gboolean
fixed_page_canvas_button_release_event (GnomeCanvas *canvas, GdkEventButton *event, DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	double worldx, worldy;

	gnome_canvas_window_to_world (canvas, event->x, event->y, &worldx, &worldy);

	/* User must not drag an have clicked the LEFT mousebutton */
	if (!info->fixed_run_mousedown && event->button == 1) {
		int charindex = stf_preview_get_char_at_x (info->fixed_run_renderdata, worldx);

		/* If the user clicked on a sane place, create a new column */
		if (charindex != -1) {
			int colindex = stf_preview_get_column_at_x (info->fixed_run_renderdata, worldx);
			int i;
			char *row[2];

			if (colindex > 0) {
				char *coltext[1];

				gtk_clist_get_text (info->fixed_collist, colindex - 1, 1, coltext);
				if (atoi (coltext[0]) == charindex) /* Don't create a new column with the same splitposition */
					return TRUE;
			} else if (charindex == 0) /* don't create a leading empty column */
				return TRUE;

			row[0] = g_strdup_printf ("%d", colindex + 1);
			row[1] = g_strdup_printf ("%d", charindex);
			gtk_clist_insert (info->fixed_collist, colindex, row);
			g_free (row[0]);
			g_free (row[1]);

			for (i = colindex; i < info->fixed_collist->rows; i++) {
				char *text = g_strdup_printf ("%d", i);

				gtk_clist_set_text (info->fixed_collist, i, 0, text);
				g_free (text);
			}

			fixed_page_update_preview (data);
		}
	} else
		info->fixed_run_mousedown = FALSE;

	return TRUE;
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
fixed_page_collist_select_row (GtkCList *clist, int row,
			       __attribute__((unused)) int column,
			       __attribute__((unused)) GdkEventButton *event,
			       DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *t[2];

	if (info->fixed_run_manual) {
		info->fixed_run_manual = FALSE;
		return;
	}

	info->fixed_run_index = row;

	gtk_clist_get_text (clist, row, 1, t);
	gtk_spin_button_set_value (info->fixed_colend, atoi (t[0]));

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
	char *text;

	if (info->fixed_run_index < 0 || (info->fixed_run_index == info->fixed_collist->rows - 1))
		return;

	text = gtk_editable_get_chars (GTK_EDITABLE (button), 0, -1);
	gtk_clist_set_text (info->fixed_collist, info->fixed_run_index, 1, text);
	g_free (text);

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
fixed_page_add_clicked (__attribute__((unused)) GtkButton *button,
			DruidPageData_t *data)
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
		gtk_clist_set_text (info->fixed_collist, colindex - 1, 1, tget[0]);
		g_free (tget[0]);
	}

	tset[0] = g_strdup_printf ("%d", colindex);
	tset[1] = g_strdup_printf ("%d", -1);
	gtk_clist_append (info->fixed_collist, tset);
	g_free (tset[0]);
	g_free (tset[1]);

	gtk_clist_select_row (info->fixed_collist, info->fixed_collist->rows - 2, 0);
	gnumeric_clist_moveto (info->fixed_collist, info->fixed_collist->rows - 2);

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
fixed_page_remove_clicked (__attribute__((unused)) GtkButton *button,
			   DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	int i;

	if (info->fixed_run_index < 0 || (info->fixed_run_index == info->fixed_collist->rows - 1))
		info->fixed_run_index--;

	gtk_clist_remove (info->fixed_collist, info->fixed_run_index);

	for (i = info->fixed_run_index; i < info->fixed_collist->rows; i++) {
		char *text = g_strdup_printf ("%d", i);

		gtk_clist_set_text (info->fixed_collist, i, 0, text);
		g_free (text);
	}

	gtk_clist_select_row (info->fixed_collist, info->fixed_run_index, 0);
	gnumeric_clist_moveto (info->fixed_collist, info->fixed_run_index);

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
fixed_page_clear_clicked (__attribute__((unused)) GtkButton *button,
			  DruidPageData_t *data)
{
	FixedInfo_t *info = data->fixed_info;
	char *tset[2];

	gtk_clist_clear (info->fixed_collist);

	tset[0] = g_strdup ("0");
	tset[1] = g_strdup ("-1");

	gtk_clist_append (info->fixed_collist, tset);

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
fixed_page_auto_clicked (__attribute__((unused)) GtkButton *button,
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
stf_dialog_fixed_page_prepare (__attribute__((unused)) GnomeDruidPage *page,
			       __attribute__((unused)) GnomeDruid *druid,
			       DruidPageData_t *pagedata)
{
	FixedInfo_t *info = pagedata->fixed_info;
	GtkAdjustment *spinadjust;

	stf_parse_options_set_trim_spaces (info->fixed_run_parseoptions, TRIM_TYPE_NEVER);
	pagedata->colcount = stf_parse_get_colcount (info->fixed_run_parseoptions, pagedata->cur);

	/*
	 * This piece of code is here to limit the number of rows we display
	 * when previewing
	 */
	{
		int rowcount = stf_parse_get_rowcount (info->fixed_run_parseoptions, pagedata->cur) + 1;

		if (rowcount > LINE_DISPLAY_LIMIT) {
			GTK_RANGE (info->fixed_scroll)->adjustment->upper = LINE_DISPLAY_LIMIT;
			stf_parse_options_set_lines_to_parse (info->fixed_run_parseoptions, LINE_DISPLAY_LIMIT);
		} else {
			GTK_RANGE (info->fixed_scroll)->adjustment->upper = pagedata->importlines;
			stf_parse_options_set_lines_to_parse (info->fixed_run_parseoptions, pagedata->importlines);
		}
	}

	gtk_adjustment_changed (GTK_RANGE (info->fixed_scroll)->adjustment);
	stf_preview_set_startrow (info->fixed_run_renderdata, GTK_RANGE (info->fixed_scroll)->adjustment->value);

	spinadjust = gtk_spin_button_get_adjustment (info->fixed_colend);
	spinadjust->lower = 1;
	spinadjust->upper = stf_parse_get_longest_row_width (info->fixed_run_parseoptions, pagedata->cur);
	gtk_spin_button_set_adjustment (info->fixed_colend, spinadjust);

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
	FixedInfo_t *info = pagedata->fixed_info;

	stf_preview_free (info->fixed_run_renderdata);

	if (info->fixed_run_parseoptions) {
		stf_parse_options_free (info->fixed_run_parseoptions);
		info->fixed_run_parseoptions = NULL;
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
	info->fixed_clear   = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_clear"));
	info->fixed_auto    = GTK_BUTTON      (glade_xml_get_widget (gui, "fixed_auto"));
	info->fixed_canvas  = GNOME_CANVAS    (glade_xml_get_widget (gui, "fixed_canvas"));
	info->fixed_scroll  = GTK_VSCROLLBAR  (glade_xml_get_widget (gui, "fixed_scroll"));

	/* Set properties */
	info->fixed_run_renderdata    = stf_preview_new (info->fixed_canvas, FALSE);
	info->fixed_run_parseoptions  = stf_parse_options_new ();
	info->fixed_run_manual        = FALSE;
	info->fixed_run_index         = -1;
	info->fixed_run_displayrows   = stf_preview_get_displayed_rowcount (info->fixed_run_renderdata);
	info->fixed_run_mousedown     = FALSE;
	info->fixed_run_xorigin       = 0;

	stf_parse_options_set_type  (info->fixed_run_parseoptions, PARSE_TYPE_FIXED);

	gtk_clist_column_titles_passive (info->fixed_collist);

	t[0] = g_strdup ("0");
	t[1] = g_strdup ("-1");
	gtk_clist_append (info->fixed_collist, t);
	g_free (t[0]);
	g_free (t[1]);

	/* Connect signals */
	g_signal_connect (G_OBJECT (info->fixed_collist),
		"select_row",
		G_CALLBACK (fixed_page_collist_select_row), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_colend),
		"changed",
		G_CALLBACK (fixed_page_colend_changed), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_add),
		"clicked",
		G_CALLBACK (fixed_page_add_clicked), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_remove),
		"clicked",
		G_CALLBACK (fixed_page_remove_clicked), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_clear),
		"clicked",
		G_CALLBACK (fixed_page_clear_clicked), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_auto),
		"clicked",
		G_CALLBACK (fixed_page_auto_clicked), pagedata);

	g_signal_connect (G_OBJECT (info->fixed_canvas),
		"motion_notify_event",
		G_CALLBACK (fixed_page_canvas_motion_notify_event), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_canvas),
		"button_press_event",
		G_CALLBACK (fixed_page_canvas_button_press_event), pagedata);
	g_signal_connect (G_OBJECT (info->fixed_canvas),
		"button_release_event",
		G_CALLBACK (fixed_page_canvas_button_release_event), pagedata);

	g_signal_connect (G_OBJECT (GTK_RANGE (info->fixed_scroll)->adjustment),
		"value_changed",
		G_CALLBACK (fixed_page_scroll_value_changed), pagedata);
}

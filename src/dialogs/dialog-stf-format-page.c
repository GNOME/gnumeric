/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dialog-stf-format-page.c : Controls the widgets on the format page of the dialog
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
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include "dialog-stf.h"
#include <gnm-format.h>
#include <sheet.h>
#include <workbook.h>
#include <workbook-control.h>
#include <gui-util.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

/*************************************************************************************************
 * MISC UTILITY FUNCTIONS
 *************************************************************************************************/

static void format_page_update_preview (StfDialogData *pagedata);

static void
format_page_update_column_selection (StfDialogData *pagedata)
{
	char *text = NULL;
	
	if (pagedata->format.col_import_count == pagedata->format.col_import_array_len) {
		text = g_strdup_printf (_("Importing %i columns and ignoring none."),
					pagedata->format.col_import_count);
	} else {
		text = g_strdup_printf (_("Importing %i columns and ignoring %i."),
					pagedata->format.col_import_count,
					pagedata->format.col_import_array_len - pagedata->format.col_import_count);
	}

	gtk_label_set_text (GTK_LABEL (pagedata->format.column_selection_label), text);

	g_free (text);
}

static void
format_page_trim_menu_changed (G_GNUC_UNUSED GtkMenu *menu,
				  StfDialogData *data)
{
	StfTrimType_t trim;
	int trimtype = gtk_combo_box_get_active (GTK_COMBO_BOX (data->format.format_trim));

	switch (trimtype) {
	case -1:
	case 0:
		trim = TRIM_TYPE_LEFT | TRIM_TYPE_RIGHT;
		break;
	default:
		g_warning ("Unknown trim type selected (%d)", trimtype);
		/* Fall through.  */
	case 1:
		trim = TRIM_TYPE_NEVER;
		break;
	case 2:
		trim = TRIM_TYPE_LEFT;
		break;
	case 3:
		trim = TRIM_TYPE_RIGHT;
		break;
	}

	stf_parse_options_set_trim_spaces (data->parseoptions, trim);
	format_page_update_preview (data);
}

/*
 * More or less a copy of gtk_tree_view_clamp_column_visible.
 */
static void
tree_view_clamp_column_visible (GtkTreeView       *tree_view,
				GtkTreeViewColumn *column)
{
	GtkAdjustment *hadjustment = gtk_tree_view_get_hadjustment (tree_view);
	GtkWidget *button = column->button;

	if ((hadjustment->value + hadjustment->page_size) <
	    (button->allocation.x + button->allocation.width))
		gtk_adjustment_set_value (hadjustment,
					  button->allocation.x +
					  button->allocation.width -
					  hadjustment->page_size);
	else if (hadjustment->value > button->allocation.x)
		gtk_adjustment_set_value (hadjustment,
					  button->allocation.x);
}

static void
activate_column (StfDialogData *pagedata, int i)
{
	GOFormat *colformat;
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	RenderData_t *renderdata = pagedata->format.renderdata;

	cell = stf_preview_get_cell_renderer (renderdata,
					      pagedata->format.index);
	if (cell) {
		g_object_set (G_OBJECT (cell),
			      "background", NULL,
			      NULL);
	}

	pagedata->format.index = i;

	column = stf_preview_get_column (renderdata, i);
	if (column) {
		tree_view_clamp_column_visible (renderdata->tree_view, column);
	}

	cell = stf_preview_get_cell_renderer (renderdata, i);
	if (cell) {
		g_object_set (G_OBJECT (cell),
			      "background", "lightgrey",
			      NULL);
		gtk_widget_queue_draw (GTK_WIDGET (renderdata->tree_view));
	}

	/* FIXME: warp focus away from the header.  */

	colformat = g_ptr_array_index (pagedata->format.formats, pagedata->format.index);
	if (colformat) {
	     g_signal_handler_block(pagedata->format.format_selector,
				    pagedata->format.format_changed_handler_id);
	     go_format_sel_set_style_format 
		  (pagedata->format.format_selector, colformat);
	     g_signal_handler_unblock(pagedata->format.format_selector,
				      pagedata->format.format_changed_handler_id);
	}
	
}

static void
cb_col_check_clicked (GtkToggleButton *togglebutton, gpointer _i)
{
	int i = GPOINTER_TO_INT (_i);
	StfDialogData *pagedata =
		g_object_get_data (G_OBJECT (togglebutton), "pagedata");
	gboolean active = gtk_toggle_button_get_active (togglebutton);
	GtkCellRenderer *renderer;

	g_return_if_fail (i < pagedata->format.col_import_array_len);

	if (pagedata->format.col_import_array[i] == active)
		return;

	renderer = stf_preview_get_cell_renderer (pagedata->format.renderdata, i);
	g_object_set (G_OBJECT (renderer), "strikethrough", !active, NULL);
	gtk_widget_queue_draw (GTK_WIDGET (pagedata->format.renderdata->tree_view));

	if (!active) {
		pagedata->format.col_import_array[i] = FALSE;
		pagedata->format.col_import_count--;
		format_page_update_column_selection (pagedata);
	} else {
		if (pagedata->format.col_import_count < gnm_sheet_get_max_cols (NULL)) {
			pagedata->format.col_import_array[i] = TRUE;
			pagedata->format.col_import_count++;
			format_page_update_column_selection (pagedata);
		} else {
			char *msg = g_strdup_printf( 
				ngettext("A maximum of %d column can be imported.",
					 "A maximum of %d columns can be imported.",
					 gnm_sheet_get_max_cols (NULL)), 
				gnm_sheet_get_max_cols (NULL));
			gtk_toggle_button_set_active (togglebutton, FALSE);
			go_gtk_notice_dialog (GTK_WINDOW (pagedata->dialog),
					 GTK_MESSAGE_WARNING, msg);
			g_free (msg);
		}
	}
	return;
}

static void 
check_columns_for_import (StfDialogData *pagedata, int from, int to)
{
	int i;

	g_return_if_fail (pagedata != NULL);
	g_return_if_fail (!(from < 0));
	g_return_if_fail (to < pagedata->format.renderdata->colcount);
	g_return_if_fail (to < pagedata->format.col_import_array_len);

	for (i = from; i <= to; i++) {
		if (!pagedata->format.col_import_array[i]) {
			GtkTreeViewColumn* column = stf_preview_get_column (pagedata->format.renderdata, i);
			GtkWidget *w = g_object_get_data (G_OBJECT (column), "checkbox");
			if (!(pagedata->format.col_import_count < gnm_sheet_get_max_cols (NULL)))
				return;
			gtk_widget_hide (w);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
			/* Note this caused a signal to be send that sets the */
			/* pagedata fields */
			gtk_widget_show (w);
		}
	}
}

static void 
uncheck_columns_for_import (StfDialogData *pagedata, int from, int to)
{
	int i;
	
	g_return_if_fail (pagedata != NULL);
	g_return_if_fail (!(from < 0));
	g_return_if_fail (to < pagedata->format.renderdata->colcount);
	g_return_if_fail (to < pagedata->format.col_import_array_len);
	
	for (i = from; i <= to; i++) {
		if (pagedata->format.col_import_array[i]) {
			GtkTreeViewColumn* column = stf_preview_get_column (pagedata->format.renderdata, i);
			GtkWidget *w = g_object_get_data (G_OBJECT (column), "checkbox");
			
			gtk_widget_hide (w);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), FALSE);
			/* Note this caused a signal to be send that sets the */
			/* pagedata fields */
			gtk_widget_show (w);
		}
	}
}

static void
cb_popup_menu_uncheck_right (GtkWidget *widget, gpointer data)
{
	StfDialogData *pagedata = data;
	
	uncheck_columns_for_import (pagedata, pagedata->format.index + 1,
				    pagedata->format.renderdata->colcount - 1);
}

static void
cb_popup_menu_check_right (GtkWidget *widget, gpointer data)
{
	StfDialogData *pagedata = data;
	
	check_columns_for_import (pagedata, pagedata->format.index + 1,
				  pagedata->format.renderdata->colcount - 1);
}

static void
cb_popup_menu_uncheck_left (GtkWidget *widget, gpointer data)
{
	StfDialogData *pagedata = data;
	
	uncheck_columns_for_import (pagedata, 0, pagedata->format.index - 1);
}

static void
cb_popup_menu_check_left (GtkWidget *widget, gpointer data)
{
	StfDialogData *pagedata = data;
	
	check_columns_for_import (pagedata, 0, pagedata->format.index - 1);
}

static void
cb_popup_menu_extend_format (GtkWidget *widget, gpointer data)
{
	StfDialogData *pagedata = data;
	guint index = pagedata->format.index;
	GOFormat *colformat = g_ptr_array_index 
		(pagedata->format.formats, pagedata->format.index);

	for (index++; index < pagedata->format.formats->len; index++) {
		GOFormat *sf = g_ptr_array_index 
			(pagedata->format.formats, index);
		go_format_unref (sf);
		g_ptr_array_index (pagedata->format.formats, index) 
			= go_format_ref (colformat); 
	}

	format_page_update_preview (data);
}

static void
format_context_menu (StfDialogData *pagedata,
		     GdkEventButton *event_button,
		     int col)
{
	enum {
		COLUMN_POPUP_ITEM_IGNORE,
		COLUMN_POPUP_ITEM_NOT_FIRST,
		COLUMN_POPUP_ITEM_NOT_LAST,
		COLUMN_POPUP_ITEM_ANY
	};

	static const struct {
		const char *text;
		void (*function) (GtkWidget *widget, gpointer data);
		int flags;
	} actions[] = {
		{ N_("Ignore all columns on right"), &cb_popup_menu_uncheck_right, COLUMN_POPUP_ITEM_NOT_LAST},
		{ N_("Ignore all columns on left"), &cb_popup_menu_uncheck_left, COLUMN_POPUP_ITEM_NOT_FIRST},
		{ N_("Import all columns on right"), &cb_popup_menu_check_right, COLUMN_POPUP_ITEM_NOT_LAST},
		{ N_("Import all columns on left"), &cb_popup_menu_check_left, COLUMN_POPUP_ITEM_NOT_FIRST},
		{ N_("Copy format to right"), &cb_popup_menu_extend_format, COLUMN_POPUP_ITEM_NOT_LAST}
	};
	
	GtkWidget *menu = gtk_menu_new ();
	unsigned i;

	for (i = 0; i < G_N_ELEMENTS (actions); i++) {
		int flags = actions[i].flags;
		GtkWidget *item = gtk_menu_item_new_with_label
			(_(actions[i].text));
		switch (flags) {
		case COLUMN_POPUP_ITEM_IGNORE:
			gtk_widget_set_sensitive (item, FALSE);
			break;
		case COLUMN_POPUP_ITEM_NOT_FIRST:
			gtk_widget_set_sensitive (item, col > 0);
			break;
		case COLUMN_POPUP_ITEM_NOT_LAST:
			gtk_widget_set_sensitive 
				(item, col < pagedata->format.renderdata->colcount - 1);
			break;
		case COLUMN_POPUP_ITEM_ANY:
		default:
			break;
		}
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
		g_signal_connect (G_OBJECT (item),
				  "activate",
				  G_CALLBACK (actions[i].function),
				  pagedata);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event_button);
}


static gint
cb_col_event (GtkWidget *widget, GdkEvent *event, gpointer _col)
{
	if (event->type == GDK_BUTTON_PRESS) {
		GdkEventButton *event_button = (GdkEventButton *) event;
		StfDialogData *pagedata =
			g_object_get_data (G_OBJECT (widget), "pagedata");
		int col = GPOINTER_TO_INT (_col);

		activate_column (pagedata, col);

		if (event_button->button == 1) {
			GtkWidget *check = g_object_get_data (G_OBJECT (widget), "checkbox");
			/*
			 * We use overlapping buttons and that does not actually work...
			 *
			 * In a square area the height of the hbox, click the
			 * checkbox.
			 */
			int xmax = GTK_BIN (widget)->child->allocation.height;
			if (event_button->x <= xmax)
				gtk_button_clicked (GTK_BUTTON (check));
		} else if (event_button->button == 3) {
			format_context_menu (pagedata, event_button, col);
		}
		return TRUE;
	}

	return FALSE;
}

static gint
cb_treeview_button_press (GtkWidget *treeview,
			  GdkEventButton *event,
			  StfDialogData *pagedata)
{
	if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
		int dx, col;
		stf_preview_find_column (pagedata->format.renderdata, (int)event->x, &col, &dx);
		activate_column (pagedata, col);
		return TRUE;
	} else if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		int dx, col;
		stf_preview_find_column (pagedata->format.renderdata, (int)event->x, &col, &dx);
		activate_column (pagedata, col);
		format_context_menu (pagedata, event, col);
		return TRUE;
	}

	return FALSE;
}

static gint
cb_treeview_key_press (GtkWidget *treeview,
		       GdkEventKey *event,
		       StfDialogData *pagedata)
{
	if (event->type == GDK_KEY_PRESS) {
		switch (event->keyval) {
		case GDK_Left:
		case GDK_KP_Left:
			if (pagedata->format.index > 0)
				activate_column (pagedata,
						 pagedata->format.index - 1);
			return TRUE;

		case GDK_Right:
		case GDK_KP_Right:
			if (pagedata->format.index + 1 < (int)pagedata->format.formats->len)
				activate_column (pagedata,
						 pagedata->format.index + 1);
			return TRUE;

		case GDK_space:
		case GDK_Return: {
			GtkTreeViewColumn *column = stf_preview_get_column
				(pagedata->format.renderdata,
				 pagedata->format.index);
			GtkToggleButton *button =
				g_object_get_data (G_OBJECT (column),
						   "checkbox");
			gtk_toggle_button_set_active
				(button,
				 !gtk_toggle_button_get_active (button));
			return TRUE;
		}

		default:
			; /*  Nothing.  */
		}
	}

	return FALSE;
}

/**
 * format_page_update_preview
 * @pagedata : mother struct
 *
 * Will simply utilize the preview rendering functions to update
 * the preview
 *
 * returns : nothing
 **/
static void
format_page_update_preview (StfDialogData *pagedata)
{
	RenderData_t *renderdata = pagedata->format.renderdata;
	unsigned int ui;
	int i;
	int col_import_array_len_old, old_part;
	GStringChunk *lines_chunk;
	char *msg = NULL;

	stf_preview_colformats_clear (renderdata);
	for (ui = 0; ui < pagedata->format.formats->len; ui++) {
		GOFormat *sf = g_ptr_array_index (pagedata->format.formats, ui);
		stf_preview_colformats_add (renderdata, sf);
	}

	lines_chunk = g_string_chunk_new (100 * 1024);
	stf_preview_set_lines (renderdata, lines_chunk,
			       stf_parse_general (pagedata->parseoptions,
						  lines_chunk,
						  pagedata->cur,
						  pagedata->cur_end));

	col_import_array_len_old = pagedata->format.col_import_array_len;
	pagedata->format.col_import_array_len = renderdata->colcount;

	pagedata->format.col_import_array = 
		g_renew(gboolean, pagedata->format.col_import_array, 
			pagedata->format.col_import_array_len);
	old_part = (col_import_array_len_old < pagedata->format.col_import_array_len)
		? col_import_array_len_old
		: pagedata->format.col_import_array_len;
	pagedata->format.col_import_count = 0;
	for (i = 0; i < old_part; i++)
		if (pagedata->format.col_import_array[i])
			pagedata->format.col_import_count++;
	for (i = old_part; 
	     i < pagedata->format.col_import_array_len; i++) 
		if (pagedata->format.col_import_count < gnm_sheet_get_max_cols (NULL)) {
			pagedata->format.col_import_array[i] = TRUE;
			pagedata->format.col_import_count++;
		} else {
			pagedata->format.col_import_array[i] = FALSE;	
		}

	format_page_update_column_selection (pagedata);
	
	if (old_part < renderdata->colcount)
		msg = g_strdup_printf 
			(_("A maximum of %d columns can be imported."), 
			 gnm_sheet_get_max_cols (NULL));

	for (i = old_part; i < renderdata->colcount; i++) {
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, i);

		if (NULL == g_object_get_data (G_OBJECT (column), "checkbox")) {
			GtkWidget *box = gtk_hbox_new (FALSE,5);
			GtkWidget *vbox = gtk_vbox_new (FALSE,5);
			GtkWidget *check = gtk_check_button_new ();
			char * label_text = g_strdup_printf 
				(pagedata->format.col_header, i+1);
			GtkWidget *label = gtk_label_new (label_text);
			GOFormat const *gf = go_format_general ();
			GtkWidget *format_label = gtk_label_new 
				(go_format_sel_format_classification (gf));
			
			g_free (label_text);
			gtk_misc_set_alignment (GTK_MISC (format_label), 0, 0);
			gtk_misc_set_alignment (GTK_MISC (label), 0, 0);

			g_object_set (G_OBJECT (stf_preview_get_cell_renderer 
						(pagedata->format.renderdata, i)), 
				      "strikethrough", 
				      !pagedata->format.col_import_array[i], NULL);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(check),
						      pagedata->
						      format.col_import_array[i]);
			go_widget_set_tooltip_text
				(check,
				 _("If this checkbox is selected, the "
				   "column will be imported into "
				   "Gnumeric."));
			g_object_set_data (G_OBJECT (check), "pagedata", pagedata);
			gtk_box_pack_start (GTK_BOX(box), check, FALSE, FALSE, 0);
			gtk_box_pack_start (GTK_BOX(box), label, TRUE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX(vbox), box, FALSE, FALSE, 0);
			gtk_box_pack_start (GTK_BOX(vbox), format_label, TRUE, TRUE, 0);
			gtk_widget_show_all (vbox);
			
			gtk_tree_view_column_set_widget (column, vbox);
			g_object_set_data (G_OBJECT (column), "pagedata", pagedata);
			g_object_set_data (G_OBJECT (column), "checkbox", check);
			g_object_set_data (G_OBJECT (column), "formatlabel", format_label);
			g_object_set_data (G_OBJECT (column->button), 
					   "pagedata", pagedata);
			g_object_set_data (G_OBJECT (column->button), 
					   "checkbox", check);
			g_object_set_data (G_OBJECT (column->button), 
					   "formatlabel", format_label);
			g_object_set (G_OBJECT (column), "clickable", TRUE, NULL);

			g_signal_connect (G_OBJECT (check),
					  "toggled",
					  G_CALLBACK (cb_col_check_clicked),
					  GINT_TO_POINTER (i));
			g_signal_connect (G_OBJECT (column->button), 
					  "event",
					  G_CALLBACK (cb_col_event), 
					  GINT_TO_POINTER (i));
		}
	}
	g_free (msg);
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

static void
locale_changed_cb (GOLocaleSel *ls, char const *new_locale,
		   StfDialogData *pagedata)
{
	g_free (pagedata->locale);
	pagedata->locale = g_strdup (new_locale);

	go_format_sel_set_locale (pagedata->format.format_selector,
					   new_locale);
}


/**
 * cb_number_format_changed
 * @widget : NumberFormatSelector which emitted the signal
 * @fmt : current selected format
 * @data : Dialog "mother" record
 *
 * Updates the selected column on the sheet with the new
 * format the user choose/entered.
 *
 * returns : nothing
 **/
static void
cb_number_format_changed (G_GNUC_UNUSED GtkWidget *widget,
			  const char *fmt,
			  StfDialogData *data)
{
	if (data->format.index >= 0) {
		GOFormat *sf;
		GtkTreeViewColumn* column = 
			stf_preview_get_column (data->format.renderdata, 
						data->format.index);
		GtkWidget *w = g_object_get_data (G_OBJECT (column), 
						  "formatlabel");

		sf = g_ptr_array_index (data->format.formats, 
					data->format.index);
		go_format_unref (sf);

		sf = go_format_new_from_XL (fmt);
		gtk_label_set_text (GTK_LABEL (w), 
				    go_format_sel_format_classification (sf));
		g_ptr_array_index (data->format.formats, data->format.index) =
			sf;
	}

	format_page_update_preview (data);
}

/**
 * stf_dialog_format_page_prepare
 * @data : mother struct
 *
 * This will prepare the widgets on the format page before
 * the page gets displayed
 *
 * returns : nothing
 **/
void
stf_dialog_format_page_prepare (StfDialogData *data)
{
	GOFormat *sf;
	GOFormat *general = go_format_general ();

	/* Set the trim.  */
	format_page_trim_menu_changed (NULL, data);

	/* If necessary add new items (non-visual) */
	while ((int)data->format.formats->len < data->format.renderdata->colcount)
		g_ptr_array_add (data->format.formats, go_format_ref (general));

	data->format.manual_change = TRUE;
	activate_column (data, 0);

	sf = g_ptr_array_index (data->format.formats, 0);
	go_format_sel_set_style_format (data->format.format_selector,
						 sf);
}

/*************************************************************************************************
 * FORMAT EXPORTED FUNCTIONS
 *************************************************************************************************/

void
stf_dialog_format_page_cleanup (StfDialogData *pagedata)
{
	GPtrArray *formats = pagedata->format.formats;
	if (formats) {
		unsigned int ui;
		for (ui = 0; ui < formats->len; ui++)
			go_format_unref (g_ptr_array_index (formats, ui));
		g_ptr_array_free (formats, TRUE);
	}

	stf_preview_free (pagedata->format.renderdata);
	g_free (pagedata->format.col_import_array);
	pagedata->format.col_import_array = NULL;
	pagedata->format.col_import_array_len = 0;
	pagedata->format.col_import_count = 0;	
}

void
stf_dialog_format_page_init (GladeXML *gui, StfDialogData *pagedata)
{
	GtkWidget * format_hbox;

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

        /* Create/get object and fill information struct */
	pagedata->format.col_import_array = NULL;
	pagedata->format.col_import_array_len = 0;
	pagedata->format.col_import_count = 0;
	pagedata->format.col_header = _("Column %d");
	
	pagedata->format.format_selector      = GO_FORMAT_SEL( go_format_sel_new ());

	pagedata->format.format_data_container = glade_xml_get_widget (gui, "format_data_container");
	pagedata->format.format_trim   = glade_xml_get_widget (gui, "format_trim");
	pagedata->format.column_selection_label   = glade_xml_get_widget (gui, "column_selection_label");

	format_hbox = glade_xml_get_widget (gui, "format_hbox");
	gtk_box_pack_end_defaults (GTK_BOX (format_hbox), GTK_WIDGET (pagedata->format.format_selector));
	gtk_widget_show (GTK_WIDGET (pagedata->format.format_selector));

	pagedata->format.locale_selector =
		GO_LOCALE_SEL (go_locale_sel_new ());
	if (pagedata->locale && !go_locale_sel_set_locale (pagedata->format.locale_selector, pagedata->locale)) {
		g_free (pagedata->locale);
		pagedata->locale = go_locale_sel_get_locale (pagedata->format.locale_selector);
	}
	gtk_table_attach (
		GTK_TABLE (glade_xml_get_widget (gui, "locale_table")),
		GTK_WIDGET (pagedata->format.locale_selector),
		3, 4, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
	gtk_widget_show_all (GTK_WIDGET (pagedata->format.locale_selector));
	gtk_widget_set_sensitive
		(GTK_WIDGET (pagedata->format.locale_selector),
		 !pagedata->fixed_locale);

	/* Set properties */
	pagedata->format.renderdata =
		stf_preview_new (pagedata->format.format_data_container,
				 workbook_date_conv (wb_control_get_workbook (WORKBOOK_CONTROL (pagedata->wbcg))));
	pagedata->format.formats          = g_ptr_array_new ();
	pagedata->format.index         = -1;
	pagedata->format.manual_change = FALSE;

	/* Update widgets before connecting signals, see #333407.  */
	gtk_combo_box_set_active (GTK_COMBO_BOX (pagedata->format.format_trim),
				  0);
	format_page_update_column_selection (pagedata);

	/* Connect signals */
	pagedata->format.format_changed_handler_id = 
	     g_signal_connect (G_OBJECT (pagedata->format.format_selector),
		    "format_changed",
		    G_CALLBACK (cb_number_format_changed), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format.locale_selector),
			  "locale_changed",
			  G_CALLBACK (locale_changed_cb), pagedata);

        g_signal_connect (G_OBJECT (pagedata->format.format_trim),
			  "changed",
			  G_CALLBACK (format_page_trim_menu_changed), pagedata);
	g_signal_connect (G_OBJECT (pagedata->format.renderdata->tree_view),
			  "button_press_event",
			  G_CALLBACK (cb_treeview_button_press),
			  pagedata);
	g_signal_connect (G_OBJECT (pagedata->format.renderdata->tree_view),
			  "key_press_event",
			  G_CALLBACK (cb_treeview_key_press),
			  pagedata);
}

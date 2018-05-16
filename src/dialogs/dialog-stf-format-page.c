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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <libgnumeric.h>
#include <dialogs/dialog-stf.h>
#include <gnm-format.h>
#include <sheet.h>
#include <workbook.h>
#include <workbook-control.h>
#include <gui-util.h>
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
	GtkAdjustment *hadjustment = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (tree_view));
	double hval = gtk_adjustment_get_value (hadjustment);
	double hps = gtk_adjustment_get_page_size (hadjustment);
	GtkWidget *button = gtk_tree_view_column_get_button (column);
	GtkAllocation ba;

	gtk_widget_get_allocation (button, &ba);

	if (hval + hps < ba.x + ba.width)
		gtk_adjustment_set_value (hadjustment,
					  ba.x + ba.width - hps);
	else if (hval > ba.x)
		gtk_adjustment_set_value (hadjustment, ba.x);
}

static void
activate_column (StfDialogData *pagedata, int i)
{
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
}

static void
cb_col_check_clicked (GtkToggleButton *togglebutton, gpointer _i)
{
	int i = GPOINTER_TO_INT (_i);
	StfDialogData *pagedata =
		g_object_get_data (G_OBJECT (togglebutton), "pagedata");
	gboolean active = gtk_toggle_button_get_active (togglebutton);
	GtkCellRenderer *renderer;
	GtkTreeViewColumn* column;
	GtkWidget *check_autofit;

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
		if (pagedata->format.col_import_count < GNM_MAX_COLS) {
			pagedata->format.col_import_array[i] = TRUE;
			pagedata->format.col_import_count++;
			format_page_update_column_selection (pagedata);
		} else {
			char *msg = g_strdup_printf(
				ngettext("A maximum of %d column can be imported.",
					 "A maximum of %d columns can be imported.",
					 GNM_MAX_COLS),
				GNM_MAX_COLS);
			gtk_toggle_button_set_active (togglebutton, FALSE);
			go_gtk_notice_dialog (GTK_WINDOW (pagedata->dialog),
					      GTK_MESSAGE_WARNING,
					      "%s", msg);
			g_free (msg);
		}
	}

	column = stf_preview_get_column (pagedata->format.renderdata, i);
	check_autofit = g_object_get_data (G_OBJECT (column), "checkbox-autofit");

	gtk_widget_set_sensitive (check_autofit, active);
	return;
}

static void
cb_format_clicked (GtkButton *widget, gpointer _i)
{
	int i = GPOINTER_TO_INT (_i);
	StfDialogData *pagedata =
		g_object_get_data (G_OBJECT (widget), "pagedata");
	gint result;
	GtkWidget *dialog = gtk_dialog_new_with_buttons
		(_("Format Selector"),
		 GTK_WINDOW (pagedata->dialog),
		 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		 GNM_STOCK_OK,      GTK_RESPONSE_ACCEPT,
		 GNM_STOCK_CANCEL,  GTK_RESPONSE_REJECT,
		 NULL);
	GOFormatSel *format_selector
		= GO_FORMAT_SEL (go_format_sel_new_full (TRUE));
	GtkWidget *w = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

	go_format_sel_set_style_format (format_selector, g_ptr_array_index (pagedata->format.formats, i));
	go_format_sel_set_locale (format_selector, pagedata->locale);
	gtk_box_pack_start (GTK_BOX (w), GTK_WIDGET (format_selector),
			    FALSE, TRUE, 5);
	gtk_widget_show (GTK_WIDGET (format_selector));

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	switch (result) {
	case GTK_RESPONSE_ACCEPT: {
		GOFormat *sf;
		GtkTreeViewColumn* column = stf_preview_get_column (pagedata->format.renderdata, i);
		GtkWidget *format_label = g_object_get_data (G_OBJECT (column),
							     "formatlabel");

		sf = g_ptr_array_index (pagedata->format.formats, i);
		go_format_unref (sf);

		sf = go_format_ref (go_format_sel_get_fmt (format_selector));
		gtk_button_set_label (GTK_BUTTON (format_label),
				      go_format_sel_format_classification (sf));
		g_ptr_array_index (pagedata->format.formats, i) = sf;

		format_page_update_preview (pagedata);
		break;
	}
	default:
		break;
	}
	gtk_widget_destroy (dialog);
	return;
}

static void
cb_col_check_autofit_clicked (GtkToggleButton *togglebutton, gpointer _i)
{
	int i = GPOINTER_TO_INT (_i);
	StfDialogData *pagedata =
		g_object_get_data (G_OBJECT (togglebutton), "pagedata");
	gboolean active = gtk_toggle_button_get_active (togglebutton);

	g_return_if_fail (i < pagedata->format.col_import_array_len);

	pagedata->format.col_autofit_array[i] = active;

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
			if (pagedata->format.col_import_count >= GNM_MAX_COLS)
				break;
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
	GPtrArray *formats = pagedata->format.formats;
	GOFormat *colformat = g_ptr_array_index (formats, pagedata->format.index);

	for (index++; index < formats->len; index++) {
		GOFormat *sf = g_ptr_array_index (formats, index);
		GtkTreeViewColumn* column =
			stf_preview_get_column (pagedata->format.renderdata,
						index);
		GtkWidget *w = g_object_get_data (G_OBJECT (column),
						  "formatlabel");
		go_format_unref (sf);
		g_ptr_array_index (formats, index) = go_format_ref (colformat);
		gtk_button_set_label (GTK_BUTTON (w),
				      go_format_sel_format_classification (colformat));
	}

	format_page_update_preview (data);
}

static void
format_context_menu (StfDialogData *pagedata,
		     GdkEvent *event,
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

	gnumeric_popup_menu (GTK_MENU (menu), event);
}


static gint
cb_col_event (GtkWidget *widget, GdkEvent *event, gpointer _col)
{
	if (event->type == GDK_BUTTON_PRESS) {
		GdkEventButton *event_button = &event->button;
		StfDialogData *pagedata =
			g_object_get_data (G_OBJECT (widget), "pagedata");
		int col = GPOINTER_TO_INT (_col);

		activate_column (pagedata, col);

		if (event_button->button == 1) {
			GtkWidget *check = g_object_get_data (G_OBJECT (widget), "checkbox");
			GtkAllocation a;
			/*
			 * We use overlapping buttons and that does not
			 * actually work...
			 *
			 * In a square area the height of the hbox, click the
			 * checkbox.
			 */
			int xmax;

			gtk_widget_get_allocation
				(gtk_bin_get_child (GTK_BIN (widget)),
				 &a);
			xmax = a.height;
			if (event_button->x <= xmax)
				gtk_button_clicked (GTK_BUTTON (check));
		} else if (event_button->button == 3) {
			format_context_menu (pagedata, event, col);
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
		format_context_menu (pagedata, (GdkEvent*)event, col);
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
		case GDK_KEY_Left:
		case GDK_KEY_KP_Left:
			if (pagedata->format.index > 0)
				activate_column (pagedata,
						 pagedata->format.index - 1);
			return TRUE;

		case GDK_KEY_Right:
		case GDK_KEY_KP_Right:
			if (pagedata->format.index + 1 < (int)pagedata->format.formats->len)
				activate_column (pagedata,
						 pagedata->format.index + 1);
			return TRUE;

		case GDK_KEY_space:
		case GDK_KEY_Return: {
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
 * @pagedata: mother struct
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

	pagedata->format.col_autofit_array =
		g_renew(gboolean, pagedata->format.col_autofit_array,
			pagedata->format.col_import_array_len);
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
	     i < pagedata->format.col_import_array_len; i++) {
		if (pagedata->format.col_import_count < GNM_MAX_COLS) {
			pagedata->format.col_import_array[i] = TRUE;
			pagedata->format.col_import_count++;
		} else {
			pagedata->format.col_import_array[i] = FALSE;
		}
		pagedata->format.col_autofit_array[i] = TRUE;
	}

	format_page_update_column_selection (pagedata);

	if (old_part < renderdata->colcount)
		msg = g_strdup_printf
			(_("A maximum of %d columns can be imported."),
			 GNM_MAX_COLS);

	for (i = old_part; i < renderdata->colcount; i++) {
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, i);
		GtkWidget *button = gtk_tree_view_column_get_button (column);

		if (NULL == g_object_get_data (G_OBJECT (column), "checkbox")) {
			GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 5);
			GtkWidget *check,
				*check_autofit = gtk_check_button_new_with_label (_("Auto fit"));
			char * label_text = g_strdup_printf
				(pagedata->format.col_header, i+1);
			GOFormat const *fmt = i < (int)pagedata->parseoptions->formats->len
				? g_ptr_array_index (pagedata->parseoptions->formats, i)
				: go_format_general ();
			GtkWidget *format_label = gtk_button_new_with_label
				(go_format_sel_format_classification (fmt));
			GtkWidget *format_icon
				= gtk_image_new_from_stock (GTK_STOCK_INFO, GTK_ICON_SIZE_BUTTON);

			check = gtk_check_button_new_with_label (label_text);
			g_free (label_text);
			gtk_button_set_image (GTK_BUTTON (format_label), format_icon);

			g_object_set (G_OBJECT (stf_preview_get_cell_renderer
						(pagedata->format.renderdata, i)),
				      "strikethrough",
				      !pagedata->format.col_import_array[i], NULL);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(check),
						      pagedata->
						      format.col_import_array[i]);
			label_text = g_strdup_printf
				(_("If this checkbox is selected, "
				   "column %i will be imported into "
				   "Gnumeric."), i+1);
			gtk_widget_set_tooltip_text
				(check,
				 label_text);
			gtk_widget_set_tooltip_text
				(check_autofit,
				 _("If this checkbox is selected, "
				   "the width of the column will be adjusted "
				   "to the longest entry."));
			g_free (label_text);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(check_autofit),
						      pagedata->
						      format.col_autofit_array[i]);
			g_object_set_data (G_OBJECT (check), "pagedata", pagedata);
			g_object_set_data (G_OBJECT (check_autofit), "pagedata", pagedata);
			g_object_set_data (G_OBJECT (format_label), "pagedata", pagedata);
			gtk_box_pack_start (GTK_BOX(vbox), check, FALSE, FALSE, 0);
			gtk_box_pack_start (GTK_BOX(vbox), format_label, TRUE, TRUE, 0);
			gtk_box_pack_start (GTK_BOX(vbox), check_autofit, TRUE, TRUE, 0);
			gtk_widget_show_all (vbox);

			gtk_tree_view_column_set_widget (column, vbox);
			g_object_set_data (G_OBJECT (column), "pagedata", pagedata);
			g_object_set_data (G_OBJECT (column), "checkbox", check);
			g_object_set_data (G_OBJECT (column), "checkbox-autofit", check_autofit);
			g_object_set_data (G_OBJECT (column), "formatlabel", format_label);
			g_object_set_data (G_OBJECT (button),
					   "pagedata", pagedata);
			g_object_set_data (G_OBJECT (button),
					   "checkbox", check);
			g_object_set_data (G_OBJECT (button),
					   "formatlabel", format_label);
			g_object_set (G_OBJECT (column), "clickable", TRUE, NULL);

			g_signal_connect (G_OBJECT (check),
					  "toggled",
					  G_CALLBACK (cb_col_check_clicked),
					  GINT_TO_POINTER (i));
			g_signal_connect (G_OBJECT (check_autofit),
					  "toggled",
					  G_CALLBACK (cb_col_check_autofit_clicked),
					  GINT_TO_POINTER (i));
			g_signal_connect (G_OBJECT (format_label),
					  "clicked",
					  G_CALLBACK (cb_format_clicked),
					  GINT_TO_POINTER (i));
			g_signal_connect (G_OBJECT (button),
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
}

/**
 * stf_dialog_format_page_prepare
 * @pagedata: mother struct
 *
 * This will prepare the widgets on the format page before
 * the page gets displayed
 **/
void
stf_dialog_format_page_prepare (StfDialogData *data)
{
	GOFormat *general = go_format_general ();
	GPtrArray *formats = data->parseoptions->formats;

	/* Set the trim.  */
	format_page_trim_menu_changed (NULL, data);

	/* If necessary add new items (non-visual) */
	while ((int)data->format.formats->len < data->format.renderdata->colcount) {
		GOFormat const *fmt =
			data->format.formats->len < formats->len
			? g_ptr_array_index (formats, data->format.formats->len)
			: general;
		g_ptr_array_add (data->format.formats, go_format_ref (fmt));
	}

	data->format.manual_change = TRUE;
	activate_column (data, 0);
}

/*************************************************************************************************
 * FORMAT EXPORTED FUNCTIONS
 *************************************************************************************************/

void
stf_dialog_format_page_cleanup (StfDialogData *pagedata)
{
	GPtrArray *formats = pagedata->format.formats;
	if (formats)
		g_ptr_array_free (formats, TRUE);

	stf_preview_free (pagedata->format.renderdata);
	g_free (pagedata->format.col_import_array);
	g_free (pagedata->format.col_autofit_array);
	pagedata->format.col_import_array = NULL;
	pagedata->format.col_autofit_array = NULL;
	pagedata->format.col_import_array_len = 0;
	pagedata->format.col_import_count = 0;
}

void
stf_dialog_format_page_init (GtkBuilder *gui, StfDialogData *pagedata)
{
/* 	GtkWidget * format_hbox; */

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

        /* Create/get object and fill information struct */
	pagedata->format.col_import_array = NULL;
	pagedata->format.col_autofit_array = NULL;
	pagedata->format.col_import_array_len = 0;
	pagedata->format.col_import_count = 0;
	pagedata->format.col_header = _("Column %d");

	pagedata->format.format_data_container = go_gtk_builder_get_widget (gui, "format_data_container");
	pagedata->format.format_trim   = go_gtk_builder_get_widget (gui, "format_trim");
	pagedata->format.column_selection_label   = go_gtk_builder_get_widget (gui, "column_selection_label");

	pagedata->format.locale_selector =
		GO_LOCALE_SEL (go_locale_sel_new ());
	if (pagedata->locale && !go_locale_sel_set_locale (pagedata->format.locale_selector, pagedata->locale)) {
		g_free (pagedata->locale);
		pagedata->locale = go_locale_sel_get_locale (pagedata->format.locale_selector);
	}
	gtk_grid_attach (
		GTK_GRID (go_gtk_builder_get_widget (gui, "locale-grid")),
		GTK_WIDGET (pagedata->format.locale_selector),
		3, 0, 1, 1);
	gtk_widget_show_all (GTK_WIDGET (pagedata->format.locale_selector));
	gtk_widget_set_sensitive
		(GTK_WIDGET (pagedata->format.locale_selector),
		 !pagedata->fixed_locale);

	/* Set properties */
	pagedata->format.renderdata =
		stf_preview_new (pagedata->format.format_data_container,
				 workbook_date_conv (wb_control_get_workbook (GNM_WBC (pagedata->wbcg))));
	pagedata->format.formats = g_ptr_array_new_with_free_func ((GDestroyNotify)go_format_unref);
	pagedata->format.index = -1;
	pagedata->format.manual_change = FALSE;

	/* Update widgets before connecting signals, see #333407.  */
	gtk_combo_box_set_active (GTK_COMBO_BOX (pagedata->format.format_trim),
				  0);
	format_page_update_column_selection (pagedata);

	/* Connect signals */
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

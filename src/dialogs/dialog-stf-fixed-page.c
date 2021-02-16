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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialog-stf.h>
#include <gui-util.h>
#include <gdk/gdkkeysyms.h>

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
fixed_page_autodiscover (StfDialogData *pagedata)
{
	stf_parse_options_fixed_autodiscover (pagedata->parseoptions,
					      pagedata->cur, pagedata->cur_end);

	if (pagedata->parseoptions->splitpositions->len <= 1) {
		GtkWidget *dialog = gtk_message_dialog_new (NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			_("Autodiscovery did not find any columns in the text. Try manually"));
		go_gtk_dialog_run (GTK_DIALOG (dialog), GTK_WINDOW (pagedata->dialog));
	}
}

static void fixed_page_update_preview (StfDialogData *pagedata);

enum {
	CONTEXT_STF_IMPORT_MERGE_LEFT = 1,
	CONTEXT_STF_IMPORT_MERGE_RIGHT = 2,
	CONTEXT_STF_IMPORT_SPLIT = 3,
	CONTEXT_STF_IMPORT_WIDEN = 4,
	CONTEXT_STF_IMPORT_NARROW = 5
};

static GnmPopupMenuElement const popup_elements[] = {
	{ N_("Merge with column on _left"), GTK_STOCK_REMOVE,
	  0, 1 << CONTEXT_STF_IMPORT_MERGE_LEFT, CONTEXT_STF_IMPORT_MERGE_LEFT },
	{ N_("Merge with column on _right"), GTK_STOCK_REMOVE,
	  0, 1 << CONTEXT_STF_IMPORT_MERGE_RIGHT, CONTEXT_STF_IMPORT_MERGE_RIGHT },
	{ "", NULL, 0, 0, 0 },
	{ N_("_Split this column"), NULL,
	  0, 1 << CONTEXT_STF_IMPORT_SPLIT, CONTEXT_STF_IMPORT_SPLIT },
	{ "", NULL, 0, 0, 0 },
	{ N_("_Widen this column"), GTK_STOCK_GO_FORWARD,
	  0, 1 << CONTEXT_STF_IMPORT_WIDEN, CONTEXT_STF_IMPORT_WIDEN },
	{ N_("_Narrow this column"), GTK_STOCK_GO_BACK,
	  0, 1 << CONTEXT_STF_IMPORT_NARROW, CONTEXT_STF_IMPORT_NARROW },
	{ NULL, NULL, 0, 0, 0 },
};

static int
calc_char_index (RenderData_t *renderdata, int col, int *dx)
{
	GtkCellRenderer *cell =	stf_preview_get_cell_renderer (renderdata, col);
	PangoLayout *layout;
	PangoFontDescription *font_desc;
	int ci, width, padx;

	gtk_cell_renderer_get_padding (cell, &padx, NULL);

	g_object_get (G_OBJECT (cell), "font_desc", &font_desc, NULL);
	layout = gtk_widget_create_pango_layout (GTK_WIDGET (renderdata->tree_view), "x");
	pango_layout_set_font_description (layout, font_desc);
	pango_layout_get_pixel_size (layout, &width, NULL);
	g_object_unref (layout);
	pango_font_description_free (font_desc);

	if (width < 1) width = 1;
	ci = (*dx < padx) ? 0 : (*dx - padx + width / 2) / width;
	*dx -= ci * width;

	return ci;
}


static gboolean
make_new_column (StfDialogData *pagedata, int col, int dx, gboolean test_only)
{
	int charindex;
	RenderData_t *renderdata = pagedata->fixed.renderdata;
	int colstart, colend;

	colstart = (col == 0)
		? 0
		: stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col - 1);
	colend = stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col);

	charindex = colstart + calc_char_index (renderdata, col, &dx);

	if (charindex <= colstart || (colend != -1 && charindex >= colend))
		return FALSE;

	if (!test_only) {
		stf_parse_options_fixed_splitpositions_add (pagedata->parseoptions, charindex);
		fixed_page_update_preview (pagedata);
	}

	return TRUE;
}


static gboolean
widen_column (StfDialogData *pagedata, int col, gboolean test_only)
{
	int colcount = stf_parse_options_fixed_splitpositions_count (pagedata->parseoptions);
	int nextstart, nextnextstart;

	if (col >= colcount - 1)
		return FALSE;

	nextstart = stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col);

	nextnextstart = (col == colcount - 2)
		? pagedata->longest_line
		: stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col + 1);

	if (nextstart + 1 >= nextnextstart)
		return FALSE;

	if (!test_only) {
		stf_parse_options_fixed_splitpositions_remove (pagedata->parseoptions, nextstart);
		stf_parse_options_fixed_splitpositions_add (pagedata->parseoptions, nextstart + 1);
		fixed_page_update_preview (pagedata);
	}
	return TRUE;
}

static gboolean
narrow_column (StfDialogData *pagedata, int col, gboolean test_only)
{
	int colcount = stf_parse_options_fixed_splitpositions_count (pagedata->parseoptions);
	int thisstart, nextstart;

	if (col >= colcount - 1)
		return FALSE;

	thisstart = (col == 0)
		? 0
		: stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col - 1);
	nextstart = stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col);

	if (nextstart - 1 <= thisstart)
		return FALSE;

	if (!test_only) {
		stf_parse_options_fixed_splitpositions_remove (pagedata->parseoptions, nextstart);
		stf_parse_options_fixed_splitpositions_add (pagedata->parseoptions, nextstart - 1);
		fixed_page_update_preview (pagedata);
	}
	return TRUE;
}

static gboolean
delete_column (StfDialogData *pagedata, int col, gboolean test_only)
{
	int colcount = stf_parse_options_fixed_splitpositions_count (pagedata->parseoptions);
	if (col < 0 || col >= colcount - 1)
		return FALSE;

	if (!test_only) {
		int nextstart = stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col);
		stf_parse_options_fixed_splitpositions_remove (pagedata->parseoptions, nextstart);
		fixed_page_update_preview (pagedata);
	}
	return TRUE;
}

static void
select_column (StfDialogData *pagedata, int col)
{
	int colcount = stf_parse_options_fixed_splitpositions_count (pagedata->parseoptions);
	GtkTreeViewColumn *column;

	if (col < 0 || col >= colcount)
		return;

	column = stf_preview_get_column (pagedata->fixed.renderdata, col);
	gtk_widget_grab_focus (gtk_tree_view_column_get_button (column));
}

static void
fixed_context_menu_handler (GnmPopupMenuElement const *element,
			    gpointer user_data)
{
	StfDialogData *pagedata = user_data;
	int col = pagedata->fixed.context_col;

	switch (element->index) {
	case CONTEXT_STF_IMPORT_MERGE_LEFT:
		delete_column (pagedata, col - 1, FALSE);
		break;
	case CONTEXT_STF_IMPORT_MERGE_RIGHT:
		delete_column (pagedata, col, FALSE);
		break;
	case CONTEXT_STF_IMPORT_SPLIT:
		make_new_column (pagedata, col, pagedata->fixed.context_dx, FALSE);
		break;
	case CONTEXT_STF_IMPORT_WIDEN:
		widen_column (pagedata, col, FALSE);
		break;
	case CONTEXT_STF_IMPORT_NARROW:
		narrow_column (pagedata, col, FALSE);
		break;
	default:
		; /* Nothing */
	}
}

static void
fixed_context_menu (StfDialogData *pagedata, GdkEventButton *event,
		    int col, int dx)
{
	int sensitivity_filter = 0;

	pagedata->fixed.context_col = col;
	pagedata->fixed.context_dx = dx;

	if (!delete_column (pagedata, col - 1, TRUE))
		sensitivity_filter |= (1 << CONTEXT_STF_IMPORT_MERGE_LEFT);
	if (!delete_column (pagedata, col, TRUE))
		sensitivity_filter |= (1 << CONTEXT_STF_IMPORT_MERGE_RIGHT);
	if (!make_new_column (pagedata, col, dx, TRUE))
		sensitivity_filter |= (1 << CONTEXT_STF_IMPORT_SPLIT);
	if (!widen_column (pagedata, col, TRUE))
		sensitivity_filter |= (1 << CONTEXT_STF_IMPORT_WIDEN);
	if (!narrow_column (pagedata, col, TRUE))
		sensitivity_filter |= (1 << CONTEXT_STF_IMPORT_NARROW);

	select_column (pagedata, col);
	gnm_create_popup_menu (popup_elements,
			       &fixed_context_menu_handler, pagedata, NULL,
			       0, sensitivity_filter,
			       (GdkEvent*)event);
}

static gint
cb_treeview_button_press (GtkWidget *treeview,
			  GdkEventButton *event,
			  StfDialogData *pagedata)
{
	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		int dx, col;
		stf_preview_find_column (pagedata->fixed.renderdata, (int)event->x, &col, &dx);
		make_new_column (pagedata, col, dx, FALSE);
		return TRUE;
	}

	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		int dx, col;
		stf_preview_find_column (pagedata->fixed.renderdata, (int)event->x, &col, &dx);
		fixed_context_menu (pagedata, event, col, dx);
		return TRUE;
	}

	return FALSE;
}


static gint
cb_col_button_press (GtkWidget *button,
		     GdkEventButton *event,
		     gpointer   _col)
{
	int col = GPOINTER_TO_INT (_col);
	StfDialogData *data = g_object_get_data (G_OBJECT (button), "fixed-data");

	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		GtkAllocation bca, ba;
		int offset;
		/* Split column.  */

		/* Correct for indentation of button.  */
		gtk_widget_get_allocation (gtk_bin_get_child (GTK_BIN (button)),
					   &bca);
		gtk_widget_get_allocation (button, &ba);
		offset = bca.x - ba.x;
		make_new_column (data, col, (int)event->x - offset, FALSE);

		return TRUE;
	}

	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		GtkAllocation bca, ba;
		int offset;

		/* Correct for indentation of button.  */
		gtk_widget_get_allocation (gtk_bin_get_child (GTK_BIN (button)),
					   &bca);
		gtk_widget_get_allocation (button, &ba);
		offset = bca.x - ba.x;

		fixed_context_menu (data, event, col, (int)event->x - offset);
		return TRUE;
	}

	return FALSE;
}

static gint
cb_col_key_press (GtkWidget *button,
		  GdkEventKey *event,
		  gpointer   _col)
{
	int col = GPOINTER_TO_INT (_col);
	StfDialogData *data = g_object_get_data (G_OBJECT (button), "fixed-data");

	if (event->type == GDK_KEY_PRESS) {
		switch (event->keyval) {
		case GDK_KEY_plus:
		case GDK_KEY_KP_Add:
		case GDK_KEY_greater:
			widen_column (data, col, FALSE);
			return TRUE;

		case GDK_KEY_minus:
		case GDK_KEY_KP_Subtract:
		case GDK_KEY_less:
			narrow_column (data, col, FALSE);
			return TRUE;

		case GDK_KEY_Left:
		case GDK_KEY_Up:
			select_column (data, col - 1);
			return TRUE;

		case GDK_KEY_Right:
		case GDK_KEY_Down:
			select_column (data, col + 1);
			return TRUE;

		default:
			; /*  Nothing.  */
		}
	}

	return FALSE;
}

/**
 * fixed_page_update_preview
 * @pagedata: mother struct
 *
 * Will simply update the preview
 **/
static void
fixed_page_update_preview (StfDialogData *pagedata)
{
	StfParseOptions_t *parseoptions = pagedata->parseoptions;
	RenderData_t *renderdata = pagedata->fixed.renderdata;
	int i;
	GStringChunk *lines_chunk;
	GPtrArray *lines;
	StfTrimType_t trim;

	lines_chunk = g_string_chunk_new (100 * 1024);

	/* Don't trim on this page.  */
	trim = parseoptions->trim_spaces;
	stf_parse_options_set_trim_spaces (parseoptions, TRIM_TYPE_NEVER);
	lines = stf_parse_general (parseoptions, lines_chunk,
				   pagedata->cur, pagedata->cur_end);
	stf_parse_options_set_trim_spaces (parseoptions, trim);

	stf_preview_set_lines (renderdata, lines_chunk, lines);

	for (i = 0; i < renderdata->colcount; i++) {
		GtkTreeViewColumn *column =
			stf_preview_get_column (renderdata, i);
		GtkCellRenderer *cell =
			stf_preview_get_cell_renderer (renderdata, i);
		GtkWidget *button =
			gtk_tree_view_column_get_button (column);

		gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

		g_object_set (G_OBJECT (cell),
			      "family", "monospace",
			      NULL);

		g_object_set_data (G_OBJECT (button), "fixed-data", pagedata);
		g_object_set (G_OBJECT (column), "clickable", TRUE, NULL);
		g_signal_connect (button, "button_press_event",
				  G_CALLBACK (cb_col_button_press),
				  GINT_TO_POINTER (i));
		g_signal_connect (button, "key_press_event",
				  G_CALLBACK (cb_col_key_press),
				  GINT_TO_POINTER (i));
	}
}

/*************************************************************************************************
 * SIGNAL HANDLERS
 *************************************************************************************************/

/**
 * fixed_page_clear_clicked:
 * @button: GtkButton
 * @data: mother struct
 *
 * Will clear all entries in fixed_collist
 **/
static void
fixed_page_clear_clicked (G_GNUC_UNUSED GtkButton *button,
			  StfDialogData *data)
{
	stf_parse_options_fixed_splitpositions_clear (data->parseoptions);
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
			 StfDialogData *data)
{
	fixed_page_autodiscover (data);

	fixed_page_update_preview (data);
}

/**
 * stf_dialog_fixed_page_prepare
 * @pagedata: mother struct
 *
 * Will prepare the fixed page
 **/
void
stf_dialog_fixed_page_prepare (StfDialogData *pagedata)
{
	stf_parse_options_set_trim_spaces (pagedata->parseoptions, TRIM_TYPE_NEVER);
	fixed_page_update_preview (pagedata);
}

static void
queue_redraw (GtkWidget *widget, int x)
{
	int hh, xo;
	GtkAllocation a;

	if (x < 0)
		return;

	gtk_tree_view_convert_bin_window_to_widget_coords
		(GTK_TREE_VIEW (widget), 0, 0, &xo, &hh);

	gtk_widget_get_allocation (widget, &a);
	gtk_widget_queue_draw_area (widget,
				    x + xo, hh,
				    1, a.height - hh);
}


static gboolean
cb_treeview_motion (GtkWidget *widget,
		    GdkEventMotion *event,
		    StfDialogData *pagedata)
{
	int x = (int)event->x;
	int col, dx;
	RenderData_t *renderdata = pagedata->fixed.renderdata;
	int old_ruler_x = pagedata->fixed.ruler_x;
	int colstart, colend, colwidth;
	gpointer user;

	pagedata->fixed.ruler_x = -1;

	/* We get events from the buttons too.  Translate x.  */
	gdk_window_get_user_data (event->window, &user);
	if (GTK_IS_BUTTON (user)) {
		int ewx;
		gdk_window_get_position (event->window, &ewx, NULL);
		x += ewx;
	}

	stf_preview_find_column (renderdata, x, &col, &dx);

	colstart = (col == 0)
		? 0
		: stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col - 1);
	colend = stf_parse_options_fixed_splitpositions_nth (pagedata->parseoptions, col);
	colwidth = (colend == -1) ? G_MAXINT : colend - colstart;

	if (col >= 0 && col < renderdata->colcount) {
		int ci = calc_char_index (renderdata, col, &dx);
		if (ci <= colwidth) {
			int padx;
			GtkCellRenderer *cell =
				stf_preview_get_cell_renderer (renderdata, col);

			gtk_cell_renderer_get_padding (cell, &padx, NULL);
			pagedata->fixed.ruler_x = x - dx + padx;
		}
	}

	gdk_event_request_motions (event);

	if (pagedata->fixed.ruler_x == old_ruler_x)
		return FALSE;

	queue_redraw (widget, old_ruler_x);
	queue_redraw (widget, pagedata->fixed.ruler_x);

	return FALSE;
}


static gboolean
cb_treeview_draw (GtkWidget *widget,
		  cairo_t *cr,
		  StfDialogData *pagedata)
{
	int ruler_x = pagedata->fixed.ruler_x;
	int height;
	GtkAllocation a;
	GdkWindow *bin_window;
	GdkRGBA ruler_color;
	GtkStyleContext *context;

	if (ruler_x < 0)
		return FALSE;

	bin_window = gtk_tree_view_get_bin_window (GTK_TREE_VIEW (widget));
	if (!gtk_cairo_should_draw_window (cr, bin_window))
		return FALSE;

	gtk_widget_get_allocation (widget, &a);
	height = a.height;

	context = gtk_widget_get_style_context (GTK_WIDGET (pagedata->dialog));
	gtk_style_context_save (context);
	gtk_style_context_add_class (context, "fixed-format-ruler");
	gnm_style_context_get_color (context, GTK_STATE_FLAG_NORMAL,
				     &ruler_color);
	gtk_style_context_restore (context);

	cairo_save (cr);
	cairo_rectangle (cr, ruler_x, 0, ruler_x + 1, height);
	cairo_clip (cr);
	gdk_cairo_set_source_rgba (cr, &ruler_color);
	cairo_move_to (cr, ruler_x, 0);
	cairo_line_to (cr, ruler_x, height);
	cairo_stroke (cr);
	cairo_restore (cr);

	return FALSE;
}

/*************************************************************************************************
 * FIXED EXPORTED FUNCTIONS
 *************************************************************************************************/

/**
 * stf_dialog_fixed_page_cleanup
 * @pagedata: mother struct
 *
 * Will cleanup fixed page run-time data
 **/
void
stf_dialog_fixed_page_cleanup (StfDialogData *pagedata)
{
	stf_preview_free (pagedata->fixed.renderdata);
}

void
stf_dialog_fixed_page_init (GtkBuilder *gui, StfDialogData *pagedata)
{
	RenderData_t *renderdata;

	g_return_if_fail (gui != NULL);
	g_return_if_fail (pagedata != NULL);

        /* Create/get object and fill information struct */
	pagedata->fixed.fixed_clear   = GTK_BUTTON      (go_gtk_builder_get_widget (gui, "fixed_clear"));
	pagedata->fixed.fixed_auto    = GTK_BUTTON      (go_gtk_builder_get_widget (gui, "fixed_auto"));
	pagedata->fixed.fixed_data_container =          (go_gtk_builder_get_widget (gui, "fixed_data_container"));

	/* Set properties */
	renderdata = pagedata->fixed.renderdata =
		stf_preview_new (pagedata->fixed.fixed_data_container,
				 NULL);
	pagedata->fixed.ruler_x = -1;

	/* Connect signals */
	g_signal_connect (G_OBJECT (pagedata->fixed.fixed_clear),
		"clicked",
		G_CALLBACK (fixed_page_clear_clicked), pagedata);
	g_signal_connect (G_OBJECT (pagedata->fixed.fixed_auto),
		"clicked",
		G_CALLBACK (fixed_page_auto_clicked), pagedata);
	g_signal_connect (G_OBJECT (renderdata->tree_view),
		"button_press_event",
		 G_CALLBACK (cb_treeview_button_press), pagedata);
	g_signal_connect (G_OBJECT (renderdata->tree_view),
		"motion_notify_event",
		 G_CALLBACK (cb_treeview_motion), pagedata);

	g_signal_connect_after (G_OBJECT (renderdata->tree_view),
		"draw",
		 G_CALLBACK (cb_treeview_draw), pagedata);
}

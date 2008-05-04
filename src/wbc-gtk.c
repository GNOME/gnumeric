/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * wbc-gtk.c: A gtk based WorkbookControl
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2006 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301
 * USA
 *
 * Port to Maemo:
 *	Eduardo Lima  (eduardo.lima@indt.org.br)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "wbc-gtk-impl.h"
#include "workbook-view.h"
#include "workbook-priv.h"
#include "gui-util.h"
#include "gui-file.h"
#include "sheet-control-gui-priv.h"
#include "sheet.h"
#include "sheet-private.h"
#include "sheet-view.h"
#include "sheet-style.h"
#include "sheet-filter.h"
#include "commands.h"
#include "dependent.h"
#include "application.h"
#include "history.h"
#include "func.h"
#include "value.h"
#include "expr.h"
#include "expr-impl.h"
#include "style-color.h"
#include "style-border.h"
#include "gnumeric-gconf.h"
#include "dialogs/dialogs.h"
#include "widgets/widget-editable-label.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"
#include "gui-clipboard.h"
#include "libgnumeric.h"
#include "gnm-pane-impl.h"
#include "graph.h"
#include "selection.h"

#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-data-set.h>
#include <goffice/graph/gog-series.h>
#include <goffice/data/go-data.h>
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context-impl.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#include "gdk/gdkkeysyms.h"
#include <glib/gi18n-lib.h>
#include <errno.h>
#include <string.h>

#ifdef GNM_USE_HILDON
#include <hildon-widgets/hildon-window.h>
#include <hildon-widgets/hildon-program.h>
#endif

#define	SHEET_CONTROL_KEY	"SheetControl"

enum {
	WBG_GTK_PROP_0,
	WBG_GTK_PROP_AUTOSAVE_PROMPT,
	WBG_GTK_PROP_AUTOSAVE_TIME
};

enum {
	WBC_GTK_MARKUP_CHANGED,
	WBC_GTK_LAST_SIGNAL
};

enum {
	TARGET_URI_LIST,
	TARGET_SHEET
};

#ifndef HILDON
char const *uifilename = NULL;
#endif
static guint wbc_gtk_signals[WBC_GTK_LAST_SIGNAL];
static GObjectClass *parent_class = NULL;

gint wbc_gtk_debug_deps = 0;
gint wbc_gtk_debug_expr_share = 0;

/****************************************************************************/

static void
wbc_gtk_set_action_sensitivity (WBCGtk const *wbcg,
				char const *action, gboolean sensitive)
{
	GtkAction *a = gtk_action_group_get_action (wbcg->actions, action);
	if (a == NULL)
		a = gtk_action_group_get_action (wbcg->permanent_actions, action);
	g_object_set (G_OBJECT (a), "sensitive", sensitive, NULL);
}

/* NOTE : The semantics of prefix and suffix seem contrived.  Why are we
 * handling it at this end ?  That stuff should be done in the undo/redo code
 **/
static void
wbc_gtk_set_action_label (WBCGtk const *wbcg,
			  char const *action,
			  char const *prefix,
			  char const *suffix,
			  char const *new_tip)
{
	GtkAction *a = gtk_action_group_get_action (wbcg->actions, action);

	if (prefix != NULL) {
		char *text;
		gboolean is_suffix = (suffix != NULL);

#ifdef GNM_USE_HILDON
		wbc_gtk_hildon_set_action_sensitive (wbcg, action, is_suffix);
#endif

		text = is_suffix ? g_strdup_printf ("%s: %s", prefix, suffix) : (char *) prefix;
			g_object_set (G_OBJECT (a),
				      "label",	   text,
				      "sensitive", is_suffix,
				      NULL);
		if (is_suffix)
			g_free (text);
	} else
		g_object_set (G_OBJECT (a), "label", suffix, NULL);

	if (new_tip != NULL)
		g_object_set (G_OBJECT (a), "tooltip", new_tip, NULL);
}

static void
wbc_gtk_set_toggle_action_state (WBCGtk const *wbcg,
				 char const *action, gboolean state)
{
	GtkAction *a = gtk_action_group_get_action (wbcg->actions, action);
	if (a == NULL)
		a = gtk_action_group_get_action (wbcg->font_actions, action);
	if (a == NULL)
		a = gtk_action_group_get_action (wbcg->toolbar.actions, action);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (a), state);
}

/****************************************************************************/

static SheetControlGUI *
wbcg_get_scg (WBCGtk *wbcg, Sheet *sheet)
{
	GtkWidget *w;
	int i, npages;

	if (sheet == NULL || wbcg->notebook == NULL)
		return NULL;

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (sheet->index_in_wb >= 0, NULL);

	w = gtk_notebook_get_nth_page (wbcg->notebook, sheet->index_in_wb);
	if (w) {
		SheetControlGUI *scg = g_object_get_data (G_OBJECT (w), SHEET_CONTROL_KEY);
		if (scg_sheet (scg) == sheet)
			return scg;
	}

	/*
	 * index_in_wb is probably not accurate because we are in the
	 * middle of removing or adding a sheet.
	 */
	npages = gtk_notebook_get_n_pages (wbcg->notebook);
	for (i = 0; i < npages; i++) {
		GtkWidget *w = gtk_notebook_get_nth_page (wbcg->notebook, i);
		SheetControlGUI *scg = g_object_get_data (G_OBJECT (w), SHEET_CONTROL_KEY);
		if (scg_sheet (scg) == sheet)
			return scg;
	}

	g_warning ("Failed to find scg for sheet %s", sheet->name_quoted);
	return NULL;
}

/* Autosave */

static gboolean
cb_autosave (WBCGtk *wbcg)
{
	WorkbookView *wb_view;

	g_return_val_if_fail (IS_WBC_GTK (wbcg), FALSE);

	wb_view = wb_control_view (WORKBOOK_CONTROL (wbcg));

	if (wb_view == NULL)
		return FALSE;

	if (wbcg->autosave_time > 0 &&
	    go_doc_is_dirty (wb_view_get_doc (wb_view))) {
		if (wbcg->autosave_prompt && !dialog_autosave_prompt (wbcg))
			return TRUE;
		gui_file_save (wbcg, wb_view);
	}
	return TRUE;
}

/**
 * wbcg_rangesel_possible
 * @wbcg : the workbook control gui
 *
 * Returns true if the cursor keys should be used to select
 * a cell range (if the cursor is in a spot in the expression
 * where it makes sense to have a cell reference), false if not.
 **/
gboolean
wbcg_rangesel_possible (WBCGtk const *wbcg)
{
	g_return_val_if_fail (IS_WBC_GTK (wbcg), FALSE);

	/* Already range selecting */
	if (wbcg->rangesel != NULL)
		return TRUE;

	/* Rangesel requires that we be editing somthing */
	if (!wbcg_is_editing (wbcg) && !wbcg_entry_has_logical (wbcg))
		return FALSE;

	return gnm_expr_entry_can_rangesel (wbcg_get_entry_logical (wbcg));
}

gboolean
wbcg_is_editing (WBCGtk const *wbcg)
{
	g_return_val_if_fail (IS_WBC_GTK (wbcg), FALSE);
	return wbcg->editing;
}

static void
wbcg_autosave_cancel (WBCGtk *wbcg)
{
	if (wbcg->autosave_timer != 0) {
		g_source_remove (wbcg->autosave_timer);
		wbcg->autosave_timer = 0;
	}
}

static void
wbcg_autosave_activate (WBCGtk *wbcg)
{
	wbcg_autosave_cancel (wbcg);

	if (wbcg->autosave_time > 0) {
		int secs = MIN (wbcg->autosave_time, G_MAXINT / 1000);
		wbcg->autosave_timer =
			g_timeout_add (secs * 1000,
				       (GSourceFunc) cb_autosave,
				       wbcg);
	}
}

static void
wbcg_set_autosave_time (WBCGtk *wbcg, int secs)
{
	if (secs == wbcg->autosave_time)
		return;

	wbcg->autosave_time = secs;
	wbcg_autosave_activate (wbcg);
}

/****************************************************************************/

static void
wbcg_edit_line_set (WorkbookControl *wbc, char const *text)
{
	GtkEntry *entry = wbcg_get_entry ((WBCGtk*)wbc);
	gtk_entry_set_text (entry, text);
}

static void
wbcg_edit_selection_descr_set (WorkbookControl *wbc, char const *text)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	gtk_entry_set_text (GTK_ENTRY (wbcg->selection_descriptor), text);
}

static void
wbcg_update_action_sensitivity (WorkbookControl *wbc)
{
	WBCGtk *wbcg = WBC_GTK (wbc);
	SheetControlGUI	   *scg = wbcg_cur_scg (wbcg);
	gboolean edit_object = scg != NULL &&
		(scg->selected_objects != NULL || scg->new_object != NULL);
	gboolean enable_actions = TRUE;
	gboolean enable_edit_ok_cancel = FALSE;

	if (edit_object || wbcg->edit_line.guru != NULL)
		enable_actions = FALSE;
	else if (wbcg_is_editing (wbcg)) {
		enable_actions = FALSE;
		enable_edit_ok_cancel = TRUE;
	}

	/* These are only sensitive while editing */
	gtk_widget_set_sensitive (wbcg->ok_button, enable_edit_ok_cancel);
	gtk_widget_set_sensitive (wbcg->cancel_button, enable_edit_ok_cancel);
	gtk_widget_set_sensitive (wbcg->func_button, enable_actions);

	if (wbcg->notebook) {
		int i;
		for (i = 0; i < gtk_notebook_get_n_pages (wbcg->notebook); i++) {
			GtkWidget *page = gtk_notebook_get_nth_page (wbcg->notebook, i);
			GtkWidget *label = gtk_notebook_get_tab_label (wbcg->notebook, page);
			editable_label_set_editable (EDITABLE_LABEL (label), enable_actions);
		}
	}

	g_object_set (G_OBJECT (wbcg->actions),
		"sensitive", enable_actions,
		NULL);
	g_object_set (G_OBJECT (wbcg->font_actions),
		"sensitive", enable_actions || enable_edit_ok_cancel,
		NULL);
}

static gboolean
cb_sheet_label_edit_finished (EditableLabel *el, char const *new_name,
			      WBCGtk *wbcg)
{
	gboolean reject = FALSE;
	if (new_name != NULL) {
		char const *old_name = editable_label_get_text (el);
		Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
		Sheet *sheet = workbook_sheet_by_name (wb, old_name);
		reject = cmd_rename_sheet (WORKBOOK_CONTROL (wbcg),
					   sheet,
					   new_name);
	}
	wbcg_focus_cur_scg (wbcg);
	return reject;
}

void
wbcg_insert_sheet (GtkWidget *unused, WBCGtk *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Workbook *wb = sheet->workbook;
	WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
	workbook_sheet_add (wb, sheet->index_in_wb);
	cmd_reorganize_sheets (wbc, old_state, sheet);
}

void
wbcg_append_sheet (GtkWidget *unused, WBCGtk *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Workbook *wb = sheet->workbook;
	WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
	workbook_sheet_add (wb, -1);
	cmd_reorganize_sheets (wbc, old_state, sheet);
}

void
wbcg_clone_sheet (GtkWidget *unused, WBCGtk *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Workbook *wb = sheet->workbook;
	WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
	Sheet *new_sheet = sheet_dup (sheet);
	workbook_sheet_attach_at_pos (wb, new_sheet, sheet->index_in_wb + 1);
	/* See workbook_sheet_add:  */
	g_signal_emit_by_name (G_OBJECT (wb), "sheet_added", 0);
	cmd_reorganize_sheets (wbc, old_state, sheet);
	g_object_unref (new_sheet);
}


static void cb_sheets_manage (SheetControlGUI *scg) { dialog_sheet_order (scg->wbcg); }
static void cb_sheets_insert (SheetControlGUI *scg) { wbcg_insert_sheet (NULL, scg->wbcg); }
static void cb_sheets_add    (SheetControlGUI *scg) { wbcg_append_sheet (NULL, scg->wbcg); }
static void cb_sheets_clone  (SheetControlGUI *scg) { wbcg_clone_sheet  (NULL, scg->wbcg); }
static void cb_sheets_rename (SheetControlGUI *scg) { editable_label_start_editing (EDITABLE_LABEL(scg->label)); }
static void
sheet_menu_label_run (SheetControlGUI *scg, GdkEventButton *event)
{
	SheetControl *sc = (SheetControl *) scg;

	struct {
		char const *text;
		void (*function) (SheetControlGUI *scg);
		gboolean req_multiple_sheets;
	} const sheet_label_context_actions [] = {
		{ N_("Manage sheets..."), &cb_sheets_manage,	FALSE},
		{ NULL, NULL, 0},
		{ N_("Insert"),		  &cb_sheets_insert,	FALSE },
		{ N_("Append"),		  &cb_sheets_add,	FALSE },
		{ N_("Duplicate"),	  &cb_sheets_clone,	FALSE },
		{ N_("Remove"),		  &scg_delete_sheet_if_possible, TRUE },
		{ N_("Rename"),		  &cb_sheets_rename,	FALSE }
	};

	unsigned int i;
	GtkWidget *item, *menu = gtk_menu_new ();

	for (i = 0; i < G_N_ELEMENTS (sheet_label_context_actions); i++){
		char const *text = sheet_label_context_actions[i].text;
		gboolean req_multiple_sheets = sheet_label_context_actions [i].req_multiple_sheets;
		gboolean inactive =
			(req_multiple_sheets && workbook_sheet_count (sc->sheet->workbook) < 2) ||
			wbc_gtk_get_guru (scg_wbcg (scg)) != NULL;

		if (text != NULL) {
			item = gtk_menu_item_new_with_label (_(text));
			g_signal_connect_swapped (G_OBJECT (item), "activate",
				G_CALLBACK (sheet_label_context_actions [i].function), scg);
		} else
			item = gtk_separator_menu_item_new ();

		gtk_widget_set_sensitive (item, !inactive);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
}

/**
 * cb_sheet_label_button_press:
 *
 * Invoked when the user has clicked on the EditableLabel widget.
 * This takes care of switching to the notebook that contains the label
 */
static gboolean
cb_sheet_label_button_press (GtkWidget *widget, GdkEventButton *event,
			     SheetControlGUI *scg)
{
	GtkNotebook *notebook;
	gint page_number;
	GtkWidget *table = GTK_WIDGET (scg->table);
	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	notebook = GTK_NOTEBOOK (table->parent);
	page_number = gtk_notebook_page_num (notebook, table);

	gtk_notebook_set_current_page (notebook, page_number);

	if (event->button == 1 || NULL != scg->wbcg->rangesel)
		return TRUE;

	if (event->button == 3 &&
	    editable_label_get_editable (EDITABLE_LABEL (widget))) {
		sheet_menu_label_run (scg, event);
		scg_take_focus (scg);
		return TRUE;
	}

	return FALSE;
}

static gint
gnm_notebook_page_num_by_label (GtkNotebook *notebook, GtkWidget *label)
{
        guint i;
        GtkWidget *page, *l;

        g_return_val_if_fail (GTK_IS_NOTEBOOK (notebook), -1);
        g_return_val_if_fail (GTK_IS_WIDGET (label), -1);

        for (i = g_list_length (notebook->children); i-- > 0 ; ) {
                page = gtk_notebook_get_nth_page (notebook, i);
                l = gtk_notebook_get_tab_label (notebook, page);
                if (label == l)
                        return i;
        }

        return -1;
}

static void
cb_sheet_label_drag_data_get (GtkWidget *widget, GdkDragContext *context,
			      GtkSelectionData *selection_data,
			      guint info, guint time,
			      WBCGtk *wbcg)
{
	SheetControlGUI *scg;
	gint n_source;
	GtkWidget *p_source;

	g_return_if_fail (IS_WBC_GTK (wbcg));

	n_source = gnm_notebook_page_num_by_label (wbcg->notebook, widget);
	p_source = gtk_notebook_get_nth_page (wbcg->notebook, n_source);
	scg = g_object_get_data (G_OBJECT (p_source), SHEET_CONTROL_KEY);

	gtk_selection_data_set (selection_data, selection_data->target,
		8, (void *) scg, sizeof (scg));
}

static void
cb_sheet_label_drag_data_received (GtkWidget *widget, GdkDragContext *context,
				   gint x, gint y, GtkSelectionData *data, guint info, guint time,
				   WBCGtk *wbcg)
{
	GtkWidget *w_source;
	gint p_src;

	g_return_if_fail (IS_WBC_GTK (wbcg));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	w_source = gtk_drag_get_source_widget (context);
	if (!w_source) {
		g_warning ("Not yet implemented!"); /* Different process */
		return;
	}

	p_src = gnm_notebook_page_num_by_label (wbcg->notebook, w_source);

	/*
	 * Is this a sheet of our workbook? If yes, we just reorder
	 * the sheets.
	 */
	if (p_src >= 0) {
		Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));
		Sheet *s_src = workbook_sheet_by_index (wb, p_src);
		int p_dst = gnm_notebook_page_num_by_label (wbcg->notebook,
							    widget);
		Sheet *s_dst = workbook_sheet_by_index (wb, p_dst);

		if (s_src && s_dst && s_src != s_dst) {
			WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
			workbook_sheet_move (s_src, p_dst - p_src);
			cmd_reorganize_sheets (WORKBOOK_CONTROL (wbcg),
					       old_state,
					       s_src);
		}
	} else {

		g_return_if_fail (IS_SHEET_CONTROL_GUI (data->data));

		/* Different workbook, same process */
		g_warning ("Not yet implemented!");
	}
}

static void
cb_sheet_label_drag_begin (GtkWidget *widget, GdkDragContext *context,
			   WBCGtk *wbcg)
{
	GtkWidget *arrow, *image;
	GdkPixbuf *pixbuf;
	GdkBitmap *bitmap;

	g_return_if_fail (IS_WBC_GTK (wbcg));

	/* Create the arrow. */
	arrow = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_widget_realize (arrow);
	pixbuf = gtk_icon_theme_load_icon (
		gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget)),
		"sheet_move_marker", 13, 0, NULL);
	image = gtk_image_new_from_pixbuf (pixbuf);
	gtk_widget_show (image);
	gtk_container_add (GTK_CONTAINER (arrow), image);
	gdk_pixbuf_render_pixmap_and_mask_for_colormap (pixbuf,
		gtk_widget_get_colormap (widget), NULL, &bitmap, 0x7f);
	g_object_unref (pixbuf);
	gtk_widget_shape_combine_mask (arrow, bitmap, 0, 0);
	g_object_unref (bitmap);
#if GLIB_CHECK_VERSION(2,10,0) && GTK_CHECK_VERSION(2,8,14)
	g_object_ref_sink (arrow);
#else
	g_object_ref (arrow);
	gtk_object_sink (GTK_OBJECT (arrow));
#endif
	g_object_set_data (G_OBJECT (widget), "arrow", arrow);
}

static void
cb_sheet_label_drag_end (GtkWidget *widget, GdkDragContext *context,
			 WBCGtk *wbcg)
{
	GtkWidget *arrow;

	g_return_if_fail (IS_WORKBOOK_CONTROL (wbcg));

	/* Destroy the arrow. */
	arrow = g_object_get_data (G_OBJECT (widget), "arrow");
	gtk_object_destroy (GTK_OBJECT (arrow));
	g_object_unref (arrow);
	g_object_set_data (G_OBJECT (widget), "arrow", NULL);
}

static void
cb_sheet_label_drag_leave (GtkWidget *widget, GdkDragContext *context,
			   guint time, WBCGtk *wbcg)
{
	GtkWidget *w_source, *arrow;

	/* Hide the arrow. */
	w_source = gtk_drag_get_source_widget (context);
	if (w_source) {
		arrow = g_object_get_data (G_OBJECT (w_source), "arrow");
		gtk_widget_hide (arrow);
	}
}

static gboolean
cb_sheet_label_drag_motion (GtkWidget *widget, GdkDragContext *context,
	gint x, gint y, guint time, WBCGtk *wbcg)
{
	GtkWidget *w_source, *arrow, *window;
	gint n_source, n_dest, root_x, root_y, pos_x, pos_y;

	g_return_val_if_fail (IS_WBC_GTK (wbcg), FALSE);
	g_return_val_if_fail (IS_WBC_GTK (wbcg), FALSE);

	/* Make sure we are really hovering over another label. */
	w_source = gtk_drag_get_source_widget (context);
	n_source = -1;
	if (w_source)
		n_source = gnm_notebook_page_num_by_label (wbcg->notebook,
							   w_source);
	else
		return FALSE;
	n_dest   = gnm_notebook_page_num_by_label (wbcg->notebook, widget);
	arrow = g_object_get_data (G_OBJECT (w_source), "arrow");
	if (n_source == n_dest) {
		gtk_widget_hide (arrow);
		return (FALSE);
	}

	/* Move the arrow to the correct position and show it. */
	window = gtk_widget_get_ancestor (widget, GTK_TYPE_WINDOW);
	gtk_window_get_position (GTK_WINDOW (window), &root_x, &root_y);
	pos_x = root_x + widget->allocation.x;
	pos_y = root_y + widget->allocation.y;
	if (n_source < n_dest)
		pos_x += widget->allocation.width;
	gtk_window_move (GTK_WINDOW (arrow), pos_x, pos_y);
	gtk_widget_show (arrow);

	return (TRUE);
}

static void
set_dir (GtkWidget *w, GtkTextDirection *dir)
{
	gtk_widget_set_direction (w, *dir);
	if (GTK_IS_CONTAINER (w))
		gtk_container_foreach (GTK_CONTAINER (w),
				       (GtkCallback)&set_dir,
				       dir);
}

static void
cb_direction_change (G_GNUC_UNUSED Sheet *null_sheet,
		     G_GNUC_UNUSED GParamSpec *null_pspec,
		     SheetControlGUI const *scg)
{
	GtkWidget *w = (GtkWidget *)scg->wbcg->notebook;
	gboolean text_is_rtl = scg->sheet_control.sheet->text_is_rtl;
	GtkTextDirection dir = text_is_rtl
		? GTK_TEXT_DIR_RTL
		: GTK_TEXT_DIR_LTR;

	if (dir != gtk_widget_get_direction (w))
		set_dir (w, &dir);
	g_object_set (scg->hs, "inverted", text_is_rtl, NULL);
}

static void
wbcg_update_menu_feedback (WBCGtk *wbcg, Sheet const *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	if (!wbcg_ui_update_begin (wbcg))
		return;

	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetDisplayFormulas", sheet->display_formulas);
	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetHideZeros", sheet->hide_zero);
	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetHideGridlines", sheet->hide_grid);
	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetHideColHeader", sheet->hide_col_header);
	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetHideRowHeader", sheet->hide_row_header);
	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetDisplayOutlines", sheet->display_outlines);
	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetOutlineBelow", sheet->outline_symbols_below);
	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetOutlineRight", sheet->outline_symbols_right);
	wbc_gtk_set_toggle_action_state (wbcg,
		"SheetUseR1C1", sheet->convs->r1c1_addresses);
	wbcg_ui_update_end (wbcg);
}

static void
cb_zoom_change (Sheet *sheet,
		G_GNUC_UNUSED GParamSpec *null_pspec,
		WBCGtk *wbcg)
{
	if (wbcg_ui_update_begin (wbcg)) {
		int pct = sheet->last_zoom_factor_used * 100 + .5;
		char *label = g_strdup_printf ("%d%%", pct);
		go_action_combo_text_set_entry (wbcg->zoom, label,
			GO_ACTION_COMBO_SEARCH_CURRENT);
		g_free (label);
		wbcg_ui_update_end (wbcg);
	}
}

static void
cb_notebook_switch_page (GtkNotebook *notebook, GtkNotebookPage *page,
			 guint page_num, WBCGtk *wbcg)
{
	Sheet *sheet;
	SheetControlGUI *new_scg;
	GtkWidget *child;

	g_return_if_fail (IS_WBC_GTK (wbcg));

	/* Ignore events during destruction */
	if (wbcg->notebook == NULL)
		return;

	/* While initializing adding the sheets will trigger page changes, but
	 * we do not actually want to change the focus sheet for the view
	 */
	if (wbcg->updating_ui)
		return;

	/* If we are not at a subexpression boundary then finish editing */
	if (NULL != wbcg->rangesel)
		scg_rangesel_stop (wbcg->rangesel, TRUE);

	child = gtk_notebook_get_nth_page (notebook, page_num);
	new_scg = g_object_get_data (G_OBJECT (child), SHEET_CONTROL_KEY);

	cb_direction_change (NULL, NULL, new_scg);

	if (wbcg_is_editing (wbcg) && wbcg_rangesel_possible (wbcg)) {
		/*
		 * When we are editing, sheet changes are not done fully.
		 * We revert to the original sheet later.
		 *
		 * On the other hand, when we are selecting a range for a
		 * dialog, we do change sheet fully.
		 */
		scg_take_focus (new_scg);
		return;
	}

	gnm_expr_entry_set_scg (wbcg->edit_line.entry, new_scg);

	/*
	 * Make absolutely sure the expression doesn't get 'lost', if it's invalid
	 * then prompt the user and don't switch the notebook page.
	 */
	if (wbcg_is_editing (wbcg)) {
		guint prev = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (notebook), "previous_page"));

		if (prev == page_num)
			return;

		if (!wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, NULL))
			gtk_notebook_set_current_page (notebook, prev);
		else
			/* Looks silly, but is really neccesarry */
			gtk_notebook_set_current_page (notebook, page_num);
		return;
	}

	g_object_set_data (G_OBJECT (notebook), "previous_page",
			   GINT_TO_POINTER (gtk_notebook_get_current_page (notebook)));

	/* if we are not selecting a range for an expression update */
	sheet = wbcg_focus_cur_scg (wbcg);
	if (sheet != wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg))) {
		wbcg_update_menu_feedback (wbcg, sheet);
		sheet_flag_status_update_range (sheet, NULL);
		sheet_update (sheet);
		wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);
		cb_zoom_change (sheet, NULL, wbcg);
	}
}

static void
workbook_setup_sheets (WBCGtk *wbcg)
{
	wbcg->notebook = g_object_new (GTK_TYPE_NOTEBOOK,
				       "tab-pos",	GTK_POS_BOTTOM,
				       "tab-hborder",	0,
				       "tab-vborder",	0,
				       NULL);
	g_signal_connect_after (G_OBJECT (wbcg->notebook),
		"switch_page",
		G_CALLBACK (cb_notebook_switch_page), wbcg);

	gtk_table_attach (GTK_TABLE (wbcg->table), GTK_WIDGET (wbcg->notebook),
			  0, 1, 1, 2,
			  GTK_FILL | GTK_EXPAND | GTK_SHRINK,
			  GTK_FILL | GTK_EXPAND | GTK_SHRINK,
			  0, 0);

	gtk_widget_show (GTK_WIDGET (wbcg->notebook));

#ifdef GNM_USE_HILDON
	gtk_notebook_set_show_border (wbcg->notebook, FALSE);
	gtk_notebook_set_show_tabs (wbcg->notebook, FALSE);
#endif
}


#ifdef GNM_USE_HILDON
static void
wbc_gtk_hildon_set_action_sensitive (WBCGtk      *wbcg,
				     const gchar *action,
				     gboolean     sensitive)
{
	GtkWidget * toolbar = gtk_ui_manager_get_widget (wbcg->ui, "/StandardToolbar");
	GList * children = gtk_container_get_children (GTK_CONTAINER (toolbar));
	GList * l;

	for (l = children; l != NULL; l = g_list_next (l)) {
		if (GTK_IS_SEPARATOR_TOOL_ITEM (l->data) == FALSE) {
			gchar * label = NULL;
			g_object_get (GTK_TOOL_ITEM (l->data), "label", &label, NULL);

			if (label != NULL && strstr (label, action) != NULL) {
				g_object_set (G_OBJECT (l->data), "sensitive", sensitive, NULL);
				g_free(label);
				break;
			}

			g_free(label);
		}
	}

	g_list_free (children);
}
#endif


static void
wbcg_menu_state_sheet_count (WBCGtk *wbcg)
{
	int const sheet_count = g_list_length (wbcg->notebook->children);
	/* Should we enable commands requiring multiple sheets */
	gboolean const multi_sheet = (sheet_count > 1);

	/* Scrollable if there are more than 3 tabs */
	gtk_notebook_set_scrollable (wbcg->notebook, sheet_count > 3);

	wbc_gtk_set_action_sensitivity (wbcg, "SheetRemove", multi_sheet);
}

static void
cb_sheet_tab_change (Sheet *sheet,
		     G_GNUC_UNUSED GParamSpec *pspec,
		     EditableLabel *el)
{
	/* We're lazy and just set all relevant attributes.  */
	editable_label_set_text (el, sheet->name_unquoted);
	editable_label_set_color (el,
				  sheet->tab_color ? &sheet->tab_color->gdk_color : NULL,
				  sheet->tab_text_color ? &sheet->tab_text_color->gdk_color : NULL);
}

static void
cb_toggle_menu_item_changed (Sheet *sheet,
			     G_GNUC_UNUSED GParamSpec *pspec,
			     WBCGtk *wbcg)
{
	/* We're lazy and just update all.  */
	wbcg_update_menu_feedback (wbcg, sheet);
}

static void
cb_sheet_visibility_change (Sheet *sheet,
			    G_GNUC_UNUSED GParamSpec *pspec,
			    GtkWidget *w)
{
	if (sheet_is_visible (sheet))
		gtk_widget_show (w);
	else
		gtk_widget_hide (w);
}

static void
disconnect_sheet_signals (WBCGtk *wbcg, Sheet *sheet, gboolean focus_signals_only)
{
	SheetControlGUI *scg = wbcg_get_scg (wbcg, sheet);

	if (scg) {
		g_signal_handlers_disconnect_by_func (sheet, cb_toggle_menu_item_changed, wbcg);
		g_signal_handlers_disconnect_by_func (sheet, cb_direction_change, scg);
		g_signal_handlers_disconnect_by_func (sheet, cb_zoom_change, wbcg);

		if (!focus_signals_only) {
			g_signal_handlers_disconnect_by_func (sheet, cb_sheet_tab_change, scg->label);
			g_signal_handlers_disconnect_by_func (sheet, cb_sheet_visibility_change, scg->table);
		}
	}
}

static void
wbcg_sheet_add (WorkbookControl *wbc, SheetView *sv)
{
	static GtkTargetEntry const drag_types[] = {
		{ (char *) "GNUMERIC_SHEET", GTK_TARGET_SAME_APP, TARGET_SHEET }
	};

	WBCGtk *wbcg = (WBCGtk *)wbc;
	SheetControlGUI *scg;
	Sheet		*sheet   = sv_sheet (sv);
	gboolean	 visible = sheet_is_visible (sheet);

	g_return_if_fail (wbcg != NULL);

	if (wbcg->notebook == NULL)
		workbook_setup_sheets (wbcg);

	scg = sheet_control_gui_new (sv, wbcg);
	g_object_set_data (G_OBJECT (scg->table), SHEET_CONTROL_KEY, scg);

	/*
	 * NB. this is so we can use editable_label_set_text since
	 * gtk_notebook_set_tab_label kills our widget & replaces with a label.
	 */
	scg->label = editable_label_new (sheet->name_unquoted,
			sheet->tab_color ? &sheet->tab_color->gdk_color : NULL,
			sheet->tab_text_color ? &sheet->tab_text_color->gdk_color : NULL);
	g_signal_connect_after (G_OBJECT (scg->label),
				"edit_finished",
				G_CALLBACK (cb_sheet_label_edit_finished), wbcg);

	/* do not preempt the editable label handler */
	g_signal_connect_after (G_OBJECT (scg->label),
		"button_press_event",
		G_CALLBACK (cb_sheet_label_button_press), scg);

	/* Drag & Drop */
	gtk_drag_source_set (scg->label, GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
			drag_types, G_N_ELEMENTS (drag_types),
			GDK_ACTION_MOVE);
	gtk_drag_dest_set (scg->label, GTK_DEST_DEFAULT_ALL,
			drag_types, G_N_ELEMENTS (drag_types),
			GDK_ACTION_MOVE);
	g_object_connect (G_OBJECT (scg->label),
		"signal::drag_begin", G_CALLBACK (cb_sheet_label_drag_begin), wbcg,
		"signal::drag_end", G_CALLBACK (cb_sheet_label_drag_end), wbcg,
		"signal::drag_leave", G_CALLBACK (cb_sheet_label_drag_leave), wbcg,
		"signal::drag_data_get", G_CALLBACK (cb_sheet_label_drag_data_get), wbcg,
		"signal::drag_data_received", G_CALLBACK (cb_sheet_label_drag_data_received), wbcg,
		"signal::drag_motion", G_CALLBACK (cb_sheet_label_drag_motion), wbcg,
		NULL);

	gtk_widget_show (scg->label);
	gtk_widget_show_all (GTK_WIDGET (scg->table));
	if (!visible)
		gtk_widget_hide (GTK_WIDGET (scg->table));
	g_object_connect (G_OBJECT (sheet),
			  "signal::notify::visibility", cb_sheet_visibility_change, scg->table,
			  "signal::notify::name", cb_sheet_tab_change, scg->label,
			  "signal::notify::tab-foreground", cb_sheet_tab_change, scg->label,
			  "signal::notify::tab-background", cb_sheet_tab_change, scg->label,
			  NULL);

	if (wbcg_ui_update_begin (wbcg)) {
		gtk_notebook_insert_page (wbcg->notebook,
					  GTK_WIDGET (scg->table), scg->label,
					  sheet->index_in_wb);
		wbcg_menu_state_sheet_count (wbcg);
		wbcg_ui_update_end (wbcg);
	}

	scg_adjust_preferences (scg);
	if (sheet == wb_control_cur_sheet (wbc)) {
		scg_take_focus (scg);
		cb_direction_change (NULL, NULL, scg);
		cb_zoom_change (sheet, NULL, wbcg);
		cb_toggle_menu_item_changed (sheet, NULL, wbcg);
	}
}

static void
wbcg_sheet_remove (WorkbookControl *wbc, Sheet *sheet)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;

	/* During destruction we may have already removed the notebook */
	if (wbcg->notebook == NULL)
		return;

	disconnect_sheet_signals (wbcg, sheet, FALSE);
	gtk_notebook_remove_page (wbcg->notebook, sheet->index_in_wb);

	wbcg_menu_state_sheet_count (wbcg);
}

static void
wbcg_sheet_focus (WorkbookControl *wbc, Sheet *sheet)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	SheetControlGUI *scg = wbcg_get_scg (wbcg, sheet);

	if (sheet)
		gtk_notebook_set_current_page (wbcg->notebook,
					       sheet->index_in_wb);
	if (wbcg->rangesel == NULL)
		gnm_expr_entry_set_scg (wbcg->edit_line.entry, scg);

	disconnect_sheet_signals (wbcg, wbcg_cur_sheet (wbcg), TRUE);

	if (sheet) {
		wbcg_update_menu_feedback (wbcg, sheet);
		cb_direction_change (NULL, NULL, scg);

		g_object_connect
			(G_OBJECT (sheet),
			 "signal::notify::display-formulas", cb_toggle_menu_item_changed, wbcg,
			 "signal::notify::display-zeros", cb_toggle_menu_item_changed, wbcg,
			 "signal::notify::display-grid", cb_toggle_menu_item_changed, wbcg,
			 "signal::notify::display-column-header", cb_toggle_menu_item_changed, wbcg,
			 "signal::notify::display-row-header", cb_toggle_menu_item_changed, wbcg,
			 "signal::notify::display-outlines", cb_toggle_menu_item_changed, wbcg,
			 "signal::notify::display-outlines-below", cb_toggle_menu_item_changed, wbcg,
			 "signal::notify::display-outlines-right", cb_toggle_menu_item_changed, wbcg,
			 "signal::notify::text-is-rtl", cb_direction_change, scg,
			 "signal::notify::zoom-factor", cb_zoom_change, wbcg,
			 NULL);
	}
}

static void
wbcg_sheet_order_changed (WBCGtk *wbcg)
{
	GtkNotebook *nb = wbcg->notebook;
	int i, n = gtk_notebook_get_n_pages (nb);
	SheetControlGUI **scgs = g_new (SheetControlGUI *, n);

	/* Collect the scgs first as we are moving pages.  */
	for (i = 0 ; i < n; i++) {
		GtkWidget *w = gtk_notebook_get_nth_page (nb, i);
		scgs[i] = g_object_get_data (G_OBJECT (w), SHEET_CONTROL_KEY);
	}

	for (i = 0 ; i < n; i++) {
		SheetControlGUI *scg = scgs[i];
		Sheet *sheet = ((SheetControl *)scg)->sheet;
		gtk_notebook_reorder_child (nb,
					    GTK_WIDGET (scg->table),
					    sheet->index_in_wb);
	}

	g_free (scgs);
}

static void
wbcg_update_title (WBCGtk *wbcg)
{
	GODoc *doc = wb_control_get_doc (WORKBOOK_CONTROL (wbcg));
	char *basename = go_basename_from_uri (doc->uri);
	char *title = g_strconcat
		(go_doc_is_dirty (doc) ? "*" : "",
		 basename ? basename : doc->uri,
		 _(" : Gnumeric"),
		 NULL);
	gtk_window_set_title (wbcg_toplevel (wbcg), title);
	g_free (title);
	g_free (basename);
}

static void
wbcg_sheet_remove_all (WorkbookControl *wbc)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;

	if (wbcg->notebook != NULL) {
		GtkWidget *tmp = GTK_WIDGET (wbcg->notebook);
		Workbook *wb = wb_control_get_workbook (wbc);
		int i;

		/* Clear notebook to disable updates as focus changes for pages
		 * during destruction */
		wbcg->notebook = NULL;

		/* Be sure we are no longer editing */
		wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);

		for (i = workbook_sheet_count (wb) - 1; i >= 0; i--)
			disconnect_sheet_signals (wbcg, workbook_sheet_by_index (wb, i), FALSE);

		gtk_widget_destroy (tmp);
	}
}

static void
wbcg_auto_expr_text_changed (WorkbookView *wbv,
			     G_GNUC_UNUSED GParamSpec *pspec,
			     WBCGtk *wbcg)
{
	gtk_label_set_text (GTK_LABEL (wbcg->auto_expr_label),
			    wbv->auto_expr_text ? wbv->auto_expr_text : "");
}

static void
wbcg_menu_state_update (WorkbookControl *wbc, int flags)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	SheetView const *sv  = wb_control_cur_sheet_view (wbc);
	Sheet const *sheet = wb_control_cur_sheet (wbc);
	gboolean const has_guru = wbc_gtk_get_guru (wbcg) != NULL;
	gboolean has_filtered_rows = sheet->has_filtered_rows;
	gboolean edit_object = scg != NULL &&
		(scg->selected_objects != NULL || scg->new_object != NULL);

	if (!has_filtered_rows) {
		GSList *ptr = sheet->filters;
		for (;ptr != NULL ; ptr = ptr->next)
			if (((GnmFilter *)ptr->data)->is_active) {
				has_filtered_rows = TRUE;
				break;
			}
	}

	if (MS_INSERT_COLS & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "InsertColumns",
			sv->enable_insert_cols);
	if (MS_INSERT_ROWS & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "InsertRows",
			sv->enable_insert_rows);
	if (MS_INSERT_CELLS & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "InsertCells",
			sv->enable_insert_cells);
	if (MS_SHOWHIDE_DETAIL & flags) {
		wbc_gtk_set_action_sensitivity (wbcg, "DataOutlineShowDetail",
			sheet->priv->enable_showhide_detail);
		wbc_gtk_set_action_sensitivity (wbcg, "DataOutlineHideDetail",
			sheet->priv->enable_showhide_detail);
	}
	if (MS_PASTE_SPECIAL & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "EditPasteSpecial",
			!gnm_app_clipboard_is_empty () &&
			!gnm_app_clipboard_is_cut () &&
			!edit_object);
	if (MS_PRINT_SETUP & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "FilePageSetup", !has_guru);
	if (MS_SEARCH_REPLACE & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "EditReplace", !has_guru);
	if (MS_DEFINE_NAME & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "EditNames", !has_guru);
	if (MS_CONSOLIDATE & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "DataConsolidate", !has_guru);
	if (MS_CONSOLIDATE & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "DataFilterShowAll", has_filtered_rows);

	if (MS_FREEZE_VS_THAW & flags) {
		/* Cheat and use the same accelerator for both states because
		 * we don't reset it when the label changes */
		char const* label = sv_is_frozen (sv)
			? _("Un_freeze Panes")
			: _("_Freeze Panes");
		char const *new_tip = sv_is_frozen (sv)
			? _("Unfreeze the top left of the sheet")
			: _("Freeze the top left of the sheet");
		wbc_gtk_set_action_label (wbcg, "ViewFreezeThawPanes", NULL, label, new_tip);
	}

	if (MS_ADD_VS_REMOVE_FILTER & flags) {
		gboolean const has_filter = (sv_first_selection_in_filter (sv) != NULL);
		char const* label = has_filter
			? _("Remove _Auto Filter")
			: _("Add _Auto Filter");
		char const *new_tip = has_filter
			? _("Remove a filter")
			: _("Add a filter");
		wbc_gtk_set_action_label (wbcg, "DataAutoFilter", NULL, label, new_tip);
	}
}

static void
wbcg_undo_redo_labels (WorkbookControl *wbc, char const *undo, char const *redo)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	g_return_if_fail (wbcg != NULL);

	wbc_gtk_set_action_label (wbcg, "Redo", _("_Redo"), redo, NULL);
	wbc_gtk_set_action_label (wbcg, "Undo", _("_Undo"), undo, NULL);
	wbc_gtk_set_action_sensitivity (wbcg, "Repeat", undo != NULL);
}

static void
wbcg_paste_from_selection (WorkbookControl *wbc, GnmPasteTarget const *pt)
{
	x_request_clipboard ((WBCGtk *)wbc, pt);
}

static gboolean
wbcg_claim_selection (WorkbookControl *wbc)
{
	return x_claim_clipboard ((WBCGtk *)wbc);
}

static int
wbcg_show_save_dialog (WBCGtk *wbcg,
		       Workbook *wb, gboolean exiting)
{
	GtkWidget *d;
	char *msg;
	char const *wb_uri = go_doc_get_uri (GO_DOC (wb));
	int ret = 0;

	if (wb_uri) {
		char *base    = go_basename_from_uri (wb_uri);
		char *display = g_markup_escape_text (base, -1);
		msg = g_strdup_printf (
			_("Save changes to workbook '%s' before closing?"),
			display);
		g_free (base);
		g_free (display);
	} else {
		msg = g_strdup (_("Save changes to workbook before closing?"));
	}

	d = gnumeric_message_dialog_new (wbcg_toplevel (wbcg),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 msg,
					 _("If you close without saving, changes will be discarded."));
	atk_object_set_role (gtk_widget_get_accessible (d), ATK_ROLE_ALERT);

	if (exiting) {
		int n_of_wb = g_list_length (gnm_app_workbook_list ());
		if (n_of_wb > 1) {
			go_gtk_dialog_add_button (GTK_DIALOG(d), _("Discard all"),
				GTK_STOCK_DELETE, GNM_RESPONSE_DISCARD_ALL);
			go_gtk_dialog_add_button (GTK_DIALOG(d), _("Discard"),
				GTK_STOCK_DELETE, GTK_RESPONSE_NO);
			go_gtk_dialog_add_button (GTK_DIALOG(d), _("Save all"),
				GTK_STOCK_SAVE, GNM_RESPONSE_SAVE_ALL);
			go_gtk_dialog_add_button (GTK_DIALOG(d), _("Don't quit"),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		} else {
			go_gtk_dialog_add_button (GTK_DIALOG(d), _("Discard"),
				GTK_STOCK_DELETE, GTK_RESPONSE_NO);
			go_gtk_dialog_add_button (GTK_DIALOG(d), _("Don't quit"),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		}
	} else {
		go_gtk_dialog_add_button (GTK_DIALOG(d), _("Discard"),
					    GTK_STOCK_DELETE, GTK_RESPONSE_NO);
		go_gtk_dialog_add_button (GTK_DIALOG(d), _("Don't close"),
					    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	}

	gtk_dialog_add_button (GTK_DIALOG(d), GTK_STOCK_SAVE, GTK_RESPONSE_YES);
	gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
	ret = go_gtk_dialog_run (GTK_DIALOG (d), wbcg_toplevel (wbcg));
	g_free (msg);

	return ret;
}

/**
 * wbcg_close_if_user_permits : If the workbook is dirty the user is
 *		prompted to see if they should exit.
 *
 * Returns :
 * 0) canceled
 * 1) closed
 * 2) pristine can close
 * 3) save any future dirty
 * 4) do not save any future dirty
 */
static int
wbcg_close_if_user_permits (WBCGtk *wbcg,
			    WorkbookView *wb_view, gboolean close_clean,
			    gboolean exiting, gboolean ask_user)
{
	gboolean   can_close = TRUE;
	gboolean   done      = FALSE;
	int        iteration = 0;
	int        button = 0;
	Workbook  *wb = wb_view_get_workbook (wb_view);
	static int in_can_close;

	g_return_val_if_fail (IS_WORKBOOK (wb), 0);

	if (!close_clean && !go_doc_is_dirty (GO_DOC (wb)))
		return 2;

	if (in_can_close)
		return 0;
	in_can_close = TRUE;

	if (!ask_user) {
		done = gui_file_save (wbcg, wb_view);
		if (done) {
			x_store_clipboard_if_needed (wb);
			g_object_unref (wb);
			return 3;
		}
	}
	while (go_doc_is_dirty (GO_DOC (wb)) && !done) {
		iteration++;
		button = wbcg_show_save_dialog(wbcg, wb, exiting);

		switch (button) {
		case GTK_RESPONSE_YES:
			done = gui_file_save (wbcg, wb_view);
			break;

		case GNM_RESPONSE_SAVE_ALL:
			done = gui_file_save (wbcg, wb_view);
			break;

		case GTK_RESPONSE_NO:
			done      = TRUE;
			go_doc_set_dirty (GO_DOC (wb), FALSE);
			break;

		case GNM_RESPONSE_DISCARD_ALL:
			done      = TRUE;
			go_doc_set_dirty (GO_DOC (wb), FALSE);
			break;

		default:  /* CANCEL */
			can_close = FALSE;
			done      = TRUE;
			break;
		}
	}

	in_can_close = FALSE;

	if (can_close) {
		x_store_clipboard_if_needed (wb);
		g_object_unref (wb);
		switch (button) {
		case GNM_RESPONSE_SAVE_ALL:
			return 3;
		case GNM_RESPONSE_DISCARD_ALL:
			return 4;
		default:
			return 1;
		}
	} else
		return 0;
}

/**
 * wbc_gtk_close:
 * @wbcg : #WBCGtk
 *
 * Returns TRUE if the control should NOT be closed.
 */
gboolean
wbc_gtk_close (WBCGtk *wbcg)
{
	WorkbookView *wb_view = wb_control_view (WORKBOOK_CONTROL (wbcg));

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wb_view), TRUE);
	g_return_val_if_fail (wb_view->wb_controls != NULL, TRUE);

	/* If we were editing when the quit request came make sure we don't
	 * lose any entered text
	 */
	if (!wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, NULL))
		return TRUE;

	/* If something is still using the control
	 * eg progress meter for a new book */
	if (G_OBJECT (wbcg)->ref_count > 1)
		return TRUE;

	/* This is the last control */
	if (wb_view->wb_controls->len <= 1) {
		Workbook *wb = wb_view_get_workbook (wb_view);

		g_return_val_if_fail (IS_WORKBOOK (wb), TRUE);
		g_return_val_if_fail (wb->wb_views != NULL, TRUE);

		/* This is the last view */
		if (wb->wb_views->len <= 1)
			return wbcg_close_if_user_permits (wbcg, wb_view, TRUE, FALSE, TRUE) == 0;

		g_object_unref (G_OBJECT (wb_view));
	} else
		g_object_unref (G_OBJECT (wbcg));

	_gnm_app_flag_windows_changed ();

	return FALSE;
}

static void
cb_cancel_input (WBCGtk *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);
}

static void
cb_accept_input (WBCGtk *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, NULL);
}

static gboolean
cb_editline_focus_in (GtkWidget *w, GdkEventFocus *event,
		      WBCGtk *wbcg)
{
	if (!wbcg_is_editing (wbcg))
		if (!wbcg_edit_start (wbcg, FALSE, TRUE)) {
			GtkEntry *entry = GTK_ENTRY (w);
			wbcg_focus_cur_scg (wbcg);
			entry->in_drag = FALSE;
			/*
			 * ->button is private, ugh.  Since the text area
			 * never gets a release event, there seems to be
			 * no official way of returning the widget to its
			 * correct state.
			 */
			entry->button = 0;
			return TRUE;
		}

	return FALSE;
}

static void
cb_statusbox_activate (GtkEntry *entry, WBCGtk *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wb_control_parse_and_jump (wbc, gtk_entry_get_text (entry));
	wbcg_focus_cur_scg (wbcg);
	wb_view_selection_desc (wb_control_view (wbc), TRUE, wbc);
}

static gboolean
cb_statusbox_focus (GtkEntry *entry, GdkEventFocus *event,
		    WBCGtk *wbcg)
{
	gtk_editable_select_region (GTK_EDITABLE (entry), 0, 0);
	return FALSE;
}

/******************************************************************************/
static void
cb_workbook_debug_info (WBCGtk *wbcg)
{
	Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));

	if (wbc_gtk_debug_deps > 0) {
		WORKBOOK_FOREACH_SHEET (wb, sheet,
			g_printerr ("Dependencies for %s:\n", sheet->name_unquoted);
			gnm_dep_container_dump (sheet->deps););
	}

	if (wbc_gtk_debug_expr_share > 0) {
		GnmExprSharer *es = workbook_share_expressions (wb, FALSE);

		g_print ("Expression sharer results:\n"
			 "Nodes in: %d, nodes stored: %d, nodes killed: %d.\n",
			 es->nodes_in, es->nodes_stored, es->nodes_killed);
		gnm_expr_sharer_destroy (es);
	}
}

static void
cb_autofunction (WBCGtk *wbcg)
{
	GtkEntry *entry;
	gchar const *txt;

	if (wbcg_is_editing (wbcg))
		return;

	entry = wbcg_get_entry (wbcg);
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=", 1)) {
		if (!wbcg_edit_start (wbcg, TRUE, TRUE))
			return; /* attempt to edit failed */
		gtk_entry_set_text (entry, "=");
		gtk_editable_set_position (GTK_EDITABLE (entry), 1);
	} else {
		if (!wbcg_edit_start (wbcg, FALSE, TRUE))
			return; /* attempt to edit failed */

		/* FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_editable_set_position (GTK_EDITABLE (entry),
			entry->text_length-1);
	}
}

static GtkWidget *
edit_area_button (WBCGtk *wbcg, GtkToolbar *tb,
		  gboolean sensitive,
		  GCallback func, char const *stock_id,
		  char const *tip)
{
	GObject *button =
		g_object_new (GTK_TYPE_TOOL_BUTTON,
			      "stock-id", stock_id,
			      "sensitive", sensitive,
			      "can-focus", FALSE,
			      NULL);
	go_tool_item_set_tooltip_text (GTK_TOOL_ITEM (button), tip);
	g_signal_connect_swapped (button, "clicked", func, wbcg);
	gtk_toolbar_insert (tb, GTK_TOOL_ITEM (button), -1);

	return GTK_WIDGET (button);
}

/*
 * We must not crash on focus=NULL. We're called like that as a result of
 * gtk_window_set_focus (toplevel, NULL) if the first sheet view is destroyed
 * just after being created. This happens e.g when we cancel a file import or
 * the import fails.
 */
static void
cb_set_focus (GtkWindow *window, GtkWidget *focus, WBCGtk *wbcg)
{
	if (focus && !window->focus_widget)
		wbcg_focus_cur_scg (wbcg);
}

/***************************************************************************/

static gboolean
cb_scroll_wheel (GtkWidget *ignored, GdkEventScroll *event,
		 WBCGtk *wbcg)
{
	/* scroll always operates on pane 0 */
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	Sheet		*sheet = scg_sheet (scg);
	GnmPane *pane = scg_pane (scg, 0);
	gboolean go_horiz = (event->direction == GDK_SCROLL_LEFT ||
			     event->direction == GDK_SCROLL_RIGHT);
	gboolean go_back = (event->direction == GDK_SCROLL_UP ||
			    event->direction == GDK_SCROLL_LEFT);

	if (!GTK_WIDGET_REALIZED (ignored))
		return FALSE;

	if ((event->state & GDK_MOD1_MASK))
		go_horiz = !go_horiz;

	if ((event->state & GDK_CONTROL_MASK)) {	/* zoom */
		int zoom = (int)(sheet->last_zoom_factor_used * 100. + .5) - 10;

		if ((zoom % 15) != 0) {
			zoom = 15 * (int)(zoom/15);
			if (go_back)
				zoom += 15;
		} else {
			if (go_back)
				zoom += 15;
			else
				zoom -= 15;
		}

		if (0 <= zoom && zoom <= 390)
			cmd_zoom (WORKBOOK_CONTROL (wbcg), g_slist_append (NULL, sheet),
				  (double) (zoom + 10) / 100);
	} else if ((event->state & GDK_SHIFT_MASK)) {
		/* XL sort of shows/hides groups */
	} else if (go_horiz) {
		int col = (pane->last_full.col - pane->first.col) / 4;
		if (col < 1)
			col = 1;
		if (go_back)
			col = pane->first.col - col;
		else
			col = pane->first.col + col;
		scg_set_left_col (pane->simple.scg, col);
	} else {
		int row = (pane->last_full.row - pane->first.row) / 4;
		if (row < 1)
			row = 1;
		if (go_back)
			row = pane->first.row - row;
		else
			row = pane->first.row + row;
		scg_set_top_row (pane->simple.scg, row);
	}
	return TRUE;
}

/*
 * Make current control size the default. Toplevel would resize
 * spontaneously. This makes it stay the same size until user resizes.
 */
static void
cb_realize (GtkWindow *toplevel, WBCGtk *wbcg)
{
	GtkAllocation *allocation;

	g_return_if_fail (GTK_IS_WINDOW (toplevel));

	allocation = &GTK_WIDGET (toplevel)->allocation;
	gtk_window_set_default_size (toplevel,
		allocation->width, allocation->height);

	/* if we are already initialized set the focus.  Without this loading a
	 * multpage book sometimes leaves focus on the last book rather than
	 * the current book.  Which leads to a slew of errors for keystrokes
	 * until focus is corrected.
	 */
	if (wbcg->notebook) {
		wbcg_focus_cur_scg (wbcg);
		wbcg_update_menu_feedback (wbcg,
			wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
	}
}

void
wbcg_set_status_text (WBCGtk *wbcg, char const *text)
{
	g_return_if_fail (IS_WBC_GTK (wbcg));
	gtk_statusbar_pop (GTK_STATUSBAR (wbcg->status_text), 0);
	gtk_statusbar_push (GTK_STATUSBAR (wbcg->status_text), 0, text);
}

static void
set_visibility (WBCGtk *wbcg,
		char const *action_name,
		gboolean visible)
{
	GtkWidget *w = g_hash_table_lookup (wbcg->visibility_widgets, action_name);
	if (w)
		(visible ? gtk_widget_show : gtk_widget_hide) (w);
	wbc_gtk_set_toggle_action_state (wbcg, action_name, visible);
}


void
wbcg_toggle_visibility (WBCGtk *wbcg, GtkToggleAction *action)
{
	if (!wbcg->updating_ui && wbcg_ui_update_begin (wbcg)) {
		char const *name = gtk_action_get_name (GTK_ACTION (action));
		set_visibility (wbcg, name,
			gtk_toggle_action_get_active (action));

		if (wbcg->toggle_for_fullscreen != NULL) {
			if (g_hash_table_lookup (wbcg->toggle_for_fullscreen, name) != NULL)
				g_hash_table_remove (wbcg->toggle_for_fullscreen, name);
			else
				g_hash_table_insert (wbcg->toggle_for_fullscreen,
					g_strdup (name), action);
		}
		wbcg_ui_update_end (wbcg);
	}
}

static void
cb_visibility (char const *action, GtkWidget *orig_widget, WBCGtk *new_wbcg)
{
	set_visibility (new_wbcg, action, GTK_WIDGET_VISIBLE (orig_widget));
}

void
wbcg_copy_toolbar_visibility (WBCGtk *new_wbcg,
			      WBCGtk *wbcg)
{
	g_hash_table_foreach (wbcg->visibility_widgets,
		(GHFunc)cb_visibility, new_wbcg);
}


void
wbcg_set_end_mode (WBCGtk *wbcg, gboolean flag)
{
	g_return_if_fail (IS_WBC_GTK (wbcg));

	if ((!wbcg->last_key_was_end) != (!flag)) {
		wbcg_set_status_text (wbcg,
			(wbcg->last_key_was_end = flag)
			? "END" : "");
	}
}

static PangoFontDescription *
settings_get_font_desc (GtkSettings *settings)
{
	PangoFontDescription *font_desc;
	char *font_str;

	g_object_get (settings, "gtk-font-name", &font_str, NULL);
	font_desc = pango_font_description_from_string (
		font_str ? font_str : "sans 10");
	g_free (font_str);

	return font_desc;
}

static void
cb_update_item_bar_font (GtkWidget *w)
{
	SheetControl *sc = g_object_get_data (G_OBJECT (w), SHEET_CONTROL_KEY);
	sc_resize (sc, TRUE);
}

static void
cb_desktop_font_changed (GtkSettings *settings, GParamSpec  *pspec,
			 WBCGtk *wbcg)
{
	if (wbcg->font_desc)
		pango_font_description_free (wbcg->font_desc);
	wbcg->font_desc = settings_get_font_desc (settings);
	gtk_container_foreach (GTK_CONTAINER (wbcg->notebook),
			       (GtkCallback)cb_update_item_bar_font, NULL);
}

static GtkSettings *
wbcg_get_gtk_settings (WBCGtk *wbcg)
{
	GdkScreen *screen = gtk_widget_get_screen (wbcg->table);
	return gtk_settings_get_for_screen (screen);
}

/* ------------------------------------------------------------------------- */
static int
show_gui (WBCGtk *wbcg)
{
	SheetControlGUI *scg;
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
	int sx, sy;
	gdouble fx, fy;
	GdkRectangle rect;

	/* In a Xinerama setup, we want the geometry of the actual display
	 * unit, if available. See bug 59902.  */
	gdk_screen_get_monitor_geometry (wbcg_toplevel (wbcg)->screen, 0, &rect);
	sx = MAX (rect.width, 600);
	sy = MAX (rect.height, 200);

	fx = gnm_app_prefs->horizontal_window_fraction;
	fy = gnm_app_prefs->vertical_window_fraction;

	/* Successfully parsed geometry string and urged WM to comply */
	if (NULL != wbcg->preferred_geometry && NULL != wbcg->toplevel &&
	    gtk_window_parse_geometry (GTK_WINDOW (wbcg->toplevel),
				       wbcg->preferred_geometry)) {
		g_free (wbcg->preferred_geometry);
		wbcg->preferred_geometry = NULL;
	} else if (wbcg->notebook != NULL &&
		   wbv != NULL &&
		   (wbv->preferred_width > 0 || wbv->preferred_height > 0)) {
		/* Set grid size to preferred width */
		int pwidth = wbv->preferred_width;
		int pheight = wbv->preferred_height;
		GtkRequisition requisition;

		pwidth = pwidth > 0 ? pwidth : -1;
		pheight = pheight > 0 ? pheight : -1;
		gtk_widget_set_size_request (GTK_WIDGET (wbcg->notebook),
					     pwidth, pheight);
		gtk_widget_size_request (GTK_WIDGET (wbcg->toplevel),
					 &requisition);
		/* We want to test if toplevel is bigger than screen.
		 * gtk_widget_size_request tells us the space
		 * allocated to the  toplevel proper, but not how much is
		 * need for WM decorations or a possible panel.
		 *
		 * The test below should very rarely maximize when there is
		 * actually room on the screen.
		 *
		 * We maximize instead of resizing for two reasons:
		 * - The preferred width / height is restored with one click on
		 *   unmaximize.
		 * - We don't have to guess what size we should resize to.
		 */
		if (requisition.height + 20 > rect.height ||
		    requisition.width > rect.width) {
			gtk_window_maximize (GTK_WINDOW (wbcg->toplevel));
		} else {
			gtk_window_set_default_size
				(wbcg_toplevel (wbcg),
				 requisition.width, requisition.height);
		}
	} else {
		/* Use default */
		gtk_window_set_default_size (wbcg_toplevel (wbcg), sx * fx, sy * fy);
	}

	if (NULL != (scg = wbcg_cur_scg (wbcg)))
		cb_direction_change (NULL, NULL, scg);

	gtk_widget_show (GTK_WIDGET (wbcg_toplevel (wbcg)));

	/* rehide headers if necessary */
	if (NULL != scg && wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)))
		scg_adjust_preferences (scg);

	return FALSE;
}

#if 0
static void
wbcg_drag_data_get (GtkWidget          *widget,
		    GdkDragContext     *context,
		    GtkSelectionData   *selection_data,
		    guint               info,
		    guint               time,
		    WorkbookControl    *wbcg)
{
	Workbook *wb    = wb_control_get_workbook (wbc);
	Sheet	 *sheet = wb_control_cur_sheet (wbc);
	BonoboMoniker *moniker;
	char *s;

	moniker = bonobo_moniker_new ();
	bonobo_moniker_set_server (moniker,
		"IDL:GNOME:Gnumeric:Workbook:1.0",
		wb->filename);
	bonobo_moniker_append_item_name (moniker,
		sheet->name_quoted);

	s = bonobo_moniker_get_as_string (moniker);
	gtk_object_destroy (GTK_OBJECT (moniker));
	gtk_selection_data_set (selection_data, selection_data->target, 8, s, strlen (s)+1);
}
#endif

static GtkWidget *
wbcg_get_label_for_position (WBCGtk *wbcg, GtkWidget *source,
			     gint x)
{
	GtkWidget *label = NULL, *page;
	guint n, i;

	g_return_val_if_fail (IS_WBC_GTK (wbcg), NULL);

	n = g_list_length (wbcg->notebook->children);
	for (i = 0; i < n; i++) {
		page = gtk_notebook_get_nth_page (wbcg->notebook, i);
		label = gtk_notebook_get_tab_label (wbcg->notebook, page);
		if (label->allocation.x + label->allocation.width >= x)
			break;
	}

	return (label);
}

static gboolean
wbcg_is_local_drag (WBCGtk *wbcg, GtkWidget *source_widget)
{
	GtkWidget *top = (GtkWidget *)wbcg_toplevel (wbcg);
	return IS_GNM_PANE (source_widget) &&
		gtk_widget_get_toplevel (source_widget) == top;
}
static gboolean
cb_wbcg_drag_motion (GtkWidget *widget, GdkDragContext *context,
		     gint x, gint y, guint time, WBCGtk *wbcg)
{
	GtkWidget *source_widget = gtk_drag_get_source_widget (context);

	if (IS_EDITABLE_LABEL (source_widget)) {
		/* The user wants to reorder sheets. We simulate a
		 * drag motion over a label.
		 */
		GtkWidget *label = wbcg_get_label_for_position (wbcg, source_widget, x);
		return cb_sheet_label_drag_motion (label, context, x, y, time, wbcg);
	} else if (wbcg_is_local_drag (wbcg, source_widget))
		gnm_pane_object_autoscroll (GNM_PANE (source_widget),
			context, x, y, time);

	return TRUE;
}

static void
cb_wbcg_drag_leave (GtkWidget *widget, GdkDragContext *context,
		    guint time, WBCGtk *wbcg)
{
	GtkWidget *source_widget = gtk_drag_get_source_widget (context);

	g_return_if_fail (IS_WBC_GTK (wbcg));

	if (IS_EDITABLE_LABEL (source_widget))
		gtk_widget_hide (
			g_object_get_data (G_OBJECT (source_widget), "arrow"));
	else if (wbcg_is_local_drag (wbcg, source_widget))
		gnm_pane_slide_stop (GNM_PANE (source_widget));
}

static void
cb_wbcg_drag_data_received (GtkWidget *widget, GdkDragContext *context,
			    gint x, gint y, GtkSelectionData *selection_data,
			    guint info, guint time, WBCGtk *wbcg)
{
	gchar *target_type = gdk_atom_name (selection_data->target);

	if (!strcmp (target_type, "text/uri-list")) { /* filenames from nautilus */
		scg_drag_data_received (wbcg_cur_scg (wbcg),
			 gtk_drag_get_source_widget (context), 0, 0,
			 selection_data);
	} else if (!strcmp (target_type, "GNUMERIC_SHEET")) {
		/* The user wants to reorder the sheets but hasn't dropped
		 * the sheet onto a label. Never mind. We figure out
		 * where the arrow is currently located and simulate a drop
		 * on that label.  */
		GtkWidget *label = wbcg_get_label_for_position (wbcg,
			gtk_drag_get_source_widget (context), x);
		cb_sheet_label_drag_data_received (label, context, x, y,
				selection_data, info, time, wbcg);

	} else {
		GtkWidget *source_widget = gtk_drag_get_source_widget (context);
		if (wbcg_is_local_drag (wbcg, source_widget))
			printf ("autoscroll complete - stop it\n");
		else
			scg_drag_data_received (wbcg_cur_scg (wbcg),
				source_widget, 0, 0, selection_data);
	}
	g_free (target_type);
}

static void
wbcg_create_edit_area (WBCGtk *wbcg)
{
	GtkToolItem *item;
	GtkEntry *entry;
	int len;
	GtkToolbar *tb;

	wbcg->selection_descriptor = gtk_entry_new ();
	wbc_gtk_init_editline (wbcg);
	entry = wbcg_get_entry (wbcg);

	tb = (GtkToolbar *)gtk_toolbar_new ();
	gtk_toolbar_set_show_arrow (tb, FALSE);
	gtk_toolbar_set_style (tb, GTK_TOOLBAR_ICONS);

	/* Set a reasonable width for the selection box. */
	len = go_pango_measure_string (
		gtk_widget_get_pango_context (GTK_WIDGET (wbcg_toplevel (wbcg))),
		GTK_WIDGET (entry)->style->font_desc,
		cell_coord_name (gnm_sheet_get_max_cols (NULL) - 1, gnm_sheet_get_max_rows (NULL) - 1));
	/*
	 * Add a little extra since font might be proportional and since
	 * we also put user defined names there.
	 */
	len = len * 3 / 2;
	gtk_widget_set_size_request (wbcg->selection_descriptor, len, -1);
	item = gtk_tool_item_new ();
	gtk_container_add (GTK_CONTAINER (item), wbcg->selection_descriptor);
	gtk_toolbar_insert (tb, item, -1);

	wbcg->cancel_button = edit_area_button
		(wbcg, tb, FALSE,
		 G_CALLBACK (cb_cancel_input), GTK_STOCK_CANCEL,
		 _("Cancel change"));
	wbcg->ok_button = edit_area_button
		(wbcg, tb, FALSE,
		 G_CALLBACK (cb_accept_input), GTK_STOCK_OK,
		 _("Accept change"));
	wbcg->func_button = edit_area_button
		(wbcg, tb, TRUE,
		 G_CALLBACK (cb_autofunction), "Gnumeric_Equal",
		 _("Enter formula..."));

	/* Dependency debugger */
	if (wbc_gtk_debug_deps > 0 ||
	    wbc_gtk_debug_expr_share > 0) {
		(void)edit_area_button (wbcg, tb, TRUE,
					G_CALLBACK (cb_workbook_debug_info),
					GTK_STOCK_DIALOG_INFO,
					/* Untranslated */
					"Dump debug info");
	}

	item = gtk_tool_item_new ();
	gtk_tool_item_set_expand (item, TRUE);
	gtk_container_add (GTK_CONTAINER (item),
			   GTK_WIDGET (wbcg->edit_line.entry));
	gtk_toolbar_insert (tb, item, -1);

	gtk_table_attach (GTK_TABLE (wbcg->table), GTK_WIDGET (tb),
			  0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 0, 0);

	/* Do signal setup for the editing input line */
	g_signal_connect (G_OBJECT (entry),
		"focus-in-event",
		G_CALLBACK (cb_editline_focus_in), wbcg);

	/* status box */
	g_signal_connect (G_OBJECT (wbcg->selection_descriptor),
		"activate",
		G_CALLBACK (cb_statusbox_activate), wbcg);
	g_signal_connect (G_OBJECT (wbcg->selection_descriptor),
		"focus-out-event",
		G_CALLBACK (cb_statusbox_focus), wbcg);

	gtk_widget_show_all (GTK_WIDGET (tb));
}

static int
wbcg_validation_msg (WorkbookControl *wbc, ValidationStyle v,
		     char const *title, char const *msg)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	ValidationStatus res0, res1 = VALIDATION_STATUS_VALID; /* supress warning */
	char const *btn0, *btn1;
	GtkMessageType  type;
	GtkWidget  *dialog;
	int response;

	switch (v) {
	case VALIDATION_STYLE_STOP :
		res0 = VALIDATION_STATUS_INVALID_EDIT;		btn0 = _("_Re-Edit");
		res1 = VALIDATION_STATUS_INVALID_DISCARD;	btn1 = _("_Discard");
		type = GTK_MESSAGE_ERROR;
		break;
	case VALIDATION_STYLE_WARNING :
		res0 = VALIDATION_STATUS_VALID;			btn0 = _("_Accept");
		res1 = VALIDATION_STATUS_INVALID_DISCARD;	btn1 = _("_Discard");
		type = GTK_MESSAGE_WARNING;
		break;
	case VALIDATION_STYLE_INFO :
		res0 = VALIDATION_STATUS_VALID;			btn0 = GTK_STOCK_OK;
		btn1 = NULL;
		type = GTK_MESSAGE_INFO;
		break;
	case VALIDATION_STYLE_PARSE_ERROR:
		res0 = VALIDATION_STATUS_INVALID_EDIT;		btn0 = _("_Re-Edit");
		res1 = VALIDATION_STATUS_VALID;			btn1 = _("_Accept");
		type = GTK_MESSAGE_ERROR;
		break;

	default : g_return_val_if_fail (FALSE, 1);
	}

	dialog = gtk_message_dialog_new (wbcg_toplevel (wbcg),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		type, GTK_BUTTONS_NONE, msg);
	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
		btn0, GTK_RESPONSE_YES,
		btn1, GTK_RESPONSE_NO,
		NULL);
	/* TODO : what to use if nothing is specified ? */
	/* TODO : do we want the document name here too ? */
	if (title)
		gtk_window_set_title (GTK_WINDOW (dialog), title);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_NO);
	response = go_gtk_dialog_run (GTK_DIALOG (dialog),
				      wbcg_toplevel (wbcg));
	return ((response == GTK_RESPONSE_NO || response == GTK_RESPONSE_CANCEL) ? res1 : res0);
}

#define DISCONNECT(obj,field)						\
	if (wbcg->field) {						\
		if (obj)						\
			g_signal_handler_disconnect (obj, wbcg->field);	\
		wbcg->field = 0;					\
	}

static void
wbcg_view_changed (WBCGtk *wbcg,
		   G_GNUC_UNUSED GParamSpec *pspec,
		   Workbook *old_wb)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Workbook *wb = wb_control_get_workbook (wbc);
	WorkbookView *wbv = wb_control_view (wbc);

	/* Reconnect self because we need to change data.  */
	DISCONNECT (wbc, sig_view_changed);
	wbcg->sig_view_changed =
		g_signal_connect_object
		(G_OBJECT (wbc),
		 "notify::view",
		 G_CALLBACK (wbcg_view_changed),
		 wb,
		 0);

	DISCONNECT (wbcg->sig_wbv, sig_auto_expr_text);
	if (wbcg->sig_wbv)
		g_object_remove_weak_pointer (wbcg->sig_wbv,
					      &wbcg->sig_wbv);
	wbcg->sig_wbv = wbv;
	if (wbv) {
		g_object_add_weak_pointer (wbcg->sig_wbv,
					   &wbcg->sig_wbv);
		wbcg->sig_auto_expr_text =
			g_signal_connect_object
			(G_OBJECT (wbv),
			 "notify::auto-expr-text",
			 G_CALLBACK (wbcg_auto_expr_text_changed),
			 wbcg,
			 0);
		wbcg_auto_expr_text_changed (wbv, NULL, wbcg);
	}

	DISCONNECT (old_wb, sig_sheet_order);
	DISCONNECT (old_wb, sig_notify_uri);
	DISCONNECT (old_wb, sig_notify_dirty);

	if (wb) {
		wbcg->sig_sheet_order =
			g_signal_connect_object
			(G_OBJECT (wb),
			 "sheet-order-changed",
			 G_CALLBACK (wbcg_sheet_order_changed),
			 wbcg, G_CONNECT_SWAPPED);

		wbcg->sig_notify_uri =
			g_signal_connect_object
			(G_OBJECT (wb),
			 "notify::uri",
			 G_CALLBACK (wbcg_update_title),
			 wbcg, G_CONNECT_SWAPPED);

		wbcg->sig_notify_dirty =
			g_signal_connect_object
			(G_OBJECT (wb),
			 "notify::dirty",
			 G_CALLBACK (wbcg_update_title),
			 wbcg, G_CONNECT_SWAPPED);

		wbcg_update_title (wbcg);
	}
}

#undef DISCONNECT

/***************************************************************************/

/*****************************************************************************/

static GNM_ACTION_DEF (cb_zoom_activated)
{
	WorkbookControl *wbc = (WorkbookControl *)wbcg;
	Sheet *sheet = wb_control_cur_sheet (wbc);
	char const *new_zoom = go_action_combo_text_get_entry (wbcg->zoom);
	int factor;
	char *end;

	if (sheet == NULL || wbcg->updating_ui)
		return;

	errno = 0; /* strtol sets errno, but does not clear it.  */
	factor = strtol (new_zoom, &end, 10);
	if (new_zoom != end && errno != ERANGE && factor == (gnm_float)factor)
		/* The GSList of sheet passed to cmd_zoom will be freed by cmd_zoom,
		 * and the sheet will force an update of the zoom combo to keep the
		 * display consistent
		 */
		cmd_zoom (wbc, g_slist_append (NULL, sheet), factor / 100.);
}

static void
wbc_gtk_init_zoom (WBCGtk *wbcg)
{
#warning TODO : Add zoom to selection
	static char const * const preset_zoom [] = {
		"200%",
		"150%",
		"100%",
		"75%",
		"50%",
		"25%",
		NULL
	};
	int i;

	wbcg->zoom = g_object_new (go_action_combo_text_get_type (),
				  "name", "Zoom",
				  "label", _("_Zoom"),
				  "visible-vertical", FALSE,
				  "tooltip", _("Zoom"),
				  "stock-id", GTK_STOCK_ZOOM_IN,
				  NULL);
#ifdef GNM_USE_HILDON
	go_action_combo_text_set_width (wbcg->zoom,  "100000000%");
#else
	go_action_combo_text_set_width (wbcg->zoom,  "10000%");
#endif
	for (i = 0; preset_zoom[i] != NULL ; ++i)
		go_action_combo_text_add_item (wbcg->zoom, preset_zoom[i]);

#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Foreground"));
#endif
	g_signal_connect (G_OBJECT (wbcg->zoom),
		"activate",
		G_CALLBACK (cb_zoom_activated), wbcg);
	gtk_action_group_add_action (wbcg->actions, GTK_ACTION (wbcg->zoom));
}

/****************************************************************************/

static GOActionComboPixmapsElement const border_combo_info[] = {
	{ N_("Left"),			"Gnumeric_BorderLeft",			11 },
	{ N_("Clear Borders"),		"Gnumeric_BorderNone",			12 },
	{ N_("Right"),			"Gnumeric_BorderRight",			13 },

	{ N_("All Borders"),		"Gnumeric_BorderAll",			21 },
	{ N_("Outside Borders"),	"Gnumeric_BorderOutside",		22 },
	{ N_("Thick Outside Borders"),	"Gnumeric_BorderThickOutside",		23 },

	{ N_("Bottom"),			"Gnumeric_BorderBottom",		31 },
	{ N_("Double Bottom"),		"Gnumeric_BorderDoubleBottom",		32 },
	{ N_("Thick Bottom"),		"Gnumeric_BorderThickBottom",		33 },

	{ N_("Top and Bottom"),		"Gnumeric_BorderTop_n_Bottom",		41 },
	{ N_("Top and Double Bottom"),	"Gnumeric_BorderTop_n_DoubleBottom",	42 },
	{ N_("Top and Thick Bottom"),	"Gnumeric_BorderTop_n_ThickBottom",	43 },

	{ NULL, NULL}
};

static void
cb_border_activated (GOActionComboPixmaps *a, WorkbookControl *wbc)
{
	Sheet *sheet = wb_control_cur_sheet (wbc);
	GnmBorder *borders[GNM_STYLE_BORDER_EDGE_MAX];
	int i;
	int index = go_action_combo_pixmaps_get_selected (a, NULL);

	/* Init the list */
	for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
		borders[i] = NULL;

	switch (index) {
	case 11 : /* left */
		borders[GNM_STYLE_BORDER_LEFT] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
			 sheet_style_get_auto_pattern_color (sheet),
			 gnm_style_border_get_orientation (GNM_STYLE_BORDER_LEFT));
		break;

	case 12 : /* none */
		for (i = GNM_STYLE_BORDER_TOP; i < GNM_STYLE_BORDER_EDGE_MAX; i++)
			borders[i] = gnm_style_border_ref (gnm_style_border_none ());
		break;

	case 13 : /* right */
		borders[GNM_STYLE_BORDER_RIGHT] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
			 sheet_style_get_auto_pattern_color (sheet),
			 gnm_style_border_get_orientation (GNM_STYLE_BORDER_RIGHT));
		break;

	case 21 : /* all */
		for (i = GNM_STYLE_BORDER_HORIZ; i <= GNM_STYLE_BORDER_VERT; ++i)
			borders[i] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
				sheet_style_get_auto_pattern_color (sheet),
				gnm_style_border_get_orientation (i));
		/* fall through */

	case 22 : /* outside */
		for (i = GNM_STYLE_BORDER_TOP; i <= GNM_STYLE_BORDER_RIGHT; ++i)
			borders[i] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
				sheet_style_get_auto_pattern_color (sheet),
				gnm_style_border_get_orientation (i));
		break;

	case 23 : /* thick_outside */
		for (i = GNM_STYLE_BORDER_TOP; i <= GNM_STYLE_BORDER_RIGHT; ++i)
			borders[i] = gnm_style_border_fetch (GNM_STYLE_BORDER_THICK,
				sheet_style_get_auto_pattern_color (sheet),
				gnm_style_border_get_orientation (i));
		break;

	case 41 : /* top_n_bottom */
	case 42 : /* top_n_double_bottom */
	case 43 : /* top_n_thick_bottom */
		borders[GNM_STYLE_BORDER_TOP] = gnm_style_border_fetch (GNM_STYLE_BORDER_THIN,
			sheet_style_get_auto_pattern_color (sheet),
			gnm_style_border_get_orientation (GNM_STYLE_BORDER_TOP));
	    /* Fall through */

	case 31 : /* bottom */
	case 32 : /* double_bottom */
	case 33 : /* thick_bottom */
	{
		int const tmp = index % 10;
		GnmStyleBorderType const t =
		    (tmp == 1) ? GNM_STYLE_BORDER_THIN :
		    (tmp == 2) ? GNM_STYLE_BORDER_DOUBLE
		    : GNM_STYLE_BORDER_THICK;

		borders[GNM_STYLE_BORDER_BOTTOM] = gnm_style_border_fetch (t,
			sheet_style_get_auto_pattern_color (sheet),
			gnm_style_border_get_orientation (GNM_STYLE_BORDER_BOTTOM));
		break;
	}

	default :
		g_warning ("Unknown border preset selected (%d)", index);
		return;
	}

	cmd_selection_format (wbc, NULL, borders, _("Set Borders"));
}

static void
wbc_gtk_init_borders (WBCGtk *wbcg)
{
	wbcg->borders = go_action_combo_pixmaps_new ("BorderSelector", border_combo_info, 3, 4);
	g_object_set (G_OBJECT (wbcg->borders),
		      "label", _("Borders"),
		      "tooltip", _("Borders"),
		      "visible-vertical", FALSE,
		      NULL);
	/* TODO: Create vertical version.  */
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Foreground"));
	go_combo_pixmaps_select (wbcg->borders, 1); /* default to none */
#endif
	g_signal_connect (G_OBJECT (wbcg->borders),
		"activate",
		G_CALLBACK (cb_border_activated), wbcg);
	gtk_action_group_add_action (wbcg->actions, GTK_ACTION (wbcg->borders));
}

/****************************************************************************/

static GOActionComboStack *
ur_stack (WorkbookControl *wbc, gboolean is_undo)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	return is_undo ? wbcg->undo_haction : wbcg->redo_haction;
}

static void
wbc_gtk_undo_redo_truncate (WorkbookControl *wbc, int n, gboolean is_undo)
{
	go_action_combo_stack_truncate (ur_stack (wbc, is_undo), n);
}

static void
wbc_gtk_undo_redo_pop (WorkbookControl *wbc, gboolean is_undo)
{
	go_action_combo_stack_pop (ur_stack (wbc, is_undo), 1);
}

static void
wbc_gtk_undo_redo_push (WorkbookControl *wbc, gboolean is_undo,
			char const *text, gpointer key)
{
	go_action_combo_stack_push (ur_stack (wbc, is_undo), text, key);
}

static void
cb_chain_sensitivity (GtkAction *src, G_GNUC_UNUSED GParamSpec *pspec,
		      GtkAction *action)
{
	gboolean old_val = gtk_action_get_sensitive (action);
	gboolean new_val = gtk_action_get_sensitive (src);
	if ((new_val != 0) == (old_val != 0))
		return;
	if (new_val)
		gtk_action_connect_accelerator (action);
	else
		gtk_action_disconnect_accelerator (action);
	g_object_set (action, "sensitive", new_val, NULL);
}

static void
create_undo_redo (GOActionComboStack **haction, char const *hname,
		  GCallback hcb,
		  GtkAction **vaction, char const *vname,
		  GCallback vcb,
		  WBCGtk *gtk,
		  char const *tooltip,
		  char const *stock_id, char const *accel)
{
	*haction = g_object_new
		(go_action_combo_stack_get_type (),
		 "name", hname,
		 "tooltip", tooltip,
		 "stock-id", stock_id,
		 "sensitive", FALSE,
		 "visible-vertical", FALSE,
		 NULL);
	gtk_action_group_add_action_with_accel (gtk->actions,
		GTK_ACTION (*haction), accel);
	g_signal_connect (G_OBJECT (*haction), "activate", hcb, gtk);

	*vaction = gtk_action_new (vname, NULL, tooltip, stock_id);
	g_object_set (G_OBJECT (*vaction),
		      "sensitive", FALSE,
		      "visible-horizontal", FALSE,
		      NULL);
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (*vaction));
	g_signal_connect_swapped (G_OBJECT (*vaction), "activate", vcb, gtk);

	g_signal_connect (G_OBJECT (*haction), "notify::sensitive",
		G_CALLBACK (cb_chain_sensitivity), *vaction);
}


static void
cb_undo_activated (GOActionComboStack *a, WorkbookControl *wbc)
{
	unsigned n = workbook_find_command (wb_control_get_workbook (wbc), TRUE,
		go_action_combo_stack_selection (a));
	while (n-- > 0)
		command_undo (wbc);
}

static void
cb_redo_activated (GOActionComboStack *a, WorkbookControl *wbc)
{
	unsigned n = workbook_find_command (wb_control_get_workbook (wbc), FALSE,
		go_action_combo_stack_selection (a));
	while (n-- > 0)
		command_redo (wbc);
}

static void
wbc_gtk_init_undo_redo (WBCGtk *gtk)
{
	create_undo_redo (
		&gtk->redo_haction, "Redo", G_CALLBACK (cb_redo_activated),
		&gtk->redo_vaction, "VRedo", G_CALLBACK (command_redo),
		gtk, _("Redo the undone action"),
		GTK_STOCK_REDO, "<control>y");
	create_undo_redo (
		&gtk->undo_haction, "Undo", G_CALLBACK (cb_undo_activated),
		&gtk->undo_vaction, "VUndo", G_CALLBACK (command_undo),
		gtk, _("Undo the last action"),
		GTK_STOCK_UNDO, "<control>z");
}

/****************************************************************************/

static void
cb_custom_color_created (GOActionComboColor *caction, GtkWidget *dialog, WBCGtk *wbcg)
{
	wbc_gtk_attach_guru (wbcg, dialog);
	wbcg_set_transient (wbcg, GTK_WINDOW (dialog));
}

static void
cb_fore_color_changed (GOActionComboColor *a, WBCGtk *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmStyle *mstyle;
	GOColor   c;
	gboolean  is_default;

	if (wbcg->updating_ui)
		return;
	c = go_action_combo_color_get_color (a, &is_default);

	if (wbcg_is_editing (wbcg)) {
		GnmColor *color = style_color_new_go (is_default ? RGBA_BLACK : c);
		wbcg_edit_add_markup (wbcg, pango_attr_foreground_new (
			color->gdk_color.red, color->gdk_color.green, color->gdk_color.blue));
		style_color_unref (color);
		return;
	}

	mstyle = gnm_style_new ();
	gnm_style_set_font_color (mstyle, is_default
		? style_color_auto_font ()
		: style_color_new_go (c));
	cmd_selection_format (wbc, mstyle, NULL, _("Set Foreground Color"));
}

static void
wbc_gtk_init_color_fore (WBCGtk *gtk)
{
	GnmColor *sc_auto_font = style_color_auto_font ();
	GOColor   default_color = GDK_TO_UINT(sc_auto_font->gdk_color);
	style_color_unref (sc_auto_font);

	gtk->fore_color = go_action_combo_color_new ("ColorFore", "font",
		_("Automatic"),	default_color, NULL); /* set group to view */
	g_object_set (G_OBJECT (gtk->fore_color),
		      "label", _("Foreground"),
		      "tooltip", _("Foreground"),
		      "visible-vertical", FALSE,
		      NULL);
	/* TODO: Create vertical version.  */
	g_signal_connect (G_OBJECT (gtk->fore_color),
		"activate",
		G_CALLBACK (cb_fore_color_changed), gtk);
	g_signal_connect (G_OBJECT (gtk->fore_color),
		"display-custom-dialog",
		G_CALLBACK (cb_custom_color_created), gtk);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Foreground"));
#endif
	gtk_action_group_add_action (gtk->font_actions,
		GTK_ACTION (gtk->fore_color));
}
/****************************************************************************/

static void
cb_back_color_changed (GOActionComboColor *a, WBCGtk *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmStyle *mstyle;
	GOColor   c;
	gboolean  is_default;

	if (wbcg->updating_ui)
		return;

	c = go_action_combo_color_get_color (a, &is_default);

	mstyle = gnm_style_new ();
	if (!is_default) {
		/* We need to have a pattern of at least solid to draw a background colour */
		if (!gnm_style_is_element_set  (mstyle, MSTYLE_PATTERN) ||
		    gnm_style_get_pattern (mstyle) < 1)
			gnm_style_set_pattern (mstyle, 1);

		gnm_style_set_back_color (mstyle, style_color_new_go (c));
	} else
		gnm_style_set_pattern (mstyle, 0);	/* Set background to NONE */
	cmd_selection_format (wbc, mstyle, NULL, _("Set Background Color"));
}

static void
wbc_gtk_init_color_back (WBCGtk *gtk)
{
	gtk->back_color = go_action_combo_color_new ("ColorBack", "bucket",
		_("Clear Background"), 0, NULL);
	g_object_set (G_OBJECT (gtk->back_color),
		      "label", _("Background"),
		      "tooltip", _("Background"),
		      "visible-vertical", FALSE,
		      NULL);
	/* TODO: Create vertical version.  */
	g_object_connect (G_OBJECT (gtk->back_color),
		"signal::activate", G_CALLBACK (cb_back_color_changed), gtk,
		"signal::display-custom-dialog", G_CALLBACK (cb_custom_color_created), gtk,
		NULL);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (back_combo), _("Background"));
#endif
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->back_color));
}
/****************************************************************************/

static void
cb_font_name_changed (GOActionComboText *a, WBCGtk *gtk)
{
	char const *new_name = go_action_combo_text_get_entry (gtk->font_name);

	while (g_ascii_isspace (*new_name))
		++new_name;

	if (*new_name) {
		if (wbcg_is_editing (WBC_GTK (gtk))) {
			wbcg_edit_add_markup (WBC_GTK (gtk),
				pango_attr_family_new (new_name));
		} else {
			GnmStyle *style = gnm_style_new ();
			char *title = g_strdup_printf (_("Font Name %s"), new_name);
			gnm_style_set_font_name (style, new_name);
			cmd_selection_format (WORKBOOK_CONTROL (gtk), style, NULL, title);
			g_free (title);
		}
	} else
		wb_control_style_feedback (WORKBOOK_CONTROL (gtk), NULL);

}

static void
wbc_gtk_init_font_name (WBCGtk *gtk)
{
	PangoContext *context;
	GSList *ptr, *families;

	gtk->font_name = g_object_new (go_action_combo_text_get_type (),
				       "name", "FontName",
				       "case-sensitive", FALSE,
				       "stock-id", GTK_STOCK_SELECT_FONT,
				       "visible-vertical", FALSE,
				       "tooltip", _("Font"),
				       NULL);

	/* TODO: Create vertical version of this.  */

	context = gtk_widget_get_pango_context
		(GTK_WIDGET (wbcg_toplevel (WBC_GTK (gtk))));
	families = go_fonts_list_families (context);
	for (ptr = families; ptr != NULL; ptr = ptr->next)
		go_action_combo_text_add_item (gtk->font_name, ptr->data);
	go_slist_free_custom (families, (GFreeFunc)g_free);

	g_signal_connect (G_OBJECT (gtk->font_name),
		"activate",
		G_CALLBACK (cb_font_name_changed), gtk);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Foreground"));
#endif
	gtk_action_group_add_action (gtk->font_actions,
				     GTK_ACTION (gtk->font_name));
}
/****************************************************************************/

static void
cb_font_size_changed (GOActionComboText *a, WBCGtk *gtk)
{
	char const *new_size = go_action_combo_text_get_entry (gtk->font_size);
	char *end;
	double size;

	size = go_strtod (new_size, &end);
	size = floor ((size * 20.) + .5) / 20.;	/* round .05 */

	if (new_size != end && errno != ERANGE && 1. <= size && size <= 400.) {
		if (wbcg_is_editing (WBC_GTK (gtk))) {
			wbcg_edit_add_markup (WBC_GTK (gtk),
				pango_attr_size_new (size * PANGO_SCALE));
		} else {
			GnmStyle *style = gnm_style_new ();
			char *title = g_strdup_printf (_("Font Size %f"), size);
			gnm_style_set_font_size (style, size);
			cmd_selection_format (WORKBOOK_CONTROL (gtk), style, NULL, title);
			g_free (title);
		}
	} else
		wb_control_style_feedback (WORKBOOK_CONTROL (gtk), NULL);
}

static void
wbc_gtk_init_font_size (WBCGtk *gtk)
{
	GSList *ptr, *font_sizes;

	gtk->font_size = g_object_new (go_action_combo_text_get_type (),
				       "name", "FontSize",
				       "stock-id", GTK_STOCK_SELECT_FONT,
				       "visible-vertical", FALSE,
				       "label", _("Font Size"),
				       "tooltip", _("Font Size"),
				       NULL);

	/* TODO: Create vertical version of this.  */

	font_sizes = go_fonts_list_sizes ();
	for (ptr = font_sizes; ptr != NULL ; ptr = ptr->next) {
		int psize = GPOINTER_TO_INT (ptr->data);
		char *size_text = g_strdup_printf ("%g", psize / (double)PANGO_SCALE);
		go_action_combo_text_add_item (gtk->font_size, size_text);
		g_free (size_text);
	}
	g_slist_free (font_sizes);
#ifdef GNM_USE_HILDON
	go_action_combo_text_set_width (gtk->font_size,  "888888");
#else
	go_action_combo_text_set_width (gtk->font_size, "888");
#endif
	g_signal_connect (G_OBJECT (gtk->font_size),
		"activate",
		G_CALLBACK (cb_font_size_changed), gtk);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Foreground"));
#endif
	gtk_action_group_add_action (gtk->font_actions,
		GTK_ACTION (gtk->font_size));
}
static WorkbookControl *
wbc_gtk_control_new (G_GNUC_UNUSED WorkbookControl *wbc,
		     WorkbookView *wbv,
		     Workbook *wb,
		     gpointer extra)
{
	return (WorkbookControl *)wbc_gtk_new (wbv, wb,
		extra ? GDK_SCREEN (extra) : NULL, NULL);
}

static void
wbc_gtk_init_state (WorkbookControl *wbc)
{
	WorkbookView *wbv  = wb_control_view (wbc);
	WBCGtk       *wbcg = WBC_GTK (wbc);

	/* Share a colour history for all a view's controls */
	go_action_combo_color_set_group (wbcg->back_color, wbv);
	go_action_combo_color_set_group (wbcg->fore_color, wbv);
}

static void
wbc_gtk_style_feedback_real (WorkbookControl *wbc, GnmStyle const *changes)
{
	WorkbookView	*wb_view = wb_control_view (wbc);
	WBCGtk		*wbcg = (WBCGtk *)wbc;

	g_return_if_fail (wb_view != NULL);

	if (!wbcg_ui_update_begin (WBC_GTK (wbc)))
		return;

	if (changes == NULL)
		changes = wb_view->current_style;

	if (gnm_style_is_element_set (changes, MSTYLE_FONT_BOLD))
		gtk_toggle_action_set_active (wbcg->font.bold,
			gnm_style_get_font_bold (changes));
	if (gnm_style_is_element_set (changes, MSTYLE_FONT_ITALIC))
		gtk_toggle_action_set_active (wbcg->font.italic,
			gnm_style_get_font_italic (changes));
	if (gnm_style_is_element_set (changes, MSTYLE_FONT_UNDERLINE)) {
		gtk_toggle_action_set_active (wbcg->font.underline,
			gnm_style_get_font_uline (changes) == UNDERLINE_SINGLE);
		gtk_toggle_action_set_active (wbcg->font.d_underline,
			gnm_style_get_font_uline (changes) == UNDERLINE_DOUBLE);
	}
	if (gnm_style_is_element_set (changes, MSTYLE_FONT_STRIKETHROUGH))
		gtk_toggle_action_set_active (wbcg->font.strikethrough,
			gnm_style_get_font_strike (changes));

	if (gnm_style_is_element_set (changes, MSTYLE_FONT_SCRIPT)) {
		gtk_toggle_action_set_active (wbcg->font.superscript,
			gnm_style_get_font_script (changes) == GO_FONT_SCRIPT_SUPER);
		gtk_toggle_action_set_active (wbcg->font.subscript,
			gnm_style_get_font_script (changes) == GO_FONT_SCRIPT_SUB);
	}

	if (gnm_style_is_element_set (changes, MSTYLE_ALIGN_H)) {
		GnmHAlign align = gnm_style_get_align_h (changes);
		gtk_toggle_action_set_active (wbcg->h_align.left,
			align == HALIGN_LEFT);
		gtk_toggle_action_set_active (wbcg->h_align.center,
			align == HALIGN_CENTER);
		gtk_toggle_action_set_active (wbcg->h_align.right,
			align == HALIGN_RIGHT);
		gtk_toggle_action_set_active (wbcg->h_align.center_across_selection,
			align == HALIGN_CENTER_ACROSS_SELECTION);
		go_action_combo_pixmaps_select_id (wbcg->halignment, align);
	}
	if (gnm_style_is_element_set (changes, MSTYLE_ALIGN_V)) {
		GnmVAlign align = gnm_style_get_align_v (changes);
		gtk_toggle_action_set_active (wbcg->v_align.top,
			align == VALIGN_TOP);
		gtk_toggle_action_set_active (wbcg->v_align.bottom,
			align == VALIGN_BOTTOM);
		gtk_toggle_action_set_active (wbcg->v_align.center,
			align == VALIGN_CENTER);
		go_action_combo_pixmaps_select_id (wbcg->valignment, align);
	}

	if (gnm_style_is_element_set (changes, MSTYLE_FONT_SIZE)) {
		char *size_str = g_strdup_printf ("%d", (int)gnm_style_get_font_size (changes));
		go_action_combo_text_set_entry (wbcg->font_size,
			size_str, GO_ACTION_COMBO_SEARCH_FROM_TOP);
		g_free (size_str);
	}

	if (gnm_style_is_element_set (changes, MSTYLE_FONT_NAME))
		go_action_combo_text_set_entry (wbcg->font_name,
			gnm_style_get_font_name (changes), GO_ACTION_COMBO_SEARCH_FROM_TOP);

	wbcg_ui_update_end (WBC_GTK (wbc));
}

static gint
cb_wbc_gtk_style_feedback (WBCGtk *gtk)
{
	wbc_gtk_style_feedback_real ((WorkbookControl *)gtk, NULL);
	gtk->idle_update_style_feedback = 0;
	return FALSE;
}
static void
wbc_gtk_style_feedback (WorkbookControl *wbc, GnmStyle const *changes)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;

	if (changes)
		wbc_gtk_style_feedback_real (wbc, changes);
	else if (0 == wbcg->idle_update_style_feedback)
		wbcg->idle_update_style_feedback = g_timeout_add (200,
			(GSourceFunc) cb_wbc_gtk_style_feedback, wbc);
}

static void
cb_handlebox_dock_status (GtkHandleBox *hb,
			  GtkToolbar *toolbar, gpointer pattached)
{
	gboolean attached = GPOINTER_TO_INT (pattached);
	GtkWidget *box = GTK_WIDGET (hb);

	/* BARF!  */
	/* See http://bugzilla.gnome.org/show_bug.cgi?id=139184  */
	GtkStyle *style = gtk_style_copy (box->style);
	style->ythickness = attached ? 2 : 0;
	gtk_widget_set_style (box, style);
	g_object_unref (style);

	gtk_toolbar_set_show_arrow (toolbar, attached);
}

static char const *
get_accel_label (GtkMenuItem *item, guint *key)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (item));
	GList *l;
	char const *res = NULL;

	*key = GDK_VoidSymbol;
	for (l = children; l; l = l->next) {
		GtkWidget *w = l->data;

		if (GTK_IS_ACCEL_LABEL (w)) {
			*key = gtk_label_get_mnemonic_keyval (GTK_LABEL (w));
			res = gtk_label_get_label (GTK_LABEL (w));
			break;
		}
	}

	g_list_free (children);
	return res;
}

static void
check_underlines (GtkWidget *w, char const *path)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (w));
	GHashTable *used = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)g_free);
	GList *l;

	for (l = children; l; l = l->next) {
		GtkMenuItem *item = GTK_MENU_ITEM (l->data);
		GtkWidget *sub = gtk_menu_item_get_submenu (item);
		guint key;
		char const *label = get_accel_label (item, &key);

		if (sub) {
			char *newpath = g_strconcat (path, *path ? "->" : "", label, NULL);
			check_underlines (sub, newpath);
			g_free (newpath);
		}

		if (key != GDK_VoidSymbol) {
			char const *prev = g_hash_table_lookup (used, GUINT_TO_POINTER (key));
			if (prev) {
				/* xgettext: Translators: if this warning shows up when
				 * running Gnumeric in your locale, the underlines need
				 * to be moved in strings representing menu entries.
				 * One slightly tricky point here is that in certain cases,
				 * the same menu entry shows up in more than one menu.
				 */
				g_warning (_("In the `%s' menu, the key `%s' is used for both `%s' and `%s'."),
					   path, gdk_keyval_name (key), prev, label);
			} else
				g_hash_table_insert (used, GUINT_TO_POINTER (key), g_strdup (label));
		}
	}

	g_list_free (children);
	g_hash_table_destroy (used);
}

/****************************************************************************/
/* window list menu */

static void
cb_window_menu_activate (GObject *action, WBCGtk *wbcg)
{
	gtk_window_present (wbcg_toplevel (wbcg));
}

static unsigned
regenerate_window_menu (WBCGtk *gtk, Workbook *wb, unsigned i)
{
	int k, count;

	/* How many controls are there?  */
	count = 0;
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc, {
		if (IS_WBC_GTK (wbc))
			count++;
	});

	k = 1;
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc, {
		char *basename;
		if (i >= 20)
			return i;
		if (IS_WBC_GTK (wbc) &&
			(basename = go_basename_from_uri (GO_DOC (wb)->uri)) != NULL) {
			GString *label = g_string_new (NULL);
			char *name;
			char const *s;
			GtkActionEntry entry;

			if (i < 10) g_string_append_c (label, '_');
			g_string_append_printf (label, "%d ", i);

			for (s = basename; *s; s++) {
				if (*s == '_')
					g_string_append_c (label, '_');
				g_string_append_c (label, *s);
			}

			if (count > 1)
				g_string_append_printf (label, " #%d", k++);

			entry.name = name = g_strdup_printf ("WindowListEntry%d", i);
			entry.stock_id = NULL;
			entry.label = label->str;
			entry.accelerator = NULL;
			entry.tooltip = NULL;
			entry.callback = G_CALLBACK (cb_window_menu_activate);

			gtk_action_group_add_actions (gtk->windows.actions,
				&entry, 1, wbc);

			g_string_free (label, TRUE);
			g_free (name);
			g_free (basename);
			i++;
		}});
	return i;
}

static void
cb_regenerate_window_menu (WBCGtk *gtk)
{
	Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (gtk));
	GList const *ptr;
	unsigned i;

	/* This can happen during exit.  */
	if (!wb)
		return;

	if (gtk->windows.merge_id != 0)
		gtk_ui_manager_remove_ui (gtk->ui, gtk->windows.merge_id);
	gtk->windows.merge_id = gtk_ui_manager_new_merge_id (gtk->ui);

	if (gtk->windows.actions != NULL)
		g_object_unref (gtk->windows.actions);
	gtk->windows.actions = gtk_action_group_new ("WindowList");

	gtk_ui_manager_insert_action_group (gtk->ui, gtk->windows.actions, 0);

	/* create the actions */
	i = regenerate_window_menu (gtk, wb, 1); /* current wb first */
	for (ptr = gnm_app_workbook_list (); ptr != NULL ; ptr = ptr->next)
		if (ptr->data != wb)
			i = regenerate_window_menu (gtk, ptr->data, i);

	/* merge them in */
	while (i-- > 1) {
		char *name = g_strdup_printf ("WindowListEntry%d", i);
		gtk_ui_manager_add_ui (gtk->ui, gtk->windows.merge_id,
#ifdef GNM_USE_HILDON
			"/popup/View/Windows",
#else
			"/menubar/View/Windows",
#endif
			name, name, GTK_UI_MANAGER_AUTO, TRUE);
		g_free (name);
	}
}

typedef struct {
	GtkActionGroup *actions;
	guint		merge_id;
} CustomUIHandle;

static void
cb_custom_ui_handler (GObject *gtk_action, WorkbookControl *wbc)
{
	GnmAction *action = g_object_get_data (gtk_action, "GnmAction");
	GnmAppExtraUI *extra_ui = g_object_get_data (gtk_action, "ExtraUI");

	g_return_if_fail (action != NULL);
	g_return_if_fail (action->handler != NULL);
	g_return_if_fail (extra_ui != NULL);

	action->handler (action, wbc, extra_ui->user_data);
}

static void
cb_add_custom_ui (G_GNUC_UNUSED GnmApp *app,
		  GnmAppExtraUI *extra_ui, WBCGtk *gtk)
{
	GtkActionEntry   entry;
	CustomUIHandle  *details;
	GSList		*ptr;
	GnmAction	*action;
	GtkAction       *res;

	details = g_new0 (CustomUIHandle, 1);
	details->actions = gtk_action_group_new ("DummyName");

	for (ptr = extra_ui->actions; ptr != NULL ; ptr = ptr->next) {
		action = ptr->data;
		entry.name = action->id;
		entry.stock_id = action->icon_name;
		entry.label = action->label;
		entry.accelerator = NULL;
		entry.tooltip = NULL;
		entry.callback = G_CALLBACK (cb_custom_ui_handler);
		gtk_action_group_add_actions (details->actions, &entry, 1, gtk);
		res = gtk_action_group_get_action (details->actions, action->id);
		g_object_set_data (G_OBJECT (res), "GnmAction", action);
		g_object_set_data (G_OBJECT (res), "ExtraUI", extra_ui);
	}
	gtk_ui_manager_insert_action_group (gtk->ui, details->actions, 0);
	details->merge_id = gtk_ui_manager_add_ui_from_string (gtk->ui,
		extra_ui->layout, -1, NULL);

	g_hash_table_insert (gtk->custom_uis, extra_ui, details);
}
static void
cb_remove_custom_ui (G_GNUC_UNUSED GnmApp *app,
		     GnmAppExtraUI *extra_ui, WBCGtk *gtk)
{
	CustomUIHandle *details = g_hash_table_lookup (gtk->custom_uis, extra_ui);
	if (NULL != details) {
		gtk_ui_manager_remove_ui (gtk->ui, details->merge_id);
		gtk_ui_manager_remove_action_group (gtk->ui, details->actions);
		g_object_unref (details->actions);
		g_hash_table_remove (gtk->custom_uis, extra_ui);
	}
}

static void
cb_init_extra_ui (GnmAppExtraUI *extra_ui, WBCGtk *gtk)
{
	cb_add_custom_ui (NULL, extra_ui, gtk);
}

/****************************************************************************/
/* Toolbar menu */

static void
set_toolbar_style_for_position (GtkToolbar *tb, GtkPositionType pos)
{
	GtkWidget *box = GTK_WIDGET (tb)->parent;

	static const GtkOrientation orientations[] = {
		GTK_ORIENTATION_VERTICAL, GTK_ORIENTATION_VERTICAL,
		GTK_ORIENTATION_HORIZONTAL, GTK_ORIENTATION_HORIZONTAL
	};

	gtk_toolbar_set_orientation (tb, orientations[pos]);

	if (GTK_IS_HANDLE_BOX (box)) {
		static const GtkPositionType hdlpos[] = {
			GTK_POS_TOP, GTK_POS_TOP,
			GTK_POS_LEFT, GTK_POS_LEFT
		};

		gtk_handle_box_set_handle_position (GTK_HANDLE_BOX (box),
						    hdlpos[pos]);
	}
}

static void
set_toolbar_position (GtkToolbar *tb, GtkPositionType pos, WBCGtk *gtk)
{
	GtkWidget *box = GTK_WIDGET (tb)->parent;
	GtkContainer *zone = GTK_CONTAINER (GTK_WIDGET (box)->parent);
	GtkContainer *new_zone = GTK_CONTAINER (gtk->toolbar_zones[pos]);
	char const *name = g_object_get_data (G_OBJECT (box), "name");

	if (zone == new_zone)
		return;

	g_object_ref (box);
	if (zone)
		gtk_container_remove (zone, box);
	set_toolbar_style_for_position (tb, pos);
	gtk_container_add (new_zone, box);
	g_object_unref (box);

	if (zone)
		gnm_gconf_set_toolbar_position (name, pos);
}

static void
cb_set_toolbar_position (GtkMenuItem *item, WBCGtk *gtk)
{
	GtkToolbar *tb = g_object_get_data (G_OBJECT (item), "toolbar");
	GtkPositionType side = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "side"));

	set_toolbar_position (tb, side, gtk);
}

static void
cb_tcm_reattach (GtkWidget *widget, GtkHandleBox *hdlbox)
{
	GdkEvent *event = gdk_event_new (GDK_DELETE);
	event->any.type = GDK_DELETE;
	event->any.window = g_object_ref (hdlbox->float_window);
	event->any.send_event = TRUE;
	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void
cb_tcm_hide (GtkWidget *widget, GtkHandleBox *hdlbox)
{
	if (hdlbox->child_detached)
		cb_tcm_reattach (widget, hdlbox);
	gtk_widget_hide (GTK_WIDGET (hdlbox));
}

static void
toolbar_context_menu (GtkToolbar *tb, WBCGtk *gtk, GdkEventButton *event_button)
{
	GtkHandleBox *hdlbox = GTK_HANDLE_BOX (GTK_WIDGET (tb)->parent);
	GtkWidget *zone = GTK_WIDGET (hdlbox)->parent;
	GtkWidget *menu = gtk_menu_new ();
	GtkWidget *item;

	static struct {
		char const *text;
		GtkPositionType pos;
	} const pos_items[] = {
		{ N_("Display above sheets"), GTK_POS_TOP },
		{ N_("Display to the left of sheets"), GTK_POS_LEFT },
		{ N_("Display to the right of sheets"), GTK_POS_RIGHT }
	};

	if (hdlbox->child_detached) {
		item = gtk_menu_item_new_with_label (_("Reattach to main window"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (cb_tcm_reattach),
				  hdlbox);
	} else {
		size_t ui;
		GSList *group = NULL;

		for (ui = 0; ui < G_N_ELEMENTS (pos_items); ui++) {
			char const *text = _(pos_items[ui].text);
			GtkPositionType pos = pos_items[ui].pos;

			item = gtk_radio_menu_item_new_with_label (group, text);
			group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));

			GTK_CHECK_MENU_ITEM (item)->active =
				(zone == gtk->toolbar_zones[pos]);

			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			g_object_set_data (G_OBJECT (item), "toolbar", tb);
			g_object_set_data (G_OBJECT (item), "side", GINT_TO_POINTER (pos));
			g_signal_connect (G_OBJECT (item), "activate",
					  G_CALLBACK (cb_set_toolbar_position),
					  gtk);
		}
	}

	item = gtk_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_set_sensitive (item, FALSE);

	item = gtk_menu_item_new_with_label (_("Hide"));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (cb_tcm_hide),
			  hdlbox);

	gtk_widget_show_all (menu);
	gnumeric_popup_menu (GTK_MENU (menu), event_button);
}

static gboolean
cb_toolbar_button_press (GtkToolbar *tb, GdkEventButton *event, WBCGtk *gtk)
{
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		toolbar_context_menu (tb, gtk, event);
		return TRUE;
	}

	return FALSE;
}

static gboolean
cb_handlebox_button_press (GtkHandleBox *hdlbox, GdkEventButton *event, WBCGtk *gtk)
{
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		GtkToolbar *tb = GTK_TOOLBAR (gtk_bin_get_child (GTK_BIN (hdlbox)));
		toolbar_context_menu (tb, gtk, event);
		return TRUE;
	}

	return FALSE;
}


static void
cb_toolbar_activate (GtkToggleAction *action, WBCGtk *wbcg)
{
	wbcg_toggle_visibility (wbcg, action);
}

static void
cb_toolbar_box_visible (GtkWidget *box, G_GNUC_UNUSED GParamSpec *pspec,
			WBCGtk *wbcg)
{
	GtkToggleAction *toggle_action = g_object_get_data (
		G_OBJECT (box), "toggle_action");
	char const *name = g_object_get_data (G_OBJECT (box), "name");
	gboolean visible = GTK_WIDGET_VISIBLE (box);

	gtk_toggle_action_set_active (toggle_action, visible);
	gnm_gconf_set_toolbar_visible (name, visible);
}

static void
cb_add_menus_toolbars (G_GNUC_UNUSED GtkUIManager *ui,
		       GtkWidget *w, WBCGtk *gtk)
{
	if (GTK_IS_TOOLBAR (w)) {
		WBCGtk *wbcg = (WBCGtk *)gtk;
		char const *name = gtk_widget_get_name (w);
		GtkToggleActionEntry entry;
		char *toggle_name = g_strconcat ("ViewMenuToolbar", name, NULL);
		char *tooltip = g_strdup_printf (_("Show/Hide toolbar %s"), _(name));
		gboolean visible = gnm_gconf_get_toolbar_visible (name);

#ifdef GNM_USE_HILDON
		hildon_window_add_toolbar (HILDON_WINDOW (wbcg_toplevel (wbcg)), GTK_TOOLBAR (w));

		gtk_widget_show_all (w);
		g_hash_table_insert (wbcg->visibility_widgets,
			g_strdup (toggle_name), g_object_ref (w));
#else
		GtkWidget *box;
		GtkPositionType pos = gnm_gconf_get_toolbar_position (name);
		gboolean toolbars_can_detach = TRUE;
#ifdef GNM_WITH_GNOME
		toolbars_can_detach = go_conf_get_bool (NULL, "/desktop/gnome/interface/toolbar_detachable");
#endif
		if (toolbars_can_detach) {
			box = gtk_handle_box_new ();
			g_object_connect (box,
				"signal::child_attached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (TRUE),
				"signal::child_detached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (FALSE),
				NULL);
		} else
			box = gtk_hbox_new (FALSE, 2);
		g_signal_connect (G_OBJECT (w),
				  "button_press_event",
				  G_CALLBACK (cb_toolbar_button_press),
				  gtk);
		g_signal_connect (G_OBJECT (box),
				  "button_press_event",
				  G_CALLBACK (cb_handlebox_button_press),
				  gtk);

		gtk_container_add (GTK_CONTAINER (box), w);
		gtk_widget_show_all (box);
		if (!visible)
			gtk_widget_hide (box);
		set_toolbar_position (GTK_TOOLBAR (w), pos, gtk);

		g_signal_connect (box,
				  "notify::visible",
				  G_CALLBACK (cb_toolbar_box_visible),
				  gtk);
		g_object_set_data_full (G_OBJECT (box), "name",
					g_strdup (name),
					(GDestroyNotify)g_free);

		g_hash_table_insert (wbcg->visibility_widgets,
			g_strdup (toggle_name), g_object_ref (box));
#endif
		gtk_toolbar_set_show_arrow (GTK_TOOLBAR (w), TRUE);
		gtk_toolbar_set_style (GTK_TOOLBAR (w), GTK_TOOLBAR_ICONS);

		entry.name = toggle_name;
		entry.stock_id = NULL;
		entry.label = _(name);
		entry.accelerator = (0 == strcmp (name, "StandardToolbar")) ? "<control>7" : NULL;
		entry.tooltip = tooltip;
		entry.callback = G_CALLBACK (cb_toolbar_activate);
		entry.is_active = visible;
		gtk_action_group_add_toggle_actions (gtk->toolbar.actions,
			&entry, 1, wbcg);
		g_object_set_data (G_OBJECT (box), "toggle_action",
			gtk_action_group_get_action (gtk->toolbar.actions, toggle_name));
		gtk_ui_manager_add_ui (gtk->ui, gtk->toolbar.merge_id,
#ifdef GNM_USE_HILDON
			"/popup/View/Toolbars",
#else
			"/menubar/View/Toolbars",
#endif
			toggle_name, toggle_name, GTK_UI_MANAGER_AUTO, FALSE);
		g_hash_table_insert (wbcg->toggle_for_fullscreen,
			g_strdup (toggle_name),
			gtk_action_group_get_action (gtk->toolbar.actions,
						     toggle_name));

		g_free (tooltip);
		g_free (toggle_name);
	} else {
		gtk_box_pack_start (GTK_BOX (gtk->menu_zone), w, FALSE, TRUE, 0);
		gtk_widget_show_all (w);
	}
}

static void
cb_clear_menu_tip (GOCmdContext *cc)
{
	go_cmd_context_progress_message_set (cc, " ");
}

static void
cb_show_menu_tip (GtkWidget *proxy, GOCmdContext *cc)
{
	GtkAction *action = g_object_get_data (G_OBJECT (proxy), "GtkAction");
	char *tip = NULL;
	g_object_get (action, "tooltip", &tip, NULL);
	if (tip) {
		go_cmd_context_progress_message_set (cc, _(tip));
		g_free (tip);
	} else
		cb_clear_menu_tip (cc);
}

static void
cb_connect_proxy (G_GNUC_UNUSED GtkUIManager *ui,
		  GtkAction    *action,
		  GtkWidget    *proxy,
		  GOCmdContext *cc)
{
	/* connect whether there is a tip or not it may change later */
	if (GTK_IS_MENU_ITEM (proxy)) {
		g_object_set_data (G_OBJECT (proxy), "GtkAction", action);
		g_object_connect (proxy,
			"signal::select",  G_CALLBACK (cb_show_menu_tip), cc,
			"swapped_signal::deselect", G_CALLBACK (cb_clear_menu_tip), cc,
			NULL);
	}
}

static void
cb_disconnect_proxy (G_GNUC_UNUSED GtkUIManager *ui,
		     G_GNUC_UNUSED GtkAction    *action,
		     GtkWidget    *proxy,
		     GOCmdContext *cc)
{
	if (GTK_IS_MENU_ITEM (proxy)) {
		g_object_set_data (G_OBJECT (proxy), "GtkAction", NULL);
		g_object_disconnect (proxy,
			"any_signal::select",  G_CALLBACK (cb_show_menu_tip), cc,
			"any_signal::deselect", G_CALLBACK (cb_clear_menu_tip), cc,
			NULL);
	}
}

static void
cb_post_activate (WBCGtk *wbcg)
{
	if (!wbcg_is_editing (wbcg))
		wbcg_focus_cur_scg (wbcg);
}

static void
cb_toggle_visibility (char const *name,
		      GtkToggleAction *action, WBCGtk *wbcg)
{
	wbc_gtk_set_toggle_action_state (wbcg, name,
		!gtk_toggle_action_get_active (action));
}

static void
cb_wbcg_window_state_event (GtkWidget           *widget,
			    GdkEventWindowState *event,
			    WBCGtk  *wbcg)
{
	GHashTable *tmp = wbcg->toggle_for_fullscreen;
	gboolean new_val = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
	if (!(event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) ||
	    new_val == wbcg->is_fullscreen ||
	    wbcg->updating_ui)
		return;

	wbc_gtk_set_toggle_action_state (wbcg, "ViewFullScreen", new_val);
	wbcg->is_fullscreen = new_val;
	wbcg->toggle_for_fullscreen = NULL;
	g_hash_table_foreach (tmp, (GHFunc)cb_toggle_visibility, wbcg);
	wbcg->toggle_for_fullscreen = tmp;
}


static void
wbc_gtk_setup_pixmaps (void)
{
	static struct {
		guchar const   *scalable_data;
		gchar const    *name;
	} const entry [] = {
		/* Cursors */
		{ gnm_cursor_cross, "cursor_cross" },
		{ gnm_bucket, "bucket" },
		{ gnm_font, "font" },
		{ sheet_move_marker, "sheet_move_marker" },
		/* Patterns */
		{ gp_125grey, "gp_125grey" },
		{ gp_25grey, "gp_25grey" },
		{ gp_50grey, "gp_50grey" },
		{ gp_625grey, "gp_625grey" },
		{ gp_75grey, "gp_75grey" },
		{ gp_bricks, "gp_bricks" },
		{ gp_diag, "gp_diag" },
		{ gp_diag_cross, "gp_diag_cross" },
		{ gp_foreground_solid, "gp_foreground_solid" },
		{ gp_horiz, "gp_horiz" },
		{ gp_large_circles, "gp_large_circles" },
		{ gp_rev_diag, "gp_rev_diag" },
		{ gp_semi_circle, "gp_semi_circle" },
		{ gp_small_circle, "gp_small_circle" },
		{ gp_solid, "gp_solid" },
		{ gp_thatch, "gp_thatch" },
		{ gp_thick_diag_cross, "gp_thick_diag_cross" },
		{ gp_thin_diag, "gp_thin_diag" },
		{ gp_thin_diag_cross, "gp_thin_diag_cross" },
		{ gp_thin_horiz, "gp_thin_horiz" },
		{ gp_thin_horiz_cross, "gp_thin_horiz_cross" },
		{ gp_thin_rev_diag, "gp_thin_rev_diag" },
		{ gp_thin_vert, "gp_thin_vert" },
		{ gp_vert, "gp_vert" },
		{ line_pattern_dash_dot, "line_pattern_dash_dot" },
		{ line_pattern_dash_dot_dot, "line_pattern_dash_dot_dot" },
		{ line_pattern_dashed, "line_pattern_dashed" },
		{ line_pattern_dotted, "line_pattern_dotted" },
		{ line_pattern_double, "line_pattern_double" },
		{ line_pattern_hair, "line_pattern_hair" },
		{ line_pattern_medium, "line_pattern_medium" },
		{ line_pattern_medium_dash, "line_pattern_medium_dash" },
		{ line_pattern_medium_dash_dot, "line_pattern_medium_dash_dot" },
		{ line_pattern_medium_dash_dot_dot, "line_pattern_medium_dash_dot_dot" },
		{ line_pattern_slant, "line_pattern_slant" },
		{ line_pattern_thick, "line_pattern_thick" },
		{ line_pattern_thin, "line_pattern_thin" },
		/* Borders */
		{ bottom_border, "bottom_border" },
		{ diag_border, "diag_border" },
		{ inside_border, "inside_border" },
		{ inside_horiz_border, "inside_horiz_border" },
		{ inside_vert_border, "inside_vert_border" },
		{ left_border, "left_border" },
		{ no_border, "no_border" },
		{ outline_border, "outline_border" },
		{ rev_diag_border, "rev_diag_border" },
		{ right_border, "right_border" },
		{ top_border, "top_border" },
		/* Stuff */
		{ unknown_image, "unknown_image" }
	};
	unsigned int ui;

	for (ui = 0; ui < G_N_ELEMENTS (entry); ui++) {
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_inline
			(-1, entry[ui].scalable_data, FALSE, NULL);
		gtk_icon_theme_add_builtin_icon (entry[ui].name,
			gdk_pixbuf_get_width (pixbuf), pixbuf);
		g_object_unref (pixbuf);
	}
}

static void
add_icon (GtkIconFactory *factory,
	  guchar const   *scalable_data,
	  guchar const   *sized_data,
	  gchar const    *stock_id)
{
	GtkIconSet *set = gtk_icon_set_new ();
	GtkIconSource *src = gtk_icon_source_new ();

	if (scalable_data != NULL) {
		gtk_icon_source_set_size_wildcarded (src, TRUE);
		gtk_icon_source_set_pixbuf (src,
			gdk_pixbuf_new_from_inline (-1, scalable_data, FALSE, NULL));
		gtk_icon_set_add_source (set, src);	/* copies the src */
	}

	/*
	 * For now, don't register a fixed-sized icon as doing so without
	 * catching style changes kills things like bug 302902.
	 */
	if (scalable_data == NULL && sized_data != NULL) {
		gtk_icon_source_set_size (src, GTK_ICON_SIZE_MENU);
		gtk_icon_source_set_size_wildcarded (src, FALSE);
		gtk_icon_source_set_pixbuf (src,
			gdk_pixbuf_new_from_inline (-1, sized_data, FALSE, NULL));
		gtk_icon_set_add_source (set, src);	/* copies the src */
	}

	gtk_icon_factory_add (factory, stock_id, set);	/* keeps reference to set */
	gtk_icon_set_unref (set);
	gtk_icon_source_free (src);
}

static void
wbc_gtk_setup_icons (void)
{
	static struct {
		guchar const   *scalable_data;
		guchar const   *sized_data;
		gchar const    *stock_id;
	} const entry [] = {
		{ gnm_column_add_24,			gnm_column_add_16,		"Gnumeric_ColumnAdd" },
		{ gnm_column_delete_24,			gnm_column_delete_16,		"Gnumeric_ColumnDelete" },
		{ gnm_column_size_24,			gnm_column_size_16,		"Gnumeric_ColumnSize" },
		{ gnm_column_hide_24,			gnm_column_hide_16,		"Gnumeric_ColumnHide" },
		{ gnm_column_unhide_24,			gnm_column_unhide_16,		"Gnumeric_ColumnUnhide" },
		{ gnm_row_add_24,			gnm_row_add_16,			"Gnumeric_RowAdd" },
		{ gnm_row_delete_24,			gnm_row_delete_16,		"Gnumeric_RowDelete" },
		{ gnm_row_size_24,			gnm_row_size_16,		"Gnumeric_RowSize" },
		{ gnm_row_hide_24,			gnm_row_hide_16,		"Gnumeric_RowHide" },
		{ gnm_row_unhide_24,			gnm_row_unhide_16,		"Gnumeric_RowUnhide" },

		{ gnm_group_24,				gnm_group_16,			"Gnumeric_Group" },
		{ gnm_ungroup_24,			gnm_ungroup_16,			"Gnumeric_Ungroup" },
		{ gnm_show_detail_24,			gnm_show_detail_16,		"Gnumeric_ShowDetail" },
		{ gnm_hide_detail_24,			gnm_hide_detail_16,		"Gnumeric_HideDetail" },

		{ gnm_graph_guru_24,			gnm_graph_guru_16,		"Gnumeric_GraphGuru" },
		{ gnm_insert_component_24,		gnm_insert_component_16,	"Gnumeric_InsertComponent" },
		{ gnm_insert_shaped_component_24,	gnm_insert_shaped_component_16,	"Gnumeric_InsertShapedComponent" },

		{ gnm_center_across_selection_24,	gnm_center_across_selection_16,	"Gnumeric_CenterAcrossSelection" },
		{ gnm_merge_cells_24,			gnm_merge_cells_16,		"Gnumeric_MergeCells" },
		{ gnm_split_cells_24,			gnm_split_cells_16,		"Gnumeric_SplitCells" },

		{ gnm_halign_fill_24,			NULL,				"Gnumeric_HAlignFill" },
		{ gnm_halign_general_24,		NULL,				"Gnumeric_HAlignGeneral" },

		{ NULL,					gnm_comment_add_16,		"Gnumeric_CommentAdd" },
		{ NULL,					gnm_comment_delete_16,		"Gnumeric_CommentDelete" },
		{ NULL,					gnm_comment_edit_16,		"Gnumeric_CommentEdit" },

		{ gnm_add_decimals,			NULL,				"Gnumeric_FormatAddPrecision" },
		{ gnm_remove_decimals,			NULL,				"Gnumeric_FormatRemovePrecision" },
		{ gnm_money,				NULL,				"Gnumeric_FormatAsAccounting" },
		{ gnm_percent,				NULL,				"Gnumeric_FormatAsPercentage" },
		{ gnm_thousand,				NULL,				"Gnumeric_FormatThousandSeparator" },

		{ gnm_auto,				NULL,				"Gnumeric_AutoSum" },
		{ gnm_equal,				NULL,				"Gnumeric_Equal" },
		{ gnm_formula_guru_24,			gnm_formula_guru_16,		"Gnumeric_FormulaGuru" },
		{ gnm_insert_image_24,			gnm_insert_image_16,		"Gnumeric_InsertImage" },
		{ gnm_bucket,				NULL,				"Gnumeric_Bucket" },
		{ gnm_font,				NULL,				"Gnumeric_Font" },
		{ gnm_expr_entry,			NULL,				"Gnumeric_ExprEntry" },
		{ gnm_brush_22,				gnm_brush_16,			"Gnumeric_Brush" },

		{ gnm_object_arrow_24,			NULL,				"Gnumeric_ObjectArrow" },
		{ gnm_object_ellipse_24,		NULL,				"Gnumeric_ObjectEllipse" },
		{ gnm_object_line_24,			NULL,				"Gnumeric_ObjectLine" },
		{ gnm_object_rectangle_24,		NULL,				"Gnumeric_ObjectRectangle" },

		{ gnm_object_frame_24,			NULL,				"Gnumeric_ObjectFrame" },
		{ gnm_object_label_24,			NULL,				"Gnumeric_ObjectLabel" },
		{ gnm_object_button_24,			NULL,				"Gnumeric_ObjectButton" },
		{ gnm_object_checkbox_24,		NULL,				"Gnumeric_ObjectCheckbox" },
		{ gnm_object_radiobutton_24,		NULL,				"Gnumeric_ObjectRadioButton" },
		{ gnm_object_scrollbar_24,		NULL,				"Gnumeric_ObjectScrollbar" },
		{ gnm_object_spinbutton_24,		NULL,				"Gnumeric_ObjectSpinButton" },
		{ gnm_object_slider_24,			NULL,				"Gnumeric_ObjectSlider" },
		{ gnm_object_combo_24,			NULL,				"Gnumeric_ObjectCombo" },
		{ gnm_object_list_24,			NULL,				"Gnumeric_ObjectList" },

		{ gnm_pivottable_24,			gnm_pivottable_16,		"Gnumeric_PivotTable" },
		{ gnm_protection_yes,			NULL,				"Gnumeric_Protection_Yes" },
		{ gnm_protection_no,			NULL,				"Gnumeric_Protection_No" },
		{ gnm_protection_yes_48,		NULL,				"Gnumeric_Protection_Yes_Dialog" },
		{ gnm_visible,				NULL,				"Gnumeric_Visible" },

		{ gnm_link_add_24,			gnm_link_add_16,		"Gnumeric_Link_Add" },
		{ NULL,					gnm_link_delete_16,		"Gnumeric_Link_Delete" },
		{ NULL,					gnm_link_edit_16,		"Gnumeric_Link_Edit" },
		{ gnm_link_external_24,			gnm_link_external_16,		"Gnumeric_Link_External" },
		{ gnm_link_internal_24,			gnm_link_internal_16,		"Gnumeric_Link_Internal" },
		{ gnm_link_email_24,			gnm_link_email_16,		"Gnumeric_Link_EMail" },
		{ gnm_link_url_24,			gnm_link_url_16,		"Gnumeric_Link_URL" },

		{ gnm_autofilter_24,			gnm_autofilter_16,		"Gnumeric_AutoFilter" },
		{ gnm_autofilter_delete_24,		gnm_autofilter_delete_16,	"Gnumeric_AutoFilterDelete" },

		{ gnm_border_left,			NULL,				"Gnumeric_BorderLeft" },
		{ gnm_border_none,			NULL,				"Gnumeric_BorderNone" },
		{ gnm_border_right,			NULL,				"Gnumeric_BorderRight" },

		{ gnm_border_all,			NULL,				"Gnumeric_BorderAll" },
		{ gnm_border_outside,			NULL,				"Gnumeric_BorderOutside" },
		{ gnm_border_thick_outside,		NULL,				"Gnumeric_BorderThickOutside" },

		{ gnm_border_bottom,			NULL,				"Gnumeric_BorderBottom" },
		{ gnm_border_double_bottom,		NULL,				"Gnumeric_BorderDoubleBottom" },
		{ gnm_border_thick_bottom,		NULL,				"Gnumeric_BorderThickBottom" },

		{ gnm_border_top_n_bottom,		NULL,				"Gnumeric_BorderTop_n_Bottom" },
		{ gnm_border_top_n_double_bottom,	NULL,				"Gnumeric_BorderTop_n_DoubleBottom" },
		{ gnm_border_top_n_thick_bottom,	NULL,				"Gnumeric_BorderTop_n_ThickBottom" },

		{ gnm_printsetup_hf_page,               NULL,                           "Gnumeric_Pagesetup_HF_Page"},
		{ gnm_printsetup_hf_pages,              NULL,                           "Gnumeric_Pagesetup_HF_Pages"},
		{ gnm_printsetup_hf_time,               NULL,                           "Gnumeric_Pagesetup_HF_Time"},
		{ gnm_printsetup_hf_date,               NULL,                           "Gnumeric_Pagesetup_HF_Date"},
		{ gnm_printsetup_hf_sheet,              NULL,                           "Gnumeric_Pagesetup_HF_Sheet"},
		{ gnm_printsetup_hf_cell,               NULL,                           "Gnumeric_Pagesetup_HF_Cell"},
	};
	static gboolean done = FALSE;

	if (!done) {
		unsigned int ui = 0;
		GtkIconFactory *factory = gtk_icon_factory_new ();
		for (ui = 0; ui < G_N_ELEMENTS (entry) ; ui++)
			add_icon (factory,
				  entry[ui].scalable_data,
				  entry[ui].sized_data,
				  entry[ui].stock_id);
		gtk_icon_factory_add_default (factory);
		g_object_unref (G_OBJECT (factory));
		done = TRUE;
	}
}

/****************************************************************************/

static void
cb_auto_expr_changed (GtkWidget *item, WBCGtk *wbcg)
{
	const GnmFunc *func;
	const char *descr;
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));

	if (wbcg->updating_ui)
		return;

	func = g_object_get_data (G_OBJECT (item), "func");
	descr = g_object_get_data (G_OBJECT (item), "descr");

	g_object_set (wbv,
		      "auto-expr-func", func,
		      "auto-expr-descr", descr,
		      NULL);
}

static void
cb_auto_expr_precision_toggled (GtkWidget *item, WBCGtk *wbcg)
{
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
	if (wbcg->updating_ui)
		return;

	go_object_toggle (wbv, "auto-expr-max-precision");
}


static gboolean
cb_select_auto_expr (GtkWidget *widget, GdkEventButton *event, WBCGtk *wbcg)
{
	/*
	 * WARNING * WARNING * WARNING
	 *
	 * Keep the functions in lower case.
	 * We currently register the functions in lower case and some locales
	 * (notably tr_TR) do not have the same encoding for tolower that
	 * locale C does.
	 *
	 * eg tolower ('I') != 'i'
	 * Which would break function lookup when looking up for function 'selectIon'
	 * when it was registered as 'selection'
	 *
	 * WARNING * WARNING * WARNING
	 */
	static struct {
		char const * const displayed_name;
		char const * const function;
	} const quick_compute_routines [] = {
		{ N_("Sum"),	       "sum" },
		{ N_("Min"),	       "min" },
		{ N_("Max"),	       "max" },
		{ N_("Average"),       "average" },
		{ N_("Count"),         "count" },
		{ NULL, NULL }
	};

	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
	GtkWidget *item, *menu;
	int i;

	if (event->button != 3)
		return FALSE;

	menu = gtk_menu_new ();

	for (i = 0; quick_compute_routines [i].displayed_name; i++) {
		GnmParsePos pp;
		char const *expr = quick_compute_routines [i].function;
		GnmExprTop const *new_auto_expr;
		GtkWidget *item;

		/* Test the expression...  */
		parse_pos_init (&pp, wb_control_get_workbook (WORKBOOK_CONTROL (wbcg)), NULL, 0, 0);
		new_auto_expr = gnm_expr_parse_str_simple (expr, &pp);
		if (!new_auto_expr)
			continue;
		gnm_expr_top_unref (new_auto_expr);

		item = gtk_menu_item_new_with_label (
			_(quick_compute_routines [i].displayed_name));
		g_object_set_data (G_OBJECT (item),
				   "func",
				   gnm_func_lookup (expr, NULL));
		g_object_set_data (G_OBJECT (item), "descr",
			(gpointer)_(quick_compute_routines [i].displayed_name));
		g_signal_connect (G_OBJECT (item),
			"activate",
			G_CALLBACK (cb_auto_expr_changed), wbcg);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_check_menu_item_new_with_label (_("Use maximum precision"));
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
		wbv->auto_expr_use_max_precision);
	g_signal_connect (G_OBJECT (item), "activate",
		G_CALLBACK (cb_auto_expr_precision_toggled), wbcg);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	gnumeric_popup_menu (GTK_MENU (menu), event);
	return TRUE;
}

static void
wbc_gtk_create_status_area (WBCGtk *wbcg)
{
	GtkWidget *tmp, *frame;

	wbcg->progress_bar = gtk_progress_bar_new ();
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (wbcg->progress_bar), " ");
	gtk_progress_bar_set_orientation (
		GTK_PROGRESS_BAR (wbcg->progress_bar), GTK_PROGRESS_LEFT_TO_RIGHT);

	wbcg->auto_expr_label = tmp = gtk_label_new ("");
	g_object_ref (wbcg->auto_expr_label);
	GTK_WIDGET_UNSET_FLAGS (tmp, GTK_CAN_FOCUS);
	gtk_widget_ensure_style (tmp);
	gtk_widget_set_size_request (tmp, go_pango_measure_string (
		gtk_widget_get_pango_context (GTK_WIDGET (wbcg->toplevel)),
		tmp->style->font_desc, "W") * 15, -1);
	tmp = gtk_event_box_new ();
	gtk_container_add (GTK_CONTAINER (tmp), wbcg->auto_expr_label);
	g_signal_connect (G_OBJECT (tmp),
		"button_press_event",
		G_CALLBACK (cb_select_auto_expr), wbcg);
	frame = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (frame), tmp);

	wbcg->status_text = tmp = gtk_statusbar_new ();
	gtk_widget_ensure_style (tmp);
	gtk_widget_set_size_request (tmp, go_pango_measure_string (
		gtk_widget_get_pango_context (GTK_WIDGET (wbcg->toplevel)),
		tmp->style->font_desc, "W") * 15, -1);

	wbcg->status_area = gtk_hbox_new (FALSE, 2);
	gtk_box_pack_end (GTK_BOX (wbcg->status_area), wbcg->status_text, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (wbcg->status_area), frame, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (wbcg->status_area), wbcg->progress_bar, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (wbcg->everything),
		wbcg->status_area, FALSE, TRUE, 0);
	gtk_widget_show_all (wbcg->status_area);

	g_hash_table_insert (wbcg->visibility_widgets,
			     g_strdup ("ViewStatusbar"),
			     g_object_ref (wbcg->status_area));
	/* disable statusbar by default going to fullscreen */
	g_hash_table_insert (wbcg->toggle_for_fullscreen,
		g_strdup ("ViewStatusbar"),
		gtk_action_group_get_action (wbcg->actions, "ViewStatusbar"));

#ifdef GNM_USE_HILDON
	g_hash_table_remove (wbcg->toggle_for_fullscreen, "ViewStatusbar");
	gtk_widget_hide (wbcg->status_area);
#endif
}

/****************************************************************************/

static void
cb_file_history_activate (GObject *action, WBCGtk *wbcg)
{
	gui_file_read (wbcg, g_object_get_data (action, "uri"), NULL, NULL);
}

static void
wbc_gtk_reload_recent_file_menu (WBCGtk const *wbcg)
{
	WBCGtk *gtk = (WBCGtk *)wbcg;
	GSList *history, *ptr;
	unsigned i;

	if (gtk->file_history.merge_id != 0)
		gtk_ui_manager_remove_ui (gtk->ui, gtk->file_history.merge_id);
	gtk->file_history.merge_id = gtk_ui_manager_new_merge_id (gtk->ui);

	if (gtk->file_history.actions != NULL)
		g_object_unref (gtk->file_history.actions);
	gtk->file_history.actions = gtk_action_group_new ("FileHistory");

	/* create the actions */
	history = gnm_app_history_get_list (3);
	for (i = 1, ptr = history; ptr != NULL ; ptr = ptr->next, i++) {
		GtkActionEntry entry;
		GtkAction *action;
		char const *uri = ptr->data;
		char *name = g_strdup_printf ("FileHistoryEntry%d", i);
		char *label = history_item_label (uri, i);
		char *filename = go_filename_from_uri (uri);
		char *filename_utf8 = filename ? g_filename_to_utf8 (filename, -1, NULL, NULL, NULL) : NULL;
		char *tooltip = g_strdup_printf (_("Open %s"), filename_utf8 ? filename_utf8 : uri);

		entry.name = name;
		entry.stock_id = NULL;
		entry.label = label;
		entry.accelerator = NULL;
		entry.tooltip = tooltip;
		entry.callback = G_CALLBACK (cb_file_history_activate);
		gtk_action_group_add_actions (gtk->file_history.actions,
			&entry, 1, (WBCGtk *)wbcg);
		action = gtk_action_group_get_action (gtk->file_history.actions,
						      name);
		g_object_set_data_full (G_OBJECT (action), "uri",
			g_strdup (uri), (GDestroyNotify)g_free);

		g_free (name);
		g_free (label);
		g_free (filename);
		g_free (filename_utf8);
		g_free (tooltip);
	}
	go_slist_free_custom (history, (GFreeFunc)g_free);

	gtk_ui_manager_insert_action_group (gtk->ui, gtk->file_history.actions, 0);

	/* merge them in */
	while (i-- > 1) {
		char *name = g_strdup_printf ("FileHistoryEntry%d", i);
		gtk_ui_manager_add_ui (gtk->ui, gtk->file_history.merge_id,
#ifdef GNM_USE_HILDON
			"/popup/File/FileHistory",
#else
			"/menubar/File/FileHistory",
#endif
			name, name, GTK_UI_MANAGER_AUTO, TRUE);
		g_free (name);
	}
}

static void
wbcg_set_toplevel (WBCGtk *wbcg, GtkWidget *w)
{
	static GtkTargetEntry const drag_types[] = {
		{ (char *) "text/uri-list", 0, TARGET_URI_LIST },
		{ (char *) "GNUMERIC_SHEET", 0, TARGET_SHEET },
		{ (char *) "GNUMERIC_SAME_PROC", GTK_TARGET_SAME_APP, 0 }
	};

	g_return_if_fail (wbcg->toplevel == NULL);

	wbcg->toplevel = w;
	w = GTK_WIDGET (wbcg_toplevel (wbcg));
	g_return_if_fail (GTK_IS_WINDOW (w));

	g_object_set (G_OBJECT (w),
		"allow-grow", TRUE,
		"allow-shrink", TRUE,
		NULL);

	g_signal_connect_data (w, "delete_event",
		G_CALLBACK (wbc_gtk_close), wbcg, NULL,
		G_CONNECT_AFTER | G_CONNECT_SWAPPED);
	g_signal_connect_after (w, "set_focus",
		G_CALLBACK (cb_set_focus), wbcg);
	g_signal_connect (w, "scroll-event",
		G_CALLBACK (cb_scroll_wheel), wbcg);
	g_signal_connect (w, "realize",
		G_CALLBACK (cb_realize), wbcg);

	/* Setup a test of Drag and Drop */
	gtk_drag_dest_set (GTK_WIDGET (w),
		GTK_DEST_DEFAULT_ALL, drag_types, G_N_ELEMENTS (drag_types),
		GDK_ACTION_COPY | GDK_ACTION_MOVE);
	gtk_drag_dest_add_image_targets (GTK_WIDGET (w));
	gtk_drag_dest_add_text_targets (GTK_WIDGET (w));
	g_object_connect (G_OBJECT (w),
		"signal::drag-leave",	G_CALLBACK (cb_wbcg_drag_leave), wbcg,
		"signal::drag-data-received", G_CALLBACK (cb_wbcg_drag_data_received), wbcg,
		"signal::drag-motion",	G_CALLBACK (cb_wbcg_drag_motion), wbcg,
#if 0
		"signal::drag-data-get", G_CALLBACK (wbcg_drag_data_get), wbc,
#endif
		NULL);
}

/***************************************************************************/

static void
wbc_gtk_get_property (GObject *object, guint property_id,
		      GValue *value, GParamSpec *pspec)
{
	WBCGtk *wbcg = (WBCGtk *)object;

	switch (property_id) {
	case WBG_GTK_PROP_AUTOSAVE_PROMPT:
		g_value_set_boolean (value, wbcg->autosave_prompt);
		break;
	case WBG_GTK_PROP_AUTOSAVE_TIME:
		g_value_set_int (value, wbcg->autosave_time);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
wbc_gtk_set_property (GObject *object, guint property_id,
		   const GValue *value, GParamSpec *pspec)
{
	WBCGtk *wbcg = (WBCGtk *)object;

	switch (property_id) {
	case WBG_GTK_PROP_AUTOSAVE_PROMPT:
		wbcg->autosave_prompt = g_value_get_boolean (value);
		break;
	case WBG_GTK_PROP_AUTOSAVE_TIME:
		wbcg_set_autosave_time (wbcg, g_value_get_int (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
wbc_gtk_finalize (GObject *obj)
{
	WBCGtk *wbcg = WBC_GTK (obj);

	if (wbcg->idle_update_style_feedback != 0)
		g_source_remove (wbcg->idle_update_style_feedback);
	if (wbcg->file_history.merge_id != 0)
		gtk_ui_manager_remove_ui (wbcg->ui, wbcg->file_history.merge_id);
	if (wbcg->file_history.actions != NULL)
		g_object_unref (wbcg->file_history.actions);
	if (wbcg->toolbar.merge_id != 0)
		gtk_ui_manager_remove_ui (wbcg->ui, wbcg->toolbar.merge_id);
	if (wbcg->toolbar.actions != NULL)
		g_object_unref (wbcg->toolbar.actions);
	g_object_unref (wbcg->ui);

	g_hash_table_destroy (wbcg->custom_uis);

	/* Disconnect signals that would attempt to change things during
	 * destruction.
	 */

	wbcg_autosave_cancel (wbcg);

	if (wbcg->notebook != NULL)
		g_signal_handlers_disconnect_by_func (
			G_OBJECT (wbcg->notebook),
			G_CALLBACK (cb_notebook_switch_page), wbcg);
	g_signal_handlers_disconnect_by_func (
		G_OBJECT (wbcg->toplevel),
		G_CALLBACK (cb_set_focus), wbcg);

	wbcg_auto_complete_destroy (wbcg);

	gtk_window_set_focus (wbcg_toplevel (wbcg), NULL);

	if (wbcg->toplevel != NULL) {
		gtk_object_destroy (GTK_OBJECT (wbcg->toplevel));
		wbcg->toplevel = NULL;
	}

	if (wbcg->font_desc) {
		pango_font_description_free (wbcg->font_desc);
		wbcg->font_desc = NULL;
	}

	if (wbcg->auto_expr_label) {
		g_object_unref (wbcg->auto_expr_label);
		wbcg->auto_expr_label = NULL;
	}

	g_hash_table_destroy (wbcg->visibility_widgets);
	g_hash_table_destroy (wbcg->toggle_for_fullscreen);

#ifdef GNM_USE_HILDON
	if (wbcg->hildon_prog != NULL) {
		g_object_unref (wbcg->hildon_prog);
		wbcg->hildon_prog = NULL;
	}
#endif

	g_free (wbcg->preferred_geometry);
	wbcg->preferred_geometry = NULL;

	parent_class->finalize (obj);
}

/***************************************************************************/

typedef struct {
	GnmExprEntry *entry;
	GogDataset *dataset;
	int dim_i;
	GogDataType data_type;

	gulong dataset_changed_handler;
	gulong entry_update_handler;
} GraphDimEditor;

static void
cb_graph_dim_editor_update (GnmExprEntry *gee,
			    G_GNUC_UNUSED gboolean user_requested,
			    GraphDimEditor *editor)
{
	GOData *data = NULL;
	Sheet *sheet;
	SheetControlGUI *scg;

	/* Ignore changes while we are insensitive. useful for displaying
	 * values, without storing then as Data.  Also ignore updates if the
	 * dataset has been cleared via the weakref handler  */
	if (!GTK_WIDGET_SENSITIVE (gee) || editor->dataset == NULL)
		return;

	g_object_get (G_OBJECT (gee), "scg", &scg, NULL);
	sheet = scg_sheet (scg);
	g_object_unref (G_OBJECT (scg));

	/* If we are setting something */
	if (!gnm_expr_entry_is_blank (editor->entry)) {
		GnmParsePos pos;
		GnmParseError  perr;
		GnmExprTop const *texpr;

		parse_error_init (&perr);
		texpr = gnm_expr_entry_parse (editor->entry,
			parse_pos_init_sheet (&pos, sheet),
			&perr, TRUE, GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS);

		/* TODO : add some error dialogs split out
		 * the code in workbok_edit to add parens.  */
		if (texpr == NULL) {
			if (editor->data_type == GOG_DATA_SCALAR)
				texpr = gnm_expr_top_new_constant (
					value_new_string (
						gnm_expr_entry_get_text (editor->entry)));
			else {
				g_return_if_fail (perr.err != NULL);

				wb_control_validation_msg (WORKBOOK_CONTROL (scg_wbcg (scg)),
					VALIDATION_STYLE_PARSE_ERROR, NULL, perr.err->message);
				parse_error_free (&perr);
				return;
			}
		}

		switch (editor->data_type) {
		case GOG_DATA_SCALAR:
			data = gnm_go_data_scalar_new_expr (sheet, texpr);
			break;
		case GOG_DATA_VECTOR:
			data = gnm_go_data_vector_new_expr (sheet, texpr);
			break;
		case GOG_DATA_MATRIX:
			data = gnm_go_data_matrix_new_expr (sheet, texpr);
		}
	}

	/* The SheetObjectGraph does the magic to link things in */
	gog_dataset_set_dim (editor->dataset, editor->dim_i, data, NULL);
}

static gboolean
cb_graph_dim_entry_focus_out_event (G_GNUC_UNUSED GtkEntry	*ignored,
				    G_GNUC_UNUSED GdkEventFocus	*event,
				    GraphDimEditor		*editor)
{
	cb_graph_dim_editor_update (editor->entry, FALSE, editor);

	return FALSE;
}

static void
cb_dataset_changed (GogDataset *dataset,
		    gboolean resize,
		    GraphDimEditor *editor)
{
	GOData *val;

	g_signal_handler_block (editor->entry, editor->entry_update_handler);

	val = gog_dataset_get_dim (dataset, editor->dim_i);
	if (val != NULL) {
		char *txt = go_data_as_str (val);
		gnm_expr_entry_load_from_text (editor->entry, txt);
		g_free (txt);
	}

	g_signal_handler_unblock (editor->entry, editor->entry_update_handler);
}

static void
cb_dim_editor_weakref_notify (GraphDimEditor *editor, GogDataset *dataset)
{
	g_return_if_fail (editor->dataset == dataset);
	editor->dataset = NULL;
}

static void
graph_dim_editor_free (GraphDimEditor *editor)
{
	if (editor->dataset) {
		g_signal_handler_disconnect (editor->dataset, editor->dataset_changed_handler);
		g_object_weak_unref (G_OBJECT (editor->dataset),
			(GWeakNotify) cb_dim_editor_weakref_notify, editor);
	}
	g_free (editor);
}

static gpointer
wbcg_data_allocator_editor (GogDataAllocator *dalloc,
			    GogDataset *dataset, int dim_i, GogDataType data_type)
{
	WBCGtk *wbcg = WBC_GTK (dalloc);
	GraphDimEditor *editor;
	GOData *val;

	editor = g_new (GraphDimEditor, 1);
	editor->dataset		= dataset;
	editor->dim_i		= dim_i;
	editor->data_type	= data_type;
	editor->entry		= gnm_expr_entry_new (wbcg, TRUE);
	g_object_weak_ref (G_OBJECT (editor->dataset),
		(GWeakNotify) cb_dim_editor_weakref_notify, editor);

	gnm_expr_entry_set_update_policy (editor->entry,
		GTK_UPDATE_DISCONTINUOUS);

	val = gog_dataset_get_dim (dataset, dim_i);
	if (val != NULL) {
		char *txt = go_data_as_str (val);
		gnm_expr_entry_load_from_text (editor->entry, txt);
		g_free (txt);
	}
	gnm_expr_entry_set_flags (editor->entry, GNM_EE_FORCE_ABS_REF, GNM_EE_MASK);

	editor->entry_update_handler = g_signal_connect (G_OBJECT (editor->entry),
		"update",
		G_CALLBACK (cb_graph_dim_editor_update), editor);
	g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (editor->entry)),
		"focus-out-event",
		G_CALLBACK (cb_graph_dim_entry_focus_out_event), editor);
	editor->dataset_changed_handler = g_signal_connect (G_OBJECT (editor->dataset),
		"changed", G_CALLBACK (cb_dataset_changed), editor);
	g_object_set_data_full (G_OBJECT (editor->entry),
		"editor", editor, (GDestroyNotify) graph_dim_editor_free);

	return editor->entry;
}

static void
wbcg_data_allocator_allocate (GogDataAllocator *dalloc, GogPlot *plot)
{
	SheetControlGUI *scg = wbcg_cur_scg (WBC_GTK (dalloc));
	sv_selection_to_plot (scg_view (scg), plot);
}


static void
wbcg_go_plot_data_allocator_init (GogDataAllocatorClass *iface)
{
	iface->editor	  = wbcg_data_allocator_editor;
	iface->allocate   = wbcg_data_allocator_allocate;
}

/*************************************************************************/
static char *
wbcg_get_password (GOCmdContext *cc, char const* filename)
{
	WBCGtk *wbcg = WBC_GTK (cc);

	return dialog_get_password (wbcg_toplevel (wbcg), filename);
}
static void
wbcg_set_sensitive (GOCmdContext *cc, gboolean sensitive)
{
	GtkWindow *toplevel = wbcg_toplevel (WBC_GTK (cc));
	if (toplevel != NULL)
		gtk_widget_set_sensitive (GTK_WIDGET (toplevel), sensitive);
}
static void
wbcg_error_error (GOCmdContext *cc, GError *err)
{
	go_gtk_notice_dialog (wbcg_toplevel (WBC_GTK (cc)),
		GTK_MESSAGE_ERROR, err->message);
}
static void
wbcg_error_error_info (GOCmdContext *cc, ErrorInfo *error)
{
	gnumeric_error_info_dialog_show (
		wbcg_toplevel (WBC_GTK (cc)), error);
}
static void
wbcg_progress_set (GOCmdContext *cc, gfloat val)
{
	WBCGtk *wbcg = WBC_GTK (cc);
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (wbcg->progress_bar), val);
}
static void
wbcg_progress_message_set (GOCmdContext *cc, gchar const *msg)
{
	WBCGtk *wbcg = WBC_GTK (cc);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (wbcg->progress_bar), msg);
}
static void
wbcg_gnm_cmd_context_init (GOCmdContextClass *iface)
{
	iface->get_password	    = wbcg_get_password;
	iface->set_sensitive	    = wbcg_set_sensitive;
	iface->error.error	    = wbcg_error_error;
	iface->error.error_info	    = wbcg_error_error_info;
	iface->progress_set	    = wbcg_progress_set;
	iface->progress_message_set = wbcg_progress_message_set;
}

/*************************************************************************/

static void
wbc_gtk_class_init (GObjectClass *gobject_class)
{
	WorkbookControlClass *wbc_class =
		WORKBOOK_CONTROL_CLASS (gobject_class);

	g_return_if_fail (wbc_class != NULL);

	parent_class = g_type_class_peek_parent (gobject_class);
	gobject_class->get_property	= wbc_gtk_get_property;
	gobject_class->set_property	= wbc_gtk_set_property;
	gobject_class->finalize		= wbc_gtk_finalize;

	wbc_class->edit_line_set	= wbcg_edit_line_set;
	wbc_class->selection_descr_set	= wbcg_edit_selection_descr_set;
	wbc_class->update_action_sensitivity = wbcg_update_action_sensitivity;

	wbc_class->sheet.add        = wbcg_sheet_add;
	wbc_class->sheet.remove	    = wbcg_sheet_remove;
	wbc_class->sheet.focus	    = wbcg_sheet_focus;
	wbc_class->sheet.remove_all = wbcg_sheet_remove_all;

	wbc_class->undo_redo.labels	= wbcg_undo_redo_labels;
	wbc_class->undo_redo.truncate	= wbc_gtk_undo_redo_truncate;
	wbc_class->undo_redo.pop	= wbc_gtk_undo_redo_pop;
	wbc_class->undo_redo.push	= wbc_gtk_undo_redo_push;

	wbc_class->menu_state.update	= wbcg_menu_state_update;

	wbc_class->claim_selection	= wbcg_claim_selection;
	wbc_class->paste_from_selection	= wbcg_paste_from_selection;
	wbc_class->validation_msg	= wbcg_validation_msg;

	wbc_class->control_new		= wbc_gtk_control_new;
	wbc_class->init_state		= wbc_gtk_init_state;
	wbc_class->style_feedback	= wbc_gtk_style_feedback;

	wbc_gtk_setup_pixmaps ();
	wbc_gtk_setup_icons ();

        g_object_class_install_property (gobject_class,
		 WBG_GTK_PROP_AUTOSAVE_PROMPT,
		 g_param_spec_boolean ("autosave-prompt",
				       _("Autosave prompt"),
				       _("Ask about autosave?"),
				       FALSE,
				       GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class,
		 WBG_GTK_PROP_AUTOSAVE_TIME,
		 g_param_spec_int ("autosave-time",
				   _("Autosave time in seconds"),
				   _("Seconds before autosave"),
				   0, G_MAXINT, 0,
				   GSF_PARAM_STATIC | G_PARAM_READWRITE));

	wbc_gtk_signals [WBC_GTK_MARKUP_CHANGED] = g_signal_new ("markup-changed",
		WBC_GTK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WBCGtkClass, markup_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);
	{
		GdkPixbuf *icon = gnumeric_load_pixbuf ("gnome-gnumeric.png");
		if (icon != NULL) {
			GList *icon_list = g_list_prepend (NULL, icon);
			gtk_window_set_default_icon_list (icon_list);
			g_list_free (icon_list);
			g_object_unref (G_OBJECT (icon));
		}
	}

}

static void
wbc_gtk_init (GObject *obj)
{
	static struct {
		char const *name;
		gboolean    is_font;
		unsigned    offset;
	} const toggles[] = {
		{ "FontBold",		   TRUE, G_STRUCT_OFFSET (WBCGtk, font.bold) },
		{ "FontItalic",		   TRUE, G_STRUCT_OFFSET (WBCGtk, font.italic) },
		{ "FontUnderline",	   TRUE, G_STRUCT_OFFSET (WBCGtk, font.underline) },
		{ "FontDoubleUnderline",   TRUE, G_STRUCT_OFFSET (WBCGtk, font.d_underline) },
		{ "FontSuperscript",	   TRUE, G_STRUCT_OFFSET (WBCGtk, font.superscript) },
		{ "FontSubscript",	   TRUE, G_STRUCT_OFFSET (WBCGtk, font.subscript) },
		{ "FontStrikeThrough",	   TRUE, G_STRUCT_OFFSET (WBCGtk, font.strikethrough) },

		{ "AlignLeft",		   FALSE, G_STRUCT_OFFSET (WBCGtk, h_align.left) },
		{ "AlignCenter",	   FALSE, G_STRUCT_OFFSET (WBCGtk, h_align.center) },
		{ "AlignRight",		   FALSE, G_STRUCT_OFFSET (WBCGtk, h_align.right) },
		{ "CenterAcrossSelection", FALSE, G_STRUCT_OFFSET (WBCGtk, h_align.center_across_selection) },
		{ "AlignTop",		   FALSE, G_STRUCT_OFFSET (WBCGtk, v_align.top) },
		{ "AlignVCenter",	   FALSE, G_STRUCT_OFFSET (WBCGtk, v_align.center) },
		{ "AlignBottom",	   FALSE, G_STRUCT_OFFSET (WBCGtk, v_align.bottom) }
	};

	WBCGtk		*wbcg = (WBCGtk *)obj;
	GtkAction	*act;
	GError		*error = NULL;
	GtkWidget	*hbox;
	char		*uifile;
	unsigned	 i;

#ifdef GNM_USE_HILDON
	static HildonProgram *hildon_program = NULL;
#endif

	wbcg->table       = gtk_table_new (0, 0, 0);
	wbcg->notebook    = NULL;
	wbcg->updating_ui = FALSE;
	wbcg->rangesel	  = NULL;
	wbcg->font_desc   = NULL;

	wbcg->visibility_widgets = g_hash_table_new_full (g_str_hash,
		g_str_equal, (GDestroyNotify)g_free, (GDestroyNotify)g_object_unref);
	wbcg->toggle_for_fullscreen = g_hash_table_new_full (g_str_hash,
		g_str_equal, (GDestroyNotify)g_free, NULL);

	wbcg->autosave_prompt = FALSE;
	wbcg->autosave_time = 0;
	wbcg->autosave_timer = 0;

	/* We are not in edit mode */
	wbcg->editing	    = FALSE;
	wbcg->editing_sheet = NULL;
	wbcg->editing_cell  = NULL;

#warning "why is this here ?"
	wbcg->current_saver = NULL;
	wbcg->menu_zone = gtk_vbox_new (TRUE, 0);
	wbcg->everything = gtk_vbox_new (FALSE, 0);

	wbcg->toolbar_zones[GTK_POS_TOP] = gtk_vbox_new (FALSE, 0);
	wbcg->toolbar_zones[GTK_POS_BOTTOM] = NULL;
	wbcg->toolbar_zones[GTK_POS_LEFT] = gtk_hbox_new (FALSE, 0);
	wbcg->toolbar_zones[GTK_POS_RIGHT] = gtk_hbox_new (FALSE, 0);

	wbcg->idle_update_style_feedback = 0;

#ifdef GNM_USE_HILDON
	if (hildon_program == NULL)
		hildon_program = HILDON_PROGRAM (hildon_program_get_instance ());
	else
		g_object_ref (hildon_program);

	wbcg->hildon_prog = hildon_program;

	wbcg_set_toplevel (wbcg, hildon_window_new ());
	hildon_program_add_window (wbcg->hildon_prog, HILDON_WINDOW (wbcg_toplevel (wbcg)));
#else
	wbcg_set_toplevel (wbcg, gtk_window_new (GTK_WINDOW_TOPLEVEL));
#endif

	g_signal_connect (wbcg_toplevel (wbcg), "window_state_event",
			  G_CALLBACK (cb_wbcg_window_state_event),
			  wbcg);

#ifndef GNM_USE_HILDON
	gtk_box_pack_start (GTK_BOX (wbcg->everything),
		wbcg->menu_zone, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (wbcg->everything),
		wbcg->toolbar_zones[GTK_POS_TOP], FALSE, TRUE, 0);
#endif

	gtk_window_set_title (wbcg_toplevel (wbcg), "Gnumeric");
	gtk_window_set_wmclass (wbcg_toplevel (wbcg), "Gnumeric", "Gnumeric");

#if 0
	bonobo_dock_set_client_area (BONOBO_DOCK (wbcg->dock), wbcg->table);
#endif

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), wbcg->toolbar_zones[GTK_POS_LEFT], FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), wbcg->table, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), wbcg->toolbar_zones[GTK_POS_RIGHT], FALSE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (wbcg->everything), hbox, TRUE, TRUE, 0);
	gtk_widget_show_all (wbcg->everything);

	wbc_gtk_init_actions (wbcg);

	for (i = G_N_ELEMENTS (toggles); i-- > 0 ; ) {
		act = gtk_action_group_get_action (
			(toggles[i].is_font ? wbcg->font_actions : wbcg->actions),
			toggles[i].name);
		G_STRUCT_MEMBER (GtkToggleAction *, wbcg, toggles[i].offset) = GTK_TOGGLE_ACTION (act);
	}

	wbc_gtk_init_undo_redo (wbcg);
	wbc_gtk_init_color_fore (wbcg);
	wbc_gtk_init_color_back (wbcg);
	wbc_gtk_init_font_name (wbcg);
	wbc_gtk_init_font_size (wbcg);
	wbc_gtk_init_zoom (wbcg);
	wbc_gtk_init_borders (wbcg);

	wbcg->ui = gtk_ui_manager_new ();
	g_object_connect (wbcg->ui,
		"signal::add_widget",	 G_CALLBACK (cb_add_menus_toolbars), wbcg,
		"signal::connect_proxy",    G_CALLBACK (cb_connect_proxy), wbcg,
		"signal::disconnect_proxy", G_CALLBACK (cb_disconnect_proxy), wbcg,
		"swapped_object_signal::post_activate", G_CALLBACK (cb_post_activate), wbcg,
		NULL);
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->permanent_actions, 0);
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->actions, 0);
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->font_actions, 0);
	gtk_window_add_accel_group (wbcg_toplevel (wbcg),
		gtk_ui_manager_get_accel_group (wbcg->ui));

#ifdef GNM_USE_HILDON
	uifile = g_build_filename (gnm_sys_data_dir (), "HILDON_Gnumeric-gtk.xml", NULL);
#else
	uifile = g_build_filename (gnm_sys_data_dir (),
		(uifilename? uifilename: "GNOME_Gnumeric-gtk.xml"), NULL);
#endif

	if (!gtk_ui_manager_add_ui_from_file (wbcg->ui, uifile, &error)) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}
	g_free (uifile);

	wbcg->custom_uis = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						 NULL, g_free);

	wbcg->file_history.actions = NULL;
	wbcg->file_history.merge_id = 0;
	wbc_gtk_reload_recent_file_menu (wbcg);

	wbcg->toolbar.merge_id = gtk_ui_manager_new_merge_id (wbcg->ui);
	wbcg->toolbar.actions = gtk_action_group_new ("Toolbars");
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->toolbar.actions, 0);

	wbcg->windows.actions = NULL;
	wbcg->windows.merge_id = 0;
	gnm_app_foreach_extra_ui ((GFunc) cb_init_extra_ui, wbcg);
	g_object_connect ((GObject *) gnm_app_get_app (),
		"swapped-object-signal::window-list-changed",
			G_CALLBACK (cb_regenerate_window_menu), wbcg,
		"object-signal::custom-ui-added",
			G_CALLBACK (cb_add_custom_ui), wbcg,
		"object-signal::custom-ui-removed",
			G_CALLBACK (cb_remove_custom_ui), wbcg,
		NULL);

	gtk_ui_manager_ensure_update (wbcg->ui);

	gtk_container_add (GTK_CONTAINER (wbcg->toplevel), wbcg->everything);

	/* updates the undo/redo menu labels before check_underlines
	 * to avoid problems like #324692. */
	wb_control_undo_redo_labels (WORKBOOK_CONTROL (wbcg), NULL, NULL);
	if (GNM_VERSION_MAJOR % 2) {
		gtk_container_foreach (GTK_CONTAINER (wbcg->menu_zone),
				       (GtkCallback)check_underlines,
				       (gpointer)"");
	}

#ifdef GNM_USE_HILDON
	hildon_window_set_menu (HILDON_WINDOW (wbcg_toplevel (wbcg)),
				GTK_MENU (gtk_ui_manager_get_widget (wbcg->ui, "/popup")));

	gtk_widget_show_all (wbcg->toplevel);

	wbc_gtk_set_toggle_action_state (wbcg, "ViewMenuToolbarFormatToolbar", FALSE);
	wbc_gtk_set_toggle_action_state (wbcg, "ViewMenuToolbarObjectToolbar", FALSE);
	wbc_gtk_set_toggle_action_state (wbcg, "ViewSheets", FALSE);
	wbc_gtk_set_toggle_action_state (wbcg, "ViewStatusbar", FALSE);
#endif
}

GSF_CLASS_FULL (WBCGtk, wbc_gtk, NULL, NULL, wbc_gtk_class_init, NULL,
	wbc_gtk_init, WORKBOOK_CONTROL_TYPE, 0,
	GSF_INTERFACE (wbcg_go_plot_data_allocator_init, GOG_DATA_ALLOCATOR_TYPE);
	GSF_INTERFACE (wbcg_gnm_cmd_context_init, GO_CMD_CONTEXT_TYPE))

/******************************************************************************/

void
wbc_gtk_markup_changer (WBCGtk *wbcg)
{
	g_signal_emit (G_OBJECT (wbcg), wbc_gtk_signals [WBC_GTK_MARKUP_CHANGED], 0);
}

/******************************************************************************/

WBCGtk *
wbc_gtk_new (WorkbookView *optional_view,
	     Workbook *optional_wb,
	     GdkScreen *optional_screen,
	     gchar *optional_geometry)
{
	Sheet *sheet;
	WorkbookView *wbv;
	WBCGtk *wbcg = g_object_new (wbc_gtk_get_type (), NULL);
	WorkbookControl *wbc = (WorkbookControl *)wbcg;

	wbcg->preferred_geometry = g_strdup (optional_geometry);

	wbcg_create_edit_area (wbcg);
	wbc_gtk_create_status_area (wbcg);
	wbc_gtk_reload_recent_file_menu (wbcg);

	g_signal_connect_object (gnm_app_get_app (),
		"notify::file-history-list",
		G_CALLBACK (wbc_gtk_reload_recent_file_menu), wbcg, G_CONNECT_SWAPPED);

	wb_control_set_view (wbc, optional_view, optional_wb);
	wbv = wb_control_view (wbc);
	sheet = wbv->current_sheet;
	if (sheet != NULL) {
		wb_control_menu_state_update (wbc, MS_ALL);
		wb_control_update_action_sensitivity (wbc);
		wb_control_style_feedback (wbc, NULL);
		cb_zoom_change (sheet, NULL, wbcg);
	}

	wbcg_view_changed (wbcg, NULL, NULL);

	if (optional_screen)
		gtk_window_set_screen (wbcg_toplevel (wbcg), optional_screen);

	/* Postpone showing the GUI, so that we may resize it freely. */
	g_idle_add ((GSourceFunc) show_gui, wbcg);

	wb_control_init_state ((WorkbookControl *)wbcg);
	return wbcg;
}

gboolean
wbcg_ui_update_begin (WBCGtk *wbcg)
{
	g_return_val_if_fail (IS_WBC_GTK (wbcg), FALSE);
	g_return_val_if_fail (!wbcg->updating_ui, FALSE);

	return (wbcg->updating_ui = TRUE);
}

void
wbcg_ui_update_end (WBCGtk *wbcg)
{
	g_return_if_fail (IS_WBC_GTK (wbcg));
	g_return_if_fail (wbcg->updating_ui);

	wbcg->updating_ui = FALSE;
}

GtkWindow *
wbcg_toplevel (WBCGtk *wbcg)
{
	g_return_val_if_fail (IS_WBC_GTK (wbcg), NULL);
	return GTK_WINDOW (wbcg->toplevel);
}

void
wbcg_set_transient (WBCGtk *wbcg, GtkWindow *window)
{
	go_gtk_window_set_transient (wbcg_toplevel (wbcg), window);
}

#warning merge these and clarfy whether we want the visible scg, or the logical (view) scg
/**
 * wbcg_focus_cur_scg :
 * @wbcg : The workbook control to operate on.
 *
 * A utility routine to safely ensure that the keyboard focus
 * is attached to the item-grid.  This is required when a user
 * edits a combo-box or and entry-line which grab focus.
 *
 * It is called for zoom, font name/size, and accept/cancel for the editline.
 */
Sheet *
wbcg_focus_cur_scg (WBCGtk *wbcg)
{
	GtkWidget *table;
	SheetControlGUI *scg;

	g_return_val_if_fail (IS_WBC_GTK (wbcg), NULL);

	if (wbcg->notebook == NULL)
		return NULL;

	table = gtk_notebook_get_nth_page (wbcg->notebook,
		gtk_notebook_get_current_page (wbcg->notebook));
	scg = g_object_get_data (G_OBJECT (table), SHEET_CONTROL_KEY);

	g_return_val_if_fail (scg != NULL, NULL);

	scg_take_focus (scg);
	return scg_sheet (scg);
}

SheetControlGUI *
wbcg_cur_scg (WBCGtk *wbcg)
{
	return wbcg_get_scg (wbcg, wbcg_cur_sheet (wbcg));
}

Sheet *
wbcg_cur_sheet (WBCGtk *wbcg)
{
	return wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
}

PangoFontDescription *
wbcg_get_font_desc (WBCGtk *wbcg)
{
	g_return_val_if_fail (IS_WBC_GTK (wbcg), NULL);

	if (!wbcg->font_desc) {
		GtkSettings *settings = wbcg_get_gtk_settings (wbcg);
		wbcg->font_desc = settings_get_font_desc (settings);
		g_signal_connect (settings, "notify::gtk-font-name",
				  G_CALLBACK (cb_desktop_font_changed), wbcg);
	}
	return wbcg->font_desc;
}

WBCGtk *
wbcg_find_for_workbook (Workbook *wb,
			WBCGtk *candidate,
			GdkScreen *pref_screen,
			GdkDisplay *pref_display)
{
	gboolean has_screen, has_display;

	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);
	g_return_val_if_fail (candidate == NULL || IS_WBC_GTK (candidate), NULL);

	if (candidate && wb_control_get_workbook (WORKBOOK_CONTROL (candidate)) == wb)
		return candidate;

	if (!pref_screen && candidate)
		pref_screen = gtk_widget_get_screen (GTK_WIDGET (wbcg_toplevel (candidate)));

	if (!pref_display && pref_screen)
		pref_display = gdk_screen_get_display (pref_screen);

	candidate = NULL;
	has_screen = FALSE;
	has_display = FALSE;
	WORKBOOK_FOREACH_CONTROL(wb, wbv, wbc, {
		if (IS_WBC_GTK (wbc)) {
			WBCGtk *wbcg = WBC_GTK (wbc);
			GdkScreen *screen = gtk_widget_get_screen (GTK_WIDGET (wbcg_toplevel (wbcg)));
			GdkDisplay *display = gdk_screen_get_display (screen);

			if (pref_screen == screen && !has_screen) {
				has_screen = has_display = TRUE;
				candidate = wbcg;
			} else if (pref_display == display && !has_display) {
				has_display = TRUE;
				candidate = wbcg;
			} else if (!candidate)
				candidate = wbcg;
		}
	});

	return candidate;
}


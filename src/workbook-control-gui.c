/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * workbook-control-gui.c: GUI specific routines for a workbook-control.
 *
 * Copyright (C) 2000 Jody Goldberg (jgoldberg@home.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <config.h>
#include "workbook-control-gui-priv.h"
#include "application.h"
#include "workbook-object-toolbar.h"
#include "workbook-format-toolbar.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook.h"
#include "sheet.h"
#include "sheet-private.h"
#include "sheet-control-gui-priv.h"
#include "gnumeric-canvas.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "commands.h"
#include "cmd-edit.h"
#include "workbook-cmd-format.h"
#include "selection.h"
#include "clipboard.h"
#include "print.h"
#include "gui-clipboard.h"
#include "workbook-edit.h"
#include "main.h"
#include "eval.h"
#include "position.h"
#include "parse-util.h"
#include "ranges.h"
#include "history.h"
#include "str.h"
#include "cell.h"
#include "gui-file.h"
#include "search.h"
#include "error-info.h"
#include "pixmaps/equal-sign.xpm"
#include "gui-util.h"
#include "widgets/gnumeric-toolbar.h"
#include "widgets/widget-editable-label.h"

#ifdef ENABLE_BONOBO
#include "sheet-object-container.h"
#ifdef ENABLE_EVOLUTION
#include <idl/Evolution-Composer.h>
#include <bonobo/bonobo-stream-memory.h>
#endif
#endif

#include <gal/util/e-util.h>
#include <gal/widgets/widget-color-combo.h>
#include <gal/widgets/gtk-combo-text.h>
#include <gal/widgets/gtk-combo-stack.h>

#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-config.h>

#include <ctype.h>
#include <stdarg.h>

gboolean
wbcg_ui_update_begin (WorkbookControlGUI *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);
	g_return_val_if_fail (!wbcg->updating_ui, FALSE);

	return (wbcg->updating_ui = TRUE);
}

void
wbcg_ui_update_end (WorkbookControlGUI *wbcg)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (wbcg->updating_ui);

	wbcg->updating_ui = FALSE;
}

static int
sheet_to_page_index (WorkbookControlGUI *wbcg, Sheet *sheet, SheetControlGUI **res)
{
	int i = 0;
	GtkWidget *w;

	if (res)
		*res = NULL;
	g_return_val_if_fail (IS_SHEET (sheet), -1);

	for ( ; NULL != (w = gtk_notebook_get_nth_page (wbcg->notebook, i)) ; i++) {
		GtkObject *obj = gtk_object_get_data (GTK_OBJECT (w),
						      SHEET_CONTROL_KEY);
		SheetControlGUI *scg = SHEET_CONTROL_GUI (obj);
		SheetControl *sc = (SheetControl *) scg;
		
		if (scg != NULL && sc->sheet == sheet) {
			if (res)
				*res = scg;
			return i;
		}
	}
	return -1;
}

GtkWindow *
wb_control_gui_toplevel (WorkbookControlGUI *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), NULL);

	return wbcg->toplevel;
}

/**
 * wb_control_gui_focus_cur_sheet :
 * @wbcg : The workbook control to operate on.
 *
 * A utility routine to safely ensure that the keyboard focus
 * is attached to the item-grid.  This is required when a user
 * edits a combo-box or and entry-line which grab focus.
 *
 * It is called for zoom, font name/size, and accept/cancel for the editline.
 */
Sheet *
wb_control_gui_focus_cur_sheet (WorkbookControlGUI *wbcg)
{
	GtkObject *table;
	GtkObject *obj;
	SheetControlGUI *scg;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), NULL);

	table = GTK_OBJECT ((wbcg->notebook)->cur_page->child);
	obj = gtk_object_get_data (table, SHEET_CONTROL_KEY);
	scg = SHEET_CONTROL_GUI (obj);

	g_return_val_if_fail (scg != NULL, NULL);

	scg_take_focus (scg);

	return ((SheetControl *) scg)->sheet;
}

SheetControlGUI *
wb_control_gui_cur_sheet (WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg;

	sheet_to_page_index (wbcg,
		wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)), &scg);

	return scg;
}

/****************************************************************************/
/* Autosave */
static gboolean
cb_autosave (gpointer *data)
{
	WorkbookView *wb_view;
        WorkbookControlGUI *wbcg = (WorkbookControlGUI *)data;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	wb_view = wb_control_view (WORKBOOK_CONTROL (wbcg));

	if (wb_view == NULL)
		return FALSE;

	if (wbcg->autosave && workbook_is_dirty (wb_view_workbook (wb_view))) {
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
 */
gboolean
wbcg_rangesel_possible (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	if (wbcg_edit_entry_redirect_p (wbcg) || NULL != wbcg->rangesel)
		return TRUE;

	if (!wbcg->editing)
		return FALSE;

	return wbcg_editing_expr (wbcg);
}

gboolean
wb_control_gui_is_editing (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);
	return wbcg->editing;
}

void
wb_control_gui_autosave_cancel (WorkbookControlGUI *wbcg)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	if (wbcg->autosave_timer != 0) {
		gtk_timeout_remove (wbcg->autosave_timer);
		wbcg->autosave_timer = 0;
	}
}

void
wb_control_gui_autosave_set (WorkbookControlGUI *wbcg,
			     int minutes, gboolean prompt)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	wb_control_gui_autosave_cancel (wbcg);

	wbcg->autosave = (minutes != 0);
	wbcg->autosave_minutes = minutes;
	wbcg->autosave_prompt = prompt;

	if (wbcg->autosave)
		wbcg->autosave_timer = gtk_timeout_add (minutes * 60000,
			(GtkFunction) cb_autosave, wbcg);
}
/****************************************************************************/

static WorkbookControl *
wbcg_control_new (WorkbookControl *wbc, WorkbookView *wbv, Workbook *wb)
{
	return workbook_control_gui_new (wbv, wb);
}

static void
wbcg_init_state (WorkbookControl *wbc)
{
	WorkbookView	   *wbv = wb_control_view (wbc);
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (wbc);
	ColorCombo *combo;

	/* Associate the combos with the view */
	combo = COLOR_COMBO (wbcg->back_color);
	color_palette_set_group (combo->palette,
		color_group_fetch ("back_color_group", wbv));
	combo = COLOR_COMBO (wbcg->fore_color);
	color_palette_set_group (combo->palette,
		color_group_fetch ("fore_color_group", wbv));
}

static void
wbcg_title_set (WorkbookControl *wbc, char const *title)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	char *full_title;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (title != NULL);

	full_title = g_strconcat (title, _(" : Gnumeric"), NULL);

 	gtk_window_set_title (wb_control_gui_toplevel (wbcg), full_title);
	g_free (full_title);
}

static void
cb_prefs_update (gpointer key, gpointer value, gpointer user_data)
{
	Sheet *sheet = value;
	sheet_adjust_preferences (sheet, FALSE, FALSE);
}

static void
wbcg_prefs_update (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	Workbook	*wb  = wb_control_workbook (wbc);
	WorkbookView	*wbv = wb_control_view (wbc);

	g_hash_table_foreach (wb->sheet_hash_private, cb_prefs_update, NULL);
	gtk_notebook_set_show_tabs (wbcg->notebook,
				    wbv->show_notebook_tabs);
}

static void
wbcg_format_feedback (WorkbookControl *wbc)
{
	workbook_feedback_set ((WorkbookControlGUI *)wbc);
}

static void
zoom_changed (WorkbookControlGUI *wbcg, Sheet* sheet)
{
	gchar buffer [25];

	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (wbcg->zoom_entry != NULL);

	snprintf (buffer, sizeof (buffer), "%d%%",
		  (int) (sheet->last_zoom_factor_used * 100 + .5));

	if (wbcg_ui_update_begin (wbcg)) {
		gtk_combo_text_set_text (GTK_COMBO_TEXT (wbcg->zoom_entry), buffer);
		wbcg_ui_update_end (wbcg);
	}

	scg_object_update_bbox (wb_control_gui_cur_sheet (wbcg),
				NULL, NULL, NULL);
}

static void
wbcg_zoom_feedback (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	Sheet *sheet = wb_control_cur_sheet (wbc);
	zoom_changed (wbcg, sheet);
}

static void
wbcg_edit_line_set (WorkbookControl *wbc, char const *text)
{
	GtkEntry *entry;

	entry = GTK_ENTRY (wbcg_get_entry ((WorkbookControlGUI*)wbc));
	gtk_entry_set_text (entry, text);
}

static void
wbcg_edit_selection_descr_set (WorkbookControl *wbc, char const *text)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	gtk_entry_set_text (GTK_ENTRY (wbcg->selection_descriptor), text);
}

/*
 * Signal handler for EditableLabel's text_changed signal.
 */
static gboolean
cb_sheet_label_changed (EditableLabel *el,
			const char *new_name, WorkbookControlGUI *wbcg)
{
	gboolean ans = !cmd_rename_sheet (WORKBOOK_CONTROL (wbcg),
					  el->text, new_name);
	wb_control_gui_focus_cur_sheet (wbcg);
	return ans;
}

static void
cb_sheet_label_edit_stopped (EditableLabel *el, WorkbookControlGUI *wbcg)
{
	wb_control_gui_focus_cur_sheet (wbcg);
}

static void
sheet_action_add_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;

	workbook_sheet_add (wb_control_workbook (sc->wbc), sc->sheet, TRUE);
}

static void
delete_sheet_if_possible (GtkWidget *ignored, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	Workbook *wb = wb_control_workbook (sc->wbc);
	GtkWidget *d, *button_no;
	char *message;
	int r;

	/*
	 * If this is the last sheet left, ignore the request
	 */
	if (workbook_sheet_count (wb) == 1)
		return;

	message = g_strdup_printf (
		_("Are you sure you want to remove the sheet called `%s'?"),
		sc->sheet->name_unquoted);

	d = gnome_message_box_new (
		message, GNOME_MESSAGE_BOX_QUESTION,
		GNOME_STOCK_BUTTON_YES,
		GNOME_STOCK_BUTTON_NO,
		NULL);
	g_free (message);
	button_no = g_list_last (GNOME_DIALOG (d)->buttons)->data;
	gtk_widget_grab_focus (button_no);

	r = gnumeric_dialog_run (scg->wbcg, GNOME_DIALOG (d));

	if (r != 0)
		return;

	workbook_sheet_delete (sc->sheet);
	workbook_recalc_all (wb);
}

static void
sheet_action_rename_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	char *new_name = dialog_get_sheet_name (scg->wbcg, sheet->name_unquoted);
	if (!new_name)
		return;

	/* We do not care if it fails */
	cmd_rename_sheet (sc->wbc, sheet->name_unquoted, new_name);
	g_free (new_name);
}

static void
sheet_action_clone_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
     	Sheet *new_sheet = sheet_duplicate (sc->sheet);
	
	workbook_sheet_attach (sc->sheet->workbook, new_sheet, sc->sheet);
	sheet_set_dirty (new_sheet, TRUE);
}

static void
sheet_action_reorder_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	dialog_sheet_order (scg->wbcg);
}

/**
 * sheet_menu_label_run:
 */
static void
sheet_menu_label_run (SheetControlGUI *scg, GdkEventButton *event)
{
#define SHEET_CONTEXT_TEST_SIZE 1
	SheetControl *sc = (SheetControl *) scg;
	
	struct {
		const char *text;
		void (*function) (GtkWidget *widget, SheetControlGUI *scg);
		int  flags;
	} const sheet_label_context_actions [] = {
		{ N_("Add another sheet"), &sheet_action_add_sheet, 0 },
		{ N_("Remove this sheet"), &delete_sheet_if_possible, SHEET_CONTEXT_TEST_SIZE },
		{ N_("Rename this sheet"), &sheet_action_rename_sheet, 0 },
		{ N_("Duplicate this sheet"), &sheet_action_clone_sheet, 0 },
		{ N_("Re-order sheets"), &sheet_action_reorder_sheet, SHEET_CONTEXT_TEST_SIZE },
		{ NULL, NULL }
	};

	GtkWidget *menu;
	GtkWidget *item;
	int i;

	menu = gtk_menu_new ();

	for (i = 0; sheet_label_context_actions [i].text != NULL; i++){
		int flags = sheet_label_context_actions [i].flags;

		if (flags & SHEET_CONTEXT_TEST_SIZE &&
		    workbook_sheet_count (sc->sheet->workbook) < 2)
				continue;

		item = gtk_menu_item_new_with_label (
			_(sheet_label_context_actions [i].text));
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);

		gtk_signal_connect (
			GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (sheet_label_context_actions [i].function),
			scg);
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
			     GtkWidget *child)
{
	GtkWidget *notebook;
	gint page_number;
	GtkObject *obj = gtk_object_get_data (GTK_OBJECT (child),
					      SHEET_CONTROL_KEY);
	SheetControlGUI *scg = SHEET_CONTROL_GUI (obj);

	g_return_val_if_fail (scg != NULL, FALSE);

	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	notebook = child->parent;
	page_number = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), child);

	if (event->button == 1 || NULL != scg->wbcg->rangesel) {
		gtk_notebook_set_page (GTK_NOTEBOOK (notebook), page_number);
		return TRUE;
	}

	if (event->button == 3) {

		sheet_menu_label_run (SHEET_CONTROL_GUI (obj), event);
		scg_take_focus (SHEET_CONTROL_GUI (obj));
		return TRUE;
	}

	return FALSE;
}

static void workbook_setup_sheets (WorkbookControlGUI *wbcg);
static void wbcg_menu_state_sheet_count (WorkbookControl *wbc);

/**
 * wbcg_sheet_add:
 * @sheet: a sheet
 *
 * Creates a new SheetControlGUI for the sheet and adds it to the workbook-control-gui.
 */
static void
wbcg_sheet_add (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;
	SheetControl *sc;
	GtkWidget *sheet_label;
	GList     *ptr;

	g_return_if_fail (wbcg != NULL);

	if (wbcg->notebook == NULL)
		workbook_setup_sheets (wbcg);

	scg = sheet_control_gui_new (sheet);
	sc = (SheetControl *) scg;
	sc->wbc = wbc;
	scg->wbcg = wbcg;

	/*
	 * NB. this is so we can use editable_label_set_text since
	 * gtk_notebook_set_tab_label kills our widget & replaces with a label.
	 */
	sheet_label = editable_label_new (sheet->name_unquoted);
	gtk_signal_connect_after (
		GTK_OBJECT (sheet_label), "text_changed",
		GTK_SIGNAL_FUNC (cb_sheet_label_changed), wbcg);
	gtk_signal_connect (
		GTK_OBJECT (sheet_label), "editing_stopped",
		GTK_SIGNAL_FUNC (cb_sheet_label_edit_stopped), wbcg);
	gtk_signal_connect (
		GTK_OBJECT (sheet_label), "button_press_event",
		GTK_SIGNAL_FUNC (cb_sheet_label_button_press), scg->table);

	gtk_widget_show (sheet_label);
	gtk_widget_show_all (GTK_WIDGET (scg->table));

	if (wbcg_ui_update_begin (wbcg)) {
		gtk_notebook_insert_page (wbcg->notebook,
			GTK_WIDGET (scg->table), sheet_label,
			workbook_sheet_index_get (wb_control_workbook (wbc), sheet));
		wbcg_ui_update_end (wbcg);
	}

	wbcg_menu_state_sheet_count (wbc);

	/* create views for the sheet objects */
	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next)
		sheet_object_new_view (ptr->data, scg);
	scg_adjust_preferences (sc);
	scg_set_zoom_factor (sc);
	scg_take_focus (scg);
}

static void
wbcg_sheet_remove (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;
	int i;

	/* During destruction we may have already removed the notebook */
	if (wbcg->notebook == NULL)
		return;

	i = sheet_to_page_index (wbcg, sheet, &scg);

	g_return_if_fail (i >= 0);

	gtk_notebook_remove_page (wbcg->notebook, i);

	wbcg_menu_state_sheet_count (wbc);
}

static void
wbcg_sheet_rename (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	GtkWidget *label;
	SheetControlGUI *scg;
	int i = sheet_to_page_index (wbcg, sheet, &scg);

	g_return_if_fail (i >= 0);

	label = gtk_notebook_get_tab_label (wbcg->notebook, GTK_WIDGET (scg->table));
	editable_label_set_text (EDITABLE_LABEL (label), sheet->name_unquoted);
}

static void
wbcg_sheet_focus (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;
	int i = sheet_to_page_index (wbcg, sheet, &scg);

	/* A sheet added in another view may not yet have a view */
	if (i >= 0) {
		gtk_notebook_set_page (wbcg->notebook, i);
		zoom_changed (wbcg, sheet);
	}
}

static void
wbcg_sheet_move (WorkbookControl *wbc, Sheet *sheet, int new_pos)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;

	g_return_if_fail (IS_SHEET (sheet));

	/* No need for sanity checking, the workbook did that */
        if (sheet_to_page_index (wbcg, sheet, &scg) >= 0)
		gtk_notebook_reorder_child (wbcg->notebook,
			GTK_WIDGET (scg->table), new_pos);
}

static void
wbcg_sheet_remove_all (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;

	if (wbcg->notebook != NULL) {
		GtkWidget *tmp = GTK_WIDGET (wbcg->notebook);

		/* Be sure we are no longer editing */
		wbcg_edit_finish (wbcg, FALSE);

		/* Clear notebook to disable updates as focus changes for pages
		 * during destruction
		 */
		wbcg->notebook = NULL;
		gtk_container_remove (GTK_CONTAINER (wbcg->table), tmp);
	}
}

static void
wbcg_history_setup (WorkbookControlGUI *wbcg)
{
	GList *hl = application_history_get_list ();
	if (hl)
		history_menu_setup (wbcg, hl);
}

static void
cb_change_zoom (GtkWidget *caller, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	int factor;

	if (sheet == NULL || wbcg->updating_ui)
		return;

	/* The GSList of sheet passed to cmd_zoom will be freed by cmd_zoom */
	factor = atoi (gtk_entry_get_text (GTK_ENTRY (caller)));
	cmd_zoom (WORKBOOK_CONTROL (wbcg), g_slist_append (NULL, sheet),
		  (double) factor / 100);

	wb_control_gui_focus_cur_sheet (wbcg);

	/* Always update the zoom combo so that things display consistently */
	zoom_changed (wbcg, sheet);
}

static void
wbcg_auto_expr_value (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (wbc);
	WorkbookView *wbv = wb_control_view (wbc);

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (wbv != NULL);
	g_return_if_fail (wbv->auto_expr_value_as_string != NULL);

	if (wbcg_ui_update_begin (wbcg)) {
		gtk_label_set_text(
			 GTK_LABEL (GTK_BIN (wbcg->auto_expr_label)->child),
			 wbv->auto_expr_value_as_string);
		wbcg_ui_update_end (wbcg);
	}
}

static GtkComboStack *
ur_stack (WorkbookControl *wbc, gboolean is_undo)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	return GTK_COMBO_STACK (is_undo ? wbcg->undo_combo : wbcg->redo_combo);
}

static void
wbcg_undo_redo_clear (WorkbookControl *wbc, gboolean is_undo)
{
	gtk_combo_stack_clear (ur_stack (wbc, is_undo));
}

static void
wbcg_undo_redo_truncate (WorkbookControl *wbc, int n, gboolean is_undo)
{
	gtk_combo_stack_truncate (ur_stack (wbc, is_undo), n);
}

static void
wbcg_undo_redo_pop (WorkbookControl *wbc, gboolean is_undo)
{
	gtk_combo_stack_remove_top (ur_stack (wbc, is_undo), 1);
}

static void
wbcg_undo_redo_push (WorkbookControl *wbc, char const *text, gboolean is_undo)
{
	gtk_combo_stack_push_item (ur_stack (wbc, is_undo), text);
}

#ifndef ENABLE_BONOBO
static void
change_menu_state (GtkWidget *menu_item, gboolean state)
{
	g_return_if_fail (menu_item != NULL);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), state);
}

static void
change_menu_sensitivity (GtkWidget *menu_item, gboolean sensitive)
{
	g_return_if_fail (menu_item != NULL);

	gtk_widget_set_sensitive (menu_item, sensitive);
}

static void
change_menu_label (GtkWidget *menu_item, char const *prefix, char const *suffix)
{
	gchar    *text;
	GtkBin   *bin = GTK_BIN (menu_item);
	GtkLabel *label = GTK_LABEL (bin->child);
	gboolean  sensitive = TRUE;

	g_return_if_fail (label != NULL);

	if (prefix == NULL) {
		gtk_label_parse_uline (label, suffix);
		return;
	}

	if (suffix == NULL) {
		suffix = _("Nothing");
		sensitive = FALSE;
	}

	text = g_strdup_printf ("%s : %s", prefix, suffix);

	gtk_label_parse_uline (label, text);
	gtk_widget_set_sensitive (menu_item, sensitive);
	g_free (text);
}
#else
static void
change_menu_state (WorkbookControlGUI const *wbcg,
		  char const *verb_path, gboolean state)
{
	CORBA_Environment  ev;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	CORBA_exception_init (&ev);
	bonobo_ui_component_set_prop (wbcg->uic, verb_path,
				      "state", state ? "1" : "0", &ev);
	CORBA_exception_free (&ev);	
}

static void
change_menu_sensitivity (WorkbookControlGUI const *wbcg,
			 char const *verb_path, gboolean sensitive)
{
	CORBA_Environment  ev;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	CORBA_exception_init (&ev);
	bonobo_ui_component_set_prop (wbcg->uic, verb_path,
				      "sensitive", sensitive ? "1" : "0", &ev);
	CORBA_exception_free (&ev);	
}

static void
change_menu_label (WorkbookControlGUI const *wbcg,
		   char const *verb_path,
		   char const *menu_path, /* FIXME we need verb level labels. */
		   char const *prefix,
		   char const *suffix)
{
	gboolean  sensitive = TRUE;
	gchar    *text;
	CORBA_Environment  ev;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	CORBA_exception_init (&ev);

	if (prefix == NULL) {
		bonobo_ui_component_set_prop (wbcg->uic, menu_path, "label",
					      suffix, &ev);
	} else {
		if (suffix == NULL) {
			suffix = _("Nothing");
			sensitive = FALSE;
		}

		text = g_strdup_printf ("%s : %s", prefix, suffix);

		bonobo_ui_component_set_prop (wbcg->uic, menu_path,
					      "label", text, &ev);
		bonobo_ui_component_set_prop (wbcg->uic, verb_path,
					      "sensitive", sensitive ? "1" : "0", &ev);
		g_free (text);
	}
	CORBA_exception_free (&ev);
}
#endif

static void
wbcg_menu_state_update (WorkbookControl *wbc, Sheet const *sheet, int flags)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	
	g_return_if_fail (wbcg != NULL);

#ifndef ENABLE_BONOBO
	if (MS_INSERT_COLS & flags) 
		change_menu_sensitivity (wbcg->menu_item_insert_cols,
					 sheet->priv->enable_insert_cols);
	if (MS_INSERT_ROWS & flags)
		change_menu_sensitivity (wbcg->menu_item_insert_rows,
					 sheet->priv->enable_insert_rows);
	if (MS_INSERT_CELLS & flags)
		change_menu_sensitivity (wbcg->menu_item_insert_cells,
					 sheet->priv->enable_insert_cells);
	if (MS_SHOWHIDE_DETAIL & flags) {
		change_menu_sensitivity (wbcg->menu_item_show_detail,
					 sheet->priv->enable_showhide_detail);
		change_menu_sensitivity (wbcg->menu_item_hide_detail,
					 sheet->priv->enable_showhide_detail);
	}
	if (MS_PASTE_SPECIAL & flags)
		change_menu_sensitivity (wbcg->menu_item_paste_special,
					 sheet->priv->enable_paste_special);
	if (MS_PRINT_SETUP & flags)
		change_menu_sensitivity (wbcg->menu_item_print_setup,
					 !wbcg_edit_has_guru (wbcg));
	if (MS_SEARCH_REPLACE & flags)
		change_menu_sensitivity (wbcg->menu_item_search_replace,
					 !wbcg_edit_has_guru (wbcg));
	if (MS_DEFINE_NAME & flags)
		change_menu_sensitivity (wbcg->menu_item_define_name,
					 !wbcg_edit_has_guru (wbcg));
	if (MS_CONSOLIDATE & flags)
		change_menu_sensitivity (wbcg->menu_item_consolidate,
					 !wbcg_edit_has_guru (wbcg));
#else
	if (MS_INSERT_COLS & flags)
		change_menu_sensitivity (wbcg, "/commands/InsertColumns",
					 sheet->priv->enable_insert_cols);
	if (MS_INSERT_ROWS & flags)
		change_menu_sensitivity (wbcg, "/commands/InsertRows",
					 sheet->priv->enable_insert_rows);
	if (MS_INSERT_CELLS & flags)
		change_menu_sensitivity (wbcg, "/commands/InsertCells",
					 sheet->priv->enable_insert_cells);
	if (MS_SHOWHIDE_DETAIL & flags) {
		change_menu_sensitivity (wbcg, "/commands/DataOutlineShowDetail",
					 sheet->priv->enable_showhide_detail);
		change_menu_sensitivity (wbcg, "/commands/DataOutlineHideDetail",
					 sheet->priv->enable_showhide_detail);
	}
	if (MS_PASTE_SPECIAL & flags)
		change_menu_sensitivity (wbcg, "/commands/EditPasteSpecial",
					 sheet->priv->enable_paste_special);
	if (MS_PRINT_SETUP & flags)
		change_menu_sensitivity (wbcg, "/commands/FilePrintSetup",
					 !wbcg_edit_has_guru (wbcg));
	if (MS_SEARCH_REPLACE & flags)
		change_menu_sensitivity (wbcg, "/commands/EditSearchReplace",
					 !wbcg_edit_has_guru (wbcg));
	if (MS_DEFINE_NAME & flags)
		change_menu_sensitivity (wbcg, "/commands/EditNames",
					 !wbcg_edit_has_guru (wbcg));
	if (MS_CONSOLIDATE & flags)
		change_menu_sensitivity (wbcg, "/commands/DataConsolidate",
					 !wbcg_edit_has_guru (wbcg));
#endif

	if (MS_FREEZE_VS_THAW & flags) {
		Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
		/* Cheat and use the same accelerator for both states because
		 * we don't reset it when the label changes */
		char const* label = sheet_is_frozen (sheet)
			? _("Un_freeze Panes") : _("_Freeze Panes");
#ifndef ENABLE_BONOBO
		change_menu_label (wbcg->menu_item_freeze_panes,
				   NULL, label);
#else
		change_menu_label (wbcg, "/commands/ViewFreezeThawPanes",
		                   "/menu/View/ViewFreezeThawPanes", NULL, label);
#endif
	}
}

static void
wbcg_undo_redo_labels (WorkbookControl *wbc, char const *undo, char const *redo)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	g_return_if_fail (wbcg != NULL);

#ifndef ENABLE_BONOBO
	change_menu_label (wbcg->menu_item_undo, _("Undo"), undo);
	change_menu_label (wbcg->menu_item_redo, _("Redo"), redo);
#else
	change_menu_label (wbcg, "/commands/EditUndo", "/menu/Edit/Undo",
			   _("_Undo"), undo);
	change_menu_label (wbcg, "/commands/EditRedo", "/menu/Edit/Redo",
			   _("_Redo"), redo);
#endif
}

static void
wbcg_menu_state_sheet_prefs (WorkbookControl *wbc, Sheet const *sheet)
{
 	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	
	if (!wbcg_ui_update_begin (wbcg))
		return;

#ifndef ENABLE_BONOBO
	change_menu_state (wbcg->menu_item_sheet_display_formulas,
		sheet->display_formulas);
	change_menu_state (wbcg->menu_item_sheet_hide_zero,
		sheet->hide_zero);
	change_menu_state (wbcg->menu_item_sheet_hide_grid,
		sheet->hide_grid);
	change_menu_state (wbcg->menu_item_sheet_hide_col_header,
		sheet->hide_col_header);
	change_menu_state (wbcg->menu_item_sheet_hide_row_header,
		sheet->hide_row_header);
	change_menu_state (wbcg->menu_item_sheet_display_outlines,
		sheet->display_outlines);
	change_menu_state (wbcg->menu_item_sheet_outline_symbols_below,
		sheet->outline_symbols_below);
	change_menu_state (wbcg->menu_item_sheet_outline_symbols_right,
		sheet->outline_symbols_right);
#else
	change_menu_state (wbcg,
		"/commands/SheetDisplayFormulas", sheet->display_formulas);
	change_menu_state (wbcg,
		"/commands/SheetHideZeros", sheet->hide_zero);
	change_menu_state (wbcg,
		"/commands/SheetHideGridlines", sheet->hide_grid);
	change_menu_state (wbcg,
		"/commands/SheetHideColHeader", sheet->hide_col_header);
	change_menu_state (wbcg,
		"/commands/SheetHideRowHeader", sheet->hide_row_header);
	change_menu_state (wbcg,
		"/commands/SheetDisplayOutlines", sheet->display_outlines);
	change_menu_state (wbcg,
		"/commands/SheetOutlineBelow", sheet->outline_symbols_below);
	change_menu_state (wbcg,
		"/commands/SheetOutlineRight", sheet->outline_symbols_right);
#endif
	wbcg_ui_update_end (wbcg);
}

static void
wbcg_menu_state_sheet_count (WorkbookControl *wbc)
{
 	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	int const sheet_count = g_list_length (wbcg->notebook->children);
	/* Should we anble commands requiring multiple sheets */
	gboolean const multi_sheet = (sheet_count > 1);

	/* Scrollable if there are more than 3 tabs */
	gtk_notebook_set_scrollable (wbcg->notebook, sheet_count > 3);
	
#ifndef ENABLE_BONOBO
	change_menu_sensitivity (wbcg->menu_item_sheet_remove, multi_sheet);
	change_menu_sensitivity (wbcg->menu_item_sheets_edit_reorder, multi_sheet);
	change_menu_sensitivity (wbcg->menu_item_sheets_format_reorder, multi_sheet);
#else
	change_menu_sensitivity (wbcg, "/commands/SheetRemove", multi_sheet);
	change_menu_sensitivity (wbcg, "/commands/SheetReorder", multi_sheet);
#endif
}

static void
wbcg_paste_from_selection (WorkbookControl *wbc, PasteTarget const *pt, guint32 time)
{
	x_request_clipboard ((WorkbookControlGUI *)wbc, pt, time);
}

static gboolean
wbcg_claim_selection (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	return gtk_selection_owner_set (GTK_WIDGET (wbcg->toplevel),
					GDK_SELECTION_PRIMARY,
					GDK_CURRENT_TIME);
}

static void
wbcg_progress_set (CommandContext *cc, gfloat val)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (cc);

#ifdef ENABLE_BONOBO
	gtk_progress_bar_update (GTK_PROGRESS_BAR (wbcg->progress_bar), val);
#else
	gnome_appbar_set_progress (wbcg->appbar, val);
#endif
}

static void
wbcg_progress_message_set (CommandContext *cc, gchar const *msg)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (cc);
	GtkProgress *progress;

#ifdef ENABLE_BONOBO
	progress = GTK_PROGRESS (wbcg->progress_bar);
#else
	progress = gnome_appbar_get_progress (wbcg->appbar);
#endif
	if (msg != NULL) {
		gtk_progress_set_format_string (progress, msg);
		gtk_progress_set_show_text (progress, TRUE);
	} else {
		gtk_progress_set_show_text (progress, FALSE);
	}
}

static void
wbcg_error_system (CommandContext *cc, char const *msg)
{
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, msg);
}
static void
wbcg_error_plugin (CommandContext *cc, char const *msg)
{
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, msg);
}
static void
wbcg_error_read (CommandContext *cc, char const *msg)
{
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, msg);
}

static void
wbcg_error_save (CommandContext *cc, char const *msg)
{
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, msg);
}
static void
wbcg_error_invalid (CommandContext *cc, char const *msg, char const * value)
{
	char *buf = g_strconcat (msg, " : ", value, NULL);
	gnumeric_notice ((WorkbookControlGUI *)cc, GNOME_MESSAGE_BOX_ERROR, buf);
	g_free (buf);
}
static void
wbcg_error_splits_array (CommandContext *context,
			 char const *cmd, Range const *array)
{
	char *message;

	message = g_strdup_printf (_("Would split array %s"), range_name (array));
	gnumeric_error_invalid (context, cmd, message);
	g_free (message);
}

static void
wbcg_error_error_info (CommandContext *context,
                       ErrorInfo *error)
{
	gnumeric_error_info_dialog_show (WORKBOOK_CONTROL_GUI (context), error);
}

static char const * const preset_zoom [] = {
	"200%",
	"100%",
	"75%",
	"50%",
	"25%",
	NULL
};

/**
 * workbook_close_if_user_permits : If the workbook is dirty the user is
 *  		prompted to see if they should exit.
 *
 * Returns : TRUE is the book remains open.
 *           FALSE if it is closed.
 */
static gboolean
workbook_close_if_user_permits (WorkbookControlGUI *wbcg, WorkbookView *wb_view)
{
	gboolean   can_close = TRUE;
	gboolean   done      = FALSE;
	int        iteration = 0;
	Workbook  *wb = wb_view_workbook (wb_view);
	static int in_can_close;

	g_return_val_if_fail (IS_WORKBOOK (wb), TRUE);

	if (in_can_close)
		return FALSE;
	in_can_close = TRUE;

	while (workbook_is_dirty (wb) && !done) {
		GtkWidget *d;
		int button;
		char *msg;

		iteration++;

		if (wb->filename)
			msg = g_strdup_printf (
				_("Workbook %s has unsaved changes, save them?"),
				g_basename (wb->filename));
		else
			msg = g_strdup (_("Workbook has unsaved changes, save them?"));

		d = gnome_message_box_new (msg,
			GNOME_MESSAGE_BOX_WARNING,
			GNOME_STOCK_BUTTON_YES,
			GNOME_STOCK_BUTTON_NO,
			GNOME_STOCK_BUTTON_CANCEL,
			NULL);

		gtk_window_set_position (GTK_WINDOW (d), GTK_WIN_POS_MOUSE);
		button = gnome_dialog_run_and_close (GNOME_DIALOG (d));
		g_free (msg);

		switch (button) {
		case 0: /* YES */
			done = gui_file_save (wbcg, wb_view);
			break;

		case 1: /* NO */
			can_close = TRUE;
			done      = TRUE;
			workbook_set_dirty (wb, FALSE);
			break;

		case -1:
		case 2: /* CANCEL */
			can_close = FALSE;
			done      = TRUE;
			break;
		}
	}

	in_can_close = FALSE;

	if (can_close) {
		workbook_unref (wb);
		return FALSE;
	} else
		return TRUE;
}

/*
 * wbcg_close_control:
 *
 * Returns TRUE if the control should not be closed.
 */
static gboolean
wbcg_close_control (WorkbookControlGUI *wbcg)
{
	WorkbookView *wb_view = wb_control_view (WORKBOOK_CONTROL (wbcg));

	g_return_val_if_fail (IS_WORKBOOK_VIEW (wb_view), TRUE);
	g_return_val_if_fail (wb_view->wb_controls != NULL, TRUE);

	/*
	 * If we were editing when the quit request came make sure we don't
	 * lose any entered text
	 */
	if (!wbcg_edit_finish (wbcg, TRUE))
		return TRUE;

	/* This is the last control */
	if (wb_view->wb_controls->len <= 1) {
		Workbook *wb = wb_view_workbook (wb_view);

		g_return_val_if_fail (IS_WORKBOOK (wb), TRUE);
		g_return_val_if_fail (wb->wb_views != NULL, TRUE);

		/* This is the last view */
		if (wb->wb_views->len <= 1)
			return workbook_close_if_user_permits (wbcg, wb_view);

		gtk_object_unref (GTK_OBJECT (wb_view));
	} else
		gtk_object_unref (GTK_OBJECT (wbcg));

	return FALSE;
}

/****************************************************************************/

static void
cb_file_new (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	/* FIXME : we should have a user configurable setting
	 * for how many sheets to create by default
	 */
	(void) workbook_control_gui_new (NULL, workbook_new_with_sheets (1));
}

static void
cb_file_open (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gui_file_open (wbcg);
}

static void
cb_file_import (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gui_file_import (wbcg);
}

static void
cb_file_save (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gui_file_save (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
}

static void
cb_file_save_as (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gui_file_save_as (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
}

#ifdef ENABLE_BONOBO
#ifdef ENABLE_EVOLUTION
static void
cb_file_send (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	CORBA_Object composer;
	CORBA_Environment ev;
	BonoboStream *stream;
#if 0
	Bonobo_StorageInfo *info;
#else
	const gchar *filename;
#endif
	GNOME_Evolution_Composer_AttachmentData *attachment_data;

	CORBA_exception_init (&ev);
	composer = oaf_activate_from_id ("OAFIID:GNOME_Evolution_Mail_Composer",
					 0, NULL, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Unable to start composer: %s",
			   bonobo_exception_get_text (&ev));
		CORBA_exception_free (&ev);
		return;
	}

	stream = bonobo_stream_mem_create (NULL, 0, FALSE, TRUE);
	gui_file_save_to_stream (stream, wbcg,
				 wb_control_view (WORKBOOK_CONTROL (wbcg)),
				 NULL, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Unable to save workbook to stream: %s",
			   bonobo_exception_get_text (&ev));
		CORBA_exception_free (&ev);
		bonobo_object_release_unref (composer, NULL);
		return;
	}

	attachment_data = GNOME_Evolution_Composer_AttachmentData__alloc ();
	attachment_data->_buffer = BONOBO_STREAM_MEM (stream)->buffer;
	attachment_data->_length = BONOBO_STREAM_MEM (stream)->size;
	BONOBO_STREAM_MEM (stream)->buffer = NULL;
	bonobo_object_unref (BONOBO_OBJECT (stream));
#if 0
	/*
	 * FIXME: Enable this code when BonoboStreamMemory has [get,set]Info
	 * capabilities.
	 */
	info = Bonobo_Stream_getInfo (BONOBO_OBJREF (stream), 0, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Could not get information about stream: %s",
			   bonobo_exception_get_text (&ev));
		CORBA_exception_free (&ev);
		bonobo_object_release_unref (composer, NULL);
		return;
	}
	GNOME_Evolution_Composer_attachData (composer, info->content_type,
					     info->name, info->name, FALSE,
					     attachment_data, &ev);
	CORBA_free (info);
#else
	filename = workbook_get_filename (wb_view_workbook (wb_control_view (
						WORKBOOK_CONTROL (wbcg))));
	GNOME_Evolution_Composer_attachData (composer, "application/x-gnumeric",
					     filename, filename, FALSE,
					     attachment_data, &ev);
#endif
	CORBA_free (attachment_data);
	if (BONOBO_EX (&ev)) {
		g_warning ("Unable to attach image: %s", 
			   bonobo_exception_get_text (&ev));
		CORBA_exception_free (&ev);
		bonobo_object_release_unref (composer, NULL);
		return;
	}

	GNOME_Evolution_Composer_show (composer, &ev);
	if (BONOBO_EX (&ev)) {
		g_warning ("Unable to show composer: %s", 
			   bonobo_exception_get_text (&ev));
		CORBA_exception_free (&ev);
		bonobo_object_release_unref (composer, NULL);
		return;
	}

	CORBA_exception_free (&ev); 
}
#endif
#endif

static void
cb_file_print_setup (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	dialog_printer_setup (wbcg, sheet);
}

static void
cb_file_print (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet_print (wbcg, sheet, FALSE, PRINT_ACTIVE_SHEET);
}

static void
cb_file_print_preview (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	sheet_print (wbcg, sheet, TRUE, PRINT_ACTIVE_SHEET);
}

static void
cb_file_summary (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Workbook *wb = wb_control_workbook (wbc);
	dialog_summary_update (wbcg, wb->summary_info);
}

static void
cb_file_close (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	wbcg_close_control (wbcg);
}

static void
cb_file_quit (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GList *ptr, *workbooks;

	/* If we are still loading initial files, short circuit */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		return;
	}

	/* If we were editing when the quit request came in abort the edit. */
	wbcg_edit_finish (wbcg, FALSE);

	/* list is modified during workbook destruction */
	workbooks = g_list_copy (application_workbook_list ());

	for (ptr = workbooks; ptr != NULL ; ptr = ptr->next) {
		Workbook *wb = ptr->data;
		WorkbookView *wb_view;

		g_return_if_fail (IS_WORKBOOK (wb));
		g_return_if_fail (wb->wb_views != NULL);

		if (wb_control_workbook (wbc) == wb)
			continue;
		wb_view = g_ptr_array_index (wb->wb_views, 0);
		workbook_close_if_user_permits (wbcg, wb_view);
	}
	workbook_close_if_user_permits (wbcg, wb_control_view (wbc));

	g_list_free (workbooks);
}

/****************************************************************************/

static void
cb_edit_clear_all (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_clear_selection (wbc, wb_control_cur_sheet (wbc),
			     CLEAR_VALUES | CLEAR_FORMATS | CLEAR_COMMENTS);
}

static void
cb_edit_clear_formats (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_clear_selection (wbc, wb_control_cur_sheet (wbc),
			     CLEAR_FORMATS);
}

static void
cb_edit_clear_comments (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_clear_selection (wbc, wb_control_cur_sheet (wbc),
			     CLEAR_COMMENTS);
}

static void
cb_edit_clear_content (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	cmd_clear_selection (wbc, wb_control_cur_sheet (wbc),
			     CLEAR_VALUES);
}

static void
cb_edit_select_all (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_all (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_row (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_cur_row (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_col (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_cur_col (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_array (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_cur_array (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_depend (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_select_cur_depends (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)));
}

static void
cb_edit_undo (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wbcg_edit_finish (wbcg, FALSE);
	command_undo (wbc);
}

static void
cb_undo_combo (GtkWidget *widget, gint num, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	int i;

	wbcg_edit_finish (wbcg, FALSE);
	for (i = 0; i < num; i++)
		command_undo (wbc);
}

static void
cb_edit_redo (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wbcg_edit_finish (wbcg, FALSE);
	command_redo (wbc);
}

static void
cb_redo_combo (GtkWidget *widget, gint num, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	int i;

	wbcg_edit_finish (wbcg, FALSE);
	for (i = 0; i < num; i++)
		command_redo (wbc);
}

static void
cb_edit_cut (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetControlGUI *scg;

	if (sheet_to_page_index (wbcg, wb_control_cur_sheet (wbc), &scg) >= 0) {
		SheetControl *sc = (SheetControl *) scg;
		if (scg->current_object != NULL)
			gtk_object_destroy (GTK_OBJECT (scg->current_object));
		scg_mode_edit (sc);
		sheet_selection_cut (wbc, sheet);
	}
}

static void
cb_edit_copy (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	sheet_selection_copy (wbc, sheet);
}

static void
cb_edit_paste (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	cmd_paste_to_selection (wbc, sheet, PASTE_DEFAULT);
}

static void
cb_edit_paste_special (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	int flags = dialog_paste_special (wbcg);
	if (flags != 0)
		cmd_paste_to_selection (wbc, sheet, flags);
}

static void
cb_edit_delete (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	dialog_delete_cells (wbcg, sheet);
}

static void
cb_sheet_remove (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetControlGUI *res;

	if (sheet_to_page_index (wbcg, wb_control_cur_sheet (wbc), &res) >= 0)
		delete_sheet_if_possible (NULL, res);
}

static void
cb_edit_duplicate_sheet (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *old_sheet = wb_control_cur_sheet (wbc);
     	Sheet *new_sheet = sheet_duplicate (old_sheet);

	workbook_sheet_attach (wb_control_workbook (wbc),
			       new_sheet,
			       old_sheet);
	sheet_set_dirty (new_sheet, TRUE);
}


static int
cb_edit_search_replace_query (SearchReplaceQuery q, SearchReplace *sr, ...)
{
	int res = 0;
	va_list pvar;
	WorkbookControlGUI *wbcg = sr->user_data;

	va_start (pvar, sr);

	switch (q) {
	case SRQ_fail: {
		Cell *cell = va_arg (pvar, Cell *);
		const char *old_text = va_arg (pvar, const char *);
		const char *new_text = va_arg (pvar, const char *);

		char *err;

		err = g_strdup_printf
			(_("In cell %s, the current contents\n"
			   "        %s\n"
			   "would have been replaced by\n"
			   "        %s\n"
			   "which is invalid.\n\n"
			   "The replace has been aborted "
			   "and nothing has been changed."),
			 cell_pos_name (&cell->pos),
			 old_text,
			 new_text);

		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR,
				 err);
		g_free (err);
	}

	}

	va_end (pvar);
	return res;
}

static gboolean
cb_edit_search_replace_action (WorkbookControlGUI *wbcg,
			       SearchReplace *sr)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);

	sr->query_func = cb_edit_search_replace_query;
	sr->user_data = wbcg;

	return cmd_search_replace (wbc, sheet, sr);
}


static void
cb_edit_search_replace (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_search_replace (wbcg, cb_edit_search_replace_action);
}

static void
cb_edit_goto (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_goto_cell (wbcg);
}

static void
cb_edit_recalc (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	/* sheet_dump_dependencies (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg))); */
	workbook_recalc_all (wb_control_workbook (WORKBOOK_CONTROL (wbcg)));
}

/****************************************************************************/

static void
cb_view_zoom (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_zoom (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_view_freeze_panes (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);

	if (scg->active_panes == 1) {
		CellPos frozen_tl, unfrozen_tl;
		GnumericCanvas const *gcanvas = scg_pane (scg, 0);
		frozen_tl = gcanvas->first;
		unfrozen_tl = sheet->edit_pos;

		if (unfrozen_tl.col <= gcanvas->first.col ||
		    unfrozen_tl.col > gcanvas->last_full.col)
			unfrozen_tl.col = (gcanvas->first.col + gcanvas->last_full.col) / 2;
		if (unfrozen_tl.row <= gcanvas->first.row ||
		    unfrozen_tl.row > gcanvas->last_full.row)
			unfrozen_tl.row = (gcanvas->first.row + gcanvas->last_full.row) / 2;

		if (unfrozen_tl.col > frozen_tl.col &&
		    unfrozen_tl.row > frozen_tl.row)
			sheet_freeze_panes (sheet, &frozen_tl, &unfrozen_tl);
	} else
		sheet_freeze_panes (sheet, NULL, NULL);
}

static void
cb_view_new_shared (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wb_control_wrapper_new (wbc, wb_control_view (wbc),
				wb_control_workbook (wbc));
}

static void
cb_view_new_unshared (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wb_control_wrapper_new (wbc, NULL,
				wb_control_workbook (wbc));
}

/****************************************************************************/

static void
cb_insert_current_date (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	cmd_set_date_time (wbc, sheet, &sheet->edit_pos, TRUE);
}

static void
cb_insert_current_time (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	cmd_set_date_time (wbc, sheet, &sheet->edit_pos, FALSE);
}

static void
cb_define_name (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_define_names (wbcg);
}

static void
cb_insert_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	workbook_sheet_add (wb_control_workbook (WORKBOOK_CONTROL (wbcg)),
			    NULL, TRUE);
}

static void
cb_insert_rows (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Range const *sel;

	wbcg_edit_finish (wbcg, FALSE);

	/* TODO : No need to check simplicty.  XL applies for each non-discrete
	 * selected region, (use selection_apply).  Arrays and Merged regions
	 * are permitted.
	 */
	if (!(sel = selection_first_range (sheet, wbc, _("Insert rows"))))
		return;
	cmd_insert_rows (wbc, sheet, sel->start.row, range_height (sel));
}

static void
cb_insert_cols (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Range const *sel;

	wbcg_edit_finish (wbcg, FALSE);

	/* TODO : No need to check simplicty.  XL applies for each non-discrete
	 * selected region, (use selection_apply).  Arrays and Merged regions
	 * are permitted.
	 */
	if (!(sel = selection_first_range (sheet, wbc, _("Insert columns"))))
		return;
	cmd_insert_cols (wbc, sheet, sel->start.col, range_width (sel));
}

static void
cb_insert_cells (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_insert_cells (wbcg, wb_control_cur_sheet (wbc));
}

#ifdef ENABLE_BONOBO
static void
insert_bonobo_object (WorkbookControlGUI *wbcg, char const **interfaces)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	SheetControl *sc = (SheetControl *) scg;
	char  *obj_id, *msg;
	SheetObject *so = NULL;

	obj_id = bonobo_selector_select_id (_("Select an object to add"),
					    interfaces);

	if (obj_id != NULL) {
		so = sheet_object_container_new_object (
			sc->sheet->workbook, obj_id);

		if (so != NULL) {
			scg_mode_create_object (scg, so);
			return;
		}
		msg = g_strdup_printf (_("Unable to create object of type \'%s\'"),
				       obj_id);
		gnumeric_notice (wbcg, GNOME_MESSAGE_BOX_ERROR, msg);
		g_free (msg);
	}
	scg_mode_edit (sc);
}

static void
cb_insert_bonobo_object (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	insert_bonobo_object (wbcg, NULL);
}
#endif

static void
cb_insert_comment (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	dialog_cell_comment (wbcg, sheet, &sheet->edit_pos);
}

/****************************************************************************/

static void
cb_sheet_change_name (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	char *new_name;

	new_name = dialog_get_sheet_name (wbcg, sheet->name_unquoted);
	if (!new_name)
		return;

	cmd_rename_sheet (wbc, sheet->name_unquoted, new_name);
	g_free (new_name);
}

static void
cb_sheet_order (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
        dialog_sheet_order (wbcg);
}

#ifdef ENABLE_BONOBO
static gboolean
toggle_util (Bonobo_UIComponent_EventType type,
	     char const *state, gboolean *flag)
{
	if (type == Bonobo_UIComponent_STATE_CHANGED) {
		gboolean new_state = atoi (state);

		if (!(*flag) != !new_state) {
			*flag = !(*flag);
			return TRUE;
		}
	}
	return FALSE;
}

#define TOGGLE_HANDLER(flag, code)					\
static void								\
cb_sheet_pref_ ## flag (BonoboUIComponent           *component,		\
			const char                  *path,		\
			Bonobo_UIComponent_EventType type,		\
			const char                  *state,		\
			gpointer                     user_data)		\
{									\
	WorkbookControlGUI *wbcg;					\
									\
	wbcg = WORKBOOK_CONTROL_GUI (user_data);			\
	g_return_if_fail (wbcg != NULL);				\
									\
	if (!wbcg->updating_ui) {					\
		Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)); \
		g_return_if_fail (IS_SHEET (sheet));			\
									\
		if (toggle_util (type, state, &sheet->flag)) 		\
			code						\
	}								\
}
#define	TOGGLE_REGISTER(flag, name) 					\
	bonobo_ui_component_set_prop (wbcg->uic, "/commands/" #name,	\
				      "state", "1", NULL);		\
	bonobo_ui_component_add_listener (wbcg->uic, #name,		\
					  cb_sheet_pref_ ## flag, wbcg)
#else
#define TOGGLE_HANDLER(flag, code)					\
static void								\
cb_sheet_pref_ ## flag (GtkWidget *ignored, WorkbookControlGUI *wbcg)	\
{									\
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));		\
									\
	if (!wbcg->updating_ui) {					\
		Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));	\
		g_return_if_fail (IS_SHEET (sheet));			\
									\
		sheet->flag = !sheet->flag;				\
		code							\
	}								\
}
#endif

TOGGLE_HANDLER (display_formulas,{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	WORKBOOK_FOREACH_DEPENDENT
		(wb, dep, {
			if (DEPENDENT_TYPE (dep) == DEPENDENT_CELL)
				sheet_cell_calc_span (DEP_TO_CELL (dep), SPANCALC_RE_RENDER);
		});
	sheet_adjust_preferences (sheet, TRUE, FALSE);
})
TOGGLE_HANDLER (hide_zero, sheet_adjust_preferences (sheet, TRUE, FALSE);)
TOGGLE_HANDLER (hide_grid, sheet_adjust_preferences (sheet, TRUE, FALSE);)
TOGGLE_HANDLER (hide_col_header, sheet_adjust_preferences (sheet, FALSE, FALSE);)
TOGGLE_HANDLER (hide_row_header, sheet_adjust_preferences (sheet, FALSE, FALSE);)
TOGGLE_HANDLER (display_outlines, sheet_adjust_preferences (sheet, TRUE, TRUE);)
TOGGLE_HANDLER (outline_symbols_below, {
		sheet_adjust_outline_dir (sheet, FALSE);
		sheet_adjust_preferences (sheet, TRUE, TRUE);
})
TOGGLE_HANDLER (outline_symbols_right,{
		sheet_adjust_outline_dir (sheet, TRUE);
		sheet_adjust_preferences (sheet, TRUE, TRUE);
})

/****************************************************************************/

static void
cb_format_cells (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_cell_format (wbcg, wb_control_cur_sheet (wbc), FD_CURRENT);
}

static void
cb_autoformat (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_autoformat (wbcg);
}

static void
cb_workbook_attr (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_workbook_attr (wbcg);
}

static void
cb_tools_plugins (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_plugin_manager (wbcg);
}

static void
cb_tools_autocorrect (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_autocorrect (wbcg);
}

static void
cb_tools_auto_save (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_autosave (wbcg);
}

static void
cb_tools_goal_seek (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_goal_seek (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_solver (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_solver (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_data_analysis (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_data_analysis (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_data_sort (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_cell_sort (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_data_filter (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_advanced_filter (wbcg);
}

#if 0
static void
cb_data_validate (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_validate (wbcg, wb_control_cur_sheet (wbc));
}
#endif

static void
cb_data_consolidate (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	
	dialog_consolidate (wbcg, wb_control_cur_sheet (wbc));
}

static void
hide_show_detail (WorkbookControlGUI *wbcg, gboolean show)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	char const *operation = show ? _("Show") : _("Hide");
	Range const *r = selection_first_range (sheet, wbc, operation);
	gboolean is_cols;

	/* We only operate on a single selection */
	if (r == NULL)
		return;

	/* Do we need to ask the user what he/she wants to group/ungroup? */
	if (range_is_full (r, TRUE) ^ range_is_full (r, FALSE))
		is_cols = !range_is_full (r, TRUE);
	else
		if (!dialog_choose_cols_vs_rows (wbcg, operation, &is_cols))
			return;

	/* This operation can only be performed on a whole existing group */
	if (sheet_col_row_can_group (sheet, is_cols ? r->start.col : r->start.row,
				     is_cols ? r->end.col : r->end.row, is_cols)) {
		gnumeric_error_invalid (COMMAND_CONTEXT (wbc), operation,
					_("can only be performed on an existing group"));
		return;
	}
	
	cmd_colrow_hide_selection (wbc, sheet, is_cols, show);
}

static void
cb_data_hide_detail (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	hide_show_detail (wbcg, FALSE);
}

static void
cb_data_show_detail (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	hide_show_detail (wbcg, TRUE);
}

static void
group_ungroup_colrow (WorkbookControlGUI *wbcg, gboolean group)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	char const *operation = group ? _("Group") : _("Ungroup");
	Range const *r = selection_first_range (sheet, wbc, operation);
	gboolean is_cols;

	/* We only operate on a single selection */
	if (r == NULL)
		return;

	/* Do we need to ask the user what he/she wants to group/ungroup? */
	if (range_is_full (r, TRUE) ^ range_is_full (r, FALSE))
		is_cols = !range_is_full (r, TRUE);
	else
		if (!dialog_choose_cols_vs_rows (wbcg, operation, &is_cols))
			return;

	cmd_group (wbc, sheet, is_cols, group);
}

static void
cb_data_group (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	group_ungroup_colrow (wbcg, TRUE);
}

static void
cb_data_ungroup (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	group_ungroup_colrow (wbcg, FALSE);
}

static void
cb_help_about (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_about (wbcg);
}

static void
cb_autosum (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	GtkEntry *entry;
	gchar *txt;

	if (wbcg->editing)
		return;

	entry = GTK_ENTRY (wbcg_get_entry (wbcg));
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=sum(", 5)) {
		wbcg_edit_start (wbcg, TRUE, TRUE);
		gtk_entry_set_text (entry, "=sum()");
		gtk_entry_set_position (entry, 5);
	} else {
		wbcg_edit_start (wbcg, FALSE, TRUE);

		/*
		 * FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_entry_set_position (entry, entry->text_length-1);
	}
}

static void
cb_formula_guru (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_formula_guru (wbcg);
}

static void
sort_by_rows (WorkbookControlGUI *wbcg, int asc)
{
	Sheet *sheet;
	Range *sel;
	Range const *tmp;
	SortData *data;
	SortClause *clause;
	int numclause, i;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	g_return_if_fail (IS_SHEET (sheet));

	if (!(tmp = selection_first_range (sheet, WORKBOOK_CONTROL (wbcg), _("Sort"))))
		return;

	sel = range_dup (tmp);
	range_clip_to_finite (sel, sheet);

	numclause = range_width (sel);
	clause = g_new0 (SortClause, numclause);
	for (i=0; i < numclause; i++) {
		clause[i].offset = i;
		clause[i].asc = asc;
		clause[i].cs = FALSE;
		clause[i].val = TRUE;
	}

	data = g_new (SortData, 1);
	data->sheet = sheet;
	data->range = sel;
	data->num_clause = numclause;
	data->clauses = clause;

	/* Hard code sorting by row.  I would prefer not to, but user testing
	 * indicates
	 * - that the button should always does the same things
	 * - that the icon matches the behavior
	 * - XL does this.
	 */
	data->top = TRUE;

	if (range_has_header (data->sheet, data->range, data->top))
		data->range->start.row += 1;

	cmd_sort (WORKBOOK_CONTROL (wbcg), data);
}

static void
cb_sort_ascending (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	sort_by_rows (wbcg, 0);
}

static void
cb_sort_descending (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	sort_by_rows (wbcg, 1);
}

#ifdef ENABLE_BONOBO
static void
cb_launch_graph_guru (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_graph_guru (wbcg);
}

static void
select_component_id (WorkbookControlGUI *wbcg, char const *interface)
{
	char const *required_interfaces [2];

	required_interfaces [0] = interface;
	required_interfaces [1] = NULL;
	insert_bonobo_object (wbcg, required_interfaces);
}

static void
cb_insert_component (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	select_component_id (wbcg, "IDL:Bonobo/Embeddable:1.0");
}

static void
cb_insert_shaped_component (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	select_component_id (wbcg, "IDL:Bonobo/Canvas/Item:1.0");
}
#endif

#ifndef ENABLE_BONOBO
/*
 * Hide/show some toolbar items depending on the toolbar orientation
 */
static void
workbook_standard_toolbar_orient (GtkToolbar *toolbar,
				  GtkOrientation orientation,
				  gpointer data)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)data;

	if (orientation == GTK_ORIENTATION_HORIZONTAL)
		gtk_widget_show (wbcg->zoom_entry);
	else
		gtk_widget_hide (wbcg->zoom_entry);
}


/* File menu */
static GnomeUIInfo workbook_menu_file [] = {
        GNOMEUIINFO_MENU_NEW_ITEM (N_("_New"), N_("Create a new spreadsheet"),
				  cb_file_new, NULL),

	GNOMEUIINFO_MENU_OPEN_ITEM (cb_file_open, NULL),
	GNOMEUIINFO_ITEM_STOCK (N_("_Import..."), N_("Imports a file"),
				cb_file_import, GNOME_STOCK_MENU_OPEN),
	GNOMEUIINFO_ITEM_STOCK (N_("_Save"), N_("Save"),
				cb_file_save,
				"Menu_Gnumeric_Save"),
	GNOMEUIINFO_ITEM_STOCK (N_("Save _As..."), N_("Save with a new name or format"),
				cb_file_save_as,
				"Menu_Gnumeric_SaveAs"),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_PRINT_SETUP_ITEM (cb_file_print_setup, NULL),
	GNOMEUIINFO_MENU_PRINT_ITEM (cb_file_print, NULL),
	GNOMEUIINFO_ITEM_STOCK (N_("Print Pre_view"), N_("Print Preview"),
				cb_file_print_preview,
				"Menu_Gnumeric_PrintPreview"),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("Su_mmary..."),
			       N_("Summary information"),
			       cb_file_summary),

	GNOMEUIINFO_MENU_CLOSE_ITEM (cb_file_close, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_EXIT_ITEM (cb_file_quit, NULL),
	GNOMEUIINFO_END
};

/* Edit menu */
static GnomeUIInfo workbook_menu_edit_clear [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_All"),
		N_("Clear the selected cells' formats, comments, and contents"),
		cb_edit_clear_all),
	GNOMEUIINFO_ITEM_NONE (N_("_Formats"),
		N_("Clear the selected cells' formats"),
		cb_edit_clear_formats),
	GNOMEUIINFO_ITEM_NONE (N_("Co_mments"),
		N_("Clear the selected cells' comments"),
		cb_edit_clear_comments),
	GNOMEUIINFO_ITEM_NONE (N_("_Content"),
		N_("Clear the selected cells' contents"),
		cb_edit_clear_content),
	GNOMEUIINFO_END
};

#define GNOME_MENU_EDIT_PATH D_("_Edit/")

static GnomeUIInfo workbook_menu_edit_select [] = {
	{ GNOME_APP_UI_ITEM, N_("Select _All"),
	  N_("Select all cells in the spreadsheet"),
	  cb_edit_select_all, NULL,
	  NULL, 0, 0, 'a', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Row"),
	  N_("Select an entire row"),
	  cb_edit_select_row, NULL,
	  NULL, 0, 0, ' ', GDK_MOD1_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Column"),
	  N_("Select an entire column"),
	  cb_edit_select_col, NULL,
	  NULL, 0, 0, ' ', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select Arra_y"),
	  N_("Select an array of cells"),
	  cb_edit_select_array, NULL,
	  NULL, 0, 0, '/', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Depends"),
	  N_("Select all the cells that depend on the current edit cell."),
	  cb_edit_select_depend, NULL,
	  NULL, 0, 0, 0, 0 },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_edit_sheet [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Duplicate"),
		N_("Make a copy of the current sheet"),
		cb_edit_duplicate_sheet),

	GNOMEUIINFO_ITEM_NONE (N_("_Insert"),
		N_("Insert a new sheet"),
		cb_insert_sheet),

	GNOMEUIINFO_ITEM_NONE (N_("Re_name..."),
		N_("Rename the current sheet"),
		cb_sheet_change_name),

	GNOMEUIINFO_ITEM_NONE (N_("Re-_Order Sheets..."),
		N_("Change the order the sheets are displayed"),
		cb_sheet_order),

	GNOMEUIINFO_ITEM_NONE (N_("_Remove"),
		N_("Irrevocably remove an entire sheet"),
		cb_sheet_remove),

	GNOMEUIINFO_END
};


static GnomeUIInfo workbook_menu_edit [] = {
	{ GNOME_APP_UI_ITEM, N_("_Undo"),
		N_("Undo the last action"), cb_edit_undo, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, "Menu_Gnumeric_Undo",
		'z', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Redo"),
		N_("Redo the undone action"), cb_edit_redo, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, "Menu_Gnumeric_Redo",
		'r', GDK_CONTROL_MASK },

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("Cu_t"),
		N_("Cut the selection"), cb_edit_cut, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, "Menu_Gnumeric_Cut",
		'x', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Copy"),
		N_("Copy the selection"), cb_edit_copy, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, "Menu_Gnumeric_Copy",
		'c', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("_Paste"),
		N_("Paste the clipboard"), cb_edit_paste, NULL, NULL,
		GNOME_APP_PIXMAP_STOCK, "Menu_Gnumeric_Paste",
		'v', GDK_CONTROL_MASK },

	GNOMEUIINFO_ITEM_NONE (N_("P_aste special..."),
		N_("Paste with optional filters and transformations"),
		cb_edit_paste_special),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("C_lear"), workbook_menu_edit_clear),

	GNOMEUIINFO_ITEM_NONE (N_("_Delete..."),
		N_("Remove selected cells, shifting other into their place"),
		cb_edit_delete),
	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("S_heet"), workbook_menu_edit_sheet),

	GNOMEUIINFO_SUBTREE(N_("_Select"), workbook_menu_edit_select),

	{ GNOME_APP_UI_ITEM, N_("Search and Replace..."),
		  N_("Search for some text and replace it with something else"),
	          cb_edit_search_replace,
		  NULL, NULL,
		  GNOME_APP_PIXMAP_STOCK, "Menu_Gnumeric_SearchAndReplace", GDK_F6, 0 },

	{ GNOME_APP_UI_ITEM, N_("_Goto cell..."),
		  N_("Jump to a specified cell"),
		  cb_edit_goto,
		  NULL, NULL, 0, 0, GDK_F5, 0 },

	{ GNOME_APP_UI_ITEM, N_("Recalculate"),
		  N_("Recalculate the spreadsheet"),
		  cb_edit_recalc,
		  NULL, NULL, 0, 0, GDK_F9, 0 },

	GNOMEUIINFO_END
};

/* View menu */

static GnomeUIInfo workbook_menu_view [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Zoom..."),
		N_("Zoom the spreadsheet in or out"),
		cb_view_zoom),
	GNOMEUIINFO_ITEM_NONE (N_("_Freeze..."),
		N_("Freeze the top left of the sheet"),
		cb_view_freeze_panes),
	GNOMEUIINFO_ITEM_NONE (N_("New _Shared"),
		N_("Create a new shared view of the workbook"),
		cb_view_new_shared),
	GNOMEUIINFO_ITEM_NONE (N_("New _Unshared"),
		N_("Create a new unshared view of the workbook"),
		cb_view_new_unshared),
	GNOMEUIINFO_END
};

/* Insert menu */

static GnomeUIInfo workbook_menu_insert_special [] = {
	/* Default <Ctrl-;> (control semi-colon) to insert the current date */
	{ GNOME_APP_UI_ITEM, N_("Current _date"),
	  N_("Insert the current date into the selected cell(s)"),
	  cb_insert_current_date,
	  NULL, NULL, 0, 0, ';', GDK_CONTROL_MASK },

	/* Default <Ctrl-:> (control colon) to insert the current time */
	{ GNOME_APP_UI_ITEM, N_("Current _time"),
	  N_("Insert the current time into the selected cell(s)"),
	  cb_insert_current_time,
	  NULL, NULL, 0, 0, ':', GDK_CONTROL_MASK },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_names [] = {
	{ GNOME_APP_UI_ITEM, N_("_Define..."),
	  N_("Edit sheet and workbook names"),
	  cb_define_name, NULL, NULL, 0, 0,
	  GDK_F3, GDK_CONTROL_MASK },
#if 0
	GNOMEUIINFO_ITEM_NONE (N_("_Auto generate names..."),
		N_("Use the current selection to create names"),
		cb_auto_generate__named_expr),
#endif
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_insert [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Sheet"),
		N_("Insert a new spreadsheet"),
		cb_insert_sheet),
	GNOMEUIINFO_ITEM_STOCK (N_("_Rows"),
		N_("Insert new rows"),
		cb_insert_rows, "Menu_Gnumeric_RowAdd"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Columns"),
		N_("Insert new columns"),
		cb_insert_cols, "Menu_Gnumeric_ColumnAdd"),
	GNOMEUIINFO_ITEM_NONE (N_("C_ells..."),
		N_("Insert new cells"),
		cb_insert_cells),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("_Name"), workbook_menu_names),

	GNOMEUIINFO_ITEM_STOCK (N_("_Add / Modify comment..."),
		N_("Edit the selected cell's comment"),
		cb_insert_comment, "Menu_Gnumeric_CommentEdit"),

	GNOMEUIINFO_SUBTREE(N_("S_pecial"), workbook_menu_insert_special),
	GNOMEUIINFO_END
};

/* Format menu */
static GnomeUIInfo workbook_menu_format_column [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Auto fit selection"),
		N_("Ensure columns are just wide enough to display content"),
		workbook_cmd_format_column_auto_fit),
	GNOMEUIINFO_ITEM_STOCK (N_("_Width..."),
		N_("Change width of the selected columns"),
		sheet_dialog_set_column_width, "Menu_Gnumeric_ColumnSize"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Hide"),
		N_("Hide the selected columns"),
		workbook_cmd_format_column_hide, "Menu_Gnumeric_ColumnHide"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Unhide"),
		N_("Make any hidden columns in the selection visible"),
		workbook_cmd_format_column_unhide, "Menu_Gnumeric_ColumnUnhide"),
	GNOMEUIINFO_ITEM_NONE (N_("_Standard Width"),
		N_("Change the default column width"),
		workbook_cmd_format_column_std_width),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_row [] = {
	GNOMEUIINFO_ITEM_STOCK (N_("H_eight..."),
		N_("Change height of the selected rows"),
		sheet_dialog_set_row_height, "Menu_Gnumeric_RowSize"),
	GNOMEUIINFO_ITEM_NONE (N_("_Auto fit selection"),
		N_("Ensure rows are just tall enough to display content"),
		workbook_cmd_format_row_auto_fit),
	GNOMEUIINFO_ITEM_STOCK (N_("_Hide"),
		N_("Hide the selected rows"),
		workbook_cmd_format_row_hide, "Menu_Gnumeric_RowHide"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Unhide"),
		N_("Make any hidden rows in the selection visible"),
		workbook_cmd_format_row_unhide, "Menu_Gnumeric_RowUnhide"),
	GNOMEUIINFO_ITEM_NONE (N_("_Standard Height"),
		N_("Change the default row height"),
		workbook_cmd_format_row_std_height),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_sheet [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Change name"),
		N_("Edit the name of the current sheet"),
		cb_sheet_change_name),
	GNOMEUIINFO_ITEM_NONE (N_("Re-_Order Sheets..."),
		N_("Change the order the sheets are displayed"),
		cb_sheet_order),
		
	/* Default <Ctrl-`> (control backquote) to insert toggle formula/value display */
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Display _Formulas"),
		N_("Display the value of a formula or the formula itself"),
		cb_sheet_pref_display_formulas, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, '`', GDK_CONTROL_MASK, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Zeros"),
		N_("Toggle whether or not to display zeros as blanks"),
		cb_sheet_pref_hide_zero, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Gridlines"),
		N_("Toggle whether or not to display gridlines"),
		cb_sheet_pref_hide_grid, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Column Header"),
		N_("Toggle whether or not to display column headers"),
		cb_sheet_pref_hide_col_header, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Row Header"),
		N_("Toggle whether or not to display row headers"),
		cb_sheet_pref_hide_row_header, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format [] = {
	/* Default <Ctrl-1> invoke the format dialog */
	{ GNOME_APP_UI_ITEM, N_("_Cells..."),
		N_("Modify the formatting of the selected cells"),
		cb_format_cells, NULL, NULL, 0, 0, GDK_1, GDK_CONTROL_MASK },

	GNOMEUIINFO_SUBTREE(N_("C_olumn"), workbook_menu_format_column),
	GNOMEUIINFO_SUBTREE(N_("_Row"),    workbook_menu_format_row),
	GNOMEUIINFO_SUBTREE(N_("_Sheet"),  workbook_menu_format_sheet),

	GNOMEUIINFO_ITEM_NONE (N_("_Autoformat..."),
			      N_("Format a region of cells according to a pre-defined template"),
			      cb_autoformat),

	GNOMEUIINFO_ITEM_NONE (N_("_Workbook..."),
		N_("Modify the workbook attributes"),
		cb_workbook_attr),
	GNOMEUIINFO_END
};

/* Tools menu */
static GnomeUIInfo workbook_menu_tools [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Plug-ins..."),
		N_("Manage available plugin modules."),
		cb_tools_plugins),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("Auto _Correct..."),
		N_("Automatically perform simple spell checking"),
		cb_tools_autocorrect),
	GNOMEUIINFO_ITEM_NONE (N_("_Auto Save..."),
		N_("Automatically save the current document at regular intervals"),
		cb_tools_auto_save),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("_Goal Seek..."),
		N_("Iteratively recalculate to find a target value"),
		cb_tools_goal_seek),
	GNOMEUIINFO_ITEM_NONE (N_("_Solver..."),
		N_("Iteratively recalculate with constraints to approach a target value"),
		cb_tools_solver),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("_Data Analysis..."),
		N_("Statistical methods."),
		cb_tools_data_analysis),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_data_outline [] = {
	GNOMEUIINFO_ITEM_STOCK (N_("_Hide Detail"),
		N_("Collapse an outline group"),
		cb_data_hide_detail, "Menu_Gnumeric_HideDetail"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Show Detail"),
		N_("Uncollapse an outline group"),
		cb_data_show_detail, "Menu_Gnumeric_ShowDetail"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Group..."),
		N_("Add an outline group"),
		cb_data_group, "Menu_Gnumeric_Group"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Ungroup..."),
		N_("Remove an outline group"),
		cb_data_ungroup, "Menu_Gnumeric_Ungroup"),

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Display _Outlines"),
		N_("Toggle whether or not to display outline groups"),
		cb_sheet_pref_display_outlines, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Outlines _Below"),
		N_("Toggle whether to display row outlines on top or bottom"),
		cb_sheet_pref_outline_symbols_below, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Outlines _Right"),
		N_("Toggle whether to display column outlines on the left or right"),
		cb_sheet_pref_outline_symbols_right, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	GNOMEUIINFO_END
};

/* Data menu */
static GnomeUIInfo workbook_menu_data [] = {
	GNOMEUIINFO_ITEM_STOCK (N_("_Sort"),
		N_("Sorts the selected region."),
		cb_data_sort, "Menu_Gnumeric_SortAscending"),
	GNOMEUIINFO_ITEM_NONE (N_("_Filter..."),
		N_("Filter data with given criteria"),
		cb_data_filter),
#if 0
	GNOMEUIINFO_ITEM_NONE (N_("_Validate..."),
		N_("Validate input with preset criteria"),
		cb_data_validate),
#endif
	GNOMEUIINFO_ITEM_NONE (N_("_Consolidate..."),
		N_("Consolidate regions using a function"),
		cb_data_consolidate),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("_Group and Outline"),   workbook_menu_data_outline),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_help [] = {
	GNOMEUIINFO_HELP ("gnumeric"),
        GNOMEUIINFO_MENU_ABOUT_ITEM (cb_help_about, NULL),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu [] = {
        GNOMEUIINFO_MENU_FILE_TREE (workbook_menu_file),
	GNOMEUIINFO_MENU_EDIT_TREE (workbook_menu_edit),
	GNOMEUIINFO_MENU_VIEW_TREE (workbook_menu_view),
	GNOMEUIINFO_SUBTREE(N_("_Insert"), workbook_menu_insert),
	GNOMEUIINFO_SUBTREE(N_("F_ormat"), workbook_menu_format),
	GNOMEUIINFO_SUBTREE(N_("_Tools"),  workbook_menu_tools),
	GNOMEUIINFO_SUBTREE(N_("_Data"),   workbook_menu_data),
	GNOMEUIINFO_MENU_HELP_TREE (workbook_menu_help),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_standard_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("New"), N_("Creates a new workbook"),
		cb_file_new, GNOME_STOCK_PIXMAP_NEW),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Open"), N_("Opens an existing workbook"),
		cb_file_open, GNOME_STOCK_PIXMAP_OPEN),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Save"), N_("Saves the workbook"),
		cb_file_save, GNOME_STOCK_PIXMAP_SAVE),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Print"), N_("Prints the workbook"),
		cb_file_print, GNOME_STOCK_PIXMAP_PRINT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Print Preview"), N_("Print Preview"),
		cb_file_print_preview, "Gnumeric_PrintPreview"),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Cut"), N_("Cuts the selection to the clipboard"),
		cb_edit_cut, "Gnumeric_Cut"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Copy"), N_("Copies the selection to the clipboard"),
		cb_edit_copy, "Gnumeric_Copy"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Paste"), N_("Pastes the clipboard"),
		cb_edit_paste, "Gnumeric_Paste"),

	GNOMEUIINFO_SEPARATOR,

#if 0
	GNOMEUIINFO_ITEM_STOCK (
		N_("Undo"), N_("Undo the operation"),
		cb_edit_undo, GNOME_STOCK_PIXMAP_UNDO),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Redo"), N_("Redo the operation"),
		cb_edit_redo, GNOME_STOCK_PIXMAP_REDO),
#else
#define TB_UNDO_POS 11
#define TB_REDO_POS 12
#endif

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Sum"), N_("Sum into the current cell."),
		cb_autosum, "Gnumeric_AutoSum"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Function"), N_("Edit a function in the current cell."),
		cb_formula_guru, "Gnumeric_FormulaGuru"),

	GNOMEUIINFO_ITEM_STOCK (
		N_("Sort Ascending"), N_("Sorts the selected region in ascending order based on the first column selected."),
		cb_sort_ascending, "Gnumeric_SortAscending"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Sort Descending"), N_("Sorts the selected region in descending order based on the first column selected."),
		cb_sort_descending, "Gnumeric_SortDescending"),

	GNOMEUIINFO_END
};
#else
static BonoboUIVerb verbs [] = {

	BONOBO_UI_UNSAFE_VERB ("FileNew", cb_file_new),
	BONOBO_UI_UNSAFE_VERB ("FileOpen", cb_file_open),
	BONOBO_UI_UNSAFE_VERB ("FileImport", cb_file_import),
	BONOBO_UI_UNSAFE_VERB ("FileSave", cb_file_save),
	BONOBO_UI_UNSAFE_VERB ("FileSaveAs", cb_file_save_as),
#ifdef ENABLE_EVOLUTION
	BONOBO_UI_UNSAFE_VERB ("FileSend", cb_file_send),
#endif
	BONOBO_UI_UNSAFE_VERB ("FilePrintSetup", cb_file_print_setup),
	BONOBO_UI_UNSAFE_VERB ("FilePrint", cb_file_print),
	BONOBO_UI_UNSAFE_VERB ("FilePrintPreview", cb_file_print_preview),
	BONOBO_UI_UNSAFE_VERB ("FileSummary", cb_file_summary),
	BONOBO_UI_UNSAFE_VERB ("FileClose", cb_file_close),
	BONOBO_UI_UNSAFE_VERB ("FileExit", cb_file_quit),

	BONOBO_UI_UNSAFE_VERB ("EditClearAll", cb_edit_clear_all),
	BONOBO_UI_UNSAFE_VERB ("EditClearFormats", cb_edit_clear_formats),
	BONOBO_UI_UNSAFE_VERB ("EditClearComments", cb_edit_clear_comments),
	BONOBO_UI_UNSAFE_VERB ("EditClearContent", cb_edit_clear_content),

	BONOBO_UI_UNSAFE_VERB ("EditSelectAll", cb_edit_select_all),
	BONOBO_UI_UNSAFE_VERB ("EditSelectRow", cb_edit_select_row),
	BONOBO_UI_UNSAFE_VERB ("EditSelectColumn", cb_edit_select_col),
	BONOBO_UI_UNSAFE_VERB ("EditSelectArray", cb_edit_select_array),
	BONOBO_UI_UNSAFE_VERB ("EditSelectDepends", cb_edit_select_depend),

	BONOBO_UI_UNSAFE_VERB ("EditUndo", cb_edit_undo),
	BONOBO_UI_UNSAFE_VERB ("EditRedo", cb_edit_redo),
	BONOBO_UI_UNSAFE_VERB ("EditCut", cb_edit_cut),
	BONOBO_UI_UNSAFE_VERB ("EditCopy", cb_edit_copy),
	BONOBO_UI_UNSAFE_VERB ("EditPaste", cb_edit_paste),
	BONOBO_UI_UNSAFE_VERB ("EditPasteSpecial", cb_edit_paste_special),
	BONOBO_UI_UNSAFE_VERB ("EditDelete", cb_edit_delete),
	BONOBO_UI_UNSAFE_VERB ("EditDuplicateSheet", cb_edit_duplicate_sheet),
	BONOBO_UI_UNSAFE_VERB ("EditSearchReplace", cb_edit_search_replace),
	BONOBO_UI_UNSAFE_VERB ("EditGoto", cb_edit_goto),
	BONOBO_UI_UNSAFE_VERB ("EditRecalc", cb_edit_recalc),

	BONOBO_UI_UNSAFE_VERB ("ViewZoom", cb_view_zoom),
	BONOBO_UI_UNSAFE_VERB ("ViewFreezeThawPanes", cb_view_freeze_panes),
	BONOBO_UI_UNSAFE_VERB ("ViewNewShared", cb_view_new_shared),
	BONOBO_UI_UNSAFE_VERB ("ViewNewUnshared", cb_view_new_unshared),

	BONOBO_UI_UNSAFE_VERB ("InsertCurrentDate", cb_insert_current_date),
	BONOBO_UI_UNSAFE_VERB ("InsertCurrentTime", cb_insert_current_time),
	BONOBO_UI_UNSAFE_VERB ("EditNames", cb_define_name),

	BONOBO_UI_UNSAFE_VERB ("InsertSheet", cb_insert_sheet),
	BONOBO_UI_UNSAFE_VERB ("InsertRows", cb_insert_rows),
	BONOBO_UI_UNSAFE_VERB ("InsertColumns", cb_insert_cols),
	BONOBO_UI_UNSAFE_VERB ("InsertCells", cb_insert_cells),
	BONOBO_UI_UNSAFE_VERB ("InsertObject", cb_insert_bonobo_object),
	BONOBO_UI_UNSAFE_VERB ("InsertComment", cb_insert_comment),

	BONOBO_UI_UNSAFE_VERB ("ColumnAutoSize",
		workbook_cmd_format_column_auto_fit),
	BONOBO_UI_UNSAFE_VERB ("ColumnSize",
		sheet_dialog_set_column_width),
	BONOBO_UI_UNSAFE_VERB ("ColumnHide",
		workbook_cmd_format_column_hide),
	BONOBO_UI_UNSAFE_VERB ("ColumnUnhide",
		workbook_cmd_format_column_unhide),
	BONOBO_UI_UNSAFE_VERB ("ColumnDefaultSize",
		workbook_cmd_format_column_std_width),

	BONOBO_UI_UNSAFE_VERB ("RowAutoSize",
		workbook_cmd_format_row_auto_fit),
	BONOBO_UI_UNSAFE_VERB ("RowSize",
		sheet_dialog_set_row_height),
	BONOBO_UI_UNSAFE_VERB ("RowHide",
		workbook_cmd_format_row_hide),
	BONOBO_UI_UNSAFE_VERB ("RowUnhide",
		workbook_cmd_format_row_unhide),
	BONOBO_UI_UNSAFE_VERB ("RowDefaultSize",
		workbook_cmd_format_row_std_height),

	BONOBO_UI_UNSAFE_VERB ("SheetChangeName", cb_sheet_change_name),
	BONOBO_UI_UNSAFE_VERB ("SheetReorder", cb_sheet_order),
	BONOBO_UI_UNSAFE_VERB ("SheetRemove", cb_sheet_remove),

	BONOBO_UI_UNSAFE_VERB ("FormatCells", cb_format_cells),
	BONOBO_UI_UNSAFE_VERB ("FormatAuto", cb_autoformat),
	BONOBO_UI_UNSAFE_VERB ("FormatWorkbook", cb_workbook_attr),

	BONOBO_UI_UNSAFE_VERB ("ToolsPlugins", cb_tools_plugins),
	BONOBO_UI_UNSAFE_VERB ("ToolsAutoCorrect", cb_tools_autocorrect),
	BONOBO_UI_UNSAFE_VERB ("ToolsAutoSave", cb_tools_auto_save),
	BONOBO_UI_UNSAFE_VERB ("ToolsGoalSeek", cb_tools_goal_seek),
	BONOBO_UI_UNSAFE_VERB ("ToolsSolver", cb_tools_solver),
	BONOBO_UI_UNSAFE_VERB ("ToolsDataAnalysis", cb_tools_data_analysis),

	BONOBO_UI_UNSAFE_VERB ("DataSort", cb_data_sort),
	BONOBO_UI_UNSAFE_VERB ("DataFilter", cb_data_filter),
#if 0
	BONOBO_UI_UNSAFE_VERB ("DataValidate", cb_data_validate),
#endif
	BONOBO_UI_UNSAFE_VERB ("DataConsolidate", cb_data_consolidate),
	
	BONOBO_UI_UNSAFE_VERB ("DataOutlineHideDetail", cb_data_hide_detail),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineShowDetail", cb_data_show_detail),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineGroup", cb_data_group),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineUngroup", cb_data_ungroup),

	BONOBO_UI_UNSAFE_VERB ("AutoSum", cb_autosum),
	BONOBO_UI_UNSAFE_VERB ("FunctionGuru", cb_formula_guru),
	BONOBO_UI_UNSAFE_VERB ("SortAscending", cb_sort_ascending),
	BONOBO_UI_UNSAFE_VERB ("SortDescending", cb_sort_descending),
	BONOBO_UI_UNSAFE_VERB ("GraphGuru", cb_launch_graph_guru),
	BONOBO_UI_UNSAFE_VERB ("InsertComponent", cb_insert_component),
	BONOBO_UI_UNSAFE_VERB ("InsertShapedComponent", cb_insert_shaped_component),

	BONOBO_UI_UNSAFE_VERB ("HelpAbout", cb_help_about),

	BONOBO_UI_VERB_END
};
#endif

/*
 * These create toolbar routines are kept independent, as they
 * will need some manual customization in the future (like adding
 * special purposes widgets for fonts, size, zoom
 */
static void
workbook_create_standard_toolbar (WorkbookControlGUI *wbcg)
{
	int i, len;
	GtkWidget *toolbar, *zoom, *entry, *undo, *redo;
#ifndef ENABLE_BONOBO
	GnomeApp *app;
	GnomeDockItemBehavior behavior;
	const char *name = "StandardToolbar";
#endif

	/* Zoom combo box */
	zoom = wbcg->zoom_entry = gtk_combo_text_new (FALSE);
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (zoom), GTK_RELIEF_NONE);
	entry = GTK_COMBO_TEXT (zoom)->entry;
	gtk_signal_connect (GTK_OBJECT (entry), "activate",
			    GTK_SIGNAL_FUNC (cb_change_zoom), wbcg);
	gtk_combo_box_set_title (GTK_COMBO_BOX (zoom), _("Zoom"));

	/* Set a reasonable default width */
	len = gdk_string_measure (entry->style->font, "%10000");
	gtk_widget_set_usize (entry, len, 0);

	/* Preset values */
	for (i = 0; preset_zoom[i] != NULL ; ++i)
		gtk_combo_text_add_item(GTK_COMBO_TEXT (zoom),
					preset_zoom[i], preset_zoom[i]);

	/* Undo dropdown list */
	undo = wbcg->undo_combo = gtk_combo_stack_new ("Gnumeric_Undo", TRUE);
	gtk_combo_box_set_title (GTK_COMBO_BOX (undo), _("Undo"));
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (undo), GTK_RELIEF_NONE);
	gtk_signal_connect (GTK_OBJECT (undo), "pop",
			    (GtkSignalFunc) cb_undo_combo, wbcg);

	/* Redo dropdown list */
	redo = wbcg->redo_combo = gtk_combo_stack_new ("Gnumeric_Redo", TRUE);
	gtk_combo_box_set_title (GTK_COMBO_BOX (redo), _("Redo"));
	if (!gnome_preferences_get_toolbar_relief_btn ())
		gtk_combo_box_set_arrow_relief (GTK_COMBO_BOX (redo), GTK_RELIEF_NONE);
	gtk_signal_connect (GTK_OBJECT (redo), "pop",
			    (GtkSignalFunc) cb_redo_combo, wbcg);

#ifdef ENABLE_BONOBO
	gnumeric_inject_widget_into_bonoboui (wbcg, undo, "/StandardToolbar/EditUndo");
	gnumeric_inject_widget_into_bonoboui (wbcg, redo, "/StandardToolbar/EditRedo");
	gnumeric_inject_widget_into_bonoboui (wbcg, zoom, "/StandardToolbar/SheetZoom");
	toolbar = NULL;
#else
	app = GNOME_APP (wbcg->toplevel);

	g_return_if_fail (app != NULL);

	toolbar = gnumeric_toolbar_new (workbook_standard_toolbar,
					app->accel_group, wbcg);

	behavior = GNOME_DOCK_ITEM_BEH_NORMAL;

	if (!gnome_preferences_get_menubar_detachable ())
		behavior |= GNOME_DOCK_ITEM_BEH_LOCKED;
	gnome_app_add_toolbar (
		GNOME_APP (wbcg->toplevel),
		GTK_TOOLBAR (toolbar),
		name,
		behavior,
		GNOME_DOCK_TOP, 1, 0, 0);

	/* Add them to the toolbar */
	gnumeric_toolbar_insert_with_eventbox (GTK_TOOLBAR (toolbar),
					       undo, _("Undo"), NULL, TB_UNDO_POS);
	gnumeric_toolbar_insert_with_eventbox (GTK_TOOLBAR (toolbar),
					       redo, _("Redo"), NULL, TB_REDO_POS);
	gnumeric_toolbar_append_with_eventbox (GTK_TOOLBAR (toolbar),
					       zoom, _("Zoom"), NULL);

	gtk_signal_connect (
		GTK_OBJECT(toolbar), "orientation-changed",
		GTK_SIGNAL_FUNC (workbook_standard_toolbar_orient), wbcg);

	wbcg->standard_toolbar = toolbar;
	gtk_widget_show (toolbar);
#endif
}

static void
cb_cancel_input (GtkWidget *IGNORED, WorkbookControlGUI *wbcg)
{
	wbcg_edit_finish (wbcg, FALSE);
}

static void
cb_accept_input (GtkWidget *IGNORED, WorkbookControlGUI *wbcg)
{
	wbcg_edit_finish (wbcg, TRUE);
}

static gboolean
cb_editline_focus_in (GtkWidget *w, GdkEventFocus *event,
		      WorkbookControlGUI *wbcg)
{
	if (!wbcg->editing)
		wbcg_edit_start (wbcg, FALSE, TRUE);

	return TRUE;
}

static void
wb_jump_to_cell (GtkEntry *entry, WorkbookControlGUI *wbcg)
{
	const char *text = gtk_entry_get_text (entry);

	workbook_parse_and_jump (WORKBOOK_CONTROL (wbcg), text);
	wb_control_gui_focus_cur_sheet (wbcg);
}

static void
cb_workbook_debug_info (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));

	if (gnumeric_debugging > 3)
		summary_info_dump (wb->summary_info);

	if (dependency_debugging > 0) {
		printf ("Dependencies\n");
		sheet_dump_dependencies (sheet);
	}
}

static void
cb_autofunction (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	GtkEntry *entry;
	gchar const *txt;

	if (wbcg->editing)
		return;

	entry = GTK_ENTRY (wbcg_get_entry (wbcg));
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=", 1)) {
		wbcg_edit_start (wbcg, TRUE, TRUE);
		gtk_entry_set_text (entry, "=");
		gtk_entry_set_position (entry, 1);
	} else {
		wbcg_edit_start (wbcg, FALSE, TRUE);

		/* FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_entry_set_position (entry, entry->text_length-1);
	}
}

static GtkWidget *
edit_area_button (WorkbookControlGUI *wbcg, gboolean sensitive,
		  GtkSignalFunc func, char const *pixmap)
{
	GtkWidget *button = gtk_button_new ();
	GtkWidget *pix = gnome_stock_pixmap_widget_new (
		GTK_WIDGET (wbcg->toplevel), pixmap);
	gtk_container_add (GTK_CONTAINER (button), pix);
	if (!sensitive)
		gtk_widget_set_sensitive (button, FALSE);
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (button), "clicked",
			    GTK_SIGNAL_FUNC (func), wbcg);

	return button;
}

static void
workbook_setup_edit_area (WorkbookControlGUI *wbcg)
{
	GtkWidget *pix, *box, *box2;
	GtkEntry *entry;

	wbcg->selection_descriptor     = gtk_entry_new ();

	wbcg_edit_ctor (wbcg);
	entry = GTK_ENTRY (wbcg_get_entry (wbcg));

	box           = gtk_hbox_new (0, 0);
	box2          = gtk_hbox_new (0, 0);

	gtk_widget_set_usize (wbcg->selection_descriptor, 100, 0);

	wbcg->cancel_button = edit_area_button (wbcg, FALSE,
		GTK_SIGNAL_FUNC (cb_cancel_input), GNOME_STOCK_BUTTON_CANCEL);
	wbcg->ok_button = edit_area_button (wbcg, FALSE,
		GTK_SIGNAL_FUNC (cb_accept_input), GNOME_STOCK_BUTTON_OK);

	/* Auto function */
	wbcg->func_button	= gtk_button_new ();
	pix = gnome_pixmap_new_from_xpm_d (equal_sign_xpm);
	gtk_container_add (GTK_CONTAINER (wbcg->func_button), pix);
	GTK_WIDGET_UNSET_FLAGS (wbcg->func_button, GTK_CAN_FOCUS);
	gtk_signal_connect (GTK_OBJECT (wbcg->func_button), "clicked",
			    GTK_SIGNAL_FUNC (cb_autofunction), wbcg);

	gtk_box_pack_start (GTK_BOX (box2), wbcg->selection_descriptor, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->cancel_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->ok_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->func_button, 0, 0, 0);

	/* Dependency + Style debugger */
	if (gnumeric_debugging > 9 ||
	    style_debugging > 0 || dependency_debugging > 0) {
		GtkWidget *deps_button = edit_area_button (wbcg, TRUE,
			GTK_SIGNAL_FUNC (cb_workbook_debug_info),
			GNOME_STOCK_PIXMAP_BOOK_RED);
		gtk_box_pack_start (GTK_BOX (box), deps_button, 0, 0, 0);
	}

	gtk_box_pack_start (GTK_BOX (box2), box, 0, 0, 0);
	gtk_box_pack_end   (GTK_BOX (box2), GTK_WIDGET (entry), 1, 1, 0);

	gtk_table_attach (GTK_TABLE (wbcg->table), box2,
			  0, 1, 0, 1,
			  GTK_FILL | GTK_EXPAND | GTK_SHRINK, 0, 0, 0);

	/* Do signal setup for the editing input line */
	gtk_signal_connect (GTK_OBJECT (entry), "focus-in-event",
			    GTK_SIGNAL_FUNC (cb_editline_focus_in),
			    wbcg);
	gtk_signal_connect (GTK_OBJECT (entry), "activate",
			    GTK_SIGNAL_FUNC (cb_accept_input),
			    wbcg);

	/* Do signal setup for the status input line */
	gtk_signal_connect (GTK_OBJECT (wbcg->selection_descriptor), "activate",
			    GTK_SIGNAL_FUNC (wb_jump_to_cell), wbcg);
	gtk_widget_show_all (box2);
}

static int
wbcg_delete_event (GtkWidget *widget, GdkEvent *event, WorkbookControlGUI *wbcg)
{
	return wbcg_close_control (wbcg);
}

/*
 * We must not crash on focus=NULL. We're called like that as a result of
 * gtk_window_set_focus (toplevel, NULL) if the first sheet view is destroyed
 * just after being created. This happens e.g when we cancel a file import or
 * the import fails.
 */
static void
wbcg_set_focus (GtkWindow *window, GtkWidget *focus, WorkbookControlGUI *wbcg)
{
	if (focus && !window->focus_widget)
		wb_control_gui_focus_cur_sheet (wbcg);
}

static void
cb_notebook_switch_page (GtkNotebook *notebook, GtkNotebookPage *page,
			 guint page_num, WorkbookControlGUI *wbcg)
{
	Sheet *sheet;
	
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

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

	if (wbcg_rangesel_possible (wbcg)) {
		GtkObject *obj;

		obj = gtk_object_get_data (GTK_OBJECT (page->child),
					   SHEET_CONTROL_KEY);
		scg_take_focus (SHEET_CONTROL_GUI (obj));
		return;
	}

	/*
	 * Make absolutely sure the expression doesn't get 'lost', if it's invalid
	 * then prompt the user and don't switch the notebook page.
	 */
	if (wbcg->editing) {
		guint prev = GPOINTER_TO_INT (gtk_object_get_data (GTK_OBJECT (notebook), "previous_page"));

		if (prev == page_num)
			return;

		if (!wbcg_edit_finish (wbcg, TRUE))
			gtk_notebook_set_page (notebook, prev);
		else
			/* Looks silly, but is really neccesarry */
			gtk_notebook_set_page (notebook, page_num);
		return;
	}

	gtk_object_set_data (GTK_OBJECT (notebook), "previous_page",
			     GINT_TO_POINTER (gtk_notebook_get_current_page (notebook)));
	
	/* if we are not selecting a range for an expression update */
	sheet = wb_control_gui_focus_cur_sheet (wbcg);
	if (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg)) != NULL) {
		sheet_flag_status_update_range (sheet, NULL);
		sheet_update (sheet);
		wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);
	}
}

static GtkObjectClass *parent_class;
static void
wbcg_destroy (GtkObject *obj)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (obj);

	/* Disconnect signals that would attempt to change things during
	 * destruction.
	 */
	if (wbcg->notebook != NULL)
		gtk_signal_disconnect_by_func (
			GTK_OBJECT (wbcg->notebook),
			GTK_SIGNAL_FUNC (cb_notebook_switch_page), wbcg);
	gtk_signal_disconnect_by_func (
		GTK_OBJECT (wbcg->toplevel),
		GTK_SIGNAL_FUNC (wbcg_set_focus), wbcg);

	wbcg_auto_complete_destroy (wbcg);
	wbcg_edit_dtor (wbcg);

	gtk_window_set_focus (GTK_WINDOW (wbcg->toplevel), NULL);

	if (!GTK_OBJECT_DESTROYED (GTK_OBJECT (wbcg->toplevel)))
		gtk_object_destroy (GTK_OBJECT (wbcg->toplevel));

	GTK_OBJECT_CLASS (parent_class)->destroy (obj);
}

static gboolean
cb_scroll_wheel_support (GtkWidget *w, GdkEventButton *event,
			 WorkbookControlGUI *wb)
{
	/* FIXME : now that we have made the split this is no longer true. */
	/* This is a stub routine to handle scroll wheel events
	 * Unfortunately the toplevel window is currently owned by the workbook
	 * rather than a workbook-view so we cannot really scroll things
	 * unless we scrolled all the views at once which is ugly.
	 */
#if 0
	if (event->button == 4)
	    puts("up");
	else if (event->button == 5)
	    puts("down");
#endif
	return FALSE;
}

/*
 * Make current control size the default. Toplevel would resize
 * spontaneously. This makes it stay the same size until user resizes.
 */
static void
cb_realize (GtkWindow *toplevel, WorkbookControlGUI *wbcg)
{
	GtkAllocation *allocation;

	g_return_if_fail (GTK_IS_WINDOW (toplevel));

	allocation = &GTK_WIDGET (toplevel)->allocation;
	gtk_window_set_default_size (toplevel,
				     allocation->width, allocation->height);
}

static void
workbook_setup_sheets (WorkbookControlGUI *wbcg)
{
	GtkWidget *w = gtk_notebook_new ();
	wbcg->notebook = GTK_NOTEBOOK (w);
	gtk_signal_connect_after (GTK_OBJECT (wbcg->notebook), "switch_page",
		GTK_SIGNAL_FUNC (cb_notebook_switch_page), wbcg);

	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (wbcg->notebook), GTK_POS_BOTTOM);
	gtk_notebook_set_tab_border (GTK_NOTEBOOK (wbcg->notebook), 0);

	gtk_table_attach (GTK_TABLE (wbcg->table), GTK_WIDGET (wbcg->notebook),
			  0, 1, 1, 2,
			  GTK_FILL | GTK_EXPAND | GTK_SHRINK,
			  GTK_FILL | GTK_EXPAND | GTK_SHRINK,
			  0, 0);
	gtk_widget_show (w);
}

#ifdef ENABLE_BONOBO
static void
setup_progress_bar (WorkbookControlGUI *wbcg)
{
	GtkProgressBar *progress_bar;
	BonoboControl  *control;

	progress_bar = (GTK_PROGRESS_BAR (gtk_progress_bar_new ()));

	gtk_progress_bar_set_orientation (
		progress_bar, GTK_PROGRESS_LEFT_TO_RIGHT);
	gtk_progress_bar_set_bar_style (
		progress_bar, GTK_PROGRESS_CONTINUOUS);

	wbcg->progress_bar = GTK_WIDGET (progress_bar);
	gtk_widget_show (wbcg->progress_bar);

	control = bonobo_control_new (wbcg->progress_bar);
	g_return_if_fail (control != NULL);

	bonobo_ui_component_object_set (
		wbcg->uic,
		"/status/Progress",
		bonobo_object_corba_objref (BONOBO_OBJECT (control)),
		NULL);
}
#endif

void
wb_control_gui_set_status_text (WorkbookControlGUI *wbcg, char const *text)
{
	gtk_label_set_text (GTK_LABEL (wbcg->status_text), text);
}

static void
cb_auto_expr_changed (GtkWidget *item, WorkbookControlGUI *wbcg)
{
	if (wbcg->updating_ui)
		return;

	wb_view_auto_expr (
		wb_control_view (WORKBOOK_CONTROL (wbcg)),
		gtk_object_get_data (GTK_OBJECT (item), "name"),
		gtk_object_get_data (GTK_OBJECT (item), "expr"));
}

static gboolean
cb_select_auto_expr (GtkWidget *widget, GdkEventButton *event, Workbook *wbcg)
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
	 * Which would break function lookup when looking up for funtion 'selectIon'
	 * when it wa sregistered as 'selection'
	 *
	 * WARNING * WARNING * WARNING
	 */
	static struct {
		char *displayed_name;
		char *function;
	} const quick_compute_routines [] = {
		{ N_("Sum"),   	       "sum(selection(0))" },
		{ N_("Min"),   	       "min(selection(0))" },
		{ N_("Max"),   	       "max(selection(0))" },
		{ N_("Average"),       "average(selection(0))" },
		{ N_("Count"),         "count(selection(0))" },
		{ NULL, NULL }
	};

	GtkWidget *menu;
	GtkWidget *item;
	int i;

	menu = gtk_menu_new ();

	for (i = 0; quick_compute_routines [i].displayed_name; i++) {
		item = gtk_menu_item_new_with_label (
			_(quick_compute_routines [i].displayed_name));
		gtk_object_set_data (GTK_OBJECT (item), "expr",
			quick_compute_routines [i].function);
		gtk_object_set_data (GTK_OBJECT (item), "name",
			_(quick_compute_routines [i].displayed_name));
		gtk_signal_connect (GTK_OBJECT (item), "activate",
			GTK_SIGNAL_FUNC (cb_auto_expr_changed), wbcg);
		gtk_menu_append (GTK_MENU (menu), item);
		gtk_widget_show (item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
	return TRUE;
}

/* Setup the autocalc and status labels */
static void
workbook_setup_auto_calc (WorkbookControlGUI *wbcg)
{
	GtkWidget *tmp, *frame1, *frame2;

	wbcg->auto_expr_label = tmp = gtk_button_new_with_label ("");
	GTK_WIDGET_UNSET_FLAGS (tmp, GTK_CAN_FOCUS);
	gtk_widget_ensure_style (tmp);
	gtk_widget_set_usize (tmp,
		gdk_text_measure (tmp->style->font, "W", 1) * 15, -1);
	gtk_signal_connect (GTK_OBJECT (tmp), "button_press_event",
			    GTK_SIGNAL_FUNC (cb_select_auto_expr), wbcg);
	frame1 = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame1), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (frame1), tmp);

	wbcg->status_text = tmp = gtk_label_new ("");
	gtk_widget_ensure_style (tmp);
	gtk_widget_set_usize (tmp,
		gdk_text_measure (tmp->style->font, "W", 1) * 15, -1);
	frame2 = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame2), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (frame2), tmp);
#ifdef ENABLE_BONOBO
	gnumeric_inject_widget_into_bonoboui (wbcg, frame1, "/status/AutoExpr");
	gnumeric_inject_widget_into_bonoboui (wbcg, frame2, "/status/Status");
#else
	gtk_box_pack_end (GTK_BOX (wbcg->appbar), frame2, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (wbcg->appbar), frame1, FALSE, TRUE, 0);
#endif
}

/*
 * Sets up the status display area
 */
static void
workbook_setup_status_area (WorkbookControlGUI *wbcg)
{
	/*
	 * Create the GnomeAppBar
	 */
#ifdef ENABLE_BONOBO
	setup_progress_bar (wbcg);
#else
	wbcg->appbar = GNOME_APPBAR (
		gnome_appbar_new (TRUE, TRUE,
				  GNOME_PREFERENCES_USER));

	gnome_app_set_statusbar (GNOME_APP (wbcg->toplevel),
				 GTK_WIDGET (wbcg->appbar));
#endif

	/*
	 * Add the auto calc widgets.
	 */
	workbook_setup_auto_calc (wbcg);
}

static int
show_gui (WorkbookControlGUI *wbcg)
{
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
	int sx = MAX (gdk_screen_width  (), 600);
	int sy = MAX (gdk_screen_height (), 200);
	
	/* Set grid size to preferred width */
	if (wbv && (wbv->preferred_width > 0 || wbv->preferred_height > 0)) {
		int pwidth = wbv->preferred_width;
		int pheight = wbv->preferred_height;
		GdkGeometry geometry;

		pwidth = pwidth > 0 ? pwidth : -2;
		pheight = pheight > 0 ? pheight : -2;
		gtk_widget_set_usize (GTK_WIDGET (wbcg->notebook),
				      pwidth, pheight);

		geometry.max_width  = sx;
		geometry.max_height = sy;
		gtk_window_set_geometry_hints (wbcg->toplevel, NULL,
					       &geometry, GDK_HINT_MAX_SIZE);
	} else {
		gdouble fx, fy;

		fx = gnome_config_get_float_with_default (
		"Gnumeric/Placement/WindowRelativeSizeX=0.75", NULL);
		fy = gnome_config_get_float_with_default (
		"Gnumeric/Placement/WindowRelativeSizeY=0.75", NULL);
		fx = MIN (fx, 1.0);
		fx = MAX (0.25, fx);
		fy = MIN (fy, 1.0);
		fy = MAX (0.2, fy);
		gtk_window_set_default_size (wbcg->toplevel, sx * fx, sy * fy);
	}

	gtk_widget_show_all (GTK_WIDGET (wbcg->toplevel));

	/* rehide headers if necessary */
	if (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg))) {
		SheetControl *sc;

		sc = SHEET_CONTROL (wb_control_gui_cur_sheet (wbcg));
		scg_adjust_preferences (sc);
	}

	return FALSE;
}

/*
 * NOTE: Keep the two strings below in sync - send_menu_item_i18n array must
 * always contain values of _label and _tip properties from XML inside
 * send_menu_item.
 */
#ifdef ENABLE_EVOLUTION
static gchar send_menu_item[] =
"<placeholder name=\"FileOperations\">"
"  <menuitem name=\"FileSend\" _label=\"Send\""
"            _tip=\"Send the current file in email\""
"            pixtype=\"stock\" pixname=\"New Mail\" verb=\"\"/>"
"</placeholder>";
#ifdef TRANSLATORS_ONLY
static gchar *send_menu_item_i18n[] = {N_("Send"), N_("Send the current file in email")};
#endif
#endif

void
workbook_control_gui_init (WorkbookControlGUI *wbcg,
			   WorkbookView *optional_view, Workbook *optional_wb)
{
#ifdef ENABLE_BONOBO
	BonoboUIContainer *ui_container;
#endif
	GtkWidget *tmp;

#ifdef ENABLE_BONOBO
	tmp  = bonobo_window_new ("Gnumeric", "Gnumeric");
#else
	tmp  = gnome_app_new ("Gnumeric", "Gnumeric");
#endif
	wbcg->toplevel = GTK_WINDOW (tmp);
	wbcg->table    = gtk_table_new (0, 0, 0);
	wbcg->notebook = NULL;
	wbcg->updating_ui = FALSE;

	workbook_control_set_view (&wbcg->wb_control, optional_view, optional_wb);

	workbook_setup_edit_area (wbcg);

#ifndef ENABLE_BONOBO
	/* Do BEFORE setting up UI bits in the non-bonobo case */
	workbook_setup_status_area (wbcg);

	gnome_app_set_contents (GNOME_APP (wbcg->toplevel), wbcg->table);
	gnome_app_create_menus_with_data (GNOME_APP (wbcg->toplevel), workbook_menu, wbcg);
	gnome_app_install_menu_hints (GNOME_APP (wbcg->toplevel), workbook_menu);

	/* Get the menu items that will be enabled disabled based on
	 * workbook state.
	 */
	wbcg->menu_item_print_setup =
		workbook_menu_file [6].widget;
	wbcg->menu_item_undo =
		workbook_menu_edit [0].widget;
	wbcg->menu_item_redo =
		workbook_menu_edit [1].widget;
	wbcg->menu_item_paste_special =
		workbook_menu_edit [6].widget;
	wbcg->menu_item_search_replace =
		workbook_menu_edit [13].widget;
	
	wbcg->menu_item_insert_rows =
		workbook_menu_insert [1].widget;
	wbcg->menu_item_insert_cols =
		workbook_menu_insert [2].widget;
	wbcg->menu_item_insert_cells =
		workbook_menu_insert [3].widget;
	wbcg->menu_item_define_name =
		workbook_menu_names [0].widget;
	/* FIXME: Once input validation is enabled change the [3] */
	wbcg->menu_item_consolidate =
		workbook_menu_data [3].widget;
	wbcg->menu_item_freeze_panes =
		workbook_menu_view [1].widget;
	
	wbcg->menu_item_sheet_display_formulas =
		workbook_menu_format_sheet [2].widget;
	wbcg->menu_item_sheet_hide_zero =
		workbook_menu_format_sheet [3].widget;
	wbcg->menu_item_sheet_hide_grid =
		workbook_menu_format_sheet [4].widget;
	wbcg->menu_item_sheet_hide_col_header =
		workbook_menu_format_sheet [5].widget;
	wbcg->menu_item_sheet_hide_row_header =
		workbook_menu_format_sheet [6].widget;

	wbcg->menu_item_hide_detail =
		workbook_menu_data_outline [0].widget;
	wbcg->menu_item_show_detail =
		workbook_menu_data_outline [1].widget;
	wbcg->menu_item_sheet_display_outlines =
		workbook_menu_data_outline [5].widget;
	wbcg->menu_item_sheet_outline_symbols_below =
		workbook_menu_data_outline [6].widget;
	wbcg->menu_item_sheet_outline_symbols_right =
		workbook_menu_data_outline [7].widget;
		
	wbcg->menu_item_sheet_remove =
		workbook_menu_edit_sheet [3].widget;
	wbcg->menu_item_sheets_edit_reorder =
		workbook_menu_edit_sheet [4].widget;
	wbcg->menu_item_sheets_format_reorder =
		workbook_menu_format_sheet [1].widget;
#else
	bonobo_window_set_contents (BONOBO_WINDOW (wbcg->toplevel), wbcg->table);

	ui_container = bonobo_ui_container_new ();
	bonobo_ui_container_set_win (ui_container, BONOBO_WINDOW (wbcg->toplevel));
	bonobo_ui_engine_config_set_path (
		bonobo_window_get_ui_engine (BONOBO_WINDOW (wbcg->toplevel)),
		"/Gnumeric/UIConf/kvps");
	wbcg->uic = bonobo_ui_component_new_default ();
	bonobo_ui_component_set_container (wbcg->uic, BONOBO_OBJREF (ui_container));

	bonobo_ui_component_add_verb_list_with_data (wbcg->uic, verbs, wbcg);

	{
		char const *dir = gnumeric_sys_data_dir (NULL);
		bonobo_ui_util_set_ui (wbcg->uic,  dir,
				       "GNOME_Gnumeric.xml", "gnumeric");
	}
#ifdef ENABLE_EVOLUTION
	bonobo_ui_component_set_translate (wbcg->uic, "/menu/File",
	                                   send_menu_item, NULL);
#endif

	TOGGLE_REGISTER (display_formulas, SheetDisplayFormulas);
	TOGGLE_REGISTER (hide_zero, SheetHideZeros);
	TOGGLE_REGISTER (hide_grid, SheetHideGridlines);
	TOGGLE_REGISTER (hide_col_header, SheetHideColHeader);
	TOGGLE_REGISTER (hide_row_header, SheetHideRowHeader);
	TOGGLE_REGISTER (display_outlines, SheetDisplayOutlines);
	TOGGLE_REGISTER (outline_symbols_below, SheetOutlineBelow);
	TOGGLE_REGISTER (outline_symbols_right, SheetOutlineRight);

	/* Do after setting up UI bits in the bonobo case */
	workbook_setup_status_area (wbcg);
#endif
	/* Create before registering verbs so that we can merge some extra. */
	workbook_create_standard_toolbar (wbcg);
	workbook_create_format_toolbar (wbcg);
	workbook_create_object_toolbar (wbcg);
	wbcg->toolbar_is_sensitive = TRUE;

	wbcg_history_setup (wbcg);		/* Dynamic history menu items. */

	/*
	 * Initialize the menu items, This will enable insert cols/rows
	 * and paste special to the currently valid values for the
	 * active sheet (if there is an active sheet)
	 */
	wb_view_menus_update (wb_control_view (WORKBOOK_CONTROL (wbcg)));
	
	/* We are not in edit mode */
	wbcg->editing = FALSE;
	wbcg->editing_sheet = NULL;
	wbcg->editing_cell = NULL;
	wbcg->rangesel = NULL;

	gtk_signal_connect_after (
		GTK_OBJECT (wbcg->toplevel), "delete_event",
		GTK_SIGNAL_FUNC (wbcg_delete_event), wbcg);
	gtk_signal_connect_after (
		GTK_OBJECT (wbcg->toplevel), "set_focus",
		GTK_SIGNAL_FUNC (wbcg_set_focus), wbcg);
	gtk_signal_connect (GTK_OBJECT (wbcg->toplevel),
			    "button-release-event",
			    GTK_SIGNAL_FUNC (cb_scroll_wheel_support),
			    wbcg);
	gtk_signal_connect (GTK_OBJECT (wbcg->toplevel), "realize",
			    GTK_SIGNAL_FUNC (cb_realize), wbcg);
#if 0
	/* Enable toplevel as a drop target */

	gtk_drag_dest_set (wbcg->toplevel,
			   GTK_DEST_DEFAULT_ALL,
			   drag_types, n_drag_types,
			   GDK_ACTION_COPY);

	gtk_signal_connect (GTK_OBJECT (wb->toplevel),
			    "drag_data_received",
			    GTK_SIGNAL_FUNC (filenames_dropped), wb);
#endif

	gtk_window_set_policy (wbcg->toplevel, TRUE, TRUE, FALSE);

	/* Init autosave */
	wbcg->autosave_timer = 0;
	wbcg->autosave_minutes = 0;
	wbcg->autosave_prompt = FALSE;

	wbcg->current_saver	= NULL;

	/* Postpone showing the GUI, so that we may resize it freely. */
	gtk_idle_add ((GtkFunction) show_gui, wbcg);
	/* Postpone clipboard setup. For mysterious reasons, connecting
	   callbacks to the table doesn't work. toplevel does work, but we
	   must wait until we live inside a toplevel. */
	gtk_idle_add ((GtkFunction) x_clipboard_bind_workbook, wbcg);
}

static void
workbook_control_gui_ctor_class (GtkObjectClass *object_class)
{
	WorkbookControlClass *wbc_class = WORKBOOK_CONTROL_CLASS (object_class);

	g_return_if_fail (wbc_class != NULL);

	parent_class = gtk_type_class (workbook_control_get_type ());

	object_class->destroy = wbcg_destroy;

	wbc_class->context_class.progress_set         = wbcg_progress_set;
	wbc_class->context_class.progress_message_set = wbcg_progress_message_set;
	wbc_class->context_class.error.system	= wbcg_error_system;
	wbc_class->context_class.error.plugin	= wbcg_error_plugin;
	wbc_class->context_class.error.read	= wbcg_error_read;
	wbc_class->context_class.error.save	= wbcg_error_save;
	wbc_class->context_class.error.invalid	= wbcg_error_invalid;
	wbc_class->context_class.error.splits_array  = wbcg_error_splits_array;
	wbc_class->context_class.error.error_info  = wbcg_error_error_info;

	wbc_class->control_new		= wbcg_control_new;
	wbc_class->init_state		= wbcg_init_state;
	wbc_class->title_set		= wbcg_title_set;
	wbc_class->prefs_update		= wbcg_prefs_update;
	wbc_class->format_feedback	= wbcg_format_feedback;
	wbc_class->zoom_feedback	= wbcg_zoom_feedback;
	wbc_class->edit_line_set	= wbcg_edit_line_set;
	wbc_class->selection_descr_set	= wbcg_edit_selection_descr_set;
	wbc_class->auto_expr_value	= wbcg_auto_expr_value;

	wbc_class->sheet.add        = wbcg_sheet_add;
	wbc_class->sheet.remove	    = wbcg_sheet_remove;
	wbc_class->sheet.rename	    = wbcg_sheet_rename;
	wbc_class->sheet.focus	    = wbcg_sheet_focus;
	wbc_class->sheet.move	    = wbcg_sheet_move;
	wbc_class->sheet.remove_all = wbcg_sheet_remove_all;
	
	wbc_class->undo_redo.clear    = wbcg_undo_redo_clear;
	wbc_class->undo_redo.truncate = wbcg_undo_redo_truncate;
	wbc_class->undo_redo.pop      = wbcg_undo_redo_pop;
	wbc_class->undo_redo.push     = wbcg_undo_redo_push;
	wbc_class->undo_redo.labels   = wbcg_undo_redo_labels;

	wbc_class->paste_from_selection  = wbcg_paste_from_selection;
	wbc_class->claim_selection	 = wbcg_claim_selection;

	wbc_class->menu_state.update      = wbcg_menu_state_update;
	wbc_class->menu_state.sheet_prefs = wbcg_menu_state_sheet_prefs;
}

E_MAKE_TYPE(workbook_control_gui, "WorkbookControlGUI", WorkbookControlGUI,
	    workbook_control_gui_ctor_class, NULL,
	    WORKBOOK_CONTROL_TYPE);

WorkbookControl *
workbook_control_gui_new (WorkbookView *optional_view, Workbook *wb)
{
	WorkbookControlGUI *wbcg;
	WorkbookControl    *wbc;

	wbcg = gtk_type_new (workbook_control_gui_get_type ());
	wbc = WORKBOOK_CONTROL (wbcg);
	workbook_control_gui_init (wbcg, optional_view, wb);

	workbook_control_init_state (wbc);

	return wbc;
}

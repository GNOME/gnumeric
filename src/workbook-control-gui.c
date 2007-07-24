/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * workbook-control-gui.c: GUI specific routines for a workbook-control.
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
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
 * 	Eduardo Lima  (eduardo.lima@indt.org.br)
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "workbook-control-gui-priv.h"

#include "application.h"
#include "workbook-view.h"
#include "workbook-edit.h"
#include "workbook-priv.h"
#include "sheet.h"
#include "sheet-view.h"
#include "sheet-merge.h"
#include "sheet-filter.h"
#include "sheet-private.h"
#include "sheet-control-gui-priv.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "commands.h"
#include "cmd-edit.h"
#include "workbook-cmd-format.h"
#include "selection.h"
#include "clipboard.h"
#include "gui-clipboard.h"
#include "workbook-edit.h"
#include "libgnumeric.h"
#include "dependent.h"
#include "expr.h"
#include "expr-impl.h"
#include "func.h"
#include "position.h"
#include "parse-util.h"
#include "ranges.h"
#include "value.h"
#include "validation.h"
#include "style-color.h"
#include "str.h"
#include "cell.h"
#include "gui-file.h"
#include "search.h"
#include "gui-util.h"
#include "widgets/widget-editable-label.h"
#include "sheet-object-image.h"
#include "gnumeric-gconf.h"
#include "filter.h"
#include "stf.h"
#include "rendered-value.h"
#include "sort.h"
#include "gnm-pane-impl.h"

#include <goffice/app/error-info.h>
#include <goffice/app/io-context.h>
#include <goffice/graph/gog-data-set.h>
#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context-impl.h>
#include <goffice/utils/go-glib-extras.h>

#include <gsf/gsf-impl-utils.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <string.h>
#include <errno.h>

enum {
	PROP_0,
	PROP_AUTOSAVE_PROMPT,
	PROP_AUTOSAVE_TIME	
};

guint wbcg_signals[WBCG_LAST_SIGNAL];

#define WBCG_CLASS(o) WORKBOOK_CONTROL_GUI_CLASS (G_OBJECT_GET_CLASS (o))
#define WBCG_VIRTUAL_FULL(func, handle, arglist, call)		\
void wbcg_ ## func arglist					\
{								\
	WorkbookControlGUIClass *wbcg_class;			\
								\
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));	\
								\
	wbcg_class = WBCG_CLASS (wbcg);				\
	if (wbcg_class != NULL && wbcg_class->handle != NULL)	\
		wbcg_class->handle call;			\
}
#define WBCG_VIRTUAL(func, arglist, call) \
        WBCG_VIRTUAL_FULL(func, func, arglist, call)

#define	SHEET_CONTROL_KEY	"SheetControl"

enum {
	TARGET_URI_LIST,
	TARGET_SHEET
};

static WBCG_VIRTUAL (actions_sensitive, 
	(WorkbookControlGUI *wbcg, gboolean actions, gboolean font_actions), (wbcg, actions, font_actions))
static WBCG_VIRTUAL (reload_recent_file_menu, 
	(WorkbookControlGUI *wbcg), (wbcg))
static WBCG_VIRTUAL (set_action_sensitivity, 
	(WorkbookControlGUI *wbcg, char const *a, gboolean sensitive),
	(wbcg, a, sensitive))
static WBCG_VIRTUAL (set_action_label, 
	(WorkbookControlGUI *wbcg, char const *a, char const *prefix, char const *suffix, char const *new_tip),
	(wbcg, a, prefix, suffix, new_tip))
static WBCG_VIRTUAL (set_toggle_action_state, 
	(WorkbookControlGUI *wbcg, char const *a, gboolean state),
	(wbcg, a, state))

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

static SheetControlGUI *
wbcg_get_scg (WorkbookControlGUI *wbcg, Sheet *sheet)
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

GtkWindow *
wbcg_toplevel (WorkbookControlGUI *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), NULL);

	return GTK_WINDOW (wbcg->toplevel);
}

void
wbcg_set_transient (WorkbookControlGUI *wbcg, GtkWindow *window)
{
	go_gtk_window_set_transient (wbcg_toplevel (wbcg), window);
}

/*#warning merge these and clarfy whether we want the visible scg, or the logical (view) scg */

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
wbcg_focus_cur_scg (WorkbookControlGUI *wbcg)
{
	GtkWidget *table;
	SheetControlGUI *scg;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), NULL);

	if (wbcg->notebook == NULL)
		return NULL;

	table = gtk_notebook_get_nth_page (wbcg->notebook,
		gtk_notebook_get_current_page (wbcg->notebook));
	scg = g_object_get_data (G_OBJECT (table), SHEET_CONTROL_KEY);

	g_return_val_if_fail (scg != NULL, NULL);

	scg_take_focus (scg);
	return scg_sheet (scg);
}

Sheet *
wbcg_cur_sheet (WorkbookControlGUI *wbcg)
{
	return wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
}

SheetControlGUI *
wbcg_cur_scg (WorkbookControlGUI *wbcg)
{
	return wbcg_get_scg (wbcg, wbcg_cur_sheet (wbcg));
}

/****************************************************************************/
/* Autosave */

static gboolean
cb_autosave (WorkbookControlGUI *wbcg)
{
	WorkbookView *wb_view;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

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
wbcg_rangesel_possible (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	/* Already range selecting */
	if (wbcg->rangesel != NULL)
		return TRUE;

	/* Rangesel requires that we be editing somthing */
	if (!wbcg_is_editing (wbcg) && !wbcg_entry_has_logical (wbcg))
		return FALSE;

	return gnm_expr_entry_can_rangesel (wbcg_get_entry_logical (wbcg));
}

gboolean
wbcg_is_editing (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);
	return wbcg->wb_control.editing;
}

static void
wbcg_autosave_cancel (WorkbookControlGUI *wbcg)
{
	if (wbcg->autosave_timer != 0) {
		g_source_remove (wbcg->autosave_timer);
		wbcg->autosave_timer = 0;
	}
}

static void
wbcg_autosave_activate (WorkbookControlGUI *wbcg)
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
wbcg_set_autosave_time (WorkbookControlGUI *wbcg, int secs)
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
	GtkEntry *entry = wbcg_get_entry ((WorkbookControlGUI*)wbc);
	gtk_entry_set_text (entry, text);
}

static void
wbcg_edit_selection_descr_set (WorkbookControl *wbc, char const *text)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	gtk_entry_set_text (GTK_ENTRY (wbcg->selection_descriptor), text);
}

static void
wbcg_set_sensitive (GOCmdContext *cc, gboolean sensitive)
{
	GtkWindow *toplevel = wbcg_toplevel (WORKBOOK_CONTROL_GUI (cc));

	if (toplevel != NULL)
		gtk_widget_set_sensitive (GTK_WIDGET (toplevel), sensitive);
}

static void
wbcg_update_action_sensitivity (WorkbookControl *wbc)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (wbc);
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

	wbcg_actions_sensitive (wbcg, enable_actions, enable_actions || enable_edit_ok_cancel);
}

static gboolean
cb_sheet_label_edit_finished (EditableLabel *el, char const *new_name,
			      WorkbookControlGUI *wbcg)
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
wbcg_insert_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Workbook *wb = sheet->workbook;
	WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
	workbook_sheet_add (wb, sheet->index_in_wb);
	cmd_reorganize_sheets (wbc, old_state, sheet);
}

void
wbcg_append_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Workbook *wb = sheet->workbook;
	WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
	workbook_sheet_add (wb, -1);
	cmd_reorganize_sheets (wbc, old_state, sheet);
}

void
wbcg_clone_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
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


static void
sheet_action_add_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	wbcg_append_sheet (NULL, scg->wbcg);
}

static void
sheet_action_insert_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	wbcg_insert_sheet (NULL, scg->wbcg);
}

void
scg_delete_sheet_if_possible (G_GNUC_UNUSED GtkWidget *ignored,
			      SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	Sheet *sheet = sc->sheet;
	Workbook *wb = sheet->workbook;

	/* If this is the last sheet left, ignore the request */
	if (workbook_sheet_count (wb) != 1) {
		WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
		WorkbookControl *wbc = sc->wbc;
		workbook_sheet_delete (sheet);
		/* Careful: sc just ceased to be valid.  */
		cmd_reorganize_sheets (wbc, old_state, sheet);
	}
}

static void
sheet_action_rename_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	editable_label_start_editing (EDITABLE_LABEL(scg->label));
}

static void
sheet_action_clone_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	wbcg_clone_sheet (NULL, scg->wbcg);
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
	SheetControl *sc = (SheetControl *) scg;

	struct {
		char const *text;
		void (*function) (GtkWidget *widget, SheetControlGUI *scg);
		enum { SHEET_CONTEXT_TEST_SIZE = 1 } flags;
	} const sheet_label_context_actions [] = {
		{ N_("Manage sheets..."), &sheet_action_reorder_sheet, 0},
		{ NULL, NULL, 0},
		{ N_("Insert"), &sheet_action_insert_sheet, 0 },
		{ N_("Append"), &sheet_action_add_sheet, 0 },
		{ N_("Duplicate"), &sheet_action_clone_sheet, 0 },
		{ N_("Remove"), &scg_delete_sheet_if_possible, SHEET_CONTEXT_TEST_SIZE },
		{ N_("Rename"), &sheet_action_rename_sheet, 0 }
	};

	GtkWidget *menu;
	unsigned int i;

	menu = gtk_menu_new ();

	for (i = 0; i < G_N_ELEMENTS (sheet_label_context_actions); i++){
		int flags = sheet_label_context_actions [i].flags;
		char const *text = sheet_label_context_actions[i].text;
		GtkWidget *item;
		gboolean inactive =
			((flags & SHEET_CONTEXT_TEST_SIZE) &&
			 workbook_sheet_count (sc->sheet->workbook) < 2) ||
			wbcg_edit_get_guru (scg_wbcg (scg)) != NULL;

		if (text == NULL) {
			item = gtk_separator_menu_item_new ();
		} else {
			item = gtk_menu_item_new_with_label (_ (text));
			g_signal_connect (G_OBJECT (item),
					  "activate",
					  G_CALLBACK (sheet_label_context_actions [i].function), scg);
		}

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
			      WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg;
	gint n_source;
	GtkWidget *p_source;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	n_source = gnm_notebook_page_num_by_label (wbcg->notebook, widget);
	p_source = gtk_notebook_get_nth_page (wbcg->notebook, n_source);
	scg = g_object_get_data (G_OBJECT (p_source), SHEET_CONTROL_KEY);

	gtk_selection_data_set (selection_data, selection_data->target,
		8, (void *) scg, sizeof (scg));
}

static void
cb_sheet_label_drag_data_received (GtkWidget *widget, GdkDragContext *context,
				   gint x, gint y, GtkSelectionData *data, guint info, guint time,
				   WorkbookControlGUI *wbcg)
{
	GtkWidget *w_source;
	gint p_src;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
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
			   WorkbookControlGUI *wbcg)
{
	GtkWidget *arrow, *image;
	GdkPixbuf *pixbuf;
	GdkBitmap *bitmap;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

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
			 WorkbookControlGUI *wbcg)
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
			   guint time, WorkbookControlGUI *wbcg)
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
	gint x, gint y, guint time, WorkbookControlGUI *wbcg)
{
	GtkWidget *w_source, *arrow, *window;
	gint n_source, n_dest, root_x, root_y, pos_x, pos_y;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

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

static void workbook_setup_sheets (WorkbookControlGUI *wbcg);

static void
wbcg_menu_state_sheet_count (WorkbookControlGUI *wbcg)
{
	int const sheet_count = g_list_length (wbcg->notebook->children);
	/* Should we enable commands requiring multiple sheets */
	gboolean const multi_sheet = (sheet_count > 1);

	/* Scrollable if there are more than 3 tabs */
	gtk_notebook_set_scrollable (wbcg->notebook, sheet_count > 3);

	wbcg_set_action_sensitivity (wbcg, "SheetRemove", multi_sheet);
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
wbcg_update_menu_feedback (WorkbookControlGUI *wbcg, Sheet const *sheet)
{
	g_return_if_fail (IS_SHEET (sheet));

	if (!wbcg_ui_update_begin (wbcg))
		return;

	wbcg_set_toggle_action_state (wbcg,
		"SheetDisplayFormulas", sheet->display_formulas);
	wbcg_set_toggle_action_state (wbcg,
		"SheetHideZeros", sheet->hide_zero);
	wbcg_set_toggle_action_state (wbcg,
		"SheetHideGridlines", sheet->hide_grid);
	wbcg_set_toggle_action_state (wbcg,
		"SheetHideColHeader", sheet->hide_col_header);
	wbcg_set_toggle_action_state (wbcg,
		"SheetHideRowHeader", sheet->hide_row_header);
	wbcg_set_toggle_action_state (wbcg,
		"SheetDisplayOutlines", sheet->display_outlines);
	wbcg_set_toggle_action_state (wbcg,
		"SheetOutlineBelow", sheet->outline_symbols_below);
	wbcg_set_toggle_action_state (wbcg,
		"SheetOutlineRight", sheet->outline_symbols_right);
	wbcg_set_toggle_action_state (wbcg,
		"SheetUseR1C1", sheet->convs->r1c1_addresses);
	wbcg_ui_update_end (wbcg);
}

static void
cb_toggle_menu_item_changed (Sheet *sheet,
			     G_GNUC_UNUSED GParamSpec *pspec,
			     WorkbookControlGUI *wbcg)
{
	/* We're lazy and just update all.  */
	wbcg_update_menu_feedback (wbcg, sheet);
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
cb_zoom_change (Sheet *sheet,
		G_GNUC_UNUSED GParamSpec *null_pspec,
		WorkbookControlGUI *wbcg)
{
	if (wbcg_ui_update_begin (wbcg)) {
		int pct = sheet->last_zoom_factor_used * 100 + .5;
		char *text = g_strdup_printf ("%d%%", pct);
		WorkbookControlGUIClass *wbcg_class = WBCG_CLASS (wbcg);
		wbcg_class->set_zoom_label (wbcg, text);
		g_free (text);
		wbcg_ui_update_end (wbcg);
	}
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
disconnect_sheet_signals (WorkbookControlGUI *wbcg, Sheet *sheet, gboolean focus_signals_only)
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

	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;
	SheetControl	*sc;
	Sheet		*sheet;
	GSList		*ptr;
	gboolean visible;

	g_return_if_fail (wbcg != NULL);

	sheet = sv_sheet (sv);
	visible = sheet_is_visible (sheet);

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

	/* create views for the sheet objects */
	sc = (SheetControl *) scg;
	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next)
		sc_object_create_view (sc, ptr->data);
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
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;

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
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg = wbcg_get_scg (wbcg, sheet);

	if (sheet)
		gtk_notebook_set_current_page (wbcg->notebook,
					       sheet->index_in_wb);
	if (wbcg->rangesel == NULL)
		gnm_expr_entry_set_scg (wbcg->edit_line.entry, scg);

	disconnect_sheet_signals (wbcg, wbcg_cur_sheet (wbcg), TRUE);

	if (sheet) {
		wbcg_update_menu_feedback (wbcg, sheet);

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
wbcg_sheet_order_changed (WorkbookControlGUI *wbcg)
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
wbcg_update_title (WorkbookControlGUI *wbcg)
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
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;

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
			     WorkbookControlGUI *wbcg)
{
	gtk_label_set_text (GTK_LABEL (wbcg->auto_expr_label),
			    wbv->auto_expr_text ? wbv->auto_expr_text : "");
}

static void
wbcg_menu_state_update (WorkbookControl *wbc, int flags)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	SheetView const *sv  = wb_control_cur_sheet_view (wbc);
	Sheet const *sheet = wb_control_cur_sheet (wbc);
	gboolean const has_guru = wbcg_edit_get_guru (wbcg) != NULL;
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
		wbcg_set_action_sensitivity (wbcg, "InsertColumns",
			sv->enable_insert_cols);
	if (MS_INSERT_ROWS & flags)
		wbcg_set_action_sensitivity (wbcg, "InsertRows",
			sv->enable_insert_rows);
	if (MS_INSERT_CELLS & flags)
		wbcg_set_action_sensitivity (wbcg, "InsertCells",
			sv->enable_insert_cells);
	if (MS_SHOWHIDE_DETAIL & flags) {
		wbcg_set_action_sensitivity (wbcg, "DataOutlineShowDetail",
			sheet->priv->enable_showhide_detail);
		wbcg_set_action_sensitivity (wbcg, "DataOutlineHideDetail",
			sheet->priv->enable_showhide_detail);
	}
	if (MS_PASTE_SPECIAL & flags)
		wbcg_set_action_sensitivity (wbcg, "EditPasteSpecial",
			!gnm_app_clipboard_is_empty () &&
			!gnm_app_clipboard_is_cut () &&
			!edit_object);
	if (MS_PRINT_SETUP & flags)
		wbcg_set_action_sensitivity (wbcg, "FilePageSetup", !has_guru);
	if (MS_SEARCH_REPLACE & flags)
		wbcg_set_action_sensitivity (wbcg, "EditReplace", !has_guru);
	if (MS_DEFINE_NAME & flags)
		wbcg_set_action_sensitivity (wbcg, "EditNames", !has_guru);
	if (MS_CONSOLIDATE & flags)
		wbcg_set_action_sensitivity (wbcg, "DataConsolidate", !has_guru);
	if (MS_CONSOLIDATE & flags)
		wbcg_set_action_sensitivity (wbcg, "DataFilterShowAll", has_filtered_rows);

	if (MS_FREEZE_VS_THAW & flags) {
		/* Cheat and use the same accelerator for both states because
		 * we don't reset it when the label changes */
		char const* label = sv_is_frozen (sv)
			? _("Un_freeze Panes")
			: _("_Freeze Panes");
		char const *new_tip = sv_is_frozen (sv)
			? _("Unfreeze the top left of the sheet")
			: _("Freeze the top left of the sheet");
		wbcg_set_action_label (wbcg, "ViewFreezeThawPanes", NULL, label, new_tip);
	}

	if (MS_ADD_VS_REMOVE_FILTER & flags) {
		gboolean const has_filter = (sv_first_selection_in_filter (sv) != NULL);
		char const* label = has_filter
			? _("Remove _Auto Filter")
			: _("Add _Auto Filter");
		char const *new_tip = has_filter
			? _("Remove a filter")
			: _("Add a filter");
		wbcg_set_action_label (wbcg, "DataAutoFilter", NULL, label, new_tip);
	}
}

static void
wbcg_undo_redo_labels (WorkbookControl *wbc, char const *undo, char const *redo)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	g_return_if_fail (wbcg != NULL);

	wbcg_set_action_label (wbcg, "Undo", _("_Undo"), undo, NULL);
	wbcg_set_action_label (wbcg, "Redo", _("_Redo"), redo, NULL);
}

static void
wbcg_paste_from_selection (WorkbookControl *wbc, GnmPasteTarget const *pt)
{
	x_request_clipboard ((WorkbookControlGUI *)wbc, pt);
}

static gboolean
wbcg_claim_selection (WorkbookControl *wbc)
{
	return x_claim_clipboard ((WorkbookControlGUI *)wbc);
}

static char *
wbcg_get_password (GOCmdContext *cc, char const* filename)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (cc);

	return dialog_get_password (wbcg_toplevel (wbcg), filename);
}

static void
wbcg_error_error (GOCmdContext *cc, GError *err)
{
	go_gtk_notice_dialog (wbcg_toplevel (WORKBOOK_CONTROL_GUI (cc)),
		GTK_MESSAGE_ERROR, err->message);
}

static void
wbcg_error_error_info (GOCmdContext *cc, ErrorInfo *error)
{
	gnumeric_error_info_dialog_show (
		wbcg_toplevel (WORKBOOK_CONTROL_GUI (cc)), error);
}

static int
wbcg_show_save_dialog (WorkbookControlGUI *wbcg, 
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
 *  		prompted to see if they should exit.
 *
 * Returns :
 * 0) canceled
 * 1) closed
 * 2) pristine can close
 * 3) save any future dirty
 * 4) do not save any future dirty
 */
static int
wbcg_close_if_user_permits (WorkbookControlGUI *wbcg,
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

/*
 * wbcg_close_control:
 *
 * Returns TRUE if the control should NOT be closed.
 */
gboolean
wbcg_close_control (WorkbookControlGUI *wbcg)
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
cb_cancel_input (WorkbookControlGUI *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);
}

static void
cb_accept_input (WorkbookControlGUI *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, NULL);
}

static gboolean
cb_editline_focus_in (GtkWidget *w, GdkEventFocus *event,
		      WorkbookControlGUI *wbcg)
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
cb_statusbox_activate (GtkEntry *entry, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wb_control_parse_and_jump (wbc, gtk_entry_get_text (entry));
	wbcg_focus_cur_scg (wbcg);
	wb_view_selection_desc (wb_control_view (wbc), TRUE, wbc);
}

static gboolean
cb_statusbox_focus (GtkEntry *entry, GdkEventFocus *event,
		    WorkbookControlGUI *wbcg)
{
	gtk_editable_select_region (GTK_EDITABLE (entry), 0, 0);
	return FALSE;
}

/******************************************************************************/
static void
cb_workbook_debug_info (WorkbookControlGUI *wbcg)
{
	Workbook *wb = wb_control_get_workbook (WORKBOOK_CONTROL (wbcg));

	if (dependency_debugging > 0) {
		WORKBOOK_FOREACH_SHEET (wb, sheet,
			g_printerr ("Dependencies for %s:\n", sheet->name_unquoted);
			gnm_dep_container_dump (sheet->deps););
	}

	if (expression_sharing_debugging > 0) {
		GnmExprSharer *es = workbook_share_expressions (wb, FALSE);

		g_print ("Expression sharer results:\n"
			 "Nodes in: %d, nodes stored: %d, nodes killed: %d.\n",
			 es->nodes_in, es->nodes_stored, es->nodes_killed);
		gnm_expr_sharer_destroy (es);
	}
}

static void
cb_autofunction (WorkbookControlGUI *wbcg)
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
edit_area_button (WorkbookControlGUI *wbcg, GtkToolbar *tb,
		  gboolean sensitive,
		  GCallback func, char const *stock_id,
		  GtkTooltips *tips, char const *tip)
{
	GObject *button =
		g_object_new (GTK_TYPE_TOOL_BUTTON,
			      "stock-id", stock_id,
			      "sensitive", sensitive,
			      "can-focus", FALSE,
			      NULL);
	gtk_tool_item_set_tooltip (GTK_TOOL_ITEM (button), tips, tip, NULL);
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
cb_set_focus (GtkWindow *window, GtkWidget *focus, WorkbookControlGUI *wbcg)
{
	if (focus && !window->focus_widget)
		wbcg_focus_cur_scg (wbcg);
}

static void
cb_notebook_switch_page (GtkNotebook *notebook, GtkNotebookPage *page,
			 guint page_num, WorkbookControlGUI *wbcg)
{
	Sheet *sheet;
	SheetControlGUI *new_scg;
	GtkWidget *child; 

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

	child = gtk_notebook_get_nth_page (notebook, page_num);
	new_scg = g_object_get_data (G_OBJECT (child), SHEET_CONTROL_KEY);

	cb_direction_change (NULL, NULL, new_scg);

	if (wbcg_rangesel_possible (wbcg)) {
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
wbcg_set_property (GObject *object, guint property_id,
		   const GValue *value, GParamSpec *pspec)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)object;

	switch (property_id) {
	case PROP_AUTOSAVE_PROMPT:
		wbcg->autosave_prompt = g_value_get_boolean (value);
		break;
	case PROP_AUTOSAVE_TIME:
		wbcg_set_autosave_time (wbcg, g_value_get_int (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
wbcg_get_property (GObject *object, guint property_id,
		   GValue *value, GParamSpec *pspec)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)object;

	switch (property_id) {
	case PROP_AUTOSAVE_PROMPT:
		g_value_set_boolean (value, wbcg->autosave_prompt);
		break;
	case PROP_AUTOSAVE_TIME:
		g_value_set_int (value, wbcg->autosave_time);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}


static GObjectClass *parent_class;
static void
wbcg_finalize (GObject *obj)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (obj);

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

#ifdef USE_HILDON
	if (wbcg->hildon_prog != NULL) {
		g_object_unref (wbcg->hildon_prog);
		wbcg->hildon_prog = NULL;
	}
#endif

	parent_class->finalize (obj);
}

static gboolean
cb_scroll_wheel (GtkWidget *ignored, GdkEventScroll *event,
		 WorkbookControlGUI *wbcg)
{
	/* scroll always operates on pane 0 */
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	Sheet	 	*sheet = scg_sheet (scg);
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
cb_realize (GtkWindow *toplevel, WorkbookControlGUI *wbcg)
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

static void
workbook_setup_sheets (WorkbookControlGUI *wbcg)
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

#ifdef USE_HILDON
	gtk_notebook_set_show_border (wbcg->notebook, FALSE);
	gtk_notebook_set_show_tabs (wbcg->notebook, FALSE);
#endif
}

void
wbcg_set_status_text (WorkbookControlGUI *wbcg, char const *text)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	gtk_statusbar_pop (GTK_STATUSBAR (wbcg->status_text), 0);
	gtk_statusbar_push (GTK_STATUSBAR (wbcg->status_text), 0, text);
}

static void
set_visibility (WorkbookControlGUI *wbcg,
		char const *action_name,
		gboolean visible)
{
	GtkWidget *w = g_hash_table_lookup (wbcg->visibility_widgets, action_name);
	if (w)
		(visible ? gtk_widget_show : gtk_widget_hide) (w);
	wbcg_set_toggle_action_state (wbcg, action_name, visible);
}	


void
wbcg_toggle_visibility (WorkbookControlGUI *wbcg, GtkToggleAction *action)
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
cb_visibility (char const *action, GtkWidget *orig_widget, WorkbookControlGUI *new_wbcg)
{
	set_visibility (new_wbcg, action, GTK_WIDGET_VISIBLE (orig_widget));
}

void
wbcg_copy_toolbar_visibility (WorkbookControlGUI *new_wbcg,
			      WorkbookControlGUI *wbcg)
{
	g_hash_table_foreach (wbcg->visibility_widgets,
		(GHFunc)cb_visibility, new_wbcg);
}


void
wbcg_set_end_mode (WorkbookControlGUI *wbcg, gboolean flag)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

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
	font_desc = pango_font_description_from_string (font_str
							 ? font_str
							 : "sans 10");
	g_free (font_str);

	return font_desc;
}

static void
cb_update_item_bar_font (GtkWidget *w, gpointer unused)
{
	SheetControl *sc = g_object_get_data (G_OBJECT (w), SHEET_CONTROL_KEY);
	sc_resize (sc, TRUE);
}

static void
cb_desktop_font_changed (GtkSettings *settings, GParamSpec  *pspec,
			 WorkbookControlGUI *wbcg)
{
	if (wbcg->font_desc)
		pango_font_description_free (wbcg->font_desc);
	wbcg->font_desc = settings_get_font_desc (settings);
	gtk_container_foreach (GTK_CONTAINER (wbcg->notebook),
			       cb_update_item_bar_font, NULL);
}

static GtkSettings *
wbcg_get_gtk_settings (WorkbookControlGUI *wbcg)
{
	GdkScreen *screen = gtk_widget_get_screen (wbcg->table);
	return gtk_settings_get_for_screen (screen);
}

PangoFontDescription *
wbcg_get_font_desc (WorkbookControlGUI *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), NULL);

	if (!wbcg->font_desc) {
		GtkSettings *settings = wbcg_get_gtk_settings (wbcg);
		wbcg->font_desc = settings_get_font_desc (settings);
		g_signal_connect (settings, "notify::gtk-font-name",
				  G_CALLBACK (cb_desktop_font_changed), wbcg);
	}
	return wbcg->font_desc;
}

WorkbookControlGUI *
wbcg_find_for_workbook (Workbook *wb,
			WorkbookControlGUI *candidate,
			GdkScreen *pref_screen,
			GdkDisplay *pref_display)
{
	gboolean has_screen, has_display;

	g_return_val_if_fail (IS_WORKBOOK (wb), NULL);
	g_return_val_if_fail (candidate == NULL || IS_WORKBOOK_CONTROL_GUI (candidate), NULL);

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
		if (IS_WORKBOOK_CONTROL_GUI (wbc)) {
			WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (wbc);
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

/* ------------------------------------------------------------------------- */

static void
cb_auto_expr_changed (GtkWidget *item, WorkbookControlGUI *wbcg)
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
cb_auto_expr_precision_toggled (GtkWidget *item, WorkbookControlGUI *wbcg)
{
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
	if (wbcg->updating_ui)
		return;

	go_object_toggle (wbv, "auto-expr-max-precision");
}

static gboolean
cb_select_auto_expr (GtkWidget *widget, GdkEventButton *event, WorkbookControlGUI *wbcg)
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
		{ N_("Sum"),   	       "sum" },
		{ N_("Min"),   	       "min" },
		{ N_("Max"),   	       "max" },
		{ N_("Average"),       "average" },
		{ N_("Count"),         "count" },
		{ NULL, NULL }
	};

	GtkWidget *menu;
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

	{
		GtkWidget *item = gtk_separator_menu_item_new ();
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	{
		GtkWidget *item = gtk_check_menu_item_new_with_label
			(_("Use maximum precision"));
		WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
						wbv->auto_expr_use_max_precision);
		g_signal_connect (G_OBJECT (item),
			"activate",
			G_CALLBACK (cb_auto_expr_precision_toggled), wbcg);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
	return TRUE;
}

static int
show_gui (WorkbookControlGUI *wbcg)
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
	if (x_geometry && wbcg->toplevel &&
	    gtk_window_parse_geometry (wbcg_toplevel (wbcg), x_geometry)) {
		/* Successfully parsed geometry string
		   and urged WM to comply */
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

	x_geometry = NULL;
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
wbcg_get_label_for_position (WorkbookControlGUI *wbcg, GtkWidget *source,
			     gint x)
{
	GtkWidget *label = NULL, *page;
	guint n, i;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), NULL);

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
wbcg_is_local_drag (WorkbookControlGUI *wbcg, GtkWidget *source_widget)
{
	GtkWidget *top = (GtkWidget *)wbcg_toplevel (wbcg);
	return IS_GNM_PANE (source_widget) &&
		gtk_widget_get_toplevel (source_widget) == top;
}
static gboolean
cb_wbcg_drag_motion (GtkWidget *widget, GdkDragContext *context,
		     gint x, gint y, guint time, WorkbookControlGUI *wbcg)
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
		    guint time, WorkbookControlGUI *wbcg)
{
	GtkWidget *source_widget = gtk_drag_get_source_widget (context);

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	if (IS_EDITABLE_LABEL (source_widget))
		gtk_widget_hide (
			g_object_get_data (G_OBJECT (source_widget), "arrow"));
	else if (wbcg_is_local_drag (wbcg, source_widget))
		gnm_pane_slide_stop (GNM_PANE (source_widget));
}

static void
cb_wbcg_drag_data_received (GtkWidget *widget, GdkDragContext *context,
			    gint x, gint y, GtkSelectionData *selection_data,
			    guint info, guint time, WorkbookControlGUI *wbcg)
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
wbcg_create_edit_area (WorkbookControlGUI *wbcg)
{
	GtkToolItem *item;
	GtkEntry *entry;
	int len;
	GtkToolbar *tb;
	GtkTooltips *tooltips;

	wbcg->selection_descriptor = gtk_entry_new ();
	wbcg_edit_ctor (wbcg);
	entry = wbcg_get_entry (wbcg);

	tb = (GtkToolbar *)gtk_toolbar_new ();
	gtk_toolbar_set_show_arrow (tb, FALSE);
	gtk_toolbar_set_style (tb, GTK_TOOLBAR_ICONS);

	tooltips = gtk_tooltips_new ();
#if GLIB_CHECK_VERSION(2,10,0) && GTK_CHECK_VERSION(2,8,14)
	g_object_ref_sink (tooltips);
#else
	g_object_ref (tooltips);
	gtk_object_sink (GTK_OBJECT (tooltips));
#endif
	g_object_set_data_full (G_OBJECT (tb),
				"tooltips", tooltips,
				(GDestroyNotify)g_object_unref);

	/* Set a reasonable width for the selection box. */
	len = go_pango_measure_string (
		gtk_widget_get_pango_context (GTK_WIDGET (wbcg_toplevel (wbcg))),
		GTK_WIDGET (entry)->style->font_desc,
		cell_coord_name (SHEET_MAX_COLS - 1, SHEET_MAX_ROWS - 1));
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
		 tooltips, _("Cancel change"));
	wbcg->ok_button = edit_area_button
		(wbcg, tb, FALSE,
		 G_CALLBACK (cb_accept_input), GTK_STOCK_OK,
		 tooltips, _("Accept change"));
	wbcg->func_button = edit_area_button
		(wbcg, tb, TRUE,
		 G_CALLBACK (cb_autofunction), "Gnumeric_Equal",
		 tooltips, _("Enter formula..."));

	/* Dependency debugger */
	if (gnumeric_debugging > 9 ||
	    dependency_debugging > 0 ||
	    expression_sharing_debugging > 0) {
		(void)edit_area_button (wbcg, tb, TRUE,
					G_CALLBACK (cb_workbook_debug_info),
					GTK_STOCK_DIALOG_INFO,
					tooltips,
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

static void
wbcg_create_status_area (WorkbookControlGUI *wbcg)
{
	WorkbookControlGUIClass *wbcg_class = WBCG_CLASS (wbcg);
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
			tmp->style->font_desc,
			"W") * 15, -1);
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

	wbcg_class->create_status_area (wbcg, wbcg->progress_bar, wbcg->status_text, frame);
}

void
wbcg_set_toplevel (WorkbookControlGUI *wbcg, GtkWidget *w)
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
		G_CALLBACK (wbcg_close_control), wbcg, NULL,
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

static int
wbcg_validation_msg (WorkbookControl *wbc, ValidationStyle v,
		     char const *title, char const *msg)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
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

static void
wbcg_progress_set (GOCmdContext *cc, gfloat val)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)cc;
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (wbcg->progress_bar), val);
}

static void
wbcg_progress_message_set (GOCmdContext *cc, gchar const *msg)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)cc;
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (wbcg->progress_bar), msg);
}

#define DISCONNECT(obj,field)						\
	if (wbcg->field) {						\
		if (obj)						\
			g_signal_handler_disconnect (obj, wbcg->field);	\
		wbcg->field = 0;					\
	}

static void
wbcg_view_changed (WorkbookControlGUI *wbcg,
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
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-series.h>
#include <goffice/data/go-data.h>
#include "graph.h"

static void
wbcg_data_allocator_allocate (GogDataAllocator *dalloc, GogPlot *plot)
{
	SheetControlGUI *scg = wbcg_cur_scg (WORKBOOK_CONTROL_GUI (dalloc));
	sv_selection_to_plot (scg_view (scg), plot);
}

typedef struct {
	GnmExprEntry *entry;
	GogDataset *dataset;
	int dim_i;
	GogDataType data_type;
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
				texpr = gnm_expr_top_new_constant
					(value_new_string
					 (gnm_expr_entry_get_text
					  (editor->entry)));
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

static void
cb_graph_dim_entry_unmap (GnmExprEntry *gee, GraphDimEditor *editor)
{
	cb_graph_dim_editor_update (gee, FALSE, editor);
}

static void
cb_graph_dim_entry_unrealize (GnmExprEntry *gee, GraphDimEditor *editor)
{
	cb_graph_dim_editor_update (gee, FALSE, editor);
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
	if (editor->dataset)
		g_object_weak_unref (G_OBJECT (editor->dataset),
			(GWeakNotify) cb_dim_editor_weakref_notify, editor);
	g_free (editor);
}

static gpointer
wbcg_data_allocator_editor (GogDataAllocator *dalloc,
			    GogDataset *dataset, int dim_i, GogDataType data_type)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (dalloc);
	GraphDimEditor *editor;
	GOData *val;

	editor = g_new (GraphDimEditor, 1);
	editor->dataset		= dataset;
	editor->dim_i		= dim_i;
	editor->data_type	= data_type;
	editor->entry  		= gnm_expr_entry_new (wbcg, TRUE);
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
	gnm_expr_entry_set_flags (editor->entry,
		GNM_EE_ABS_COL|GNM_EE_ABS_ROW, GNM_EE_MASK);

	g_signal_connect (G_OBJECT (editor->entry),
		"update",
		G_CALLBACK (cb_graph_dim_editor_update), editor);
	g_signal_connect (G_OBJECT (editor->entry),
		"unmap",
		G_CALLBACK (cb_graph_dim_entry_unmap), editor);
	g_signal_connect (G_OBJECT (editor->entry),
		"unrealize",
		G_CALLBACK (cb_graph_dim_entry_unrealize), editor);
	g_object_set_data_full (G_OBJECT (editor->entry),
		"editor", editor, (GDestroyNotify) graph_dim_editor_free);

	return editor->entry;
}

static void
wbcg_go_plot_data_allocator_init (GogDataAllocatorClass *iface)
{
	iface->allocate   = wbcg_data_allocator_allocate;
	iface->editor	  = wbcg_data_allocator_editor;
}

/***************************************************************************/

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

static void
workbook_control_gui_class_init (GObjectClass *gobject_class)
{
	WorkbookControlClass *wbc_class =
		WORKBOOK_CONTROL_CLASS (gobject_class);

	g_return_if_fail (wbc_class != NULL);

	parent_class = g_type_class_peek_parent (gobject_class);
	gobject_class->finalize = wbcg_finalize;
	gobject_class->get_property = wbcg_get_property;
	gobject_class->set_property = wbcg_set_property;

        g_object_class_install_property
		(gobject_class,
		 PROP_AUTOSAVE_PROMPT,
		 g_param_spec_boolean ("autosave-prompt",
				       _("Autosave prompt"),
				       _("Ask about autosave?"),
				       FALSE,
				       GSF_PARAM_STATIC |
				       G_PARAM_READWRITE));
        g_object_class_install_property
		(gobject_class,
		 PROP_AUTOSAVE_TIME,
		 g_param_spec_int ("autosave-time",
				   _("Autosave time in seconds"),
				   _("Seconds before autosave"),
				   0, G_MAXINT, 0,
				   GSF_PARAM_STATIC |
				   G_PARAM_READWRITE));

	wbc_class->edit_line_set	= wbcg_edit_line_set;
	wbc_class->selection_descr_set	= wbcg_edit_selection_descr_set;
	wbc_class->update_action_sensitivity = wbcg_update_action_sensitivity;

	wbc_class->sheet.add        = wbcg_sheet_add;
	wbc_class->sheet.remove	    = wbcg_sheet_remove;
	wbc_class->sheet.focus	    = wbcg_sheet_focus;
	wbc_class->sheet.remove_all = wbcg_sheet_remove_all;

	wbc_class->undo_redo.labels   = wbcg_undo_redo_labels;

	wbc_class->menu_state.update      = wbcg_menu_state_update;

	wbc_class->claim_selection	 = wbcg_claim_selection;
	wbc_class->paste_from_selection  = wbcg_paste_from_selection;
	wbc_class->validation_msg	 = wbcg_validation_msg;

	wbcg_signals [WBCG_MARKUP_CHANGED] = g_signal_new ("markup-changed",
		WORKBOOK_CONTROL_GUI_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WorkbookControlGUIClass, markup_changed),
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
workbook_control_gui_init (WorkbookControlGUI *wbcg)
{
	wbcg->table       = gtk_table_new (0, 0, 0);
	wbcg->notebook    = NULL;
	wbcg->updating_ui = FALSE;
	wbcg->rangesel	  = NULL;
	wbcg->font_desc   = NULL;

	wbcg->visibility_widgets =
		g_hash_table_new_full (g_str_hash, g_str_equal,
				       (GDestroyNotify)g_free,
				       (GDestroyNotify)g_object_unref);
	wbcg->toggle_for_fullscreen = g_hash_table_new_full (
		g_str_hash, g_str_equal, (GDestroyNotify)g_free, NULL);

	wbcg->autosave_prompt = FALSE;
	wbcg->autosave_time = 0;
	wbcg->autosave_timer = 0;

#warning "why is this here ?"
	wbcg->current_saver = NULL;
}

GSF_CLASS_FULL (WorkbookControlGUI, workbook_control_gui,
		NULL, NULL, workbook_control_gui_class_init, NULL,
		workbook_control_gui_init, WORKBOOK_CONTROL_TYPE, G_TYPE_FLAG_ABSTRACT,
		GSF_INTERFACE (wbcg_go_plot_data_allocator_init, GOG_DATA_ALLOCATOR_TYPE);
		GSF_INTERFACE (wbcg_gnm_cmd_context_init, GO_CMD_CONTEXT_TYPE))

/* Move the rubber bands if we are the source */

static void
wbcg_create (WorkbookControlGUI *wbcg,
	     WorkbookView *optional_view,
	     Workbook *optional_wb,
	     GdkScreen *optional_screen)
{
	Sheet *sheet;
	WorkbookView *wbv;
	WorkbookControl *wbc = (WorkbookControl *)wbcg;

	wbcg_create_edit_area (wbcg);
	wbcg_create_status_area (wbcg);

	wbcg_reload_recent_file_menu (wbcg);
	g_signal_connect_object (gnm_app_get_app (),
		"notify::file-history-list",
		G_CALLBACK (wbcg_reload_recent_file_menu), wbcg, G_CONNECT_SWAPPED);

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
}

extern GType wbc_gtk_get_type (void);

WorkbookControl *
workbook_control_gui_new (WorkbookView *optional_view,
			  Workbook *optional_wb,
			  GdkScreen *optional_screen)
{
	WorkbookControlGUI *wbcg = g_object_new (wbc_gtk_get_type (), NULL);
	wbcg_create (wbcg, optional_view, optional_wb, optional_screen);
	wb_control_init_state ((WorkbookControl *)wbcg);
	return (WorkbookControl *)wbcg;
}

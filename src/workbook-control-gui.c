/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * workbook-control-gui.c: GUI specific routines for a workbook-control.
 *
 * Copyright (C) 2000-2002 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <gnumeric-config.h>
#include <glib/gi18n.h>
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
#include "gnumeric-canvas.h"
#include "sheet-object.h"
#include "dialogs.h"
#include "commands.h"
#include "datetime.h"
#include "cmd-edit.h"
#include "workbook-cmd-format.h"
#include "selection.h"
#include "clipboard.h"
#include "print.h"
#include "gui-clipboard.h"
#include "workbook-edit.h"
#include "libgnumeric.h"
#include "dependent.h"
#include "expr.h"
#include "position.h"
#include "parse-util.h"
#include "ranges.h"
#include "value.h"
#include "validation.h"
#include "history.h"
#include "style-color.h"
#include "str.h"
#include "cell.h"
#include "format.h"
#include "gui-file.h"
#include "search.h"
#include "error-info.h"
#include "gui-util.h"
#include "widgets/widget-editable-label.h"
#include "widgets/preview-file-selection.h"
#include "src/plugin-util.h"
#include "sheet-object-image.h"
#include "gnumeric-gconf.h"
#include "filter.h"
#include "io-context.h"
#include "stf.h"
#include "rendered-value.h"
#include "sort.h"
#include <goffice/graph/gog-data-set.h>

#include <libgnomevfs/gnome-vfs-uri.h>

#include <gsf/gsf-impl-utils.h>
#include <widgets/widget-color-combo.h>
#include <widgets/gnm-combo-stack.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkdnd.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtkprogressbar.h>

#include <string.h>
#include <errno.h>

#define WBCG_CLASS(o) WORKBOOK_CONTROL_GUI_CLASS (G_OBJECT_GET_CLASS (o))
#define WBCG_VIRTUAL_FULL(func, handle, arglist, call)		\
void wbcg_ ## func arglist				\
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

enum {
	TARGET_URI_LIST,
	TARGET_SHEET
};

WBCG_VIRTUAL (set_transient,
	(WorkbookControlGUI *wbcg, GtkWindow *window), (wbcg, window))

static WBCG_VIRTUAL (actions_sensitive, 
	(WorkbookControlGUI *wbcg, gboolean sensitive), (wbcg, sensitive))
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

int
wbcg_sheet_to_page_index (WorkbookControlGUI *wbcg, Sheet *sheet,
			  SheetControlGUI **res)
{
	int i = 0;
	GtkWidget *w;

	if (res)
		*res = NULL;

	if (sheet == NULL || wbcg->notebook == NULL)
		return -1;

	g_return_val_if_fail (IS_SHEET (sheet), -1);

	for ( ; NULL != (w = gtk_notebook_get_nth_page (wbcg->notebook, i)) ; i++) {
		SheetControlGUI *scg = g_object_get_data (G_OBJECT (w), SHEET_CONTROL_KEY);
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
wbcg_toplevel (WorkbookControlGUI *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), NULL);

	return wbcg->toplevel;
}

static void
wbcg_set_transient_for (WorkbookControlGUI *wbcg, GtkWindow *window)
{
	gnumeric_set_transient (wbcg_toplevel (wbcg), window);
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
	return sc_sheet (SHEET_CONTROL (scg));
}

SheetControlGUI *
wbcg_cur_scg (WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg;

	wbcg_sheet_to_page_index (
		wbcg,
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

	/* Already range selecting */
	if (wbcg->rangesel != NULL)
		return TRUE;

	/* Rangesel requires that we be editing somthing */
	if (!wbcg_is_editing (wbcg) && !wbcg_edit_entry_redirect_p (wbcg))
		return FALSE;

	return gnm_expr_entry_can_rangesel (wbcg_get_entry_logical (wbcg));
}

gboolean
wbcg_is_editing (WorkbookControlGUI const *wbcg)
{
	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);
	return wbcg->wb_control.editing;
}

void
wbcg_autosave_cancel (WorkbookControlGUI *wbcg)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	if (wbcg->autosave_timer != 0) {
		g_source_remove (wbcg->autosave_timer);
		wbcg->autosave_timer = 0;
	}
}

void
wbcg_autosave_set (WorkbookControlGUI *wbcg, int minutes, gboolean prompt)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	wbcg_autosave_cancel (wbcg);

	wbcg->autosave = (minutes != 0);
	wbcg->autosave_minutes = minutes;
	wbcg->autosave_prompt = prompt;

	if (wbcg->autosave)
		wbcg->autosave_timer = g_timeout_add (minutes * 60000,
			(GSourceFunc) cb_autosave, wbcg);
}
/****************************************************************************/

static void
wbcg_title_set (WorkbookControl *wbc, char const *title)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	char *full_title;

	g_return_if_fail (wbcg != NULL);
	g_return_if_fail (title != NULL);

	full_title = g_strconcat (title, _(" : Gnumeric"), NULL);

 	gtk_window_set_title (wbcg_toplevel (wbcg), full_title);
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
wbcg_zoom_feedback (WorkbookControl *wbc)
{
	Sheet *sheet = wb_control_cur_sheet (wbc);
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;

	if (wbcg_ui_update_begin (wbcg)) {
		int pct = sheet->last_zoom_factor_used * 100 + .5;
		char *buffer = g_strdup_printf ("%d%%", pct);
		WorkbookControlGUIClass *wbcg_class = WBCG_CLASS (wbcg);
		wbcg_class->set_zoom_label (wbcg, buffer);
		g_free (buffer);
		wbcg_ui_update_end (wbcg);
	}

	scg_object_update_bbox (wbcg_cur_scg (wbcg), NULL, NULL);
}

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

void
wbcg_toolbar_timer_clear (WorkbookControlGUI *wbcg)
{
	/* Remove previous ui timer */
	if (wbcg->toolbar_sensitivity_timer != 0) {
		g_source_remove (wbcg->toolbar_sensitivity_timer);
		wbcg->toolbar_sensitivity_timer = 0;
	}
}

static gboolean
cb_thaw_ui_toolbar (gpointer *data)
{
        WorkbookControlGUI *wbcg = (WorkbookControlGUI *)data;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	wbcg_actions_sensitive (wbcg, TRUE);
	wbcg_toolbar_timer_clear (wbcg);

	return TRUE;
}

static void
wbcg_set_sensitive (GnmCmdContext *cc, gboolean sensitive)
{
	GtkWindow *toplevel = wbcg_toplevel (WORKBOOK_CONTROL_GUI (cc));

	if (toplevel != NULL)
		gtk_widget_set_sensitive (GTK_WIDGET (toplevel), sensitive);
}

static void
wbcg_edit_set_sensitive (WorkbookControl *wbc,
			 gboolean ok_cancel_flag, gboolean func_guru_flag)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (wbc);

	/* These are only sensitive while editing */
	gtk_widget_set_sensitive (wbcg->ok_button, ok_cancel_flag);
	gtk_widget_set_sensitive (wbcg->cancel_button, ok_cancel_flag);

	gtk_widget_set_sensitive (wbcg->func_button, func_guru_flag);
	wbcg_toolbar_timer_clear (wbcg);

	/* Toolbars are insensitive while editing */
	if (func_guru_flag) {
		/* We put the re-enabling of the ui on a timer */
		wbcg->toolbar_sensitivity_timer = g_timeout_add (300,
			(GSourceFunc) cb_thaw_ui_toolbar, wbcg);
	} else
		wbcg_actions_sensitive (wbcg, func_guru_flag);
}

static gboolean
cb_sheet_label_edit_finished (EditableLabel *el, char const *new_name,
			      WorkbookControlGUI *wbcg)
{
	gboolean reject = FALSE;
	if (new_name != NULL)
		reject = cmd_rename_sheet (WORKBOOK_CONTROL (wbcg), NULL,
				editable_label_get_text (el), new_name);
	wbcg_focus_cur_scg (wbcg);
	return reject;
}

static void
insert_sheet_at (WorkbookControlGUI *wbcg, gint before)
{
	GSList *new_order = NULL;
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	gint sheet_count = workbook_sheet_count (wb);
	gint n;

	for (n = 0; n < sheet_count; n++)
		new_order = g_slist_prepend (new_order, GINT_TO_POINTER (n));
	new_order = g_slist_reverse (new_order);

	new_order = g_slist_insert (new_order, GINT_TO_POINTER (-1), before);
	
	cmd_reorganize_sheets (WORKBOOK_CONTROL (wbcg), new_order,
			       g_slist_prepend (NULL, GINT_TO_POINTER (-1)),
			       g_slist_prepend (NULL, NULL),
			       NULL, NULL, NULL, NULL, NULL, NULL);
}


void
wbcg_insert_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	insert_sheet_at (wbcg, sheet->index_in_wb);
}

void
wbcg_append_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	cmd_reorganize_sheets (WORKBOOK_CONTROL (wbcg), NULL,
			       g_slist_prepend (NULL, GINT_TO_POINTER (-1)),
			       g_slist_prepend (NULL, NULL),
			       NULL, NULL, NULL, NULL, NULL, NULL);
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
scg_delete_sheet_if_possible (GtkWidget *ignored, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;
	Workbook *wb = wb_control_workbook (sc->wbc);

	/* If this is the last sheet left, ignore the request */
	if (workbook_sheet_count (wb) != 1)
		cmd_reorganize_sheets 
			(WORKBOOK_CONTROL (scg->wbcg), NULL, NULL, NULL, 
			 g_slist_prepend 
			 (NULL, GINT_TO_POINTER (sc->sheet->index_in_wb)),
			 NULL, NULL, NULL, NULL, NULL);
}

static void
sheet_action_rename_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	editable_label_start_editing (EDITABLE_LABEL(scg->label));
}

static void
sheet_action_clone_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	SheetControl *sc = (SheetControl *) scg;

	cmd_clone_sheet (WORKBOOK_CONTROL (scg->wbcg), sc->sheet);
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
		{ N_("Manage sheets..."), &sheet_action_reorder_sheet, 0},
		{ "", NULL, 0},
		{ N_("Insert"), &sheet_action_insert_sheet, 0 },
		{ N_("Append"), &sheet_action_add_sheet, 0 },
		{ N_("Duplicate"), &sheet_action_clone_sheet, 0 },
		{ N_("Remove"), &scg_delete_sheet_if_possible, SHEET_CONTEXT_TEST_SIZE },
		{ N_("Rename"), &sheet_action_rename_sheet, 0 },
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
		if (sheet_label_context_actions [i].text[0] == '\0') {
			item = gtk_separator_menu_item_new ();
		} else {
			item = gtk_menu_item_new_with_label (
				_(sheet_label_context_actions [i].text));
			g_signal_connect (G_OBJECT (item),
					  "activate",
					  G_CALLBACK (sheet_label_context_actions [i].function), scg);
		}
		
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
			     GtkWidget *child)
{
	GtkWidget *notebook;
	gint page_number;
	SheetControlGUI *scg = g_object_get_data (G_OBJECT (child), SHEET_CONTROL_KEY);

	g_return_val_if_fail (scg != NULL, FALSE);

	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	notebook = child->parent;
	page_number = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), child);

	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), page_number);

	if (event->button == 1 || NULL != scg->wbcg->rangesel)
		return TRUE;

	if (event->button == 3) {
		sheet_menu_label_run (scg, event);
		scg_take_focus (scg);
		return TRUE;
	}

	return FALSE;
}

static gint
gtk_notebook_page_num_by_label (GtkNotebook *notebook, GtkWidget *label)
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

	n_source = gtk_notebook_page_num_by_label (wbcg->notebook, widget);
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
	gint n_source, n_dest;
	GSList *new_order = NULL;
	Workbook *wb;
	guint n, i;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	w_source = gtk_drag_get_source_widget (context);
	n_source = gtk_notebook_page_num_by_label (wbcg->notebook, w_source);

	/*
	 * Is this a sheet of our workbook? If yes, we just reorder
	 * the sheets.
	 */
	if (n_source >= 0) {

		/* Make a list of the current order. */
		wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
		n = workbook_sheet_count (wb);
		for (i = 0; i < n; i++)
			new_order = g_slist_append (new_order, 
						    GINT_TO_POINTER (i));
		new_order = g_slist_remove (new_order, 
					    GINT_TO_POINTER (n_source));
		n_dest = gtk_notebook_page_num_by_label (wbcg->notebook,
							 widget);
		new_order = g_slist_insert (new_order, 
					    GINT_TO_POINTER (n_source), 
					    n_dest);

		/* Reorder the sheets! */
		cmd_reorganize_sheets (WORKBOOK_CONTROL (wbcg),
			new_order, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL);
	} else {

		g_return_if_fail (IS_SHEET_CONTROL_GUI (data->data));

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
	pixbuf = gnm_app_get_pixbuf ("sheet_move_marker");
	image = gtk_image_new_from_pixbuf (pixbuf);
	gtk_widget_show (image);
	gtk_container_add (GTK_CONTAINER (arrow), image);
	gdk_pixbuf_render_pixmap_and_mask_for_colormap (pixbuf,
		gtk_widget_get_colormap (widget), NULL, &bitmap, 0x7f);
	gtk_widget_shape_combine_mask (arrow, bitmap, 0, 0);
	g_object_unref (bitmap);
	g_object_ref (G_OBJECT (arrow));
	gtk_object_sink (GTK_OBJECT (arrow));
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
	g_object_set_data (G_OBJECT (widget), "arrow", NULL);
}

static void
cb_sheet_label_drag_leave (GtkWidget *widget, GdkDragContext *context,
			   guint time, WorkbookControlGUI *wbcg)
{
	GtkWidget *w_source, *arrow;

	/* Hide the arrow. */
	w_source = gtk_drag_get_source_widget (context);
	arrow = g_object_get_data (G_OBJECT (w_source), "arrow");
	gtk_widget_hide (arrow);
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
	n_source = gtk_notebook_page_num_by_label (wbcg->notebook, w_source);
	n_dest   = gtk_notebook_page_num_by_label (wbcg->notebook, widget);
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
static void wbcg_menu_state_sheet_count (WorkbookControl *wbc);

/**
 * wbcg_sheet_add:
 * @sheet: a sheet
 *
 * Creates a new SheetControlGUI for the sheet and adds it to the workbook-control-gui.
 */
static void
wbcg_sheet_add (WorkbookControl *wbc, SheetView *sv)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;
	SheetControl	*sc;
	Sheet		*sheet;
	GList *ptr;
	static GtkTargetEntry const drag_types[] = {
		{ (char *) "GNUMERIC_SHEET", 0, TARGET_SHEET }
	};

	g_return_if_fail (wbcg != NULL);

	if (wbcg->notebook == NULL)
		workbook_setup_sheets (wbcg);

	sheet = sv_sheet (sv);
	scg = sheet_control_gui_new (sv, wbcg);

	/*
	 * NB. this is so we can use editable_label_set_text since
	 * gtk_notebook_set_tab_label kills our widget & replaces with a label.
	 */
	scg->label = editable_label_new (sheet->name_unquoted,
			sheet->tab_color ? &sheet->tab_color->color : NULL,
			sheet->tab_text_color ? &sheet->tab_text_color->color : NULL);
	g_signal_connect_after (G_OBJECT (scg->label),
		"edit_finished",
		G_CALLBACK (cb_sheet_label_edit_finished), wbcg);

	/* do not preempt the editable label handler */
	g_signal_connect_after (G_OBJECT (scg->label),
		"button_press_event",
		G_CALLBACK (cb_sheet_label_button_press), scg->table);

	/* Drag & Drop */
	gtk_drag_source_set (scg->label, GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
			drag_types, G_N_ELEMENTS (drag_types),
			GDK_ACTION_MOVE);
	gtk_drag_dest_set (scg->label, GTK_DEST_DEFAULT_ALL,
			drag_types, G_N_ELEMENTS (drag_types),
			GDK_ACTION_MOVE);
	g_signal_connect (G_OBJECT (scg->label), "drag_begin",
			G_CALLBACK (cb_sheet_label_drag_begin), wbcg);
	g_signal_connect (G_OBJECT (scg->label), "drag_end",
			G_CALLBACK (cb_sheet_label_drag_end), wbcg);
	g_signal_connect (G_OBJECT (scg->label), "drag_leave",
			G_CALLBACK (cb_sheet_label_drag_leave), wbcg);
	g_signal_connect (G_OBJECT (scg->label), "drag_data_get",
			G_CALLBACK (cb_sheet_label_drag_data_get), wbcg);
	g_signal_connect (G_OBJECT (scg->label), "drag_data_received",
			G_CALLBACK (cb_sheet_label_drag_data_received), wbcg);
	g_signal_connect (G_OBJECT (scg->label), "drag_motion",
			G_CALLBACK (cb_sheet_label_drag_motion), wbcg);

	gtk_widget_show (scg->label);
	gtk_widget_show_all (GTK_WIDGET (scg->table));

	if (wbcg_ui_update_begin (wbcg)) {
		gtk_notebook_insert_page (wbcg->notebook,
			GTK_WIDGET (scg->table), scg->label,
			sheet->index_in_wb);
		wbcg_ui_update_end (wbcg);
	}

	wb_control_menu_state_sheet_count (wbc);

	/* create views for the sheet objects */
	sc = (SheetControl *) scg;
	for (ptr = sheet->sheet_objects; ptr != NULL ; ptr = ptr->next)
		sc_object_create_view (sc, ptr->data);
	scg_adjust_preferences (sc);
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

	i = wbcg_sheet_to_page_index (wbcg, sheet, &scg);

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
	int i = wbcg_sheet_to_page_index (wbcg, sheet, &scg);

	g_return_if_fail (i >= 0);

	label = gtk_notebook_get_tab_label (wbcg->notebook, GTK_WIDGET (scg->table));
	editable_label_set_text (EDITABLE_LABEL (label), sheet->name_unquoted);
	editable_label_set_color (EDITABLE_LABEL (label),
		&sheet->tab_color->color, &sheet->tab_text_color->color);
}

static void
wbcg_sheet_focus (WorkbookControl *wbc, Sheet *sheet)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;
	int i = wbcg_sheet_to_page_index (wbcg, sheet, &scg);

	/* A sheet added in another view may not yet have a view */
	if (i >= 0) {
		gtk_notebook_set_current_page (wbcg->notebook, i);
		wb_control_zoom_feedback (wbc);
		if (wbcg->rangesel == NULL)
			gnm_expr_entry_set_scg (wbcg->edit_line.entry, scg);
	}
}

static void
wbcg_sheet_move (WorkbookControl *wbc, Sheet *sheet, int new_pos)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg;

	g_return_if_fail (IS_SHEET (sheet));

	/* No need for sanity checking, the workbook did that */
        if (wbcg_sheet_to_page_index (wbcg, sheet, &scg) >= 0)
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
		wbcg_edit_finish (wbcg, FALSE, NULL);

		/* Clear notebook to disable updates as focus changes for pages
		 * during destruction
		 */
		wbcg->notebook = NULL;
		gtk_container_remove (GTK_CONTAINER (wbcg->table), tmp);
	}
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
			 GTK_LABEL (wbcg->auto_expr_label),
			 wbv->auto_expr_value_as_string);
		wbcg_ui_update_end (wbcg);
	}
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
		(scg->current_object != NULL || scg->new_object != NULL);

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
	if (MS_CLIPBOARD & flags) {
		wbcg_set_action_sensitivity (wbcg, "EditCut", !edit_object);
		wbcg_set_action_sensitivity (wbcg, "EditCopy", !edit_object);
		wbcg_set_action_sensitivity (wbcg, "EditPaste", !edit_object);
	}
	if (MS_PASTE_SPECIAL & flags)
		wbcg_set_action_sensitivity (wbcg, "EditPasteSpecial",
			!gnm_app_clipboard_is_empty () &&
			!gnm_app_clipboard_is_cut () &&
			!edit_object);
	if (MS_PRINT_SETUP & flags)
		wbcg_set_action_sensitivity (wbcg, "FilePageSetup", !has_guru);
	if (MS_SEARCH_REPLACE & flags)
		wbcg_set_action_sensitivity (wbcg, "EditSearchReplace", !has_guru);
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
wbcg_menu_state_sheet_prefs (WorkbookControl *wbc, Sheet const *sheet)
{
 	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;

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

	wbcg_set_action_sensitivity (wbcg, "SheetRemove", multi_sheet);
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
wbcg_get_password (GnmCmdContext *cc, char const* filename)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (cc);

	return dialog_get_password (wbcg_toplevel (wbcg), filename);
}

static void
wbcg_error_error (GnmCmdContext *cc, GError *err)
{
	gnumeric_notice (WORKBOOK_CONTROL_GUI (cc),
		GTK_MESSAGE_ERROR, err->message);
}

static void
wbcg_error_error_info (GnmCmdContext *cc, ErrorInfo *error)
{
	gnumeric_error_info_dialog_show (WORKBOOK_CONTROL_GUI (cc), error);
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
int
wbcg_close_if_user_permits (WorkbookControlGUI *wbcg,
				WorkbookView *wb_view, gboolean close_clean,
				gboolean exiting, gboolean ask_user)
{
	gboolean   can_close = TRUE;
	gboolean   done      = FALSE;
	int        iteration = 0;
	int        button = 0;
	Workbook  *wb = wb_view_workbook (wb_view);
	static int in_can_close;

	g_return_val_if_fail (IS_WORKBOOK (wb), 0);

	if (!close_clean && !workbook_is_dirty (wb))
		return 2;

	if (in_can_close)
		return 0;
	in_can_close = TRUE;

	if (!ask_user) {
		done = gui_file_save (wbcg, wb_view);
		if (done) {
			workbook_unref (wb);
			return 3;
		}
	}
	while (workbook_is_dirty (wb) && !done) {
		GtkWidget *d;
		char *msg;

		iteration++;

		if (wb->filename) {
			char *base = g_path_get_basename (wb->filename);
			msg = g_strdup_printf (
				_("Save changes to workbook '%s' before closing?"),
				base);
			g_free (base);
		} else
			msg = g_strdup (_("Save changes to workbook before closing?"));

		d = gnumeric_message_dialog_new (wbcg_toplevel (wbcg),
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_WARNING,
						 msg,
						 _("If you close without saving, changes will be discarded."));
						 
		if (exiting) {
			int n_of_wb = g_list_length (gnm_app_workbook_list ());
			if (n_of_wb > 1)
			{
			  	gnumeric_dialog_add_button (GTK_DIALOG(d), _("Discard all"), 
							    GTK_STOCK_DELETE, - GTK_RESPONSE_NO);
				gnumeric_dialog_add_button (GTK_DIALOG(d), _("Discard"), 
							    GTK_STOCK_DELETE, GTK_RESPONSE_NO);
				gnumeric_dialog_add_button (GTK_DIALOG(d), _("Save all"), 
							    GTK_STOCK_SAVE, - GTK_RESPONSE_YES);
				gnumeric_dialog_add_button (GTK_DIALOG(d), _("Don't quit"), 
							    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
			}
			else
			{
				gnumeric_dialog_add_button (GTK_DIALOG(d), _("Discard"),
							    GTK_STOCK_DELETE, GTK_RESPONSE_NO);
				gnumeric_dialog_add_button (GTK_DIALOG(d), _("Don't quit"), 
							    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
			}
		} 
		else
		{
			gnumeric_dialog_add_button (GTK_DIALOG(d), _("Discard"), 
						    GTK_STOCK_DELETE, GTK_RESPONSE_NO);
			gnumeric_dialog_add_button (GTK_DIALOG(d), _("Don't close"), 
						    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		}

		gtk_dialog_add_button (GTK_DIALOG(d), GTK_STOCK_SAVE, GTK_RESPONSE_YES);
		gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
		button = gnumeric_dialog_run (wbcg, GTK_DIALOG (d));
		g_free (msg);

		switch (button) {
		case GTK_RESPONSE_YES:
			done = gui_file_save (wbcg, wb_view);
			break;

		case (- GTK_RESPONSE_YES):
			done = gui_file_save (wbcg, wb_view);
			break;

		case GTK_RESPONSE_NO:
			done      = TRUE;
			workbook_set_dirty (wb, FALSE);
			break;

		case (- GTK_RESPONSE_NO):
			done      = TRUE;
			workbook_set_dirty (wb, FALSE);
			break;

		default:  /* CANCEL */
			can_close = FALSE;
			done      = TRUE;
			break;
		}
	}

	in_can_close = FALSE;

	if (can_close) {
		workbook_unref (wb);
		switch (button) {
		case (- GTK_RESPONSE_YES):
			return 3;
		case (- GTK_RESPONSE_NO):
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
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return TRUE;

	/* If something is still using the control
	 * eg progress meter for a new book */
	if (G_OBJECT (wbcg)->ref_count > 1)
		return TRUE;

	/* This is the last control */
	if (wb_view->wb_controls->len <= 1) {
		Workbook *wb = wb_view_workbook (wb_view);

		g_return_val_if_fail (IS_WORKBOOK (wb), TRUE);
		g_return_val_if_fail (wb->wb_views != NULL, TRUE);

		/* This is the last view */
		if (wb->wb_views->len <= 1)
			return wbcg_close_if_user_permits (wbcg, wb_view, TRUE, FALSE, TRUE) == 0;

		g_object_unref (G_OBJECT (wb_view));
	} else
		g_object_unref (G_OBJECT (wbcg));

	return FALSE;
}

static void
cb_cancel_input (G_GNUC_UNUSED gpointer p, WorkbookControlGUI *wbcg)
{
	wbcg_edit_finish (wbcg, FALSE, NULL);
}

static void
cb_accept_input (G_GNUC_UNUSED gpointer p, WorkbookControlGUI *wbcg)
{
	wbcg_edit_finish (wbcg, TRUE, NULL);
}

static gboolean
cb_editline_focus_in (GtkWidget *w, GdkEventFocus *event,
		      WorkbookControlGUI *wbcg)
{
	if (!wbcg_is_editing (wbcg))
		if (!wbcg_edit_start (wbcg, FALSE, TRUE))
			wbcg_focus_cur_scg (wbcg);

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

static GnmValue *
cb_share_a_cell (Sheet *sheet, int col, int row, GnmCell *cell, gpointer _es)
{
	if (cell && cell_has_expr (cell)) {
		ExprTreeSharer *es = _es;
		cell->base.expression =
			expr_tree_sharer_share (es, cell->base.expression);
	}

	return NULL;
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
		gnm_dep_container_dump (sheet->deps);
	}

	if (expression_sharing_debugging > 0) {
		ExprTreeSharer *es = expr_tree_sharer_new ();

		WORKBOOK_FOREACH_SHEET (wb, sheet, {
			sheet_foreach_cell_in_range (sheet, TRUE, 0, 0,
						     SHEET_MAX_COLS - 1,
						     SHEET_MAX_ROWS - 1,
						     &cb_share_a_cell,
						     es);
		});

		g_warning ("Nodes in: %d, nodes stored: %d.",
			   es->nodes_in, es->nodes_stored);
		expr_tree_sharer_destroy (es);
	}
}

static void
cb_autofunction (GtkWidget *widget, WorkbookControlGUI *wbcg)
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
edit_area_button (WorkbookControlGUI *wbcg, gboolean sensitive,
		  GCallback func, char const *stock_id)
{
	GtkWidget *button = gtk_button_new ();
	gtk_container_add (GTK_CONTAINER (button),
		gtk_image_new_from_stock (stock_id,
					  GTK_ICON_SIZE_BUTTON));
	GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
	if (!sensitive)
		gtk_widget_set_sensitive (button, FALSE);

	g_signal_connect (G_OBJECT (button),
		"clicked",
		G_CALLBACK (func), wbcg);

	return button;
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

		if (!wbcg_edit_finish (wbcg, TRUE, NULL))
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
		sheet_flag_status_update_range (sheet, NULL);
		sheet_update (sheet);
		wb_view_sheet_focus (wb_control_view (WORKBOOK_CONTROL (wbcg)), sheet);
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
	if (wbcg->notebook != NULL)
		g_signal_handlers_disconnect_by_func (
			G_OBJECT (wbcg->notebook),
			G_CALLBACK (cb_notebook_switch_page), wbcg);
	g_signal_handlers_disconnect_by_func (
		G_OBJECT (wbcg->toplevel),
		G_CALLBACK (cb_set_focus), wbcg);

	wbcg_auto_complete_destroy (wbcg);
	wbcg_edit_dtor (wbcg);

	gtk_window_set_focus (GTK_WINDOW (wbcg->toplevel), NULL);

	if (wbcg->toplevel != NULL)
		gtk_object_destroy (GTK_OBJECT (wbcg->toplevel));

	if (wbcg->font_desc)
		pango_font_description_free (wbcg->font_desc);

	if (parent_class != NULL && parent_class->finalize != NULL)
		(parent_class)->finalize (obj);
}

/* protected */
gboolean
wbcg_scroll_wheel_support_cb (GtkWidget *ignored, GdkEventScroll *event,
			      WorkbookControlGUI *wbcg)
{
	/* scroll always operates on pane 0 */
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	Sheet	 	*sheet = sc_sheet (SHEET_CONTROL (scg));
	GnmCanvas *gcanvas = scg_pane (scg, 0);
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
		int col = (gcanvas->last_full.col - gcanvas->first.col) / 4;
		if (col < 1)
			col = 1;
		if (go_back)
			col = gcanvas->first.col - col;
		else
			col = gcanvas->first.col + col;
		scg_set_left_col (gcanvas->simple.scg, col);
	} else {
		int row = (gcanvas->last_full.row - gcanvas->first.row) / 4;
		if (row < 1)
			row = 1;
		if (go_back)
			row = gcanvas->first.row - row;
		else
			row = gcanvas->first.row + row;
		scg_set_top_row (gcanvas->simple.scg, row);
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
	if (wbcg->notebook)
		wbcg_focus_cur_scg (wbcg);
}

static void
workbook_setup_sheets (WorkbookControlGUI *wbcg)
{
	wbcg->notebook = g_object_new (GTK_TYPE_NOTEBOOK,
				       "tab_pos",	GTK_POS_BOTTOM,
				       "tab_hborder",	0,
				       "tab_vborder",	0,
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
}

void
wbcg_set_status_text (WorkbookControlGUI *wbcg, char const *text)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	gtk_label_set_text (GTK_LABEL (wbcg->status_text), text);
}
void
wbcg_toggle_end_mode (WorkbookControlGUI *wbcg)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	wbcg_set_end_mode (wbcg, !wbcg->last_key_was_end);
}
void
wbcg_set_end_mode (WorkbookControlGUI *wbcg, gboolean flag)
{
	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	if (wbcg->last_key_was_end == flag)
		return;

	if (flag == TRUE) {
		wbcg->last_key_was_end = TRUE;
		wbcg_set_status_text (wbcg, "END");
	} else {
		wbcg->last_key_was_end = FALSE;
		wbcg_set_status_text (wbcg, "");
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

static void
cb_auto_expr_changed (GtkWidget *item, WorkbookControlGUI *wbcg)
{
	if (wbcg->updating_ui)
		return;

	wb_view_auto_expr (
		wb_control_view (WORKBOOK_CONTROL (wbcg)),
		g_object_get_data (G_OBJECT (item), "name"),
		g_object_get_data (G_OBJECT (item), "expr"));
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
	GtkWidget *item;
	int i;

	if (event->button != 3)
		return FALSE;

	menu = gtk_menu_new ();

	for (i = 0; quick_compute_routines [i].displayed_name; i++) {
		GnmParsePos pp;
		const char *expr = quick_compute_routines [i].function;
		const GnmExpr *new_auto_expr;

		/* Test the expression...  */
		parse_pos_init (&pp, wb_control_workbook (WORKBOOK_CONTROL (wbcg)), NULL, 0, 0);
		new_auto_expr = gnm_expr_parse_str_simple (expr, &pp);
		if (!new_auto_expr)
			continue;
		gnm_expr_unref (new_auto_expr);

		item = gtk_menu_item_new_with_label (
			_(quick_compute_routines [i].displayed_name));
		g_object_set_data (G_OBJECT (item), "expr", (gpointer)expr);
		g_object_set_data (G_OBJECT (item), "name",
			(gpointer)_(quick_compute_routines [i].displayed_name));
		g_signal_connect (G_OBJECT (item),
			"activate",
			G_CALLBACK (cb_auto_expr_changed), wbcg);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
	return TRUE;
}

static int
show_gui (WorkbookControlGUI *wbcg)
{
	WorkbookView *wbv = wb_control_view (WORKBOOK_CONTROL (wbcg));
	int sx, sy;
	gdouble fx, fy;
	GdkRectangle rect;

	/* In a Xinerama setup, we want the geometry of the actual display
	 * unit, if available. See bug 59902.  */
	gdk_screen_get_monitor_geometry (wbcg->toplevel->screen, 0, &rect);
	sx = MAX (rect.width, 600);
	sy = MAX (rect.height, 200);

	fx = gnm_app_prefs->horizontal_window_fraction;
	fy = gnm_app_prefs->vertical_window_fraction;
	if (x_geometry && wbcg->toplevel &&
	    gtk_window_parse_geometry (wbcg->toplevel, x_geometry)) {
		/* Successfully parsed geometry string
		   and urged WM to comply */
	} else if (wbcg->notebook != NULL &&
		   wbv != NULL &&
		   (wbv->preferred_width > 0 || wbv->preferred_height > 0)) {
		/* Set grid size to preferred width */
		int pwidth = wbv->preferred_width;
		int pheight = wbv->preferred_height;

		pwidth = pwidth > 0 ? pwidth : -2;
		pheight = pheight > 0 ? pheight : -2;
		gtk_widget_set_size_request (GTK_WIDGET (wbcg->notebook),
					     pwidth, pheight);
	} else {
		/* Use default */
		gtk_window_set_default_size (wbcg->toplevel, sx * fx, sy * fy);
	}

	x_geometry = NULL;
	gtk_widget_show_all (GTK_WIDGET (wbcg->toplevel));

	/* rehide headers if necessary */
	if (wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg))) {
		SheetControl *sc;

		sc = SHEET_CONTROL (wbcg_cur_scg (wbcg));
		scg_adjust_preferences (sc);
	}

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
	Workbook *wb    = wb_control_workbook (wbc);
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
cb_wbcg_drag_motion (GtkWidget *widget, GdkDragContext *context,
		     gint x, gint y, guint time, WorkbookControlGUI *wbcg)
{
	GtkWidget *source_widget;

	source_widget = gtk_drag_get_source_widget (context);
	if (IS_EDITABLE_LABEL (source_widget)) {
		GtkWidget *label;

		/* The user wants to reorder sheets. We simulate a
		 * drag motion over a label.
		 */
		label = wbcg_get_label_for_position (wbcg, source_widget, x);
		return (cb_sheet_label_drag_motion (label, context, x, y,
						    time, wbcg));
	}

	return (TRUE);
}

static void
cb_wbcg_drag_leave (GtkWidget *widget, GdkDragContext *context,
		    gint x, gint y, guint time, WorkbookControlGUI *wbcg)
{
	GtkWidget *source_widget, *arrow;

	source_widget = gtk_drag_get_source_widget (context);
	if (IS_EDITABLE_LABEL (source_widget)) {
		arrow = g_object_get_data (G_OBJECT (source_widget), "arrow");
		gtk_widget_hide (arrow);
	}
}

static void
cb_wbcg_drag_data_received (GtkWidget *widget, GdkDragContext *context,
		gint x, gint y, GtkSelectionData *selection_data,
		guint info, guint time, WorkbookControlGUI *wbcg)
{
	gchar *target_type;

	target_type = gdk_atom_name (selection_data->target);

	/* First possibility: User dropped some filenames. */
	if (!strcmp (target_type, "text/uri-list")) {
		GList *ptr, *uris = gnome_vfs_uri_list_parse (selection_data->data);

		for (ptr = uris; ptr != NULL; ptr = ptr->next) {
			GnomeVFSURI const *uri = ptr->data;
			gchar *str = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
			gchar *msg = g_strdup_printf (_("File \"%s\" has unknown format."), str);
			gnm_cmd_context_error_import (GNM_CMD_CONTEXT (wbcg), msg);
			g_free (msg);
#if 0
			if (gnome_vfs_uri_is_local (uri)) {
				if (!wb_view_open (file_name, wbc, FALSE, NULL)) {
				}
			}
			/* If it wasn't a workbook, see if we have a control for it */
			SheetObject *so = sheet_object_container_new_file (
				sc->sheet->workbook, file_name);
			if (so != NULL)
				scg_mode_create_object (gcanvas->simple.scg, so);
#endif
		}
		gnome_vfs_uri_list_free (uris);

	/* Second possibility: User dropped a sheet. */
	} else if (!strcmp (target_type, "GNUMERIC_SHEET")) {
		GtkWidget *label;
		GtkWidget *source_widget;

		/*
		 * The user wants to reorder the sheets but hasn't dropped
		 * the sheet onto a label. Never mind. We figure out
		 * where the arrow is currently located and simulate a drop
		 * on that label.
		 */
		source_widget = gtk_drag_get_source_widget (context);
		label = wbcg_get_label_for_position (wbcg, source_widget, x);
		cb_sheet_label_drag_data_received (label, context, x, y,
				selection_data, info, time, wbcg);

	} else
		g_warning ("Unknown target type '%s'!", target_type);
	g_free (target_type);
}

static void
wbcg_create_edit_area (WorkbookControlGUI *wbcg)
{
	GtkWidget *box, *box2;
	GtkEntry *entry;
	int len;

	wbcg->selection_descriptor = gtk_entry_new ();
	wbcg_edit_ctor (wbcg);
	entry = wbcg_get_entry (wbcg);
	box   = gtk_hbox_new (0, 0);
	box2  = gtk_hbox_new (0, 0);

	/* Set a reasonable width for the selection box. */
	len = gnm_measure_string (
		gtk_widget_get_pango_context (GTK_WIDGET (wbcg->toplevel)),
		GTK_WIDGET (entry)->style->font_desc,
		cell_coord_name (SHEET_MAX_COLS - 1, SHEET_MAX_ROWS - 1));
	/*
	 * Add a little extra since font might be proportional and since
	 * we also put user defined names there.
	 */
	len = len * 3 / 2;
	gtk_widget_set_size_request (wbcg->selection_descriptor, len, -1);

	wbcg->cancel_button = edit_area_button (wbcg, FALSE,
		G_CALLBACK (cb_cancel_input), GTK_STOCK_CANCEL);
	wbcg->ok_button = edit_area_button (wbcg, FALSE,
		G_CALLBACK (cb_accept_input), GTK_STOCK_OK);
	wbcg->func_button = edit_area_button (wbcg, TRUE,
		G_CALLBACK (cb_autofunction), "Gnumeric_Equal");

	gtk_box_pack_start (GTK_BOX (box2), wbcg->selection_descriptor, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->cancel_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->ok_button, 0, 0, 0);
	gtk_box_pack_start (GTK_BOX (box), wbcg->func_button, 0, 0, 0);

	/* Dependency debugger */
	if (gnumeric_debugging > 9 ||
	    dependency_debugging > 0 ||
	    expression_sharing_debugging > 0) {
		GtkWidget *deps_button = edit_area_button (wbcg, TRUE,
			G_CALLBACK (cb_workbook_debug_info),
			GTK_STOCK_DIALOG_INFO);
		gtk_box_pack_start (GTK_BOX (box), deps_button, 0, 0, 0);
	}

	gtk_box_pack_start (GTK_BOX (box2), box, 0, 0, 0);
	gtk_box_pack_end   (GTK_BOX (box2), GTK_WIDGET (wbcg->edit_line.entry), 1, 1, 0);

	gtk_table_attach (GTK_TABLE (wbcg->table), box2,
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

	gtk_widget_show_all (box2);
}

static void
wbcg_create_status_area (WorkbookControlGUI *wbcg)
{
	WorkbookControlGUIClass *wbcg_class = WBCG_CLASS (wbcg);
	GtkWidget *tmp, *frame0, *frame1, *frame2;

	wbcg->progress_bar = gtk_progress_bar_new ();
	gtk_progress_bar_set_orientation (
		GTK_PROGRESS_BAR (wbcg->progress_bar), GTK_PROGRESS_LEFT_TO_RIGHT);
	frame0 = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame0), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (frame0), wbcg->progress_bar);

	wbcg->auto_expr_label = tmp = gtk_label_new ("");
	GTK_WIDGET_UNSET_FLAGS (tmp, GTK_CAN_FOCUS);
	gtk_widget_ensure_style (tmp);
	gtk_widget_set_size_request (tmp, gnm_measure_string (
					     gtk_widget_get_pango_context (GTK_WIDGET (wbcg->toplevel)),
					     tmp->style->font_desc,
					     "W") * 15, -1);
	tmp = gtk_event_box_new ();
	gtk_container_add (GTK_CONTAINER (tmp), wbcg->auto_expr_label);
	g_signal_connect (G_OBJECT (tmp),
		"button_press_event",
		G_CALLBACK (cb_select_auto_expr), wbcg);
	frame1 = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame1), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (frame1), tmp);

	wbcg->status_text = tmp = gtk_label_new ("");
	gtk_widget_ensure_style (tmp);
	gtk_widget_set_size_request (tmp, gnm_measure_string (
					     gtk_widget_get_pango_context (GTK_WIDGET (wbcg->toplevel)),
					     tmp->style->font_desc,
					     "W") * 15, -1);
	frame2 = gtk_frame_new (NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame2), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (frame2), tmp);

	wbcg_class->create_status_area (wbcg, frame0, frame2, frame1);
}

void
wbcg_set_toplevel (WorkbookControlGUI *wbcg, GtkWidget *w)
{
	static GtkTargetEntry const drag_types[] = {
		{ (char *) "text/uri-list", 0, TARGET_URI_LIST },
		{ (char *) "GNUMERIC_SHEET", 0, TARGET_SHEET }
	};

	g_return_if_fail (wbcg->toplevel == NULL);
	g_return_if_fail (GTK_IS_WINDOW (w));

	wbcg->toplevel = GTK_WINDOW (w);
	g_object_set (G_OBJECT (wbcg->toplevel),
		"allow_grow", TRUE,
		"allow_shrink", TRUE,
		NULL);

	g_signal_connect_data (w, "delete_event",
		G_CALLBACK (wbcg_close_control), wbcg, NULL,
		G_CONNECT_AFTER | G_CONNECT_SWAPPED);
	g_signal_connect_after (w, "set_focus",
		G_CALLBACK (cb_set_focus), wbcg);
	g_signal_connect (w, "scroll-event",
		G_CALLBACK (wbcg_scroll_wheel_support_cb), wbcg);
	g_signal_connect (w, "realize",
		G_CALLBACK (cb_realize), wbcg);

	/* Setup a test of Drag and Drop */
	gtk_drag_dest_set (GTK_WIDGET (w),
		GTK_DEST_DEFAULT_ALL, drag_types, G_N_ELEMENTS (drag_types),
		GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect (w, "drag_data_received",
		G_CALLBACK (cb_wbcg_drag_data_received), wbcg);
	g_signal_connect (w, "drag_motion",
		G_CALLBACK (cb_wbcg_drag_motion), wbcg);
	g_signal_connect (w, "drag_leave",
		G_CALLBACK (cb_wbcg_drag_leave), wbcg);
#if 0
	g_signal_connect (G_OBJECT (gcanvas),
		"drag_data_get",
		G_CALLBACK (wbcg_drag_data_get), WORKBOOK_CONTROL (wbc));
#endif

}

static int
wbcg_validation_msg (WorkbookControl *wbc, ValidationStyle v,
		     char const *title, char const *msg)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	ValidationStatus res0, res1 = VALIDATION_STATUS_VALID; /* supress warning */
	const char *btn0, *btn1;
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
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	return ((response == GTK_RESPONSE_NO) ? res1 : res0);
}

static void
wbcg_progress_set (GnmCmdContext *cc, gfloat val)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)cc;
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (wbcg->progress_bar), val);
}

static void
wbcg_progress_message_set (GnmCmdContext *cc, gchar const *msg)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)cc;
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (wbcg->progress_bar), msg);
}

/***************************************************************************/
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-series.h>
#include <goffice/graph/go-data.h>
#include "graph.h"

static void
wbcg_data_allocator_allocate (GogDataAllocator *dalloc, GogPlot *plot)
{
	SheetControlGUI *scg = wbcg_cur_scg (WORKBOOK_CONTROL_GUI (dalloc));
	sv_selection_to_plot (sc_view (SHEET_CONTROL (scg)), plot);
}

typedef struct {
	GnmExprEntry *entry;
	GogDataset *dataset;
	int dim_i;
	gboolean prefers_scalar;
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
	sheet = sc_sheet (SHEET_CONTROL (scg));

	/* If we are setting something */
	if (!gnm_expr_entry_is_blank (editor->entry)) {
		GnmParsePos pos;
		GnmParseError  perr;
		GnmExpr const *expr;

		parse_error_init (&perr);
		expr = gnm_expr_entry_parse (editor->entry,
			parse_pos_init_sheet (&pos, sheet),
			&perr, TRUE, GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS);

		/* TODO : add some error dialogs split out
		 * the code in workbok_edit to add parens.  */
		if (expr == NULL) {
			if (editor->prefers_scalar)
				expr = gnm_expr_new_constant (value_new_string (
					gnm_expr_entry_get_text	(editor->entry)));
			else {
				g_return_if_fail (perr.err != NULL);

				wb_control_validation_msg (WORKBOOK_CONTROL (scg_get_wbcg (scg)),
					VALIDATION_STYLE_PARSE_ERROR, NULL, perr.err->message);
				parse_error_free (&perr);
				return;
			}
		}

		data = (editor->prefers_scalar)
			? gnm_go_data_scalar_new_expr (sheet, expr)
			: gnm_go_data_vector_new_expr (sheet, expr);
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
			    GogDataset *dataset, int dim_i, gboolean prefers_scalar)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (dalloc);
	GraphDimEditor *editor;
	GOData *val;

	editor = g_new (GraphDimEditor, 1);
	editor->dataset		= dataset;
	editor->dim_i		= dim_i;
	editor->prefers_scalar	= prefers_scalar;
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
workbook_control_gui_class_init (GObjectClass *object_class)
{
	GnmCmdContextClass  *cc_class =
		GNM_CMD_CONTEXT_CLASS (object_class);
	WorkbookControlClass *wbc_class =
		WORKBOOK_CONTROL_CLASS (object_class);
	WorkbookControlGUIClass *wbcg_class =
		WORKBOOK_CONTROL_GUI_CLASS (object_class);

	g_return_if_fail (wbc_class != NULL);

	parent_class = g_type_class_peek_parent (object_class);
	object_class->finalize = wbcg_finalize;

	cc_class->get_password		= wbcg_get_password;
	cc_class->set_sensitive		= wbcg_set_sensitive;
	cc_class->error.error		= wbcg_error_error;
	cc_class->error.error_info	= wbcg_error_error_info;
	cc_class->progress_set		= wbcg_progress_set;
	cc_class->progress_message_set	= wbcg_progress_message_set;

	wbc_class->title_set		= wbcg_title_set;
	wbc_class->prefs_update		= wbcg_prefs_update;
	wbc_class->zoom_feedback	= wbcg_zoom_feedback;
	wbc_class->edit_line_set	= wbcg_edit_line_set;
	wbc_class->selection_descr_set	= wbcg_edit_selection_descr_set;
	wbc_class->edit_set_sensitive	= wbcg_edit_set_sensitive;
	wbc_class->auto_expr_value	= wbcg_auto_expr_value;

	wbc_class->sheet.add        = wbcg_sheet_add;
	wbc_class->sheet.remove	    = wbcg_sheet_remove;
	wbc_class->sheet.rename	    = wbcg_sheet_rename;
	wbc_class->sheet.focus	    = wbcg_sheet_focus;
	wbc_class->sheet.move	    = wbcg_sheet_move;
	wbc_class->sheet.remove_all = wbcg_sheet_remove_all;

	wbc_class->undo_redo.labels   = wbcg_undo_redo_labels;

	wbc_class->menu_state.update      = wbcg_menu_state_update;
	wbc_class->menu_state.sheet_prefs = wbcg_menu_state_sheet_prefs;
	wbc_class->menu_state.sheet_count = wbcg_menu_state_sheet_count;

	wbc_class->claim_selection	 = wbcg_claim_selection;
	wbc_class->paste_from_selection  = wbcg_paste_from_selection;
	wbc_class->validation_msg	 = wbcg_validation_msg;
	wbcg_class->set_transient        = wbcg_set_transient_for;

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

	/* Autosave */
	wbcg->autosave_timer = 0;
	wbcg->autosave_minutes = 0;
	wbcg->autosave_prompt = FALSE;

#warning why is this here ?
	wbcg->current_saver = NULL;
}

GSF_CLASS_FULL (WorkbookControlGUI, workbook_control_gui,
		workbook_control_gui_class_init, workbook_control_gui_init,
		WORKBOOK_CONTROL_TYPE, G_TYPE_FLAG_ABSTRACT,
		GSF_INTERFACE (wbcg_go_plot_data_allocator_init, GOG_DATA_ALLOCATOR_TYPE))

static void
wbcg_create (WorkbookControlGUI *wbcg,
	     WorkbookView *optional_view,
	     Workbook *optional_wb,
	     GdkScreen *optional_screen)
{
	Sheet *sheet;
	WorkbookView *wbv;
	WorkbookControl *wbc;

	wbcg_create_edit_area (wbcg);
	wbcg_create_status_area (wbcg);

	wbcg_reload_recent_file_menu (wbcg);
	g_signal_connect_object (gnm_app_get_app (),
		"notify::file-history-list",
		G_CALLBACK (wbcg_reload_recent_file_menu), wbcg, G_CONNECT_SWAPPED);

	wbc = (WorkbookControl *)wbcg;
	wb_control_set_view (wbc, optional_view, optional_wb);
	wbv = wb_control_view (wbc);
	sheet = wbv->current_sheet;
	if (sheet != NULL) {
		wb_control_menu_state_update (wbc, MS_ALL);
		wb_control_menu_state_sheet_prefs (wbc, sheet);
		wb_control_style_feedback (wbc, NULL);
		wb_control_zoom_feedback (wbc);
	}

	if (optional_screen)
		gtk_window_set_screen (wbcg->toplevel, optional_screen);
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

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
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "workbook-control-gui-priv.h"

#include "application.h"
#include "workbook-object-toolbar.h"
#include "workbook-format-toolbar.h"
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
#include "widgets/gnumeric-combo-text.h"
#include "widgets/preview-file-selection.h"
#include "src/plugin-util.h"
#include "sheet-object-image.h"
#include "gnumeric-gconf.h"
#include "filter.h"
#include "io-context.h"
#include "stf.h"
#include "rendered-value.h"
#include "sort.h"

#ifdef WITH_BONOBO
#include <bonobo/bonobo-ui-component.h>
#include "sheet-object-container.h"
#endif

#include <gsf/gsf-impl-utils.h>
#include <widgets/widget-color-combo.h>
#include <widgets/gnm-combo-stack.h>

#include <libgnomeui/gnome-app-helper.h>
#include <libgnomeui/gnome-stock-icons.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkseparatormenuitem.h>
#include <gtk/gtkcheckmenuitem.h>
#include <gtk/gtkimage.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkframe.h>
#include <gtk/gtkdnd.h>

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

struct _CustomXmlUI {
	char *xml_ui;
	char *textdomain;
	GSList *verb_list;
	GCallback /*BonoboUIVerbFn*/ verb_fn;
	gpointer verb_fn_data;
};

static GSList *registered_xml_uis = NULL;

enum {
	TARGET_URI_LIST,
	TARGET_SHEET
};

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

WBCG_VIRTUAL (set_transient,
	(WorkbookControlGUI *wbcg, GtkWindow *window), (wbcg, window))

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

static WorkbookControl *
wbcg_control_new (G_GNUC_UNUSED WorkbookControl *wbc,
		  WorkbookView *wbv,
		  Workbook *wb,
		  void *extra)
{
	return workbook_control_gui_new (wbv, wb,
					 extra ? GDK_SCREEN (extra) : NULL);
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
wbcg_format_feedback (WorkbookControl *wbc)
{
	workbook_feedback_set ((WorkbookControlGUI *)wbc);
}

static void
zoom_changed (WorkbookControlGUI *wbcg, Sheet* sheet)
{
	g_return_if_fail (IS_SHEET (sheet));
	g_return_if_fail (wbcg->zoom_entry != NULL);

	if (wbcg_ui_update_begin (wbcg)) {
		int pct = sheet->last_zoom_factor_used * 100 + .5;
		char *buffer = g_strdup_printf ("%d%%", pct);
		gnm_combo_text_set_text (GNM_COMBO_TEXT (wbcg->zoom_entry),
			buffer, GNM_COMBO_TEXT_CURRENT);
		wbcg_ui_update_end (wbcg);
		g_free (buffer);
	}

	scg_object_update_bbox (wbcg_cur_scg (wbcg),
				NULL, NULL);
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

static void
wbcg_menu_state_sensitivity (WorkbookControlGUI *wbcg, gboolean sensitive)
{
#ifdef WITH_BONOBO
	CORBA_Environment ev;
#endif

	/* Don't disable/enable again (prevent toolbar flickering) */
	if (wbcg->toolbar_is_sensitive == sensitive)
		return;
	wbcg->toolbar_is_sensitive = sensitive;

#ifdef WITH_BONOBO
	CORBA_exception_init (&ev);
	bonobo_ui_component_set_prop (wbcg->uic, "/commands/MenuBar",
				      "sensitive", sensitive ? "1" : "0", &ev);
	bonobo_ui_component_set_prop (wbcg->uic, "/commands/StandardToolbar",
				      "sensitive", sensitive ? "1" : "0", &ev);
	bonobo_ui_component_set_prop (wbcg->uic, "/commands/FormatToolbar",
				      "sensitive", sensitive ? "1" : "0", &ev);
	bonobo_ui_component_set_prop (wbcg->uic, "/commands/ObjectToolbar",
				      "sensitive", sensitive ? "1" : "0", &ev);
	CORBA_exception_free (&ev);
	/* TODO : Ugly hack to work around strange bonobo semantics for
	 * sensitivity of containers.  Bonono likes to recursively set the state
	 * rather than just setting the container.
	 */
	if (sensitive) {
		Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
		if (wb->undo_commands == NULL)
			gtk_widget_set_sensitive (wbcg->undo_combo, FALSE);
		if (wb->redo_commands == NULL)
			gtk_widget_set_sensitive (wbcg->redo_combo, FALSE);
	}
#else
	gtk_widget_set_sensitive (GNOME_APP (wbcg->toplevel)->menubar, sensitive);
	gtk_widget_set_sensitive (wbcg->standard_toolbar, sensitive);
	gtk_widget_set_sensitive (wbcg->format_toolbar, sensitive);
	gtk_widget_set_sensitive (wbcg->object_toolbar, sensitive);
#endif

}

static gboolean
cb_thaw_ui_toolbar (gpointer *data)
{
        WorkbookControlGUI *wbcg = (WorkbookControlGUI *)data;

	g_return_val_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg), FALSE);

	wbcg_menu_state_sensitivity (wbcg, TRUE);
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
		wbcg_menu_state_sensitivity (wbcg, func_guru_flag);
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


static void
cb_insert_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	GList *workbooks = workbook_sheets (wb);
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));

	insert_sheet_at (wbcg, g_list_index (workbooks, sheet));

	g_list_free (workbooks);
}

static void
cb_append_sheet (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	cmd_reorganize_sheets (WORKBOOK_CONTROL (wbcg), NULL,
			       g_slist_prepend (NULL, GINT_TO_POINTER (-1)),
			       g_slist_prepend (NULL, NULL),
			       NULL, NULL, NULL, NULL, NULL, NULL);
}

static void
sheet_action_add_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	cb_append_sheet (NULL, scg->wbcg);
}

static void
sheet_action_insert_sheet (GtkWidget *widget, SheetControlGUI *scg)
{
	cb_insert_sheet (NULL, scg->wbcg);
}

static void
delete_sheet_if_possible (GtkWidget *ignored, SheetControlGUI *scg)
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
		{ N_("Remove"), &delete_sheet_if_possible, SHEET_CONTEXT_TEST_SIZE },
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
		zoom_changed (wbcg, sheet);
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

/* Command callback called on activation of a file history menu item. */
#ifndef WITH_BONOBO

#define UGLY_GNOME_UI_KEY "HistoryFilename"

static void
file_history_cmd (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	char *filename = g_object_get_data (G_OBJECT (widget), UGLY_GNOME_UI_KEY);
	gui_file_read (wbcg, filename, NULL, NULL);
}

static void
wbcg_load_file_history (WorkbookControlGUI *wbcg)
{
	/*
	 * xgettext:
	 * This string must translate to exactly the same strings as the
	 * 'Preferences...' item in the
	 * 'File' menu
	 */
	char const *seperator_path = _("_File/Preferen_ces...");
	GtkWidget  *sep, *w;
	int sep_pos, accel_number = 1;
	GSList const *ptr = gnm_app_history_get_list (FALSE);
	unsigned new_history_size = g_slist_length ((GSList *)ptr);
	GnomeUIInfo info[] = {
		{ GNOME_APP_UI_ITEM, NULL, NULL, file_history_cmd, NULL },
		GNOMEUIINFO_END
	};

	sep = gnome_app_find_menu_pos (GNOME_APP (wbcg->toplevel)->menubar,
		seperator_path, &sep_pos);
	if (sep == NULL) {
		g_warning ("Probable mis-translation. '%s' : was not found. "
			   "Does this match the '_File/Preferen_ces...' menu exactly ?",
			   seperator_path);
		return;
	}

	/* remove the old items including the seperator */
	if (wbcg->file_history_size > 0) {
		char *label = history_item_label ((gchar *)ptr->data, 1);
		char *path = g_strconcat (_("File/"), label, NULL);
		gnome_app_remove_menu_range (GNOME_APP (wbcg->toplevel),
			seperator_path, 1, wbcg->file_history_size + 1);
		g_free (path);
		g_free (label);
	}

	/* add seperator */
	if (new_history_size > 0) {
		w = gtk_menu_item_new ();
		gtk_menu_shell_insert (GTK_MENU_SHELL (sep), w, sep_pos++);
		gtk_widget_set_sensitive (w, FALSE);
		gtk_widget_show (w);
	}

	for (accel_number = 1; ptr != NULL ; ptr = ptr->next, accel_number++) {
		char *label = history_item_label (ptr->data, accel_number);
		info [0].hint = ptr->data;;
		info [0].label = label;
		info [0].user_data = wbcg;

		gnome_app_fill_menu (GTK_MENU_SHELL (sep), info,
			GNOME_APP (wbcg->toplevel)->accel_group, TRUE,
			sep_pos++);
		gnome_app_install_menu_hints (GNOME_APP (wbcg->toplevel), info);
		g_object_set_data (G_OBJECT (info[0].widget),
			UGLY_GNOME_UI_KEY, ptr->data);
		g_free (label);
	}

	wbcg->file_history_size = new_history_size;
}
#else

static void
file_history_cmd (BonoboUIComponent *uic, WorkbookControlGUI *wbcg, const char *path)
{
	char *fullpath = g_strconcat ("/menu/File/FileHistory/", path, NULL);
	char *filename = bonobo_ui_component_get_prop (wbcg->uic, fullpath,
						       "tip", NULL);

	gui_file_read (wbcg, filename, NULL, NULL);
	g_free (filename);
	g_free (fullpath);
}

static void
wbcg_load_file_history (WorkbookControlGUI *wbcg)
{
	GSList const *ptr = gnm_app_history_get_list (FALSE);
	unsigned new_history_size = g_slist_length ((GSList *)ptr);
	unsigned i, accel_number = 1;
	CORBA_Environment ev;

	CORBA_exception_init (&ev);
	bonobo_ui_component_freeze (wbcg->uic, &ev);

	/* first remove the items, then re-insert */
	for (i = 0 ; i < wbcg->file_history_size  ; i++) {
		char *tmp = g_strdup_printf (
			"/menu/File/FileHistory/FileHistory%d", accel_number++);
		bonobo_ui_component_rm (wbcg->uic, tmp, &ev);
		g_free (tmp);

		tmp = g_strdup_printf ("FileHistory%d", accel_number);
		bonobo_ui_component_remove_verb (wbcg->uic, tmp);
		g_free (tmp);
	}

	/* Add a new menu item for each item in the history list */
	for (accel_number = 1; ptr != NULL ; ptr = ptr->next, accel_number++) {
		char *id, *str, *label, *filename;

		id = g_strdup_printf ("FileHistory%d", accel_number);
		str = history_item_label (ptr->data, accel_number);
		label = bonobo_ui_util_encode_str (str);
		g_free (str);

		filename = bonobo_ui_util_encode_str ((char const *) ptr->data);
		str = g_strdup_printf ("<menuitem name=\"%s\" "
				       "verb=\"%s\" "
				       "label=\"%s\" "
				       "tip=\"%s\"/>\n",
				       id, id, label, filename);
		bonobo_ui_component_set (wbcg->uic,
					 "/menu/File/FileHistory", str, &ev);
		bonobo_ui_component_add_verb (
			wbcg->uic, id, (BonoboUIVerbFn) file_history_cmd, wbcg);
		g_free (id);
		g_free (str);
		g_free (filename);
		g_free (label);
	}

	bonobo_ui_component_thaw (wbcg->uic, &ev);
	CORBA_exception_free (&ev);
	wbcg->file_history_size = new_history_size;
}
#endif


static void
wbcg_file_history_setup (WorkbookControlGUI *wbcg)
{
	wbcg_load_file_history (wbcg);
	g_signal_connect_object (gnm_app_get_app (),
		"notify::file-history-list",
		G_CALLBACK (wbcg_load_file_history), wbcg, G_CONNECT_SWAPPED);
}

static gboolean
cb_change_zoom (GtkWidget *caller, char *new_zoom, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	int factor;
	char *end;

	if (sheet == NULL || wbcg->updating_ui)
		return TRUE;

	errno = 0; /* strtol sets errno, but does not clear it.  */
	factor = strtol (new_zoom, &end, 10);
	if (new_zoom != end && errno != ERANGE && factor == (gnm_float)factor)
		/* The GSList of sheet passed to cmd_zoom will be freed by cmd_zoom,
		 * and the sheet will force an update of the zoom combo to keep the
		 * display consistent
		 */
		cmd_zoom (WORKBOOK_CONTROL (wbcg), g_slist_append (NULL, sheet),
			  (double) factor / 100);
	else
		zoom_changed (wbcg, sheet);

	wbcg_focus_cur_scg (wbcg);

	/* because we are updating it there is no need to apply it now */
	return FALSE;
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

static GnmComboStack *
ur_stack (WorkbookControl *wbc, gboolean is_undo)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	return GNM_COMBO_STACK (is_undo ? wbcg->undo_combo : wbcg->redo_combo);
}

static void
wbcg_undo_redo_clear (WorkbookControl *wbc, gboolean is_undo)
{
	gnm_combo_stack_clear (ur_stack (wbc, is_undo));
}

static void
wbcg_undo_redo_truncate (WorkbookControl *wbc, int n, gboolean is_undo)
{
	gnm_combo_stack_truncate (ur_stack (wbc, is_undo), n);
}

static void
wbcg_undo_redo_pop (WorkbookControl *wbc, gboolean is_undo)
{
	gnm_combo_stack_remove_top (ur_stack (wbc, is_undo), 1);
}

static void
wbcg_undo_redo_push (WorkbookControl *wbc, char const *text, gboolean is_undo)
{
	gnm_combo_stack_push_item (ur_stack (wbc, is_undo), text);
}

#ifndef WITH_BONOBO
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
change_menu_label (WorkbookControlGUI *wbcg, GtkWidget *menu_item,
		   char const *prefix, char const *suffix, char const *new_tip)
{
	GtkBin   *bin = GTK_BIN (menu_item);
	GtkLabel *label = GTK_LABEL (bin->child);

	g_return_if_fail (label != NULL);

	if (prefix == NULL) {
		gtk_label_set_text (label, suffix);
		gtk_label_set_use_underline (label, TRUE);
	} else {
		gchar    *text;
		gboolean  sensitive = TRUE;

		if (suffix == NULL) {
			suffix = _("Nothing");
			sensitive = FALSE;
		}

		text = g_strdup_printf ("%s : %s", prefix, suffix);

		gtk_label_set_text (label, text);
		gtk_label_set_use_underline (label, TRUE);
		g_free (text);

		gtk_widget_set_sensitive (menu_item, sensitive);
	}

	if (new_tip != NULL) {
		/* MASSIVE HACK
		 * libgnomeui adds the signal handlers every time we call
		 * gnome_app_install_menu_hints.  Which builds up a rather
		 * large signal queue.  So cheat andjsut tweak the underlying
		 * data structure.  This code is going to get burned out as
		 * soon as we move to egg menu */
		g_object_set_data (G_OBJECT (menu_item),
			"apphelper_statusbar_hint", (gpointer)(L_(new_tip)));
	}
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
		   char const *prefix,
		   char const *suffix,
		   char const *new_tip)
{
	gboolean  sensitive = TRUE;
	gchar    *text;
	CORBA_Environment  ev;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	CORBA_exception_init (&ev);

	if (prefix == NULL) {
		bonobo_ui_component_set_prop (wbcg->uic, verb_path, "label",
					      suffix, &ev);
	} else {
		if (suffix == NULL) {
			suffix = _("Nothing");
			sensitive = FALSE;
		}

		text = g_strdup_printf ("%s : %s", prefix, suffix);
		bonobo_ui_component_set_prop (wbcg->uic, verb_path,
					      "label", text, &ev);
		g_free (text);

		bonobo_ui_component_set_prop (wbcg->uic, verb_path,
					      "sensitive", sensitive ? "1" : "0", &ev);
	}
	if (new_tip)
		bonobo_ui_component_set_prop (wbcg->uic, verb_path,
					      "tip", new_tip, &ev);
	CORBA_exception_free (&ev);
}
#endif

static void
wbcg_menu_state_update (WorkbookControl *wbc, int flags)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	SheetView const *sv  = wb_control_cur_sheet_view (wbc);
	Sheet const *sheet = wb_control_cur_sheet (wbc);
	gboolean has_filtered_rows;
	gboolean edit_object = scg != NULL &&
		(scg->current_object != NULL || scg->new_object != NULL);

	/* we are being finalized ignore this */
	if (G_OBJECT (wbc)->ref_count == 0)
		return;

	has_filtered_rows = sheet->has_filtered_rows;
	if (!has_filtered_rows) {
		GSList *ptr = sheet->filters;
		for (;ptr != NULL ; ptr = ptr->next)
			if (((GnmFilter *)ptr->data)->is_active) {
				has_filtered_rows = TRUE;
				break;
			}
	}

#ifndef WITH_BONOBO
	if (MS_INSERT_COLS & flags)
		change_menu_sensitivity (wbcg->menu_item_insert_cols,
					 sv->enable_insert_cols);
	if (MS_INSERT_ROWS & flags)
		change_menu_sensitivity (wbcg->menu_item_insert_rows,
					 sv->enable_insert_rows);
	if (MS_INSERT_CELLS & flags)
		change_menu_sensitivity (wbcg->menu_item_insert_cells,
					 sv->enable_insert_cells);
	if (MS_SHOWHIDE_DETAIL & flags) {
		change_menu_sensitivity (wbcg->menu_item_show_detail,
					 sheet->priv->enable_showhide_detail);
		change_menu_sensitivity (wbcg->menu_item_hide_detail,
					 sheet->priv->enable_showhide_detail);
	}
	if (MS_CLIPBOARD & flags) {
		change_menu_sensitivity (wbcg->tool_item_cut, !edit_object);
		change_menu_sensitivity (wbcg->tool_item_copy, !edit_object);
		change_menu_sensitivity (wbcg->tool_item_paste, !edit_object);
		change_menu_sensitivity (wbcg->menu_item_cut, !edit_object);
		change_menu_sensitivity (wbcg->menu_item_copy, !edit_object);
		change_menu_sensitivity (wbcg->menu_item_paste, !edit_object);
	}

	if (MS_PASTE_SPECIAL & flags)
		change_menu_sensitivity (wbcg->menu_item_paste_special,
			!gnm_app_clipboard_is_empty () &&
			!gnm_app_clipboard_is_cut () &&
			!edit_object);
	if (MS_PRINT_SETUP & flags)
		change_menu_sensitivity (wbcg->menu_item_page_setup,
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
	if (MS_CONSOLIDATE & flags)
		change_menu_sensitivity (wbcg->menu_item_filter_show_all,
					 has_filtered_rows);
#else
	if (MS_INSERT_COLS & flags)
		change_menu_sensitivity (wbcg, "/commands/InsertColumns",
					 sv->enable_insert_cols);
	if (MS_INSERT_ROWS & flags)
		change_menu_sensitivity (wbcg, "/commands/InsertRows",
					 sv->enable_insert_rows);
	if (MS_INSERT_CELLS & flags)
		change_menu_sensitivity (wbcg, "/commands/InsertCells",
					 sv->enable_insert_cells);
	if (MS_SHOWHIDE_DETAIL & flags) {
		change_menu_sensitivity (wbcg, "/commands/DataOutlineShowDetail",
					 sheet->priv->enable_showhide_detail);
		change_menu_sensitivity (wbcg, "/commands/DataOutlineHideDetail",
					 sheet->priv->enable_showhide_detail);
	}
	if (MS_CLIPBOARD & flags) {
		change_menu_sensitivity (wbcg, "/commands/EditCut", !edit_object);
		change_menu_sensitivity (wbcg, "/commands/EditCopy", !edit_object);
		change_menu_sensitivity (wbcg, "/commands/EditPaste", !edit_object);
	}
	if (MS_PASTE_SPECIAL & flags)
		change_menu_sensitivity (wbcg, "/commands/EditPasteSpecial",
			!gnm_app_clipboard_is_empty () &&
			!gnm_app_clipboard_is_cut () &&
			!edit_object);
	if (MS_PRINT_SETUP & flags)
		change_menu_sensitivity (wbcg, "/commands/FilePageSetup",
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
	if (MS_CONSOLIDATE & flags)
		change_menu_sensitivity (wbcg, "/commands/DataFilterShowAll",
					 has_filtered_rows);
#endif

	if (MS_FREEZE_VS_THAW & flags) {
		/* Cheat and use the same accelerator for both states because
		 * we don't reset it when the label changes */
		char const* label = sv_is_frozen (sv)
			? _("Un_freeze Panes")
			: _("_Freeze Panes");
		char const *new_tip = sv_is_frozen (sv)
			? _("Unfreeze the top left of the sheet")
			: _("Freeze the top left of the sheet");

#ifndef WITH_BONOBO
		change_menu_label (wbcg, wbcg->menu_item_freeze_panes,
				   NULL, label, new_tip);
#else
		change_menu_label (wbcg, "/commands/ViewFreezeThawPanes",
		                   NULL, label, new_tip);
#endif
	}

	if (MS_ADD_VS_REMOVE_FILTER & flags) {
		gboolean const has_filter = (sv_first_selection_in_filter (sv) != NULL);
		char const* label = has_filter
			? _("Remove _Auto Filter")
			: _("Add _Auto Filter");
		char const *new_tip = has_filter
			? _("Remove a filter")
			: _("Add a filter");

#ifndef WITH_BONOBO
		change_menu_label (wbcg, wbcg->menu_item_auto_filter,
				   NULL, label, new_tip);
#else
		change_menu_label (wbcg, "/commands/DataAutoFilter",
		                   NULL, label, new_tip);
#endif
	}
}

static void
wbcg_undo_redo_labels (WorkbookControl *wbc, char const *undo, char const *redo)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;
	g_return_if_fail (wbcg != NULL);

#ifndef WITH_BONOBO
	change_menu_label (wbcg, wbcg->menu_item_undo, _("Undo"), undo, NULL);
	change_menu_label (wbcg, wbcg->menu_item_redo, _("Redo"), redo, NULL);
#else
	change_menu_label (wbcg, "/commands/EditUndo", _("_Undo"), undo, NULL);
	change_menu_label (wbcg, "/commands/EditRedo", _("_Redo"), redo, NULL);
#endif
}

static void
wbcg_menu_state_sheet_prefs (WorkbookControl *wbc, Sheet const *sheet)
{
 	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)wbc;

	if (!wbcg_ui_update_begin (wbcg))
		return;

#ifndef WITH_BONOBO
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

#ifndef WITH_BONOBO
	change_menu_sensitivity (wbcg->menu_item_sheet_remove, multi_sheet);
#else
	change_menu_sensitivity (wbcg, "/commands/SheetRemove", multi_sheet);
#endif
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
wbcg_progress_set (GnmCmdContext *cc, gfloat val)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (cc);

#ifdef WITH_BONOBO
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (wbcg->progress_bar),
				       val);
#else
	gnome_appbar_set_progress_percentage (wbcg->appbar, val);
#endif
}

static void
wbcg_progress_message_set (GnmCmdContext *cc, gchar const *msg)
{
	WorkbookControlGUI *wbcg = WORKBOOK_CONTROL_GUI (cc);
	GtkProgressBar *progress;

#ifdef WITH_BONOBO
	progress = GTK_PROGRESS_BAR (wbcg->progress_bar);
#else
	progress = gnome_appbar_get_progress (wbcg->appbar);
#endif
	gtk_progress_bar_set_text (progress, msg);
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

static char const * const preset_zoom [] = {
	"200%",
	"150%",
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
 * Returns :
 * 0) canceled
 * 1) closed
 * 2) pristine can close
 * 3) save any future dirty
 * 4) do not save any future dirty
 */
static int
workbook_close_if_user_permits (WorkbookControlGUI *wbcg,
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
 * Returns TRUE if the control should not be closed.
 */
static gboolean
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
	 * eg progress meter for a new book
	 */
	if (G_OBJECT (wbcg)->ref_count > 1)
		return TRUE;

	/* This is the last control */
	if (wb_view->wb_controls->len <= 1) {
		Workbook *wb = wb_view_workbook (wb_view);

		g_return_val_if_fail (IS_WORKBOOK (wb), TRUE);
		g_return_val_if_fail (wb->wb_views != NULL, TRUE);

		/* This is the last view */
		if (wb->wb_views->len <= 1)
			return workbook_close_if_user_permits (wbcg, wb_view, TRUE, FALSE, TRUE)
				== 0;

		g_object_unref (G_OBJECT (wb_view));
	} else
		g_object_unref (G_OBJECT (wbcg));

	return FALSE;
}

/****************************************************************************/

static void
cb_file_new (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	Workbook *wb = workbook_new_with_sheets
		(gnm_app_prefs->initial_sheet_number);
	
	(void) workbook_control_gui_new (NULL, wb, NULL);
}

static void
cb_file_open (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gui_file_open (wbcg, NULL);
	wbcg_focus_cur_scg (wbcg); /* force focus back to sheet */
}

static void
cb_file_save (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gui_file_save (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
	wbcg_focus_cur_scg (wbcg); /* force focus back to sheet */
}

static void
cb_file_save_as (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gui_file_save_as (wbcg, wb_control_view (WORKBOOK_CONTROL (wbcg)));
	wbcg_focus_cur_scg (wbcg); /* force focus back to sheet */
}
static void
cb_file_sendto (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	wb_view_sendto (wb_control_view (WORKBOOK_CONTROL (wbcg)),
		GNM_CMD_CONTEXT (wbcg));
}

static void
cb_file_page_setup (GtkWidget *widget, WorkbookControlGUI *wbcg)
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
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	sheet_print (wbcg, sheet, TRUE, PRINT_ACTIVE_SHEET);
}

static void
cb_file_summary (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_summary_update (wbcg, TRUE);
}

static void
cb_file_preferences (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_preferences (wbcg, 0);
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
	GList *ptr, *workbooks, *clean_no_closed = NULL;
	gboolean ok = TRUE;
	gboolean ask_user = TRUE;
	gboolean discard_all = FALSE;

	/* If we are still loading initial files, short circuit */
	if (!initial_workbook_open_complete) {
		initial_workbook_open_complete = TRUE;
		return;
	}

	/* If we were editing when the quit request came in abort the edit. */
	wbcg_edit_finish (wbcg, FALSE, NULL);

	/* list is modified during workbook destruction */
	workbooks = g_list_copy (gnm_app_workbook_list ());

	for (ptr = workbooks; ok && ptr != NULL ; ptr = ptr->next) {
		Workbook *wb = ptr->data;
		WorkbookView *wb_view;
		GList *old_ptr;

		g_return_if_fail (IS_WORKBOOK (wb));
		g_return_if_fail (wb->wb_views != NULL);

		if (wb_control_workbook (wbc) == wb)
			continue;
		if (discard_all) {

		} else {
			wb_view = g_ptr_array_index (wb->wb_views, 0);
			switch (workbook_close_if_user_permits (wbcg, wb_view, FALSE,
								TRUE, ask_user)) {
			case 0 : ok = FALSE;	/* canceled */
				break;
			case 1 :		/* closed */
				break;
			case 2 : clean_no_closed = g_list_prepend (clean_no_closed, wb);
				break;
			case 3 :		/* save_all */
				ask_user = FALSE;
				break;
			case 4 :		/* discard_all */
				discard_all = TRUE;
				old_ptr = ptr;
				for (ptr = ptr->next; ptr != NULL ; ptr = ptr->next) {
					Workbook *wba = ptr->data;
					old_ptr = ptr;
					if (wb_control_workbook (wbc) == wba)
						continue;
					workbook_set_dirty (wba, FALSE);
					workbook_unref (wba);
				}
				ptr = old_ptr;
				break;
			};
		}
	}

	if (discard_all) {
		workbook_set_dirty (wb_control_workbook (wbc), FALSE);
		workbook_unref (wb_control_workbook (wbc));
		for (ptr = clean_no_closed; ptr != NULL ; ptr = ptr->next)
			workbook_unref (ptr->data);
	} else
	/* only close pristine books if nothing was canceled. */
	if (ok && workbook_close_if_user_permits (wbcg,
						  wb_control_view (wbc), TRUE, TRUE, ask_user)
	    > 0)
		for (ptr = clean_no_closed; ptr != NULL ; ptr = ptr->next)
			workbook_unref (ptr->data);

	g_list_free (workbooks);
	g_list_free (clean_no_closed);
}

/****************************************************************************/

static void
cb_edit_clear_all (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_selection_clear (WORKBOOK_CONTROL (wbcg),
		CLEAR_VALUES | CLEAR_FORMATS | CLEAR_COMMENTS);
}

static void
cb_edit_clear_formats (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_selection_clear (WORKBOOK_CONTROL (wbcg),
		CLEAR_FORMATS);
}

static void
cb_edit_clear_comments (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_selection_clear (WORKBOOK_CONTROL (wbcg),
		CLEAR_COMMENTS);
}

static void
cb_edit_clear_content (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	cmd_selection_clear (WORKBOOK_CONTROL (wbcg),
		CLEAR_VALUES);
}

static void
cb_edit_select_all (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	scg_select_all (wbcg_cur_scg (wbcg));
}
static void
cb_edit_select_row (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	sv_select_cur_row (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_col (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	sv_select_cur_col (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_array (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	sv_select_cur_array (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_depends (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	sv_select_cur_depends (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}
static void
cb_edit_select_inputs (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	sv_select_cur_inputs (wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg)));
}

static void
cb_edit_undo (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wbcg_edit_finish (wbcg, FALSE, NULL);
	command_undo (wbc);
}

static void
cb_undo_combo (GtkWidget *widget, gint num, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	int i;

	wbcg_edit_finish (wbcg, FALSE, NULL);
	for (i = 0; i < num; i++)
		command_undo (wbc);
}

static void
cb_edit_redo (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	wbcg_edit_finish (wbcg, FALSE, NULL);
	command_redo (wbc);
}

static void
cb_redo_combo (GtkWidget *widget, gint num, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	int i;

	wbcg_edit_finish (wbcg, FALSE, NULL);
	for (i = 0; i < num; i++)
		command_redo (wbc);
}

static void
cb_edit_cut (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	sv_selection_cut (sv, wbc);
}

static void
cb_edit_copy (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	sv_selection_copy (sv, wbc);
}

static void
cb_edit_paste (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	cmd_paste_to_selection (wbc, sv, PASTE_DEFAULT);
}

static void
cb_edit_paste_special (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_paste_special (wbcg);
}

static void
cb_edit_delete (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_delete_cells (wbcg);
}

static void
cb_sheet_remove (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetControlGUI *res;

	if (wbcg_sheet_to_page_index (wbcg, wb_control_cur_sheet (wbc), &res)
	    >= 0)
		delete_sheet_if_possible (NULL, res);
}

static void
cb_edit_duplicate_sheet (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *old_sheet = wb_control_cur_sheet (wbc);

	cmd_clone_sheet (wbc, old_sheet);
}


static void
common_cell_goto (WorkbookControlGUI *wbcg, Sheet *sheet, GnmCellPos const *pos)
{
	SheetView *sv = sheet_get_view (sheet,
		wb_control_view (WORKBOOK_CONTROL (wbcg)));

	wb_view_sheet_focus (sv_wbv (sv), sheet);
	sv_selection_set (sv, pos,
		pos->col, pos->row,
		pos->col, pos->row);
	sv_make_cell_visible (sv, pos->col, pos->row, FALSE);
}

static int
cb_edit_search_replace_query (SearchReplaceQuery q, GnmSearchReplace *sr, ...)
{
	int res = 0;
	va_list pvar;
	WorkbookControlGUI *wbcg = sr->user_data;

	va_start (pvar, sr);

	switch (q) {
	case SRQ_fail: {
		GnmCell *cell = va_arg (pvar, GnmCell *);
		char const *old_text = va_arg (pvar, const char *);
		char const *new_text = va_arg (pvar, const char *);

		char *err;

		err = g_strdup_printf
			(_("In cell %s, the current contents\n"
			   "        %s\n"
			   "would have been replaced by\n"
			   "        %s\n"
			   "which is invalid.\n\n"
			   "The replace has been aborted "
			   "and nothing has been changed."),
			 cellpos_as_string (&cell->pos),
			 old_text,
			 new_text);

		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR, err);
		g_free (err);
		break;
	}

	case SRQ_query: {
		GnmCell *cell = va_arg (pvar, GnmCell *);
		char const *old_text = va_arg (pvar, const char *);
		char const *new_text = va_arg (pvar, const char *);
		Sheet *sheet = cell->base.sheet;
		char *pos_name = g_strconcat (sheet->name_unquoted, "!",
					      cell_name (cell), NULL);

		common_cell_goto (wbcg, sheet, &cell->pos);

		res = dialog_search_replace_query (wbcg, sr, pos_name,
						   old_text, new_text);
		g_free (pos_name);
		break;
	}

	case SRQ_querycommment: {
		Sheet *sheet = va_arg (pvar, Sheet *);
		GnmCellPos *cp = va_arg (pvar, GnmCellPos *);
		const char *old_text = va_arg (pvar, const char *);
		const char *new_text = va_arg (pvar, const char *);
		char *pos_name = g_strdup_printf (_("Comment in cell %s!%s"),
						  sheet->name_unquoted,
						  cellpos_as_string (cp));
		common_cell_goto (wbcg, sheet, cp);

		res = dialog_search_replace_query (wbcg, sr, pos_name,
						   old_text, new_text);
		g_free (pos_name);
		break;
	}

	}

	va_end (pvar);
	return res;
}

static gboolean
cb_edit_search_replace_action (WorkbookControlGUI *wbcg,
			       GnmSearchReplace *sr)
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
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_search_replace (wbcg, cb_edit_search_replace_action);
}


static void
cb_edit_search (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_search (wbcg);
}

static void
cb_edit_fill_autofill (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	Sheet	  *sheet = wb_control_cur_sheet (wbc);

	GnmRange const *total = selection_first_range (sv, GNM_CMD_CONTEXT (wbc), _("Autofill"));
	if (total) {
		GnmRange src = *total;
		gboolean do_loop;
		GSList *merges, *ptr;

		/* This could be more efficient, but it is not important */
		if (range_trim (sheet, &src, TRUE) ||
		    range_trim (sheet, &src, FALSE))
			return; /* Region totally empty */

		/* trim is a bit overzealous, it forgets about merges */
		do {
			do_loop = FALSE;
			merges = sheet_merge_get_overlap (sheet, &src);
			for (ptr = merges ; ptr != NULL ; ptr = ptr->next) {
				GnmRange const *r = ptr->data;
				if (src.end.col < r->end.col) {
					src.end.col = r->end.col;
					do_loop = TRUE;
				}
				if (src.end.row < r->end.row) {
					src.end.row = r->end.row;
					do_loop = TRUE;
				}
			}
		} while (do_loop);

		/* Make it autofill in only one direction */
 		if ((total->end.col - src.end.col) >=
		    (total->end.row - src.end.row))
 			src.end.row = total->end.row;
 		else
 			src.end.col = total->end.col;

 		cmd_autofill (wbc, sheet, FALSE,
			      total->start.col, total->start.row,
			      src.end.col - total->start.col + 1,
			      src.end.row - total->start.row + 1,
			      total->end.col, total->end.row,
			      FALSE);
 	}
}

static void
cb_edit_fill_series (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_fill_series (wbcg);
}

static void
cb_edit_goto (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_goto_cell (wbcg);
}

static void
cb_edit_recalc (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	/* TODO :
	 * f9  -  do any necessary calculations across all sheets
	 * shift-f9  -  do any necessary calcs on current sheet only
	 * ctrl-alt-f9 -  force a full recalc across all sheets
	 * ctrl-alt-shift-f9  -  a full-monty super recalc
	 */
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
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);

	scg_mode_edit (SHEET_CONTROL (scg));
	if (scg->active_panes == 1) {
		gboolean center = FALSE;
		GnmCanvas const *gcanvas = scg_pane (scg, 0);
		GnmCellPos frozen_tl, unfrozen_tl;

		frozen_tl = gcanvas->first;
		unfrozen_tl = sv->edit_pos;

		/* If edit pos is out of visible range */
		if (unfrozen_tl.col < gcanvas->first.col ||
		    unfrozen_tl.col > gcanvas->last_visible.col ||
		    unfrozen_tl.row < gcanvas->first.row ||
		    unfrozen_tl.row > gcanvas->last_visible.row)
			center = TRUE;

		if (unfrozen_tl.col == gcanvas->first.col) {
			/* or edit pos is in top left visible cell */
			if (unfrozen_tl.row == gcanvas->first.row)
				center = TRUE;
			else
				unfrozen_tl.col = frozen_tl.col = 0;
		} else if (unfrozen_tl.row == gcanvas->first.row)
			unfrozen_tl.row = frozen_tl.row = 0;

		if (center) {
			unfrozen_tl.col = (gcanvas->first.col + gcanvas->last_visible.col) / 2;
			unfrozen_tl.row = (gcanvas->first.row + gcanvas->last_visible.row) / 2;
		}

		g_return_if_fail (unfrozen_tl.col > frozen_tl.col ||
				  unfrozen_tl.row > frozen_tl.row);

		sv_freeze_panes (sv, &frozen_tl, &unfrozen_tl);
	} else
		sv_freeze_panes (sv, NULL, NULL);
}

static void
cb_view_new (G_GNUC_UNUSED GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_new_view (wbcg);
}

/****************************************************************************/

static void
cb_insert_current_date (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	if (wbcg_edit_start (wbcg, FALSE, FALSE)) {
		Workbook const *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
		GnmValue *v = value_new_int (datetime_timet_to_serial (time (NULL),
								    workbook_date_conv (wb)));
		char *txt = format_value (style_format_default_date (), v, NULL, -1,
				workbook_date_conv (wb));
		value_release (v);
		wbcg_edit_line_set (WORKBOOK_CONTROL (wbcg), txt);
		g_free (txt);
	}
}

static void
cb_insert_current_time (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	if (wbcg_edit_start (wbcg, FALSE, FALSE)) {
		Workbook const *wb = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
		GnmValue *v = value_new_float (datetime_timet_to_seconds (time (NULL)) / (24.0 * 60 * 60));
		char *txt = format_value (style_format_default_time (), v, NULL, -1,
				workbook_date_conv (wb));
		value_release (v);
		wbcg_edit_line_set (WORKBOOK_CONTROL (wbcg), txt);
		g_free (txt);
	}
}

static void
cb_define_name (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_define_names (wbcg);
}

static void
cb_insert_rows (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet     *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const *sel;

	wbcg_edit_finish (wbcg, FALSE, NULL);

	/* TODO : No need to check simplicty.  XL applies for each non-discrete
	 * selected region, (use selection_apply).  Arrays and Merged regions
	 * are permitted.
	 */
	if (!(sel = selection_first_range (sv, GNM_CMD_CONTEXT (wbc), _("Insert rows"))))
		return;
	cmd_insert_rows (wbc, sheet, sel->start.row, range_height (sel));
}

static void
cb_insert_cols (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmRange const *sel;

	wbcg_edit_finish (wbcg, FALSE, NULL);

	/* TODO : No need to check simplicty.  XL applies for each non-discrete
	 * selected region, (use selection_apply).  Arrays and Merged regions
	 * are permitted.
	 */
	if (!(sel = selection_first_range (sv, GNM_CMD_CONTEXT (wbc),
					   _("Insert columns"))))
		return;
	cmd_insert_cols (wbc, sheet, sel->start.col, range_width (sel));
}

static void
cb_insert_cells (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_insert_cells (wbcg);
}

static void
cb_insert_comment (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	dialog_cell_comment (wbcg, sheet, &sv->edit_pos);
}

/****************************************************************************/

static void
cb_sheet_name (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wbcg_cur_scg(wbcg);
	editable_label_start_editing (EDITABLE_LABEL(scg->label));
}

static void
cb_sheet_order (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
        dialog_sheet_order (wbcg);
}

static GnmValue *
cb_rerender_zeroes (Sheet *sheet, int col, int row, GnmCell *cell,
		    gpointer ignored)
{
	if (!cell->rendered_value || !cell_is_zero (cell))
		return NULL;
	cell_render_value (cell, TRUE);
	return NULL;
}


#ifdef WITH_BONOBO
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
	GnmCell *cell;
	WORKBOOK_FOREACH_DEPENDENT
		(wb, dep, {
			if (dependent_is_cell (dep)) {
				cell = DEP_TO_CELL (dep);
				if (cell_has_expr(cell)) {
					if (cell->rendered_value != NULL) {
						rendered_value_destroy (cell->rendered_value);
						cell->rendered_value = NULL;
					}
					if (cell->row_info != NULL)
						cell->row_info->needs_respan = TRUE;
				}
			}
		});
	sheet_adjust_preferences (sheet, TRUE, FALSE);
})
TOGGLE_HANDLER (hide_zero, {
	sheet_foreach_cell_in_range (sheet, CELL_ITER_IGNORE_BLANK,
				     0, 0,
				     SHEET_MAX_COLS - 1, SHEET_MAX_ROWS - 1,
				     cb_rerender_zeroes, 0);
	sheet_adjust_preferences (sheet, TRUE, FALSE);
})
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
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_cell_format (wbcg, FD_CURRENT);
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
cb_format_preferences (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_preferences (wbcg, 1);
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
cb_tools_tabulate (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_tabulate (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_merge (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_merge (wbcg);
}

static void
cb_tools_solver (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_solver (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_scenario_add (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_scenario_add (wbcg);
}

static void
cb_tools_scenarios (GtkWidget *unused, WorkbookControlGUI *wbcg)
{
	dialog_scenarios (wbcg);
}

static void
cb_tools_simulation (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_simulation (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_anova_one_factor (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_anova_single_factor_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_anova_two_factor (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_anova_two_factor_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_correlation (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_correlation_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_covariance (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_covariance_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_desc_statistics (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_descriptive_stat_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_exp_smoothing (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_exp_smoothing_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_average (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_average_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_fourier (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_fourier_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_histogram (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_histogram_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_ranking (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ranking_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_regression (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_regression_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_sampling (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_sampling_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_ttest_paired (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ttest_tool (wbcg, wb_control_cur_sheet (wbc), TTEST_PAIRED);
}

static void
cb_tools_ttest_equal_var (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ttest_tool (wbcg, wb_control_cur_sheet (wbc), TTEST_UNPAIRED_EQUALVARIANCES);
}

static void
cb_tools_ttest_unequal_var (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ttest_tool (wbcg, wb_control_cur_sheet (wbc), TTEST_UNPAIRED_UNEQUALVARIANCES);
}

static void
cb_tools_ztest (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ttest_tool (wbcg, wb_control_cur_sheet (wbc), TTEST_ZTEST);
}

static void
cb_tools_ftest (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_ftest_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_tools_random_generator (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	dialog_random_tool (wbcg, wb_control_cur_sheet (wbc));
}

static void
cb_data_sort (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_cell_sort (wbcg);
}

static void
cb_data_shuffle (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_shuffle (wbcg);
}

static void
cb_data_import_text (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	gui_file_open (wbcg, "Gnumeric_stf:stf_druid");
	wbcg_focus_cur_scg (wbcg); /* force focus back to sheet */
}

static void
cb_auto_filter (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	GnmFilter *filter = sv_first_selection_in_filter (sv);

#warning Add undo/redo
	if (filter == NULL) {
		GnmRange region;
		GnmRange const *src = selection_first_range (sv,
			GNM_CMD_CONTEXT (wbcg), _("Add Filter"));

		if (src == NULL)
			return;
		/* only one row selected -- assume that the user wants to
		 * filter the region below this row. */
		region = *src;
		if (src != NULL && src->start.row == src->end.row)
			sheet_filter_guess_region  (sv->sheet, &region);

		if (src == NULL || region.start.row == region.end.row) {
			gnm_cmd_context_error_invalid	(GNM_CMD_CONTEXT (wbcg),
				 _("AutoFilter"), _("Requires more than 1 row"));
			return;
		}
		gnm_filter_new (sv->sheet, &region);
	} else {
		/* keep distinct to simplify undo/redo later */
		gnm_filter_remove (filter);
		gnm_filter_free (filter);
	}
	sheet_update (sv->sheet);
}

static void
cb_show_all (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	filter_show_all (wb_control_cur_sheet (wbc));
}

static void
cb_data_filter (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_advanced_filter (wbcg);
}

static void
cb_data_validate (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_cell_format (wbcg, FD_VALIDATION);
}

static void
cb_data_text_to_columns (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	stf_text_to_columns (WORKBOOK_CONTROL (wbcg),
			     GNM_CMD_CONTEXT (wbcg));
}

#ifdef ENABLE_PIVOTS
static void
cb_data_pivottable (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_pivottable (wbcg);
}
#endif

static void
cb_data_consolidate (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_consolidate (wbcg);
}

static void
hide_show_detail_real (WorkbookControlGUI *wbcg, gboolean is_cols, gboolean show)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	char const *operation = show ? _("Show Detail") : _("Hide Detail");
	GnmRange const *r = selection_first_range (sv, GNM_CMD_CONTEXT (wbc),
						operation);

	/* This operation can only be performed on a whole existing group */
	if (sheet_colrow_can_group (sv_sheet (sv), r, is_cols)) {
		gnm_cmd_context_error_invalid (GNM_CMD_CONTEXT (wbc), operation,
					_("can only be performed on an existing group"));
		return;
	}

	cmd_selection_colrow_hide (wbc, is_cols, show);
}

static void
hide_show_detail (WorkbookControlGUI *wbcg, gboolean show)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	char const *operation = show ? _("Show Detail") : _("Hide Detail");
	GnmRange const *r = selection_first_range (sv, GNM_CMD_CONTEXT (wbc),
						operation);
	gboolean is_cols;

	/* We only operate on a single selection */
	if (r == NULL)
		return;

	/* Do we need to ask the user what he/she wants to group/ungroup? */
	if (range_is_full (r, TRUE) ^ range_is_full (r, FALSE))
		is_cols = !range_is_full (r, TRUE);
	else {
		GtkWidget *dialog = dialog_col_row (wbcg, operation,
						    (ColRowCallback_t) hide_show_detail_real,
						    GINT_TO_POINTER (show));
		gtk_widget_show (dialog);
		return;
	}

	hide_show_detail_real (wbcg, is_cols, show);
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
group_ungroup_colrow_real (WorkbookControl *wbc, gboolean is_cols, gboolean group)
{
	cmd_selection_group (wbc, is_cols, group);
}

static void
group_ungroup_colrow (WorkbookControlGUI *wbcg, gboolean group)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	SheetView *sv = wb_control_cur_sheet_view (wbc);
	char const *operation = group ? _("Group") : _("Ungroup");
	GnmRange const *r = selection_first_range (sv, GNM_CMD_CONTEXT (wbc), operation);
	gboolean is_cols;

	/* We only operate on a single selection */
	if (r == NULL)
		return;

	/* Do we need to ask the user what he/she wants to group/ungroup? */
	if (range_is_full (r, TRUE) ^ range_is_full (r, FALSE))
		is_cols = !range_is_full (r, TRUE);
	else {
		GtkWidget *dialog = dialog_col_row (wbcg, operation,
						    (ColRowCallback_t) group_ungroup_colrow_real,
						    GINT_TO_POINTER (group));
		gtk_widget_show (dialog);
		return;
	}

	group_ungroup_colrow_real (WORKBOOK_CONTROL (wbcg), is_cols, group);
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
	gchar const *txt;

	if (wbcg_is_editing (wbcg))
		return;

	entry = wbcg_get_entry (wbcg);
	txt = gtk_entry_get_text (entry);
	if (strncmp (txt, "=sum(", 5)) {
		if (!wbcg_edit_start (wbcg, TRUE, TRUE))
			return; /* attempt to edit failed */
		gtk_entry_set_text (entry, "=sum()");
		gtk_editable_set_position (GTK_EDITABLE (entry), 5);
	} else {
		if (!wbcg_edit_start (wbcg, FALSE, TRUE))
			return; /* attempt to edit failed */

		/*
		 * FIXME : This is crap!
		 * When the function druid is more complete use that.
		 */
		gtk_editable_set_position (GTK_EDITABLE (entry), entry->text_length-1);
	}

	/* force focus back to sheet */
	wbcg_focus_cur_scg (wbcg);
}

static void
cb_insert_image (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	GtkFileSelection *fsel;

	fsel = GTK_FILE_SELECTION (
		preview_file_selection_new (_("Select an image"), TRUE));
	gtk_file_selection_hide_fileop_buttons (fsel);

	if (gnumeric_dialog_file_selection (wbcg, fsel)) {
		int fd, file_size;
		IOContext *ioc = gnumeric_io_context_new (GNM_CMD_CONTEXT (wbcg));
		unsigned char const *data = gnumeric_mmap_open (ioc,
			gtk_file_selection_get_filename (fsel), &fd, &file_size);

		if (data != NULL) {
			SheetObject *so = sheet_object_image_new ("",
				(guint8 *)data, file_size, TRUE);
			gnumeric_mmap_close (ioc, data, fd, file_size);
			scg_mode_create_object (scg, so);
		} else
			gnumeric_io_error_display (ioc);

		g_object_unref (G_OBJECT (ioc));
	}

	gtk_widget_destroy (GTK_WIDGET (fsel));
}

static void
cb_insert_hyperlink (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	if (!wbcg_edit_finish (wbcg, TRUE, NULL))
		return;
	dialog_hyperlink (wbcg, SHEET_CONTROL (wbcg_cur_scg (wbcg)));
}

static void
cb_formula_guru (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	dialog_formula_guru (wbcg, NULL);
}

static void
sort_by_rows (WorkbookControlGUI *wbcg, int asc)
{
	SheetView *sv;
	GnmRange *sel;
	GnmRange const *tmp;
	GnmSortData *data;
	GnmSortClause *clause;
	int numclause, i;

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));

	sv = wb_control_cur_sheet_view (WORKBOOK_CONTROL (wbcg));

	if (!(tmp = selection_first_range (sv, GNM_CMD_CONTEXT (wbcg), _("Sort"))))
		return;

	sel = range_dup (tmp);
	range_clip_to_finite (sel, sv_sheet (sv));

	numclause = range_width (sel);
	clause = g_new0 (GnmSortClause, numclause);
	for (i=0; i < numclause; i++) {
		clause[i].offset = i;
		clause[i].asc = asc;
		clause[i].cs = gnm_app_prefs->sort_default_by_case;
		clause[i].val = TRUE;
	}

	data = g_new (GnmSortData, 1);
	data->sheet = sv_sheet (sv);
	data->range = sel;
	data->num_clause = numclause;
	data->clauses = clause;

	data->retain_formats = gnm_app_prefs->sort_default_retain_formats;

	/* Hard code sorting by row.  I would prefer not to, but user testing
	 * indicates
	 * - that the button should always does the same things
	 * - that the icon matches the behavior
	 * - XL does this.
	 */
	data->top = TRUE;

	if (range_has_header (data->sheet, data->range, data->top, FALSE))
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

#include <goffice/graph/gog-guru.h>
#include <goffice/graph/gog-data-allocator.h>
#include <goffice/graph/gog-data-set.h>
#include <sheet-object-graph.h>
static void
cb_add_graph (GogGraph *graph, gpointer wbcg)
{
	SheetControlGUI *scg = wbcg_cur_scg (WORKBOOK_CONTROL_GUI (wbcg));
	scg_mode_create_object (scg, sheet_object_graph_new (graph));
}

static void
cb_launch_chart_guru (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	GClosure *closure = g_cclosure_new (G_CALLBACK (cb_add_graph),
					    wbcg, NULL);
	sheet_object_graph_guru (wbcg, NULL, closure);
	g_closure_sink (closure);
}

#ifdef WITH_BONOBO
#ifdef GNOME2_CONVERSION_COMPLETE
static void
insert_bonobo_object (WorkbookControlGUI *wbcg, char const **interfaces)
{
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
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
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR, msg);
		g_free (msg);
	}
	scg_mode_edit (sc);
}

static void
cb_insert_component (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	static char const *required_interfaces [2] = {
		"IDL:Bonobo/ControlFactory:1.0", NULL
	};
	insert_bonobo_object (wbcg, required_interfaces);
}

static void
cb_insert_shaped_component (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	static char const *required_interfaces [2] = {
		"IDL:Bonobo/CanvasComponentFactory:1.0", NULL
	};
	insert_bonobo_object (wbcg, required_interfaces);
}
#endif
#endif

#ifndef WITH_BONOBO
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
        GNOMEUIINFO_MENU_NEW_ITEM (N_("_New"), N_("Create a new workbook"),
				  cb_file_new, NULL),

	GNOMEUIINFO_MENU_OPEN_ITEM (cb_file_open, NULL),
	GNOMEUIINFO_SEPARATOR,
	GNOMEUIINFO_MENU_SAVE_ITEM (cb_file_save, NULL),
	GNOMEUIINFO_MENU_SAVE_AS_ITEM (cb_file_save_as, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("Page Set_up..."),
			       N_("Setup the page settings for your current printer"),
				cb_file_page_setup),
	{ GNOME_APP_UI_ITEM, N_("Print Pre_view..."),
	  N_("Print preview"),
	  cb_file_print_preview,
	  NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_PRINT_PREVIEW,
	  'p', GDK_CONTROL_MASK | GDK_SHIFT_MASK },
	GNOMEUIINFO_MENU_PRINT_ITEM (cb_file_print, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (N_("Sen_d To..."),
				N_("Send the current file via email"),
				cb_file_sendto, "Gnumeric_Link_EMail"),
	GNOMEUIINFO_ITEM_STOCK (N_("Proper_ties..."),
			       N_("Edit descriptive information"),
			       cb_file_summary, GTK_STOCK_PROPERTIES),

	GNOMEUIINFO_ITEM_STOCK (N_("Preferen_ces..."),
			       N_("Change Gnumeric Preferences"),
			       cb_file_preferences, GTK_STOCK_PREFERENCES),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_CLOSE_ITEM (cb_file_close, NULL),
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
	GNOMEUIINFO_ITEM_NONE (N_("_Contents"),
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

	{ GNOME_APP_UI_ITEM, N_("Select _Column"),
	  N_("Select an entire column"),
	  cb_edit_select_col, NULL,
	  NULL, 0, 0, ' ', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Row"),
	  N_("Select an entire row"),
	  cb_edit_select_row, NULL,
	  NULL, 0, 0, ' ', GDK_MOD1_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select Arra_y"),
	  N_("Select an array of cells"),
	  cb_edit_select_array, NULL,
	  NULL, 0, 0, '/', GDK_CONTROL_MASK },

	{ GNOME_APP_UI_ITEM, N_("Select _Depends"),
	  N_("Select all the cells that depend on the current edit cell"),
	  cb_edit_select_depends, NULL,
	  NULL, 0, 0, ']', GDK_CONTROL_MASK },
	{ GNOME_APP_UI_ITEM, N_("Select _Inputs"),
	  N_("Select all the cells are used by the current edit cell"),
	  cb_edit_select_inputs, NULL,
	  NULL, 0, 0, '[', GDK_CONTROL_MASK },
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_edit_fill [] = {
	GNOMEUIINFO_ITEM_NONE (N_("Auto_fill"),
		N_("Automatically fill the current selection"),
		cb_edit_fill_autofill),
	GNOMEUIINFO_ITEM_NONE (N_("_Merge..."),
		N_("Merges columnar data into a sheet creating duplicate sheets for each row"),
		cb_tools_merge),
	GNOMEUIINFO_ITEM_NONE (N_("_Tabulate Dependency..."),
		N_("Make a table of a cell's value as a function of other cells"),
		cb_tools_tabulate),
	GNOMEUIINFO_ITEM_NONE (N_("_Series..."),
		N_("Fill according to a linear or exponential series"),
		cb_edit_fill_series),
	GNOMEUIINFO_ITEM_NONE (N_("_Random Generator..."),
		N_("Generate random numbers of a selection of distributions"),
		cb_tools_random_generator),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_edit_sheet [] = {

	GNOMEUIINFO_ITEM_NONE (N_("_Manage Sheets..."),
		N_("Manage the sheets in this workbook"),
		cb_sheet_order),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("_Insert"),
		N_("Insert a new sheet"),
		cb_insert_sheet),

	GNOMEUIINFO_ITEM_NONE (N_("_Append"),
		N_("Append a new sheet"),
		cb_append_sheet),

	GNOMEUIINFO_ITEM_NONE (N_("_Duplicate"),
		N_("Make a copy of the current sheet"),
		cb_edit_duplicate_sheet),

	GNOMEUIINFO_ITEM_NONE (N_("_Remove"),
		N_("Irrevocably remove an entire sheet"),
		cb_sheet_remove),

	GNOMEUIINFO_ITEM_NONE (N_("Re_name"),
		N_("Rename the current sheet"),
		cb_sheet_name),

	GNOMEUIINFO_END
};


static GnomeUIInfo workbook_menu_edit [] = {
	GNOMEUIINFO_MENU_UNDO_ITEM(cb_edit_undo, NULL),
	GNOMEUIINFO_MENU_REDO_ITEM(cb_edit_redo, NULL),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_MENU_CUT_ITEM (cb_edit_cut, NULL),
	GNOMEUIINFO_MENU_COPY_ITEM (cb_edit_copy, NULL),
	GNOMEUIINFO_MENU_PASTE_ITEM (cb_edit_paste, NULL),

	GNOMEUIINFO_ITEM_NONE (N_("P_aste special..."),
		N_("Paste with optional filters and transformations"),
		cb_edit_paste_special),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("C_lear"), workbook_menu_edit_clear),

	{ GNOME_APP_UI_ITEM, N_("_Delete..."),
	  N_("Remove selected cells, shifting others into their place"),
	  cb_edit_delete,
	  NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_DELETE,
	  0, 0 },

	GNOMEUIINFO_ITEM_STOCK (N_("Co_mment..."),
		N_("Edit the selected cell's comment"),
		cb_insert_comment, "Gnumeric_CommentEdit"),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("S_heet"), workbook_menu_edit_sheet),

	GNOMEUIINFO_SUBTREE(N_("_Select"), workbook_menu_edit_select),

	GNOMEUIINFO_SUBTREE(N_("_Fill"), workbook_menu_edit_fill),

	{ GNOME_APP_UI_ITEM, N_("Search..."),
		  N_("Search for some text"),
	          cb_edit_search,
		  NULL, NULL,
		  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_FIND, GDK_F7, 0 },

	{ GNOME_APP_UI_ITEM, N_("Search and Replace..."),
		  N_("Search for some text and replace it with something else"),
	          cb_edit_search_replace,
		  NULL, NULL,
		  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_FIND_AND_REPLACE, GDK_F6, 0 },

	{ GNOME_APP_UI_ITEM, N_("_Goto cell..."),
		  N_("Jump to a specified cell"),
		  cb_edit_goto,
		  NULL, NULL,
		  GNOME_APP_PIXMAP_STOCK, GTK_STOCK_JUMP_TO, GDK_F5, 0 },

	{ GNOME_APP_UI_ITEM, N_("Recalculate"),
		  N_("Recalculate the spreadsheet"),
		  cb_edit_recalc,
		  NULL, NULL, 0, 0, GDK_F9, 0 },

	GNOMEUIINFO_END
};

/* View menu */

static GnomeUIInfo workbook_menu_view [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_New View..."),
		N_("Create a new view of the workbook"),
		cb_view_new),
	GNOMEUIINFO_ITEM_NONE (N_("_Zoom..."),
		N_("Zoom the spreadsheet in or out"),
		cb_view_zoom),
	GNOMEUIINFO_ITEM_NONE (N_("_Freeze Panes"),
		N_("Freeze the top left of the sheet"),
		cb_view_freeze_panes),
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
	GNOMEUIINFO_ITEM_NONE (N_("C_ells..."),
		N_("Insert new cells"),
		cb_insert_cells),
	GNOMEUIINFO_ITEM_STOCK (N_("_Columns"),
		N_("Insert new columns"),
		cb_insert_cols, "Gnumeric_ColumnAdd"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Rows"),
		N_("Insert new rows"),
		cb_insert_rows, "Gnumeric_RowAdd"),
	GNOMEUIINFO_ITEM_NONE (N_("_Sheet"),
		N_("Insert a new sheet"),
		cb_insert_sheet),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (N_("C_hart..."),
		N_("Launch the Chart Guru"),
		cb_launch_chart_guru, "Gnumeric_GraphGuru"),

	GNOMEUIINFO_ITEM_STOCK (N_("_Image..."),
		N_("Insert an image"),
		cb_insert_image, "Gnumeric_InsertImage"),

	GNOMEUIINFO_ITEM_STOCK (N_("_Function..."),
		N_("Insert a function into the selected cell"),
		cb_formula_guru, "Gnumeric_FormulaGuru"),

	GNOMEUIINFO_SUBTREE(N_("_Name"), workbook_menu_names),

	{ GNOME_APP_UI_ITEM, N_("Hyper_link..."),
	  N_("Insert a Hyperlink"),
	  cb_insert_hyperlink, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, "Gnumeric_Link_Add", 'k', GDK_CONTROL_MASK },

	GNOMEUIINFO_SUBTREE(N_("S_pecial"), workbook_menu_insert_special),
	GNOMEUIINFO_END
};

/* Format menu */
static GnomeUIInfo workbook_menu_format_column [] = {
	GNOMEUIINFO_ITEM_STOCK (N_("_Width..."),
		N_("Change width of the selected columns"),
		sheet_dialog_set_column_width, "Gnumeric_ColumnSize"),
	GNOMEUIINFO_ITEM_NONE (N_("_Auto fit selection"),
		N_("Ensure columns are just wide enough to display content"),
		workbook_cmd_format_column_auto_fit),
	GNOMEUIINFO_ITEM_STOCK (N_("_Hide"),
		N_("Hide the selected columns"),
		workbook_cmd_format_column_hide, "Gnumeric_ColumnHide"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Unhide"),
		N_("Make any hidden columns in the selection visible"),
		workbook_cmd_format_column_unhide, "Gnumeric_ColumnUnhide"),
	GNOMEUIINFO_ITEM_NONE (N_("_Standard Width"),
		N_("Change the default column width"),
		workbook_cmd_format_column_std_width),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_row [] = {
	GNOMEUIINFO_ITEM_STOCK (N_("H_eight..."),
		N_("Change height of the selected rows"),
		sheet_dialog_set_row_height, "Gnumeric_RowSize"),
	GNOMEUIINFO_ITEM_NONE (N_("_Auto fit selection"),
		N_("Ensure rows are just tall enough to display content"),
		workbook_cmd_format_row_auto_fit),
	GNOMEUIINFO_ITEM_STOCK (N_("_Hide"),
		N_("Hide the selected rows"),
		workbook_cmd_format_row_hide, "Gnumeric_RowHide"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Unhide"),
		N_("Make any hidden rows in the selection visible"),
		workbook_cmd_format_row_unhide, "Gnumeric_RowUnhide"),
	GNOMEUIINFO_ITEM_NONE (N_("_Standard Height"),
		N_("Change the default row height"),
		workbook_cmd_format_row_std_height),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_format_sheet [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Manage Sheets..."),
		N_("Manage the sheets in this workbook"),
		cb_sheet_order),
	GNOMEUIINFO_ITEM_NONE (N_("Re_name"),
		N_("Rename the current sheet"),
		cb_sheet_name),
	GNOMEUIINFO_SEPARATOR,
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
		N_("Hide _Column Headers"),
		N_("Toggle whether or not to display column headers"),
		cb_sheet_pref_hide_col_header, NULL, NULL,
		GNOME_APP_PIXMAP_NONE, NULL, 0, (GdkModifierType) 0, NULL
	},
	{ GNOME_APP_UI_TOGGLEITEM,
		N_("Hide _Row Headers"),
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

	GNOMEUIINFO_ITEM_NONE (N_("_Workbook..."),
		N_("Modify the workbook attributes"),
		cb_workbook_attr),
	GNOMEUIINFO_ITEM_STOCK (N_("_Gnumeric..."),
		N_("Edit the Gnumeric Preferences"),
		cb_format_preferences, GTK_STOCK_PREFERENCES),

	GNOMEUIINFO_ITEM_NONE (N_("_Autoformat..."),
			      N_("Format a region of cells according to a pre-defined template"),
			      cb_autoformat),
	GNOMEUIINFO_END
};

/* Tools menu */

static GnomeUIInfo workbook_menu_tools_scenarios [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_View..."),
                N_("View, delete and report different scenarios"),
                cb_tools_scenarios),
	GNOMEUIINFO_ITEM_NONE (N_("_Add..."),
                N_("Add a new scenario"),
                cb_tools_scenario_add),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_tools_anova [] = {

	GNOMEUIINFO_ITEM_NONE (N_("_One Factor..."),
		N_("One Factor Analysis of Variance..."),
		cb_tools_anova_one_factor),
	GNOMEUIINFO_ITEM_NONE (N_("_Two Factor..."),
		N_("Two Factor Analysis of Variance..."),
		cb_tools_anova_two_factor),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_tools_forecasting [] = {

	GNOMEUIINFO_ITEM_NONE (N_("_Exponential Smoothing..."),
		N_("Exponential smoothing..."),
		cb_tools_exp_smoothing),

	GNOMEUIINFO_ITEM_NONE (N_("_Moving Average..."),
		N_("Moving average..."),
		cb_tools_average),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_tools_two_means [] = {

	GNOMEUIINFO_ITEM_NONE (N_("_Paired Samples: T-Test..."),
		N_("Comparing two population means for two paired samples: t-test..."),
		cb_tools_ttest_paired),

	GNOMEUIINFO_ITEM_NONE (N_("Unpaired Samples, _Equal Variances: T-Test..."),
		N_("Comparing two population means for two unpaired samples "
		   "from populations with equal variances: t-test..."),
		cb_tools_ttest_equal_var),

	GNOMEUIINFO_ITEM_NONE (N_("Unpaired Samples, _Unequal Variances: T-Test..."),
		N_("Comparing two population means for two unpaired samples "
		   "from populations with unequal variances: t-test..."),
		cb_tools_ttest_unequal_var),

	GNOMEUIINFO_ITEM_NONE (N_("_Known Variances or Large Sample: Z-Test..."),
		N_("Comparing two population means from populations "
		   "with known variances "
		   "or using a large sample: z-test..."),
		cb_tools_ztest),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_tools_analysis [] = {

	GNOMEUIINFO_SUBTREE(N_("_ANOVA"), workbook_menu_tools_anova),

	GNOMEUIINFO_ITEM_NONE (N_("_Correlation..."),
		N_("Pearson Correlation"),
		cb_tools_correlation),
	GNOMEUIINFO_ITEM_NONE (N_("Co_variance..."),
		N_("Covariance"),
		cb_tools_covariance),
	GNOMEUIINFO_ITEM_NONE (N_("_Descriptive Statistics..."),
		N_("Various summary statistics"),
		cb_tools_desc_statistics),

	GNOMEUIINFO_SUBTREE(N_("F_orecasting"), workbook_menu_tools_forecasting),

	GNOMEUIINFO_ITEM_NONE (N_("_Fourier Analysis..."),
		N_("Fourier Analysis"),
		cb_tools_fourier),
	GNOMEUIINFO_ITEM_NONE (N_("_Histogram..."),
		N_("Various frequency tables"),
		cb_tools_histogram),
	GNOMEUIINFO_ITEM_NONE (N_("Ranks And _Percentiles..."),
		N_("Ranks, placements and percentiles"),
		cb_tools_ranking),
	GNOMEUIINFO_ITEM_NONE (N_("_Regression..."),
		N_("Regression Analysis"),
		cb_tools_regression),
	GNOMEUIINFO_ITEM_NONE (N_("_Sampling..."),
		N_("Periodic and random samples"),
		cb_tools_sampling),

	GNOMEUIINFO_SUBTREE(N_("Two _Means"), workbook_menu_tools_two_means),

	GNOMEUIINFO_ITEM_NONE (N_("_Two Variances: FTest..."),
		N_("Comparing two population variances"),
		cb_tools_ftest),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_tools [] = {
	GNOMEUIINFO_ITEM_NONE (N_("_Plug-ins..."),
		N_("Manage available plugin modules"),
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

	GNOMEUIINFO_ITEM_NONE (N_("Si_mulation..."),
		N_("Test decision alternatives by using Monte Carlo simulation "
		   "to find out probable outputs and risks related to them"),
		cb_tools_simulation),

       GNOMEUIINFO_SUBTREE(N_("Sce_narios"),
                           workbook_menu_tools_scenarios),


	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_SUBTREE(N_("Statistical Anal_ysis"),
			    workbook_menu_tools_analysis),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_data_outline [] = {
	GNOMEUIINFO_ITEM_STOCK (N_("_Hide Detail"),
		N_("Collapse an outline group"),
		cb_data_hide_detail, "Gnumeric_HideDetail"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Show Detail"),
		N_("Uncollapse an outline group"),
		cb_data_show_detail, "Gnumeric_ShowDetail"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Group..."),
		N_("Add an outline group"),
		cb_data_group, "Gnumeric_Group"),
	GNOMEUIINFO_ITEM_STOCK (N_("_Ungroup..."),
		N_("Remove an outline group"),
		cb_data_ungroup, "Gnumeric_Ungroup"),

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

static GnomeUIInfo workbook_menu_data_external [] = {
	GNOMEUIINFO_ITEM_STOCK (N_("Import _Text File..."),
		N_("Import the text from a file"),
		cb_data_import_text, "gtk-dnd"),
	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_data_filter [] = {
	GNOMEUIINFO_ITEM_NONE (N_("Add _Auto Filter"),
		N_("Add or remove a filter"),
		cb_auto_filter),
	GNOMEUIINFO_ITEM_NONE (N_("_Show All"),
		N_("Show all filtered and hidden rows"),
		cb_show_all),
	GNOMEUIINFO_ITEM_NONE (N_("Advanced _Filter..."),
		N_("Filter data with given criteria"),
		cb_data_filter),

	GNOMEUIINFO_END
};

/* Data menu */
static GnomeUIInfo workbook_menu_data [] = {
	GNOMEUIINFO_ITEM_STOCK (N_("_Sort..."),
		N_("Sort the selected region"),
		cb_data_sort, GTK_STOCK_SORT_ASCENDING),
	GNOMEUIINFO_ITEM_NONE (N_("Sh_uffle..."),
		N_("Shuffle cells, rows or columns"),
		cb_data_shuffle),
	GNOMEUIINFO_SUBTREE(N_("_Filter"), workbook_menu_data_filter),
	GNOMEUIINFO_ITEM_NONE (N_("_Validate..."),
		N_("Validate input with preset criteria"),
		cb_data_validate),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_NONE (N_("_Text to Columns..."),
		N_("Parse the text in the selection into data"),
		cb_data_text_to_columns),
	GNOMEUIINFO_ITEM_NONE (N_("_Consolidate..."),
		N_("Consolidate regions using a function"),
		cb_data_consolidate),

	GNOMEUIINFO_SUBTREE(N_("_Group and Outline"),   workbook_menu_data_outline),
#ifdef ENABLE_PIVOTS
	GNOMEUIINFO_ITEM_STOCK (N_("_PivotTable..."),
		N_("Create a pivot table"),
		cb_data_pivottable, "Gnumeric_PivotTable"),
#endif
	GNOMEUIINFO_SUBTREE(N_("Get _External Data"),   workbook_menu_data_external),

	GNOMEUIINFO_END
};

static GnomeUIInfo workbook_menu_help [] = {
	GNOMEUIINFO_HELP ((char *)"gnumeric"),
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
		N_("New"), N_("Create a new workbook"),
		cb_file_new, GTK_STOCK_NEW),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Open"), N_("Open a file"),
		cb_file_open, GTK_STOCK_OPEN),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Save"), N_("Save the current workbook"),
		cb_file_save, GTK_STOCK_SAVE),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Print"), N_("Print the workbook"),
		cb_file_print, GTK_STOCK_PRINT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Print Preview"), N_("Print preview"),
		cb_file_print_preview, GTK_STOCK_PRINT_PREVIEW),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (
		N_("Cut"), N_("Cut the selection to the clipboard"),
		cb_edit_cut, GTK_STOCK_CUT),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Copy"), N_("Copy the selection to the clipboard"),
		cb_edit_copy, GTK_STOCK_COPY),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Paste"), N_("Paste the contents of the clipboard"),
		cb_edit_paste, GTK_STOCK_PASTE),

	GNOMEUIINFO_SEPARATOR,

#if 0
	GNOMEUIINFO_ITEM_STOCK (
		N_("Undo"), N_("Undo the operation"),
		cb_edit_undo, GTK_STOCK_UNDO),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Redo"), N_("Redo the operation"),
		cb_edit_redo, GTK_STOCK_REDO),
#else
#define TB_UNDO_POS 11
#define TB_REDO_POS 12
#endif

	GNOMEUIINFO_SEPARATOR,

	{ GNOME_APP_UI_ITEM, N_("Hyperlink"),
	  N_("Insert a Hyperlink"),
	  cb_insert_hyperlink, NULL, NULL,
	  GNOME_APP_PIXMAP_STOCK, "Gnumeric_Link_Add", 'k', GDK_CONTROL_MASK },

	GNOMEUIINFO_ITEM_STOCK (N_("Sum"),
		N_("Sum into the current cell"),
		cb_autosum, "Gnumeric_AutoSum"),
	GNOMEUIINFO_ITEM_STOCK (N_("Function"),
		N_("Edit a function in the current cell"),
		cb_formula_guru, "Gnumeric_FormulaGuru"),

	GNOMEUIINFO_ITEM_STOCK (N_("Sort Ascending"),
		N_("Sort the selected region in ascending order based on the first column selected"),
		cb_sort_ascending, GTK_STOCK_SORT_ASCENDING),
	GNOMEUIINFO_ITEM_STOCK (N_("Sort Descending"),
		N_("Sort the selected region in descending order based on the first column selected"),
		cb_sort_descending, GTK_STOCK_SORT_DESCENDING),

	GNOMEUIINFO_SEPARATOR,

	GNOMEUIINFO_ITEM_STOCK (N_("Chart Guru"),
		N_("Launch the Chart Guru"),
		cb_launch_chart_guru, "Gnumeric_GraphGuru"),


	GNOMEUIINFO_END
};
#else
static BonoboUIVerb verbs [] = {

	BONOBO_UI_UNSAFE_VERB ("FileNew", cb_file_new),
	BONOBO_UI_UNSAFE_VERB ("FileOpen", cb_file_open),
	BONOBO_UI_UNSAFE_VERB ("FileSave", cb_file_save),
	BONOBO_UI_UNSAFE_VERB ("FileSaveAs", cb_file_save_as),
	BONOBO_UI_UNSAFE_VERB ("FileSend", cb_file_sendto),
	BONOBO_UI_UNSAFE_VERB ("FilePageSetup", cb_file_page_setup),
	BONOBO_UI_UNSAFE_VERB ("FilePrint", cb_file_print),
	BONOBO_UI_UNSAFE_VERB ("FilePrintPreview", cb_file_print_preview),
	BONOBO_UI_UNSAFE_VERB ("FileSummary", cb_file_summary),
	BONOBO_UI_UNSAFE_VERB ("FilePreferences", cb_file_preferences),
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
	BONOBO_UI_UNSAFE_VERB ("EditSelectDepends", cb_edit_select_depends),
	BONOBO_UI_UNSAFE_VERB ("EditSelectInputs", cb_edit_select_inputs),

	BONOBO_UI_UNSAFE_VERB ("EditUndo", cb_edit_undo),
	BONOBO_UI_UNSAFE_VERB ("EditRedo", cb_edit_redo),
	BONOBO_UI_UNSAFE_VERB ("EditCut", cb_edit_cut),
	BONOBO_UI_UNSAFE_VERB ("EditCopy", cb_edit_copy),
	BONOBO_UI_UNSAFE_VERB ("EditPaste", cb_edit_paste),
	BONOBO_UI_UNSAFE_VERB ("EditPasteSpecial", cb_edit_paste_special),
	BONOBO_UI_UNSAFE_VERB ("EditDelete", cb_edit_delete),
	BONOBO_UI_UNSAFE_VERB ("EditDuplicateSheet", cb_edit_duplicate_sheet),
	BONOBO_UI_UNSAFE_VERB ("EditFillAutofill", cb_edit_fill_autofill),
	BONOBO_UI_UNSAFE_VERB ("EditFillSeries", cb_edit_fill_series),
	BONOBO_UI_UNSAFE_VERB ("EditSearch", cb_edit_search),
	BONOBO_UI_UNSAFE_VERB ("EditSearchReplace", cb_edit_search_replace),
	BONOBO_UI_UNSAFE_VERB ("EditGoto", cb_edit_goto),
	BONOBO_UI_UNSAFE_VERB ("EditRecalc", cb_edit_recalc),

	BONOBO_UI_UNSAFE_VERB ("ViewNew", cb_view_new),
	BONOBO_UI_UNSAFE_VERB ("ViewZoom", cb_view_zoom),
	BONOBO_UI_UNSAFE_VERB ("ViewFreezeThawPanes", cb_view_freeze_panes),

	BONOBO_UI_UNSAFE_VERB ("InsertCurrentDate", cb_insert_current_date),
	BONOBO_UI_UNSAFE_VERB ("InsertCurrentTime", cb_insert_current_time),
	BONOBO_UI_UNSAFE_VERB ("EditNames", cb_define_name),

	BONOBO_UI_UNSAFE_VERB ("InsertSheet", cb_insert_sheet),
	BONOBO_UI_UNSAFE_VERB ("InsertSheetAtEnd", cb_append_sheet),
	BONOBO_UI_UNSAFE_VERB ("InsertRows", cb_insert_rows),
	BONOBO_UI_UNSAFE_VERB ("InsertColumns", cb_insert_cols),
	BONOBO_UI_UNSAFE_VERB ("InsertCells", cb_insert_cells),
	BONOBO_UI_UNSAFE_VERB ("InsertFormula", cb_formula_guru),
	BONOBO_UI_UNSAFE_VERB ("InsertComment", cb_insert_comment),
	BONOBO_UI_UNSAFE_VERB ("InsertImage", cb_insert_image),
	BONOBO_UI_UNSAFE_VERB ("InsertHyperlink", cb_insert_hyperlink),

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

	BONOBO_UI_UNSAFE_VERB ("SheetChangeName", cb_sheet_name),
	BONOBO_UI_UNSAFE_VERB ("SheetReorder", cb_sheet_order),
	BONOBO_UI_UNSAFE_VERB ("SheetRemove", cb_sheet_remove),

	BONOBO_UI_UNSAFE_VERB ("FormatCells", cb_format_cells),
	BONOBO_UI_UNSAFE_VERB ("FormatAuto", cb_autoformat),
	BONOBO_UI_UNSAFE_VERB ("FormatWorkbook", cb_workbook_attr),
	BONOBO_UI_UNSAFE_VERB ("FormatGnumeric", cb_format_preferences),

	BONOBO_UI_UNSAFE_VERB ("ToolsPlugins", cb_tools_plugins),
	BONOBO_UI_UNSAFE_VERB ("ToolsAutoCorrect", cb_tools_autocorrect),
	BONOBO_UI_UNSAFE_VERB ("ToolsAutoSave", cb_tools_auto_save),
	BONOBO_UI_UNSAFE_VERB ("ToolsGoalSeek", cb_tools_goal_seek),
	BONOBO_UI_UNSAFE_VERB ("ToolsTabulate", cb_tools_tabulate),
	BONOBO_UI_UNSAFE_VERB ("ToolsMerge", cb_tools_merge),
	BONOBO_UI_UNSAFE_VERB ("ToolsSolver", cb_tools_solver),
        BONOBO_UI_UNSAFE_VERB ("ToolsScenarioAdd", cb_tools_scenario_add),
        BONOBO_UI_UNSAFE_VERB ("ToolsScenarios", cb_tools_scenarios),
	BONOBO_UI_UNSAFE_VERB ("ToolsSimulation", cb_tools_simulation),

	BONOBO_UI_UNSAFE_VERB ("ToolsANOVAoneFactor", cb_tools_anova_one_factor),
	BONOBO_UI_UNSAFE_VERB ("ToolsANOVAtwoFactor", cb_tools_anova_two_factor),
	BONOBO_UI_UNSAFE_VERB ("ToolsCorrelation", cb_tools_correlation),
	BONOBO_UI_UNSAFE_VERB ("ToolsCovariance", cb_tools_covariance),
	BONOBO_UI_UNSAFE_VERB ("ToolsDescStatistics", cb_tools_desc_statistics),
	BONOBO_UI_UNSAFE_VERB ("ToolsExpSmoothing", cb_tools_exp_smoothing),
	BONOBO_UI_UNSAFE_VERB ("ToolsAverage", cb_tools_average),
	BONOBO_UI_UNSAFE_VERB ("ToolsFourier", cb_tools_fourier),
	BONOBO_UI_UNSAFE_VERB ("ToolsHistogram", cb_tools_histogram),
	BONOBO_UI_UNSAFE_VERB ("ToolsRanking", cb_tools_ranking),
	BONOBO_UI_UNSAFE_VERB ("ToolsRegression", cb_tools_regression),
	BONOBO_UI_UNSAFE_VERB ("ToolsSampling", cb_tools_sampling),
	BONOBO_UI_UNSAFE_VERB ("ToolTTestPaired", cb_tools_ttest_paired),
	BONOBO_UI_UNSAFE_VERB ("ToolTTestEqualVar", cb_tools_ttest_equal_var),
	BONOBO_UI_UNSAFE_VERB ("ToolTTestUnequalVar", cb_tools_ttest_unequal_var),
	BONOBO_UI_UNSAFE_VERB ("ToolZTest", cb_tools_ztest),
	BONOBO_UI_UNSAFE_VERB ("ToolsFTest", cb_tools_ftest),
	BONOBO_UI_UNSAFE_VERB ("RandomGenerator", cb_tools_random_generator),

	BONOBO_UI_UNSAFE_VERB ("DataSort", cb_data_sort),
	BONOBO_UI_UNSAFE_VERB ("DataShuffle", cb_data_shuffle),
	BONOBO_UI_UNSAFE_VERB ("DataAutoFilter", cb_auto_filter),
	BONOBO_UI_UNSAFE_VERB ("DataFilterShowAll", cb_show_all),
	BONOBO_UI_UNSAFE_VERB ("DataFilterAdvancedfilter", cb_data_filter),
	BONOBO_UI_UNSAFE_VERB ("DataValidate", cb_data_validate),
	BONOBO_UI_UNSAFE_VERB ("DataTextToColumns", cb_data_text_to_columns),
	BONOBO_UI_UNSAFE_VERB ("DataConsolidate", cb_data_consolidate),
	BONOBO_UI_UNSAFE_VERB ("DataImportText", cb_data_import_text),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineHideDetail", cb_data_hide_detail),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineShowDetail", cb_data_show_detail),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineGroup", cb_data_group),
	BONOBO_UI_UNSAFE_VERB ("DataOutlineUngroup", cb_data_ungroup),

#ifdef ENABLE_PIVOTS
	BONOBO_UI_UNSAFE_VERB ("PivotTable", cb_data_pivottable),
#endif

	BONOBO_UI_UNSAFE_VERB ("AutoSum", cb_autosum),
	BONOBO_UI_UNSAFE_VERB ("FunctionGuru", cb_formula_guru),
	BONOBO_UI_UNSAFE_VERB ("SortAscending", cb_sort_ascending),
	BONOBO_UI_UNSAFE_VERB ("SortDescending", cb_sort_descending),
	BONOBO_UI_UNSAFE_VERB ("ChartGuru", cb_launch_chart_guru),
#ifdef GNOME2_CONVERSION_COMPLETE
	BONOBO_UI_UNSAFE_VERB ("InsertComponent", cb_insert_component),
	BONOBO_UI_UNSAFE_VERB ("InsertShapedComponent", cb_insert_shaped_component),
#endif

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
	GtkWidget *zoom, *entry, *undo, *redo;

	/* Zoom combo box */
	zoom = wbcg->zoom_entry = gnm_combo_text_new (NULL);
	entry = GNM_COMBO_TEXT (zoom)->entry;
	g_signal_connect (G_OBJECT (zoom),
		"entry_changed",
		G_CALLBACK (cb_change_zoom), wbcg);
	gnm_combo_box_set_title (GNM_COMBO_BOX (zoom), _("Zoom"));

	/* Set a reasonable default width */
	len = gnm_measure_string (
		gtk_widget_get_pango_context (GTK_WIDGET (wbcg->toplevel)),
		entry->style->font_desc,
		"%10000");
	gtk_widget_set_size_request (entry, len, -1);

	/* Preset values */
	for (i = 0; preset_zoom[i] != NULL ; ++i)
		gnm_combo_text_add_item (GNM_COMBO_TEXT (zoom), preset_zoom[i]);

	/* Undo dropdown list */
	undo = wbcg->undo_combo = gnm_combo_stack_new (GTK_STOCK_UNDO, TRUE);
	gnm_combo_box_set_title (GNM_COMBO_BOX (undo), _("Undo"));
	g_signal_connect (G_OBJECT (undo),
		"pop",
		G_CALLBACK (cb_undo_combo), wbcg);

	/* Redo dropdown list */
	redo = wbcg->redo_combo = gnm_combo_stack_new (GTK_STOCK_REDO, TRUE);
	gnm_combo_box_set_title (GNM_COMBO_BOX (redo), _("Redo"));
	g_signal_connect (G_OBJECT (redo),
		"pop",
		G_CALLBACK (cb_redo_combo), wbcg);

#ifdef WITH_BONOBO
	gnumeric_inject_widget_into_bonoboui (wbcg, undo, "/StandardToolbar/EditUndo");
	gnumeric_inject_widget_into_bonoboui (wbcg, redo, "/StandardToolbar/EditRedo");
	gnumeric_inject_widget_into_bonoboui (wbcg, zoom, "/StandardToolbar/SheetZoom");
#else

	wbcg->standard_toolbar = gnumeric_toolbar_new (wbcg,
		workbook_standard_toolbar, "StandardToolbar", 1, 0, 0);
	wbcg->tool_item_cut   = workbook_standard_toolbar [7].widget;
	wbcg->tool_item_copy  = workbook_standard_toolbar [8].widget;
	wbcg->tool_item_paste = workbook_standard_toolbar [9].widget;
	gnumeric_toolbar_insert_with_eventbox (
		GTK_TOOLBAR (wbcg->standard_toolbar),
					       undo, _("Undo the last action"), NULL, TB_UNDO_POS);
	gnumeric_toolbar_insert_with_eventbox (
		GTK_TOOLBAR (wbcg->standard_toolbar),
					       redo, _("Redo the undone action"), NULL, TB_REDO_POS);
	gnumeric_toolbar_append_with_eventbox (
		GTK_TOOLBAR (wbcg->standard_toolbar),
					       zoom, _("Zoom the spreadsheet in or out"), NULL);
	g_signal_connect (G_OBJECT(wbcg->standard_toolbar),
		"orientation-changed",
		G_CALLBACK (workbook_standard_toolbar_orient), wbcg);
	gtk_widget_show (wbcg->standard_toolbar);
#endif
}

static void
cb_cancel_input (GtkWidget *IGNORED, WorkbookControlGUI *wbcg)
{
	wbcg_edit_finish (wbcg, FALSE, NULL);
}

static void
cb_accept_input (GtkWidget *IGNORED, WorkbookControlGUI *wbcg)
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

static void
workbook_setup_edit_area (WorkbookControlGUI *wbcg)
{
	GtkWidget *box, *box2;
	GtkEntry *entry;
	int len;

	wbcg->selection_descriptor     = gtk_entry_new ();

	wbcg_edit_ctor (wbcg);
	entry = wbcg_get_entry (wbcg);

	box           = gtk_hbox_new (0, 0);
	box2          = gtk_hbox_new (0, 0);

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
		G_CALLBACK (wbcg_set_focus), wbcg);

	wbcg_auto_complete_destroy (wbcg);
	wbcg_edit_dtor (wbcg);

#ifdef WITH_BONOBO
	g_hash_table_destroy (wbcg->custom_ui_components);
	bonobo_object_unref (BONOBO_OBJECT (wbcg->uic));
	wbcg->uic = NULL;
#endif

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

#ifdef WITH_BONOBO
static void
setup_progress_bar (WorkbookControlGUI *wbcg)
{
	GtkProgressBar *progress_bar;

	progress_bar = (GTK_PROGRESS_BAR (gtk_progress_bar_new ()));

	gtk_progress_bar_set_orientation (
		progress_bar, GTK_PROGRESS_LEFT_TO_RIGHT);

	wbcg->progress_bar = GTK_WIDGET (progress_bar);

	gnumeric_inject_widget_into_bonoboui (wbcg, wbcg->progress_bar,
					      "/status/Progress");
}
#endif

void
wb_control_gui_set_status_text (WorkbookControlGUI *wbcg, char const *text)
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
		wb_control_gui_set_status_text (wbcg, "END");
	} else {
		wbcg->last_key_was_end = FALSE;
		wb_control_gui_set_status_text (wbcg, "");
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
#ifdef HAVE_GTK_SETTINGS_GET_FOR_SCREEN
	GdkScreen *screen = gtk_widget_get_screen (wbcg->table);
	return gtk_settings_get_for_screen (screen);
#else
	return gtk_settings_get_default ();
#endif
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

/* Setup the autocalc and status labels */
static void
workbook_setup_auto_calc (WorkbookControlGUI *wbcg)
{
	GtkWidget *tmp, *frame1, *frame2;

	wbcg->auto_expr_label = tmp = gtk_button_new_with_label ("");
	GTK_WIDGET_UNSET_FLAGS (tmp, GTK_CAN_FOCUS);
	gtk_widget_ensure_style (tmp);
	gtk_widget_set_size_request (tmp, gnm_measure_string (
					     gtk_widget_get_pango_context (GTK_WIDGET (wbcg->toplevel)),
					     tmp->style->font_desc,
					     "W") * 15, -1);
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
#ifdef WITH_BONOBO
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
#ifdef WITH_BONOBO
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
	int sx, sy;
	gdouble fx, fy;
#ifdef HAVE_GDK_SCREEN_GET_MONITOR_GEOMETRY
	GdkRectangle rect;

	/* In a Xinerama setup, we want the geometry of the actual display
	 * unit, if available. See bug 59902. This call was added for
	 * gtk2.2*/
	gdk_screen_get_monitor_geometry (wbcg->toplevel->screen, 0, &rect);
	sx = MAX (rect.width, 600);
	sy = MAX (rect.height, 200);
#else
	sx = MAX (gdk_screen_width  (), 600);
	sy = MAX (gdk_screen_height (), 200);
#endif

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

static void
wbcg_add_custom_ui (WorkbookControlGUI *wbcg, CustomXmlUI *ui)
{
#ifdef WITH_BONOBO
	BonoboUIComponent *uic;

	uic = bonobo_ui_component_new_default ();
	bonobo_ui_component_set_container (
		uic, bonobo_ui_component_get_container (wbcg->uic), NULL);
	GNM_SLIST_FOREACH (ui->verb_list, char, name,
		bonobo_ui_component_add_verb (uic, name,
					      (BonoboUIVerbFn)(ui->verb_fn),
					      ui->verb_fn_data);
	);
	if (ui->textdomain != NULL) {
		char *old_textdomain;

		old_textdomain = g_strdup (textdomain (NULL));
		textdomain (ui->textdomain);
		bonobo_ui_component_set_translate (uic, "/", ui->xml_ui, NULL);
		textdomain (old_textdomain);
		g_free (old_textdomain);
	} else {
		bonobo_ui_component_set_translate (uic, "/", ui->xml_ui, NULL);
	}
	g_object_set_data (G_OBJECT (uic), "gnumeric-workbook-control-gui", wbcg);
	g_hash_table_insert (wbcg->custom_ui_components, ui, uic);
#endif
}

static void
wbcg_remove_custom_ui (WorkbookControlGUI *wbcg, const CustomXmlUI *ui)
{
#ifdef WITH_BONOBO
	g_hash_table_remove (wbcg->custom_ui_components, ui);
#endif
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

#ifdef WITH_BONOBO
static void
custom_uic_destroy (gpointer data)
{
	BonoboUIComponent *uic = data;

	bonobo_ui_component_unset_container (uic, NULL);
	bonobo_object_unref (BONOBO_OBJECT (uic));
}
#endif

static void
workbook_control_gui_init (WorkbookControlGUI *wbcg,
			   WorkbookView *optional_view,
			   Workbook *optional_wb,
			   GdkScreen *optional_screen)
{
	static GtkTargetEntry const drag_types[] = {
		{ (char *) "text/uri-list", 0, TARGET_URI_LIST },
		{ (char *) "GNUMERIC_SHEET", 0, TARGET_SHEET }
	};

#ifdef WITH_BONOBO
	BonoboUIContainer *ui_container;
#endif
	GtkWidget *tmp;

#ifdef WITH_BONOBO
	tmp  = bonobo_window_new ("Gnumeric", "Gnumeric");
#else
	tmp  = gnome_app_new ("Gnumeric", "Gnumeric");
#endif
	wbcg->toplevel = GTK_WINDOW (tmp);

	if (optional_screen)
		gtk_window_set_screen (wbcg->toplevel, optional_screen);

	wbcg->table    = gtk_table_new (0, 0, 0);
	wbcg->notebook = NULL;
	wbcg->updating_ui = FALSE;

	workbook_control_set_view (&wbcg->wb_control, optional_view, optional_wb);

	workbook_setup_edit_area (wbcg);

#ifndef WITH_BONOBO
	/* Do BEFORE setting up UI bits in the non-bonobo case */
	workbook_setup_status_area (wbcg);

	gnome_app_set_contents (GNOME_APP (wbcg->toplevel), wbcg->table);
	gnome_app_create_menus_with_data (GNOME_APP (wbcg->toplevel), workbook_menu, wbcg);
	gnome_app_install_menu_hints (GNOME_APP (wbcg->toplevel), workbook_menu);

	/* Get the menu items that will be enabled disabled based on
	 * workbook state.
	 */
	wbcg->menu_item_page_setup =
		workbook_menu_file [6].widget;
	wbcg->menu_item_undo =
		workbook_menu_edit [0].widget;
	wbcg->menu_item_redo =
		workbook_menu_edit [1].widget;

	wbcg->menu_item_cut =
		workbook_menu_edit [3].widget;
	wbcg->menu_item_copy =
		workbook_menu_edit [4].widget;
	wbcg->menu_item_paste =
		workbook_menu_edit [5].widget;
	wbcg->menu_item_paste_special =
		workbook_menu_edit [6].widget;
	wbcg->menu_item_search_replace =
		workbook_menu_edit [13].widget;

	wbcg->menu_item_insert_cols =
		workbook_menu_insert [1].widget;
	wbcg->menu_item_insert_rows =
		workbook_menu_insert [2].widget;
	wbcg->menu_item_insert_cells =
		workbook_menu_insert [3].widget;
	wbcg->menu_item_define_name =
		workbook_menu_names [0].widget;

	wbcg->menu_item_consolidate =
		workbook_menu_data [4].widget;
	wbcg->menu_item_freeze_panes =
		workbook_menu_view [2].widget;

	wbcg->menu_item_sheet_display_formulas =
		workbook_menu_format_sheet [3].widget;
	wbcg->menu_item_sheet_hide_zero =
		workbook_menu_format_sheet [4].widget;
	wbcg->menu_item_sheet_hide_grid =
		workbook_menu_format_sheet [5].widget;
	wbcg->menu_item_sheet_hide_col_header =
		workbook_menu_format_sheet [6].widget;
	wbcg->menu_item_sheet_hide_row_header =
		workbook_menu_format_sheet [7].widget;

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

	wbcg->menu_item_auto_filter =
		workbook_menu_data_filter [0].widget;
	wbcg->menu_item_filter_show_all =
		workbook_menu_data_filter [1].widget;

	wbcg->menu_item_sheet_remove =
		workbook_menu_edit_sheet [3].widget;
#else
	bonobo_window_set_contents (BONOBO_WINDOW (wbcg->toplevel), wbcg->table);

	ui_container = bonobo_window_get_ui_container (BONOBO_WINDOW (wbcg->toplevel));
	bonobo_ui_engine_config_set_path (
		bonobo_window_get_ui_engine (BONOBO_WINDOW (wbcg->toplevel)),
		"/Gnumeric/UIConf/kvps");
	wbcg->uic = bonobo_ui_component_new_default ();
	bonobo_ui_component_set_container (wbcg->uic,
		BONOBO_OBJREF (ui_container), NULL);

	bonobo_ui_component_add_verb_list_with_data (wbcg->uic, verbs, wbcg);

	{
		const char *dir = gnumeric_sys_data_dir (NULL);
		bonobo_ui_util_set_ui (wbcg->uic, dir,
			"GNOME_Gnumeric.xml", "gnumeric", NULL);
	}

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

	wbcg_file_history_setup (wbcg);		/* Dynamic history menu items. */

	/*
	 * Initialize the menu items, This will enable insert cols/rows
	 * and paste special to the currently valid values for the
	 * active sheet (if there is an active sheet)
	 */
	wb_view_menus_update (wb_control_view (WORKBOOK_CONTROL (wbcg)));

	/* We are not in edit mode */
	wbcg->wb_control.editing = FALSE;
	wbcg->wb_control.editing_sheet = NULL;
	wbcg->wb_control.editing_cell = NULL;
	wbcg->rangesel = NULL;

	g_signal_connect_after (G_OBJECT (wbcg->toplevel),
		"delete_event",
		G_CALLBACK (wbcg_delete_event), wbcg);
	g_signal_connect_after (G_OBJECT (wbcg->toplevel),
		"set_focus",
		G_CALLBACK (wbcg_set_focus), wbcg);
	g_signal_connect (G_OBJECT (wbcg->toplevel),
		"scroll-event",
		G_CALLBACK (wbcg_scroll_wheel_support_cb), wbcg);
	g_signal_connect (G_OBJECT (wbcg->toplevel),
		"realize",
		G_CALLBACK (cb_realize), wbcg);
	/* Setup a test of Drag and Drop */
	gtk_drag_dest_set (GTK_WIDGET (wbcg_toplevel (wbcg)),
		GTK_DEST_DEFAULT_ALL, drag_types, G_N_ELEMENTS (drag_types),
		GDK_ACTION_COPY | GDK_ACTION_MOVE);
	g_signal_connect (G_OBJECT (wbcg_toplevel (wbcg)),
		"drag_data_received",
		G_CALLBACK (cb_wbcg_drag_data_received), wbcg);
	g_signal_connect (G_OBJECT (wbcg_toplevel (wbcg)),
		"drag_motion", G_CALLBACK (cb_wbcg_drag_motion), wbcg);
	g_signal_connect (G_OBJECT (wbcg_toplevel (wbcg)),
		"drag_leave", G_CALLBACK (cb_wbcg_drag_leave), wbcg);
#if 0
	g_signal_connect (G_OBJECT (gcanvas),
		"drag_data_get",
		G_CALLBACK (wbcg_drag_data_get), WORKBOOK_CONTROL (wbc));
#endif

	g_object_set (G_OBJECT (wbcg->toplevel),
		"allow_grow", TRUE,
		"allow_shrink", TRUE,
		NULL);

	/* Init autosave */
	wbcg->autosave_timer = 0;
	wbcg->autosave_minutes = 0;
	wbcg->autosave_prompt = FALSE;

	wbcg->current_saver = NULL;
	wbcg->font_desc     = NULL;

#ifdef WITH_BONOBO
	wbcg->custom_ui_components =
		g_hash_table_new_full (NULL, NULL, NULL, custom_uic_destroy);
	GNM_SLIST_FOREACH (registered_xml_uis, CustomXmlUI, ui,
		wbcg_add_custom_ui (wbcg, ui);
	);
#endif

	/* Postpone showing the GUI, so that we may resize it freely. */
	g_idle_add ((GSourceFunc) show_gui, wbcg);
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
	cc_class->progress_set		= wbcg_progress_set;
	cc_class->progress_message_set	= wbcg_progress_message_set;
	cc_class->error.error		= wbcg_error_error;
	cc_class->error.error_info	= wbcg_error_error_info;

	wbc_class->control_new		= wbcg_control_new;
	wbc_class->init_state		= wbcg_init_state;
	wbc_class->title_set		= wbcg_title_set;
	wbc_class->prefs_update		= wbcg_prefs_update;
	wbc_class->format_feedback	= wbcg_format_feedback;
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

	wbc_class->undo_redo.clear    = wbcg_undo_redo_clear;
	wbc_class->undo_redo.truncate = wbcg_undo_redo_truncate;
	wbc_class->undo_redo.pop      = wbcg_undo_redo_pop;
	wbc_class->undo_redo.push     = wbcg_undo_redo_push;
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

GSF_CLASS_FULL (WorkbookControlGUI, workbook_control_gui,
		workbook_control_gui_class_init, NULL,
		WORKBOOK_CONTROL_TYPE, 0,
		GSF_INTERFACE (wbcg_go_plot_data_allocator_init, GOG_DATA_ALLOCATOR_TYPE))

WorkbookControl *
workbook_control_gui_new (WorkbookView *optional_view,
			  Workbook *optional_wb,
			  GdkScreen *optional_screen)
{
	WorkbookControlGUI *wbcg;
	WorkbookControl    *wbc;

	wbcg = g_object_new (workbook_control_gui_get_type (), NULL);
	wbc = WORKBOOK_CONTROL (wbcg);
	workbook_control_gui_init (wbcg, optional_view, optional_wb, optional_screen);

	workbook_control_init_state (wbc);

	return wbc;
}

static gboolean
add_ui_to_workbook_controls (Workbook *wb, gpointer ui)
{
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc,
		if (IS_WORKBOOK_CONTROL_GUI (wbc)) {
			wbcg_add_custom_ui (WORKBOOK_CONTROL_GUI (wbc), ui);
		}
	);

	return TRUE;
}

static gboolean
remove_ui_from_workbook_controls (Workbook *wb, gpointer ui)
{
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc,
		if (IS_WORKBOOK_CONTROL_GUI (wbc)) {
			wbcg_remove_custom_ui (WORKBOOK_CONTROL_GUI (wbc), ui);
		}
	);

	return TRUE;
}

CustomXmlUI *
register_xml_ui (const char *xml_ui, const char *textdomain, GSList *verb_list,
		 GCallback /*BonoboUIVerbFn*/ verb_fn, gpointer verb_fn_data)
{
	CustomXmlUI *new_ui;

	new_ui = g_new (CustomXmlUI, 1);
	new_ui->xml_ui = g_strdup (xml_ui);
	new_ui->textdomain = g_strdup (textdomain);
	new_ui->verb_list = g_string_slist_copy (verb_list);
	new_ui->verb_fn = verb_fn;
	new_ui->verb_fn_data = verb_fn_data;

	GNM_SLIST_APPEND (registered_xml_uis, new_ui);
	gnm_app_workbook_foreach (add_ui_to_workbook_controls, new_ui);

	return new_ui;
}

void
unregister_xml_ui (CustomXmlUI *ui)
{
	gnm_app_workbook_foreach (remove_ui_from_workbook_controls, ui);
	GNM_SLIST_REMOVE (registered_xml_uis, ui);
	g_free (ui->xml_ui);
	g_free (ui->textdomain);
	g_slist_free_custom (ui->verb_list, g_free);
	g_free (ui);

}

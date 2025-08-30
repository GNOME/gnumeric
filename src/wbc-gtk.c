/*
 * wbc-gtk.c: A gtk based WorkbookControl
 *
 * Copyright (C) 2000-2007 Jody Goldberg (jody@gnome.org)
 * Copyright (C) 2006-2012 Morten Welinder (terra@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include <wbc-gtk-impl.h>
#include <workbook-view.h>
#include <workbook-priv.h>
#include <gui-util.h>
#include <gutils.h>
#include <gui-file.h>
#include <sheet-control-gui-priv.h>
#include <sheet.h>
#include <sheet-private.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <sheet-conditions.h>
#include <sheet-filter.h>
#include <commands.h>
#include <dependent.h>
#include <application.h>
#include <history.h>
#include <func.h>
#include <value.h>
#include <style-font.h>
#include <gnm-format.h>
#include <expr.h>
#include <style-color.h>
#include <style-border.h>
#include <gnumeric-conf.h>
#include <dialogs/dialogs.h>
#include <gui-clipboard.h>
#include <libgnumeric.h>
#include <gnm-pane-impl.h>
#include <graph.h>
#include <selection.h>
#include <file-autoft.h>
#include <ranges.h>
#include <tools/analysis-auto-expression.h>
#include <sheet-object-cell-comment.h>
#include <print-info.h>
#include <expr-name.h>

#include <goffice/goffice.h>
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-doc-meta-data.h>
#include <gdk/gdkkeysyms-compat.h>
#include <gnm-i18n.h>
#include <string.h>

#define GET_GUI_ITEM(i_) (gpointer)(gtk_builder_get_object(wbcg->gui, (i_)))

#define	SHEET_CONTROL_KEY "SheetControl"

#define AUTO_EXPR_SAMPLE "Sumerage = -012345678901234"


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


static gboolean debug_tab_order;
static char const *uifilename = NULL;
static GnmActionEntry const *extra_actions = NULL;
static int extra_actions_nb;
static guint wbc_gtk_signals[WBC_GTK_LAST_SIGNAL];
static GObjectClass *parent_class = NULL;

static gboolean
wbcg_ui_update_begin (WBCGtk *wbcg)
{
	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), FALSE);
	g_return_val_if_fail (!wbcg->updating_ui, FALSE);

	return (wbcg->updating_ui = TRUE);
}

static void
wbcg_ui_update_end (WBCGtk *wbcg)
{
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));
	g_return_if_fail (wbcg->updating_ui);

	wbcg->updating_ui = FALSE;
}

/****************************************************************************/

G_MODULE_EXPORT void
set_uifilename (char const *name, GnmActionEntry const *actions, int nb)
{
	uifilename = name;
	extra_actions = actions;
	extra_actions_nb = nb;
}

/**
 * wbcg_find_action:
 * @wbcg: the workbook control gui
 * @name: name of action
 *
 * Returns: (transfer none): The action with the given name
 **/
GtkAction *
wbcg_find_action (WBCGtk *wbcg, const char *name)
{
	GtkAction *a;

	a = gtk_action_group_get_action (wbcg->actions, name);
	if (a == NULL)
		a = gtk_action_group_get_action (wbcg->permanent_actions, name);
	if (a == NULL)
		a = gtk_action_group_get_action (wbcg->semi_permanent_actions, name);
	if (a == NULL)
		a = gtk_action_group_get_action (wbcg->data_only_actions, name);
	if (a == NULL)
		a = gtk_action_group_get_action (wbcg->font_actions, name);
	if (a == NULL)
		a = gtk_action_group_get_action (wbcg->toolbar.actions, name);

	return a;
}

static void
wbc_gtk_set_action_sensitivity (WBCGtk *wbcg,
				char const *action, gboolean sensitive)
{
	GtkAction *a = wbcg_find_action (wbcg, action);
	g_object_set (G_OBJECT (a), "sensitive", sensitive, NULL);
}

/* NOTE : The semantics of prefix and suffix seem contrived.  Why are we
 * handling it at this end ?  That stuff should be done in the undo/redo code
 **/
static void
wbc_gtk_set_action_label (WBCGtk *wbcg,
			  char const *action,
			  char const *prefix,
			  char const *suffix,
			  char const *new_tip)
{
	GtkAction *a = wbcg_find_action (wbcg, action);

	if (prefix != NULL) {
		char *text;
		gboolean is_suffix = (suffix != NULL);

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
wbcg_set_action_feedback (WBCGtk *wbcg,
			  GtkToggleAction *action, gboolean active)
{
	guint sig;
	gulong handler;
	gboolean debug = FALSE;
	const char *name = gtk_action_get_name (GTK_ACTION (action));

	if (active == gtk_toggle_action_get_active (action))
		return;

	sig = wbcg->updating_ui
		? g_signal_lookup ("activate", G_TYPE_FROM_INSTANCE (action))
		: 0;
	handler = sig
		? g_signal_handler_find (action, G_SIGNAL_MATCH_ID, sig,
					 0, NULL, NULL, NULL)
		: 0;
	if (handler) {
		if (debug)
			g_printerr ("Blocking signal %d for %s\n", sig, name);
		g_signal_handler_block (action, handler);
	}
	gtk_toggle_action_set_active (action, active);
	if (sig) {
		if (debug)
			g_printerr ("Unblocking signal %d %s\n", sig, name);
		g_signal_handler_unblock (action, handler);
	}
}

static void
wbc_gtk_set_toggle_action_state (WBCGtk *wbcg,
				 char const *action, gboolean state)
{
	GtkAction *a = wbcg_find_action (wbcg, action);
	wbcg_set_action_feedback (wbcg, GTK_TOGGLE_ACTION (a), state);
}

/****************************************************************************/

static SheetControlGUI *
wbcg_get_scg (WBCGtk *wbcg, Sheet *sheet)
{
	SheetControlGUI *scg;
	int i, npages;

	if (sheet == NULL || wbcg->snotebook == NULL)
		return NULL;

	npages = wbcg_get_n_scg (wbcg);
	if (npages == 0) {
		/*
		 * This can happen during construction when the clipboard is
		 * being cleared.  Ctrl-C Ctrl-Q.
		 */
		return NULL;
	}

	g_return_val_if_fail (IS_SHEET (sheet), NULL);
	g_return_val_if_fail (sheet->index_in_wb >= 0, NULL);

	scg = wbcg_get_nth_scg (wbcg, sheet->index_in_wb);
	if (NULL != scg && scg_sheet (scg) == sheet)
		return scg;

	/*
	 * index_in_wb is probably not accurate because we are in the
	 * middle of removing or adding a sheet.
	 */
	for (i = 0; i < npages; i++) {
		scg = wbcg_get_nth_scg (wbcg, i);
		if (NULL != scg && scg_sheet (scg) == sheet)
			return scg;
	}

	g_warning ("Failed to find scg for sheet %s", sheet->name_quoted);
	return NULL;
}

static SheetControlGUI *
get_scg (const GtkWidget *w)
{
	return g_object_get_data (G_OBJECT (w), SHEET_CONTROL_KEY);
}

static GSList *
get_all_scgs (WBCGtk *wbcg)
{
	int i, n = gtk_notebook_get_n_pages (wbcg->snotebook);
	GSList *l = NULL;

	for (i = 0; i < n; i++) {
		GtkWidget *w = gtk_notebook_get_nth_page (wbcg->snotebook, i);
		SheetControlGUI *scg = get_scg (w);
		l = g_slist_prepend (l, scg);
	}

	return g_slist_reverse (l);
}

/* Autosave */

static gboolean
cb_autosave (WBCGtk *wbcg)
{
	WorkbookView *wb_view;

	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), FALSE);

	wb_view = wb_control_view (GNM_WBC (wbcg));

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
 * wbcg_rangesel_possible:
 * @wbcg: the workbook control gui
 *
 * Returns true if the cursor keys should be used to select
 * a cell range (if the cursor is in a spot in the expression
 * where it makes sense to have a cell reference), false if not.
 **/
gboolean
wbcg_rangesel_possible (WBCGtk const *wbcg)
{
	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), FALSE);

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
	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), FALSE);
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
		(scg->selected_objects != NULL || wbcg->new_object != NULL ||
		 scg_sheet (scg)->sheet_type == GNM_SHEET_OBJECT);
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

	if (wbcg->snotebook) {
		gboolean tab_context_menu =
			enable_actions ||
			scg_sheet (scg)->sheet_type == GNM_SHEET_OBJECT;
		int i, N = wbcg_get_n_scg (wbcg);
		for (i = 0; i < N; i++) {
			GtkWidget *label =
				gnm_notebook_get_nth_label (wbcg->bnotebook, i);
			g_object_set_data (G_OBJECT (label), "editable",
					   GINT_TO_POINTER (tab_context_menu));
		}
	}

	g_object_set (G_OBJECT (wbcg->actions),
		"sensitive", enable_actions,
		NULL);
	g_object_set (G_OBJECT (wbcg->font_actions),
		"sensitive", enable_actions || enable_edit_ok_cancel,
		NULL);

	if (scg && scg_sheet (scg)->sheet_type == GNM_SHEET_OBJECT) {
		g_object_set (G_OBJECT (wbcg->data_only_actions),
			"sensitive", FALSE,
			NULL);
		g_object_set (G_OBJECT (wbcg->semi_permanent_actions),
			"sensitive",TRUE,
			NULL);
		gtk_widget_set_sensitive (GTK_WIDGET (wbcg->edit_line.entry), FALSE);
		gtk_widget_set_sensitive (GTK_WIDGET (wbcg->selection_descriptor), FALSE);
	} else {
		g_object_set (G_OBJECT (wbcg->data_only_actions),
			"sensitive", TRUE,
			NULL);
		g_object_set (G_OBJECT (wbcg->semi_permanent_actions),
			"sensitive", enable_actions,
			NULL);
		gtk_widget_set_sensitive (GTK_WIDGET (wbcg->edit_line.entry), TRUE);
		gtk_widget_set_sensitive (GTK_WIDGET (wbcg->selection_descriptor), TRUE);
	}
}

void
wbcg_insert_sheet (GtkWidget *unused, WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Workbook *wb = sheet->workbook;
	WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
	/* Use same size as current sheet.  */
	workbook_sheet_add (wb, sheet->index_in_wb,
			    gnm_sheet_get_max_cols (sheet),
			    gnm_sheet_get_max_rows (sheet));
	cmd_reorganize_sheets (wbc, old_state, sheet);
}

void
wbcg_append_sheet (GtkWidget *unused, WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	Sheet *sheet = wb_control_cur_sheet (wbc);
	Workbook *wb = sheet->workbook;
	WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
	/* Use same size as current sheet.  */
	workbook_sheet_add (wb, -1,
			    gnm_sheet_get_max_cols (sheet),
			    gnm_sheet_get_max_rows (sheet));
	cmd_reorganize_sheets (wbc, old_state, sheet);
}

void
wbcg_clone_sheet (GtkWidget *unused, WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
cb_show_sheet (SheetControlGUI *scg)
{
	WBCGtk *wbcg = scg->wbcg;
	int page_number = gtk_notebook_page_num (wbcg->snotebook,
						 GTK_WIDGET (scg->grid));
	gnm_notebook_set_current_page (wbcg->bnotebook, page_number);
}



static void cb_sheets_manage (SheetControlGUI *scg) { dialog_sheet_order (scg->wbcg); }
static void cb_sheets_insert (SheetControlGUI *scg) { wbcg_insert_sheet (NULL, scg->wbcg); }
static void cb_sheets_add    (SheetControlGUI *scg) { wbcg_append_sheet (NULL, scg->wbcg); }
static void cb_sheets_clone  (SheetControlGUI *scg) { wbcg_clone_sheet  (NULL, scg->wbcg); }
static void cb_sheets_rename (SheetControlGUI *scg) { dialog_sheet_rename (scg->wbcg, scg_sheet (scg)); }
static void cb_sheets_resize (SheetControlGUI *scg) { dialog_sheet_resize (scg->wbcg); }


static gint
cb_by_scg_sheet_name (gconstpointer a_, gconstpointer b_)
{
	const SheetControlGUI *a = a_;
	const SheetControlGUI *b = b_;
	Sheet *sa = scg_sheet (a);
	Sheet *sb = scg_sheet (b);

	return g_utf8_collate (sa->name_unquoted, sb->name_unquoted);
}


static void
sheet_menu_label_run (SheetControlGUI *scg, GdkEvent *event)
{
	enum { CM_MULTIPLE = 1, CM_DATA_SHEET = 2 };
	struct SheetTabMenu {
		char const *text;
		void (*function) (SheetControlGUI *scg);
		int flags;
		int submenu;
	} const sheet_label_context_actions [] = {
		{ N_("Manage Sheets..."), &cb_sheets_manage,	0, 0},
		{ NULL, NULL, 0, 0 },
		{ N_("Insert"),		  &cb_sheets_insert,	0, 0 },
		{ N_("Append"),		  &cb_sheets_add,	0, 0 },
		{ N_("Duplicate"),	  &cb_sheets_clone,	0, 0 },
		{ N_("Remove"),		  &scg_delete_sheet_if_possible, CM_MULTIPLE, 0 },
		{ N_("Rename"),		  &cb_sheets_rename,	0, 0 },
		{ N_("Resize..."),        &cb_sheets_resize,    CM_DATA_SHEET, 0 },
		{ N_("Select"),           NULL,                 0, 1 },
		{ N_("Select (sorted)"),  NULL,                 0, 2 }
	};

	unsigned int ui;
	GtkWidget *item, *menu = gtk_menu_new ();
	GtkWidget *guru = wbc_gtk_get_guru (scg_wbcg (scg));
	unsigned int N_visible, pass;
	GtkWidget *submenus[2 + 1];
	GSList *scgs = get_all_scgs (scg->wbcg);

	for (pass = 1; pass <= 2; pass++) {
		GSList *l;

		submenus[pass] = gtk_menu_new ();
		N_visible = 0;
		for (l = scgs; l; l = l->next) {
			SheetControlGUI *scg1 = l->data;
			Sheet *sheet = scg_sheet (scg1);
			if (!sheet_is_visible (sheet))
				continue;

			N_visible++;

			item = gtk_menu_item_new_with_label (sheet->name_unquoted);
			g_signal_connect_swapped (G_OBJECT (item), "activate",
						  G_CALLBACK (cb_show_sheet), scg1);
			gtk_menu_shell_append (GTK_MENU_SHELL (submenus[pass]), item);
			gtk_widget_show (item);
		}

		scgs = g_slist_sort (scgs, cb_by_scg_sheet_name);
	}
	g_slist_free (scgs);

	for (ui = 0; ui < G_N_ELEMENTS (sheet_label_context_actions); ui++) {
		const struct SheetTabMenu *it =
			sheet_label_context_actions + ui;
		gboolean inactive =
			((it->flags & CM_MULTIPLE) && N_visible <= 1) ||
			((it->flags & CM_DATA_SHEET) && scg_sheet (scg)->sheet_type != GNM_SHEET_DATA) ||
			(!it->submenu && guru != NULL);

		item = it->text
			? gtk_menu_item_new_with_label (_(it->text))
			: gtk_separator_menu_item_new ();
		if (it->function)
			g_signal_connect_swapped (G_OBJECT (item), "activate",
						  G_CALLBACK (it->function), scg);
		if (it->submenu)
			gtk_menu_item_set_submenu (GTK_MENU_ITEM (item),
						   submenus[it->submenu]);

		gtk_widget_set_sensitive (item, !inactive);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
}

/**
 * cb_sheet_label_button_press:
 *
 * Invoked when the user has clicked on the sheet name widget.
 * This takes care of switching to the notebook that contains the label
 */
static gboolean
cb_sheet_label_button_press (GtkWidget *widget, GdkEvent *event,
			     SheetControlGUI *scg)
{
	WBCGtk *wbcg = scg->wbcg;
	gint page_number;

	if (event->type != GDK_BUTTON_PRESS)
		return FALSE;

	page_number = gtk_notebook_page_num (wbcg->snotebook,
					     GTK_WIDGET (scg->grid));
	gnm_notebook_set_current_page (wbcg->bnotebook, page_number);

	if (event->button.button == 1 || NULL != wbcg->rangesel)
		return FALSE;

	if (event->button.button == 3) {
		if ((scg_wbcg (scg))->edit_line.guru == NULL)
			scg_object_unselect (scg, NULL);
		if (g_object_get_data (G_OBJECT (widget), "editable")) {
			sheet_menu_label_run (scg, event);
			scg_take_focus (scg);
			return TRUE;
		}
	}

	return FALSE;
}

static void
cb_sheet_label_drag_data_get (GtkWidget *widget, GdkDragContext *context,
			      GtkSelectionData *selection_data,
			      guint info, guint time)
{
	SheetControlGUI *scg = get_scg (widget);
	g_return_if_fail (GNM_IS_SCG (scg));

	scg_drag_data_get (scg, selection_data);
}

static void
cb_sheet_label_drag_data_received (GtkWidget *widget, GdkDragContext *context,
				   gint x, gint y, GtkSelectionData *data, guint info, guint time,
				   WBCGtk *wbcg)
{
	GtkWidget *w_source;
	SheetControlGUI *scg_src, *scg_dst;
	Sheet *s_src, *s_dst;

	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));
	g_return_if_fail (GTK_IS_WIDGET (widget));

	w_source = gtk_drag_get_source_widget (context);
	if (!w_source) {
		g_warning ("Not yet implemented!"); /* Different process */
		return;
	}

	scg_src = get_scg (w_source);
	g_return_if_fail (scg_src != NULL);
	s_src = scg_sheet (scg_src);

	scg_dst = get_scg (widget);
	g_return_if_fail (scg_dst != NULL);
	s_dst = scg_sheet (scg_dst);

	if (s_src == s_dst) {
		/* Nothing */
	} else if (s_src->workbook == s_dst->workbook) {
		/* Move within workbook */
		Workbook *wb = s_src->workbook;
		int p_src = s_src->index_in_wb;
		int p_dst = s_dst->index_in_wb;
		WorkbookSheetState *old_state = workbook_sheet_state_new (wb);
		workbook_sheet_move (s_src, p_dst - p_src);
		cmd_reorganize_sheets (GNM_WBC (wbcg),
				       old_state,
				       s_src);
	} else {
		g_return_if_fail (GNM_IS_SCG (gtk_selection_data_get_data (data)));

		/* Different workbook, same process */
		g_warning ("Not yet implemented!");
	}
}

/*
 * Not currently reachable, I believe.  We use the notebook's dragging.
 */
static void
cb_sheet_label_drag_begin (GtkWidget *widget, GdkDragContext *context,
			   WBCGtk *wbcg)
{
	GtkWidget *arrow, *image;

	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	/* Create the arrow. */
	arrow = gtk_window_new (GTK_WINDOW_POPUP);
	gtk_window_set_screen (GTK_WINDOW (arrow),
			       gtk_widget_get_screen (widget));
	gtk_widget_realize (arrow);
	image = gtk_image_new_from_resource ("/org/gnumeric/gnumeric/images/sheet_move_marker.png");
	gtk_widget_show (image);
	gtk_container_add (GTK_CONTAINER (arrow), image);
	g_object_ref_sink (arrow);
	g_object_set_data (G_OBJECT (widget), "arrow", arrow);
}

static void
cb_sheet_label_drag_end (GtkWidget *widget, GdkDragContext *context,
			 WBCGtk *wbcg)
{
	GtkWidget *arrow;

	g_return_if_fail (GNM_IS_WBC (wbcg));

	/* Destroy the arrow. */
	arrow = g_object_get_data (G_OBJECT (widget), "arrow");
	gtk_widget_destroy (arrow);
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
	SheetControlGUI *scg_src, *scg_dst;
	GtkWidget *w_source, *arrow, *window;
	gint root_x, root_y, pos_x, pos_y;
	GtkAllocation wa, wsa;

	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), FALSE);
	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), FALSE);

	/* Make sure we are really hovering over another label. */
	w_source = gtk_drag_get_source_widget (context);
	if (!w_source)
		return FALSE;

	arrow = g_object_get_data (G_OBJECT (w_source), "arrow");

	scg_src = get_scg (w_source);
	scg_dst = get_scg (widget);

	if (scg_src == scg_dst) {
		gtk_widget_hide (arrow);
		return (FALSE);
	}

	/* Move the arrow to the correct position and show it. */
	window = gtk_widget_get_ancestor (widget, GTK_TYPE_WINDOW);
	gtk_window_get_position (GTK_WINDOW (window), &root_x, &root_y);
	gtk_widget_get_allocation (widget ,&wa);
	pos_x = root_x + wa.x;
	pos_y = root_y + wa.y;
	gtk_widget_get_allocation (w_source ,&wsa);
	if (wsa.x < wa.x)
		pos_x += wa.width;
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
wbcg_set_direction (SheetControlGUI const *scg)
{
	GtkWidget *w = (GtkWidget *)scg->wbcg->snotebook;
	gboolean text_is_rtl = scg_sheet (scg)->text_is_rtl;
	GtkTextDirection dir = text_is_rtl
		? GTK_TEXT_DIR_RTL
		: GTK_TEXT_DIR_LTR;

	if (dir != gtk_widget_get_direction (w))
		set_dir (w, &dir);
	if (scg->hs)
		g_object_set (scg->hs, "inverted", text_is_rtl, NULL);
}

static void
cb_direction_change (G_GNUC_UNUSED Sheet *null_sheet,
		     G_GNUC_UNUSED GParamSpec *null_pspec,
		     SheetControlGUI const *scg)
{
	if (scg && scg == wbcg_cur_scg (scg->wbcg))
		wbcg_set_direction (scg);
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
		go_action_combo_text_set_entry (wbcg->zoom_haction, label,
			GO_ACTION_COMBO_SEARCH_CURRENT);
		g_free (label);
		wbcg_ui_update_end (wbcg);
	}
}

static void
cb_notebook_switch_page (G_GNUC_UNUSED GtkNotebook *notebook_,
			 G_GNUC_UNUSED GtkWidget *page_,
			 guint page_num, WBCGtk *wbcg)
{
	Sheet *sheet;
	SheetControlGUI *new_scg;

	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	/* Ignore events during destruction */
	if (wbcg->snotebook == NULL)
		return;

	if (debug_tab_order)
		g_printerr ("Notebook page switch\n");

	/* While initializing adding the sheets will trigger page changes, but
	 * we do not actually want to change the focus sheet for the view
	 */
	if (wbcg->updating_ui)
		return;

	/* If we are not at a subexpression boundary then finish editing */
	if (NULL != wbcg->rangesel)
		scg_rangesel_stop (wbcg->rangesel, TRUE);

	/*
	 * Make snotebook follow bnotebook.  This should be the only place
	 * that changes pages for snotebook.
	 */
	gtk_notebook_set_current_page (wbcg->snotebook, page_num);

	new_scg = wbcg_get_nth_scg (wbcg, page_num);
	wbcg_set_direction (new_scg);

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
	 * Make absolutely sure the expression doesn't get 'lost',
	 * if it's invalid then prompt the user and don't switch
	 * the notebook page.
	 */
	if (wbcg_is_editing (wbcg)) {
		guint prev = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (wbcg->snotebook),
								 "previous_page"));

		if (prev == page_num)
			return;

		if (!wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, NULL))
			gnm_notebook_set_current_page (wbcg->bnotebook,
						       prev);
		else
			/* Looks silly, but is really necessary */
			gnm_notebook_set_current_page (wbcg->bnotebook,
						       page_num);

		return;
	}

	g_object_set_data (G_OBJECT (wbcg->snotebook), "previous_page",
			   GINT_TO_POINTER (gtk_notebook_get_current_page (wbcg->snotebook)));

	/* if we are not selecting a range for an expression update */
	sheet = wbcg_focus_cur_scg (wbcg);
	if (sheet != wbcg_cur_sheet (wbcg)) {
		wbcg_update_menu_feedback (wbcg, sheet);
		sheet_flag_status_update_range (sheet, NULL);
		sheet_update (sheet);
		wb_view_sheet_focus (wb_control_view (GNM_WBC (wbcg)), sheet);
		cb_zoom_change (sheet, NULL, wbcg);
	}
}

static gboolean
cb_bnotebook_button_press (GtkWidget *widget, GdkEventButton *event)
{
	if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
		/*
		 * Eat the click so cb_paned_button_press doesn't see it.
		 * see bug #607794.
		 */
		return TRUE;
	}

	return FALSE;
}

static void
cb_bnotebook_page_reordered (GtkNotebook *notebook, GtkWidget *child,
			     int page_num, WBCGtk *wbcg)
{
	GtkNotebook *snotebook = GTK_NOTEBOOK (wbcg->snotebook);
	int old = gtk_notebook_get_current_page (snotebook);

	if (wbcg->updating_ui)
		return;

	if (debug_tab_order)
		g_printerr ("Reordered %d -> %d\n", old, page_num);

	if (old != page_num) {
		WorkbookControl * wbc = GNM_WBC (wbcg);
		Workbook *wb = wb_control_get_workbook (wbc);
		Sheet *sheet = workbook_sheet_by_index (wb, old);
		WorkbookSheetState * old_state = workbook_sheet_state_new(wb);
		workbook_sheet_move (sheet, page_num - old);
		cmd_reorganize_sheets (wbc, old_state, sheet);
	}
}


static void
wbc_gtk_create_notebook_area (WBCGtk *wbcg)
{
	GtkWidget *placeholder;
	GtkStyleContext *context;

	wbcg->bnotebook = g_object_new (GNM_NOTEBOOK_TYPE,
					"can-focus", FALSE,
					NULL);
	g_object_ref (wbcg->bnotebook);
	context = gtk_widget_get_style_context (GTK_WIDGET (wbcg->bnotebook));
	gtk_style_context_add_class (context, "buttons");

	g_signal_connect_after (G_OBJECT (wbcg->bnotebook),
		"switch_page",
		G_CALLBACK (cb_notebook_switch_page), wbcg);
	g_signal_connect (G_OBJECT (wbcg->bnotebook),
			  "button-press-event",
			  G_CALLBACK (cb_bnotebook_button_press),
			  NULL);
	g_signal_connect (G_OBJECT (wbcg->bnotebook),
			  "page-reordered",
			  G_CALLBACK (cb_bnotebook_page_reordered),
			  wbcg);
	placeholder = gtk_paned_get_child1 (wbcg->tabs_paned);
	if (placeholder)
		gtk_widget_destroy (placeholder);
	gtk_paned_pack1 (wbcg->tabs_paned, GTK_WIDGET (wbcg->bnotebook), FALSE, TRUE);

	gtk_widget_show_all (GTK_WIDGET (wbcg->tabs_paned));
}


static void
wbcg_menu_state_sheet_count (WBCGtk *wbcg)
{
	int const sheet_count = gnm_notebook_get_n_visible (wbcg->bnotebook);
	/* Should we enable commands requiring multiple sheets */
	gboolean const multi_sheet = (sheet_count > 1);

	wbc_gtk_set_action_sensitivity (wbcg, "SheetRemove", multi_sheet);
}

static void
cb_sheet_direction_change (Sheet *sheet,
			   G_GNUC_UNUSED GParamSpec *pspec,
			   GtkAction *a)
{
	g_object_set (a,
		      "icon-name", (sheet->text_is_rtl
				    ? "format-text-direction-rtl"
				    : "format-text-direction-ltr"),
		      NULL);
}

static void
cb_sheet_tab_change (Sheet *sheet,
		     G_GNUC_UNUSED GParamSpec *pspec,
		     GtkWidget *widget)
{
	GdkRGBA cfore, cback;
	SheetControlGUI *scg = get_scg (widget);

	g_return_if_fail (GNM_IS_SCG (scg));

	/* We're lazy and just set all relevant attributes.  */
	g_object_set (widget,
		      "label", sheet->name_unquoted,
		      "background-color",
		      (sheet->tab_color
		       ? go_color_to_gdk_rgba (sheet->tab_color->go_color,
					       &cback)
		       : NULL),
		      "text-color",
		      (sheet->tab_text_color
		       ? go_color_to_gdk_rgba (sheet->tab_text_color->go_color,
					       &cfore)
		       : NULL),
		      NULL);
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
			    SheetControlGUI *scg)
{
	gboolean viz;

	g_return_if_fail (GNM_IS_SCG (scg));

	viz = sheet_is_visible (sheet);
	gtk_widget_set_visible (GTK_WIDGET (scg->grid), viz);
	gtk_widget_set_visible (GTK_WIDGET (scg->label), viz);

	wbcg_menu_state_sheet_count (scg->wbcg);
}

static void
disconnect_sheet_focus_signals (WBCGtk *wbcg)
{
	SheetControlGUI *scg = wbcg->active_scg;
	Sheet *sheet;

	if (!scg)
		return;

	sheet = scg_sheet (scg);

#if 0
	g_printerr ("Disconnecting focus for %s with scg=%p\n", sheet->name_unquoted, scg);
#endif

	g_signal_handlers_disconnect_by_func (sheet, cb_toggle_menu_item_changed, wbcg);
	g_signal_handlers_disconnect_by_func (sheet, cb_direction_change, scg);
	g_signal_handlers_disconnect_by_func (sheet, cb_zoom_change, wbcg);

	wbcg->active_scg = NULL;
}

static void
disconnect_sheet_signals (SheetControlGUI *scg)
{
	WBCGtk *wbcg = scg->wbcg;
	Sheet *sheet = scg_sheet (scg);

	if (scg == wbcg->active_scg)
		disconnect_sheet_focus_signals (wbcg);

#if 0
	g_printerr ("Disconnecting all for %s with scg=%p\n", sheet->name_unquoted, scg);
#endif

	g_signal_handlers_disconnect_by_func (sheet, cb_sheet_direction_change,
					      wbcg_find_action (wbcg, "SheetDirection"));
	g_signal_handlers_disconnect_by_func (sheet, cb_sheet_tab_change, scg->label);
	g_signal_handlers_disconnect_by_func (sheet, cb_sheet_visibility_change, scg);
}

static void
wbcg_sheet_add (WorkbookControl *wbc, SheetView *sv)
{
	static GtkTargetEntry const drag_types[] = {
		{ (char *)"GNUMERIC_SHEET", GTK_TARGET_SAME_APP, TARGET_SHEET },
		{ (char *)"UTF8_STRING", 0, 0 },
		{ (char *)"image/svg+xml", 0, 0 },
		{ (char *)"image/x-wmf", 0, 0 },
		{ (char *)"image/x-emf", 0, 0 },
		{ (char *)"image/png", 0, 0 },
		{ (char *)"image/jpeg", 0, 0 },
		{ (char *)"image/bmp", 0, 0 }
	};

	WBCGtk *wbcg = (WBCGtk *)wbc;
	SheetControlGUI *scg;
	Sheet		*sheet   = sv_sheet (sv);
	gboolean	 visible = sheet_is_visible (sheet);

	g_return_if_fail (wbcg != NULL);

	scg = sheet_control_gui_new (sv, wbcg);

	g_object_set_data (G_OBJECT (scg->grid), SHEET_CONTROL_KEY, scg);

	g_object_set_data (G_OBJECT (scg->label), SHEET_CONTROL_KEY, scg);

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
		"signal::drag_data_get", G_CALLBACK (cb_sheet_label_drag_data_get), NULL,
		"signal::drag_data_received", G_CALLBACK (cb_sheet_label_drag_data_received), wbcg,
		"signal::drag_motion", G_CALLBACK (cb_sheet_label_drag_motion), wbcg,
		NULL);

	gtk_widget_show (scg->label);
	gtk_widget_show_all (GTK_WIDGET (scg->grid));
	if (!visible) {
		gtk_widget_hide (GTK_WIDGET (scg->grid));
		gtk_widget_hide (GTK_WIDGET (scg->label));
	}
	g_object_connect (G_OBJECT (sheet),
			  "signal::notify::visibility", cb_sheet_visibility_change, scg,
			  "signal::notify::name", cb_sheet_tab_change, scg->label,
			  "signal::notify::tab-foreground", cb_sheet_tab_change, scg->label,
			  "signal::notify::tab-background", cb_sheet_tab_change, scg->label,
			  "signal::notify::text-is-rtl", cb_sheet_direction_change, wbcg_find_action (wbcg, "SheetDirection"),
			  NULL);

	if (wbcg_ui_update_begin (wbcg)) {
		/*
		 * Just let wbcg_sheet_order_changed deal with where to put
		 * it.
		 */
		int pos = -1;
		gtk_notebook_insert_page (wbcg->snotebook,
					  GTK_WIDGET (scg->grid), NULL,
					  pos);
		gnm_notebook_insert_tab (wbcg->bnotebook,
					 GTK_WIDGET (scg->label),
					 pos);
		wbcg_menu_state_sheet_count (wbcg);
		wbcg_ui_update_end (wbcg);
	}

	scg_adjust_preferences (scg);
	if (sheet == wb_control_cur_sheet (wbc)) {
		scg_take_focus (scg);
		wbcg_set_direction (scg);
		cb_zoom_change (sheet, NULL, wbcg);
		cb_toggle_menu_item_changed (sheet, NULL, wbcg);
	}
}

static void
wbcg_sheet_remove (WorkbookControl *wbc, Sheet *sheet)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	SheetControlGUI *scg = wbcg_get_scg (wbcg, sheet);

	/* During destruction we may have already removed the notebook */
	if (scg == NULL)
		return;

	disconnect_sheet_signals (scg);

	gtk_widget_destroy (GTK_WIDGET (scg->label));
	gtk_widget_destroy (GTK_WIDGET (scg->grid));

	wbcg_menu_state_sheet_count (wbcg);
}

static void
wbcg_sheet_focus (WorkbookControl *wbc, Sheet *sheet)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	SheetControlGUI *scg = wbcg_get_scg (wbcg, sheet);

	if (scg) {
		int n = gtk_notebook_page_num (wbcg->snotebook,
					       GTK_WIDGET (scg->grid));
		gnm_notebook_set_current_page (wbcg->bnotebook, n);

		if (wbcg->rangesel == NULL)
			gnm_expr_entry_set_scg (wbcg->edit_line.entry, scg);
	}

	disconnect_sheet_focus_signals (wbcg);

	if (sheet) {
		wbcg_update_menu_feedback (wbcg, sheet);

		if (scg)
			wbcg_set_direction (scg);

#if 0
		g_printerr ("Connecting for %s with scg=%p\n", sheet->name_unquoted, scg);
#endif

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

		wbcg->active_scg = scg;
	}
}

static gint
by_sheet_index (gconstpointer a, gconstpointer b)
{
	SheetControlGUI *scga = (SheetControlGUI *)a;
	SheetControlGUI *scgb = (SheetControlGUI *)b;
	return scg_sheet (scga)->index_in_wb - scg_sheet (scgb)->index_in_wb;
}

static void
wbcg_sheet_order_changed (WBCGtk *wbcg)
{
	if (wbcg_ui_update_begin (wbcg)) {
		GSList *l, *scgs;
		int i;

		/* Reorder all tabs so they end up in index_in_wb order. */
		scgs = g_slist_sort (get_all_scgs (wbcg), by_sheet_index);

		for (i = 0, l = scgs; l; l = l->next, i++) {
			SheetControlGUI *scg = l->data;
			gtk_notebook_reorder_child (wbcg->snotebook,
						    GTK_WIDGET (scg->grid),
						    i);
			gnm_notebook_move_tab (wbcg->bnotebook,
					       GTK_WIDGET (scg->label),
					       i);
		}
		g_slist_free (scgs);

		wbcg_ui_update_end (wbcg);
	}
}

static void
wbcg_update_title (WBCGtk *wbcg)
{
	GODoc *doc = wb_control_get_doc (GNM_WBC (wbcg));
	char *basename = doc->uri ? go_basename_from_uri (doc->uri) : NULL;
	char *title = g_strconcat
		(go_doc_is_dirty (doc) ? "*" : "",
		 basename ? basename : doc->uri,
#ifdef GNM_WITH_DECIMAL64
		 _(" - Gnumeric [Decimal]"),
#else
		 _(" - Gnumeric"),
#endif
		 NULL);
	gtk_window_set_title (wbcg_toplevel (wbcg), title);
	g_free (title);
	g_free (basename);
}

static void
wbcg_sheet_remove_all (WorkbookControl *wbc)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;

	if (wbcg->snotebook != NULL) {
		GtkNotebook *tmp = wbcg->snotebook;
		GSList *l, *all = get_all_scgs (wbcg);
		SheetControlGUI *current = wbcg_cur_scg (wbcg);

		/* Clear notebook to disable updates as focus changes for pages
		 * during destruction */
		wbcg->snotebook = NULL;

		/* Be sure we are no longer editing */
		wbcg_edit_finish (wbcg, WBC_EDIT_REJECT, NULL);

		for (l = all; l; l = l->next) {
			SheetControlGUI *scg = l->data;
			disconnect_sheet_signals (scg);
			if (scg != current) {
				gtk_widget_destroy (GTK_WIDGET (scg->label));
				gtk_widget_destroy (GTK_WIDGET (scg->grid));
			}
		}

		g_slist_free (all);

		/* Do current scg last.  */
		if (current) {
			gtk_widget_destroy (GTK_WIDGET (current->label));
			gtk_widget_destroy (GTK_WIDGET (current->grid));
		}

		wbcg->snotebook = tmp;
	}
}

static double
color_diff (const GdkRGBA *a, const GdkRGBA *b)
{
	/* Ignoring alpha.  */
	return ((a->red - b->red) * (a->red - b->red) +
		(a->green - b->green) * (a->green - b->green) +
		(a->blue - b->blue) * (a->blue - b->blue));
}


static gboolean
cb_adjust_foreground_attributes (PangoAttribute *attribute,
				 gpointer data)
{
	const GdkRGBA *back = data;

	if (attribute->klass->type == PANGO_ATTR_FOREGROUND) {
		PangoColor *pfore = &((PangoAttrColor *)attribute)->color;
		GdkRGBA fore;
		const double threshold = 0.01;

		fore.red = pfore->red / 65535.0;
		fore.green = pfore->green / 65535.0;
		fore.blue = pfore->blue / 65535.0;

		if (color_diff (&fore, back) < threshold) {
			static const GdkRGBA black = { 0, 0, 0, 1 };
			static const GdkRGBA white = { 1, 1, 1, 1 };
			double back_norm = color_diff (back, &black);
			double f = 0.2;
			const GdkRGBA *ref =
				back_norm > 0.75 ? &black : &white;

#define DO_CHANNEL(channel)						\
do {									\
	double val = fore.channel * (1 - f) + ref->channel * f;		\
	pfore->channel = CLAMP (val, 0, 1) * 65535;			\
} while (0)
			DO_CHANNEL(red);
			DO_CHANNEL(green);
			DO_CHANNEL(blue);
#undef DO_CHANNEL
		}
	}
	return FALSE;
}

static void
adjust_foreground_attributes (PangoAttrList *attrs, GtkWidget *w)
{
	GdkRGBA c;
	GtkStyleContext *ctxt = gtk_widget_get_style_context (w);

	gtk_style_context_get_background_color (ctxt, GTK_STATE_FLAG_NORMAL,
						&c);

	if (0)
		g_printerr ("back=%s\n", gdk_rgba_to_string (&c));

	pango_attr_list_unref
		(pango_attr_list_filter
		 (attrs,
		  cb_adjust_foreground_attributes,
		  &c));
}


static void
wbcg_auto_expr_value_changed (WorkbookView *wbv,
			      G_GNUC_UNUSED GParamSpec *pspec,
			      WBCGtk *wbcg)
{
	GtkLabel *lbl = GTK_LABEL (wbcg->auto_expr_label);
	GnmValue const *v = wbv->auto_expr.value;

	if (v) {
		GOFormat const *format = VALUE_FMT (v);
		GString *str = g_string_new (wbv->auto_expr.descr);
		PangoAttrList *attrs = NULL;

		g_string_append (str, " = ");

		if (wbv->auto_expr.use_max_precision && VALUE_IS_NUMBER (v)) {
			// "G" to match what format "General" does.
			go_dtoa (str, "!" GNM_FORMAT_G, value_get_as_float (v));
		} else if (format) {
			PangoLayout *layout = gtk_widget_create_pango_layout (GTK_WIDGET (wbcg->toplevel), NULL);
			gsize old_len = str->len;
			GODateConventions const *date_conv = workbook_date_conv (wb_view_get_workbook (wbv));
			int max_width = go_format_is_general (format) && VALUE_IS_NUMBER (v)
				? 14
				: strlen (AUTO_EXPR_SAMPLE) - g_utf8_strlen (str->str, -1);
			GOFormatNumberError err =
				format_value_layout (layout, format, v,
						     max_width, date_conv);
			switch (err) {
			case GO_FORMAT_NUMBER_OK:
			case GO_FORMAT_NUMBER_DATE_ERROR: {
				PangoAttrList *atl;

				go_pango_translate_layout (layout); /* translating custom attributes */
				g_string_append (str, pango_layout_get_text (layout));
				/* We need to shift the attribute list  */
				atl = pango_attr_list_ref (pango_layout_get_attributes (layout));
				if (atl != NULL) {
					attrs = pango_attr_list_new ();
					pango_attr_list_splice
						(attrs, atl, old_len,
						 str->len - old_len);
					pango_attr_list_unref (atl);
					/* Adjust colours to make text visible. */
					adjust_foreground_attributes
						(attrs,
						 gtk_widget_get_parent (GTK_WIDGET (lbl)));
				}
				break;
			}
			default:
			case GO_FORMAT_NUMBER_INVALID_FORMAT:
				g_string_append (str,  _("Invalid format"));
				break;
			}
			g_object_unref (layout);
		} else {
			g_string_append (str, value_peek_string (v));
		}

		gtk_label_set_text (lbl, str->str);
		gtk_label_set_attributes (lbl, attrs);

		pango_attr_list_unref (attrs);
		g_string_free (str, TRUE);
	} else {
		gtk_label_set_text (lbl, "");
		gtk_label_set_attributes (lbl, NULL);
	}
}

static void
wbcg_scrollbar_visibility (WorkbookView *wbv,
			   G_GNUC_UNUSED GParamSpec *pspec,
			   WBCGtk *wbcg)
{
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	scg_adjust_preferences (scg);
}

static void
wbcg_notebook_tabs_visibility (WorkbookView *wbv,
			       G_GNUC_UNUSED GParamSpec *pspec,
			       WBCGtk *wbcg)
{
	gtk_widget_set_visible (GTK_WIDGET (wbcg->bnotebook),
				wbv->show_notebook_tabs);
}


static void
wbcg_menu_state_update (WorkbookControl *wbc, int flags)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	SheetView const *sv  = wb_control_cur_sheet_view (wbc);
	Sheet const *sheet = wb_control_cur_sheet (wbc);
	gboolean const has_guru = wbc_gtk_get_guru (wbcg) != NULL;
	gboolean edit_object = scg != NULL &&
		(scg->selected_objects != NULL || wbcg->new_object != NULL);
	gboolean has_print_area;

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
			// Inter-process paste special is now allowed
			!gnm_app_clipboard_is_cut () &&
			!edit_object);
	if (MS_PRINT_SETUP & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "FilePageSetup", !has_guru);
	if (MS_SEARCH_REPLACE & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "EditReplace", !has_guru);
	if (MS_DEFINE_NAME & flags) {
		wbc_gtk_set_action_sensitivity (wbcg, "EditNames", !has_guru);
		wbc_gtk_set_action_sensitivity (wbcg, "InsertNames", !has_guru);
	}
	if (MS_CONSOLIDATE & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "DataConsolidate", !has_guru);
	if (MS_FILTER_STATE_CHANGED & flags)
		wbc_gtk_set_action_sensitivity (wbcg, "DataFilterShowAll", sheet->has_filtered_rows);
	if (MS_SHOW_PRINTAREA & flags) {
		GnmRange *print_area = sheet_get_nominal_printarea (sheet);
		has_print_area = (print_area != NULL);
		g_free (print_area);
		wbc_gtk_set_action_sensitivity (wbcg, "FilePrintAreaClear", has_print_area);
		wbc_gtk_set_action_sensitivity (wbcg, "FilePrintAreaShow", has_print_area);
	}
	if (MS_PAGE_BREAKS & flags) {
		gint col = sv->edit_pos.col;
		gint row = sv->edit_pos.row;
		GnmPrintInformation *pi = sheet->print_info;
		char const* new_label = NULL;
		char const *new_tip = NULL;

		if (pi->page_breaks.v != NULL &&
		    gnm_page_breaks_get_break (pi->page_breaks.v, col) == GNM_PAGE_BREAK_MANUAL) {
			new_label = _("Remove Column Page Break");
			new_tip = _("Remove the page break to the left of the current column");
		} else {
			new_label = _("Add Column Page Break");
			new_tip = _("Add a page break to the left of the current column");
		}
		wbc_gtk_set_action_label (wbcg, "FilePrintAreaToggleColPageBreak",
					  NULL, new_label, new_tip);
		if (pi->page_breaks.h != NULL &&
		    gnm_page_breaks_get_break (pi->page_breaks.h, col) == GNM_PAGE_BREAK_MANUAL) {
			new_label = _("Remove Row Page Break");
			new_tip = _("Remove the page break above the current row");
		} else {
			new_label = _("Add Row Page Break");
			new_tip = _("Add a page break above current row");
		}
		wbc_gtk_set_action_label (wbcg, "FilePrintAreaToggleRowPageBreak",
					  NULL, new_label, new_tip);
		wbc_gtk_set_action_sensitivity (wbcg, "FilePrintAreaToggleRowPageBreak",
						row != 0);
		wbc_gtk_set_action_sensitivity (wbcg, "FilePrintAreaToggleColPageBreak",
						col != 0);
		wbc_gtk_set_action_sensitivity (wbcg, "FilePrintAreaClearAllPageBreak",
						print_info_has_manual_breaks (sheet->print_info));
	}
	if (MS_SELECT_OBJECT & flags) {
		wbc_gtk_set_action_sensitivity (wbcg, "EditSelectObject",
						sheet->sheet_objects != NULL);
	}

	if (MS_FREEZE_VS_THAW & flags) {
		/* Cheat and use the same accelerator for both states because
		 * we don't reset it when the label changes */
		char const* label = gnm_sheet_view_is_frozen (sv)
			? _("Un_freeze Panes")
			: _("_Freeze Panes");
		char const *new_tip = gnm_sheet_view_is_frozen (sv)
			? _("Unfreeze the top left of the sheet")
			: _("Freeze the top left of the sheet");
		wbc_gtk_set_action_label (wbcg, "ViewFreezeThawPanes", NULL, label, new_tip);
	}

	if (MS_ADD_VS_REMOVE_FILTER & flags) {
		gboolean const has_filter = (NULL != gnm_sheet_view_editpos_in_filter (sv));
		GnmFilter *f = gnm_sheet_view_selection_intersects_filter_rows (sv);
		char const* label;
		char const *new_tip;
		gboolean active = TRUE;
		GnmRange *r = NULL;

		if ((!has_filter) && (NULL != f)) {
			gchar *nlabel = NULL;
			if (NULL != (r = gnm_sheet_view_selection_extends_filter (sv, f))) {
				active = TRUE;
				nlabel = g_strdup_printf
					(_("Extend _Auto Filter to %s"),
					 range_as_string (r));
				new_tip = _("Extend the existing filter.");
				wbc_gtk_set_action_label
					(wbcg, "DataAutoFilter", NULL,
					 nlabel, new_tip);
				g_free (r);
			} else {
				active = FALSE;
				nlabel = g_strdup_printf
					(_("Auto Filter blocked by %s"),
					 range_as_string (&f->r));
				new_tip = _("The selection intersects an "
					    "existing auto filter.");
				wbc_gtk_set_action_label
					(wbcg, "DataAutoFilter", NULL,
					 nlabel, new_tip);
			}
			g_free (nlabel);
		} else {
			label = has_filter
				? _("Remove _Auto Filter")
				: _("Add _Auto Filter");
			new_tip = has_filter
				? _("Remove a filter")
				: _("Add a filter");
			wbc_gtk_set_action_label (wbcg, "DataAutoFilter", NULL, label, new_tip);
		}

		wbc_gtk_set_action_sensitivity (wbcg, "DataAutoFilter", active);
	}
	if (MS_COMMENT_LINKS & flags) {
		gboolean has_comment
			= (sheet_get_comment (sheet, &sv->edit_pos) != NULL);
		gboolean has_link;
		GnmRange rge;
		range_init_cellpos (&rge, &sv->edit_pos);
		has_link = (NULL !=
			    sheet_style_region_contains_link (sheet, &rge));
		wbc_gtk_set_action_sensitivity
			(wbcg, "EditComment", has_comment);
		wbc_gtk_set_action_sensitivity
			(wbcg, "EditHyperlink", has_link);
	}

	if (MS_COMMENT_LINKS_RANGE & flags) {
		GSList *l;
		int count = 0;
		gboolean has_links = FALSE, has_comments = FALSE;
		gboolean sel_is_vector = FALSE;
		SheetView *sv = scg_view (scg);
		for (l = sv->selections;
		     l != NULL; l = l->next) {
			GnmRange const *r = l->data;
			GSList *objs;
			GnmStyleList *styles;
			if (!has_links) {
				styles = sheet_style_collect_hlinks
					(sheet, r);
				has_links = (styles != NULL);
				style_list_free (styles);
			}
			if (!has_comments) {
				objs = sheet_objects_get
					(sheet, r, GNM_CELL_COMMENT_TYPE);
				has_comments = (objs != NULL);
				g_slist_free (objs);
			}
			if((count++ > 1) && has_comments && has_links)
				break;
		}
		wbc_gtk_set_action_sensitivity
			(wbcg, "EditClearHyperlinks", has_links);
		wbc_gtk_set_action_sensitivity
			(wbcg, "EditClearComments", has_comments);
		if (count == 1) {
			GnmRange const *r = sv->selections->data;
			sel_is_vector = (range_width (r) == 1 ||
					 range_height (r) == 1) &&
				!range_is_singleton (r);
 		}
		wbc_gtk_set_action_sensitivity
			(wbcg, "InsertSortDecreasing", sel_is_vector);
		wbc_gtk_set_action_sensitivity
			(wbcg, "InsertSortIncreasing", sel_is_vector);
	}
	if (MS_FILE_EXPORT_IMPORT & flags) {
		Workbook *wb = wb_control_get_workbook (wbc);
		gboolean has_export_info = workbook_get_file_exporter (wb) &&
			workbook_get_last_export_uri (wb);
		wbc_gtk_set_action_sensitivity (wbcg, "DataExportRepeat", has_export_info);
		if (has_export_info) {
			gchar *base = go_basename_from_uri (workbook_get_last_export_uri (wb));
			gchar *new_label = g_strdup_printf (_("Repeat Export to %s"),
							    base);
			g_free (base);
			wbc_gtk_set_action_label (wbcg, "DataExportRepeat", NULL,
						  new_label, N_("Repeat the last data export"));
			g_free (new_label);
		} else
			wbc_gtk_set_action_label (wbcg, "DataExportRepeat", NULL,
						  N_("Repeat Export"), N_("Repeat the last data export"));
	}
	{
		gboolean const has_slicer = (NULL != gnm_sheet_view_editpos_in_slicer (sv));
		char const* label = has_slicer
			? _("Remove _Data Slicer")
			: _("Create _Data Slicer");
		char const *new_tip = has_slicer
			? _("Remove a Data Slicer")
			: _("Create a Data Slicer");
		wbc_gtk_set_action_label (wbcg, "DataSlicer", NULL, label, new_tip);
		wbc_gtk_set_action_sensitivity (wbcg, "DataSlicerRefresh", has_slicer);
		wbc_gtk_set_action_sensitivity (wbcg, "DataSlicerEdit", has_slicer);
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
	gnm_x_request_clipboard ((WBCGtk *)wbc, pt);
}

static gboolean
wbcg_claim_selection (WorkbookControl *wbc)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	GdkDisplay *display = gtk_widget_get_display (GTK_WIDGET (wbcg_toplevel (wbcg)));
	return gnm_x_claim_clipboard (display);
}

static int
wbcg_show_save_dialog (WBCGtk *wbcg, Workbook *wb)
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

	d = gnm_message_dialog_create (wbcg_toplevel (wbcg),
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 msg,
					 _("If you close without saving, changes will be discarded."));
	atk_object_set_role (gtk_widget_get_accessible (d), ATK_ROLE_ALERT);

	go_gtk_dialog_add_button (GTK_DIALOG(d), _("Discard"),
				  GTK_STOCK_DELETE, GTK_RESPONSE_NO);
	go_gtk_dialog_add_button (GTK_DIALOG(d), _("Don't close"),
				  GNM_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	gtk_dialog_add_button (GTK_DIALOG(d), GNM_STOCK_SAVE, GTK_RESPONSE_YES);
	gtk_dialog_set_default_response (GTK_DIALOG (d), GTK_RESPONSE_YES);
	ret = go_gtk_dialog_run (GTK_DIALOG (d), wbcg_toplevel (wbcg));
	g_free (msg);

	return ret;
}

/*
 * wbcg_close_if_user_permits : If the workbook is dirty the user is
 *		prompted to see if they should exit.
 *
 * Returns:
 * 0) canceled
 * 1) closed
 * 2) -
 * 3) save any future dirty
 * 4) do not save any future dirty
 */
static int
wbcg_close_if_user_permits (WBCGtk *wbcg, WorkbookView *wb_view)
{
	gboolean   can_close = TRUE;
	gboolean   done      = FALSE;
	int        iteration = 0;
	int        button = 0;
	Workbook  *wb = wb_view_get_workbook (wb_view);
	static int in_can_close;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), 0);

	if (in_can_close)
		return 0;
	in_can_close = TRUE;

	while (go_doc_is_dirty (GO_DOC (wb)) && !done) {
		iteration++;
		button = wbcg_show_save_dialog(wbcg, wb);

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
		gnm_x_store_clipboard_if_needed (wb);
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
 * @wbcg: #WBCGtk
 *
 * Returns: %TRUE if the control should NOT be closed.
 */
gboolean
wbc_gtk_close (WBCGtk *wbcg)
{
	WorkbookView *wb_view = wb_control_view (GNM_WBC (wbcg));

	g_return_val_if_fail (GNM_IS_WORKBOOK_VIEW (wb_view), TRUE);
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

		g_return_val_if_fail (GNM_IS_WORKBOOK (wb), TRUE);
		g_return_val_if_fail (wb->wb_views != NULL, TRUE);

		/* This is the last view */
		if (wb->wb_views->len <= 1) {
			if (wbcg_close_if_user_permits (wbcg, wb_view) == 0)
				return TRUE;
			return FALSE;
		}

		g_object_unref (wb_view);
	} else
		g_object_unref (wbcg);

	gnm_app_flag_windows_changed_ ();

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

static void
cb_accept_input_wo_ac (WBCGtk *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT_WO_AC, NULL);
}

static void
cb_accept_input_array (WBCGtk *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT_ARRAY, NULL);
}

static void
cb_accept_input_selected_cells (WBCGtk *wbcg)
{
	wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT_RANGE, NULL);
}

static void
cb_accept_input_selected_merged (WBCGtk *wbcg)
{
	Sheet *sheet = wbcg->editing_sheet;

#warning FIXME: this creates 2 undo items!
	if (wbcg_is_editing (wbcg) &&
	    wbcg_edit_finish (wbcg, WBC_EDIT_ACCEPT, NULL)) {
		WorkbookControl *wbc = GNM_WBC (wbcg);
		WorkbookView	*wbv = wb_control_view (wbc);
		SheetView *sv = sheet_get_view (sheet, wbv);
		GnmRange sel = *(selection_first_range (sv, NULL, NULL));
		GSList *selection = g_slist_prepend (NULL, &sel);

		cmd_merge_cells	(wbc, sheet, selection, FALSE);
		g_slist_free (selection);
	}
}

static gboolean
cb_accept_input_menu_sensitive_selected_cells (WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	WorkbookView	*wbv = wb_control_view (wbc);
	SheetView *sv = sheet_get_view (wbcg->editing_sheet, wbv);
	gboolean result = TRUE;
	GSList	*selection = selection_get_ranges (sv, FALSE), *l;

	for (l = selection; l != NULL; l = l->next) {
		GnmRange const *sel = l->data;
		if (sheet_range_splits_array
		    (wbcg->editing_sheet, sel, NULL, NULL, NULL)) {
			result = FALSE;
			break;
		}
	}
	range_fragment_free (selection);
	return result;
}

static gboolean
cb_accept_input_menu_sensitive_selected_merged (WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
	WorkbookView	*wbv = wb_control_view (wbc);
	SheetView *sv = sheet_get_view (wbcg->editing_sheet, wbv);
	GnmRange const *sel = selection_first_range (sv, NULL, NULL);

	return (sel && !range_is_singleton (sel) &&
		sv->edit_pos.col == sel->start.col &&
		sv->edit_pos.row == sel->start.row &&
		!sheet_range_splits_array
		(wbcg->editing_sheet, sel, NULL, NULL, NULL));
}

static void
cb_accept_input_menu (GtkMenuToolButton *button, WBCGtk *wbcg)
{
	GtkWidget *menu = gtk_menu_tool_button_get_menu (button);
	GList     *l, *children = gtk_container_get_children (GTK_CONTAINER (menu));

	struct AcceptInputMenu {
		gchar const *text;
		void (*function) (WBCGtk *wbcg);
		gboolean (*sensitive) (WBCGtk *wbcg);
	} const accept_input_actions [] = {
		{ N_("Enter in current cell"),       cb_accept_input,
		  NULL },
		{ N_("Enter in current cell without autocorrection"), cb_accept_input_wo_ac,
		  NULL },
/* 		{ N_("Enter on all non-hidden sheets"), cb_accept_input_sheets,  */
/* 		  cb_accept_input_menu_sensitive_sheets}, */
/* 		{ N_("Enter on multiple sheets..."), cb_accept_input_selected_sheets,  */
/* 		  cb_accept_input_menu_sensitive_selected_sheets }, */
		{ NULL,                              NULL, NULL },
		{ N_("Enter in current range merged"), cb_accept_input_selected_merged,
		  cb_accept_input_menu_sensitive_selected_merged },
		{ NULL,                              NULL, NULL },
		{ N_("Enter in selected ranges"), cb_accept_input_selected_cells,
		  cb_accept_input_menu_sensitive_selected_cells },
		{ N_("Enter in selected ranges as array"), cb_accept_input_array,
		  cb_accept_input_menu_sensitive_selected_cells },
	};
	unsigned int ui;
	GtkWidget *item;
	const struct AcceptInputMenu *it;

	if (children == NULL)
		for (ui = 0; ui < G_N_ELEMENTS (accept_input_actions); ui++) {
			it = accept_input_actions + ui;

			if (it->text) {
				item = gtk_image_menu_item_new_with_label
					(_(it->text));
				if (it->function)
					g_signal_connect_swapped
						(G_OBJECT (item), "activate",
						 G_CALLBACK (it->function),
						 wbcg);
				if (wbcg->editing_sheet) {
					if (it->sensitive)
						gtk_widget_set_sensitive
							(item, (it->sensitive) (wbcg));
					else
						gtk_widget_set_sensitive (item, TRUE);
				} else
					gtk_widget_set_sensitive (item, FALSE);
			} else
				item = gtk_separator_menu_item_new ();
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);
		}
	else
		for (ui = 0, l = children;
		     ui < G_N_ELEMENTS (accept_input_actions) && l != NULL;
		     ui++, l = l->next) {
			it = accept_input_actions + ui;
			if (wbcg->editing_sheet) {
				if (it->sensitive)
					gtk_widget_set_sensitive
						(GTK_WIDGET (l->data),
						 (it->sensitive) (wbcg));
				else
					gtk_widget_set_sensitive
						(GTK_WIDGET (l->data), TRUE);
			} else
					gtk_widget_set_sensitive (l->data, FALSE);
		}


	g_list_free (children);
}

static gboolean
cb_editline_focus_in (GtkWidget *w, GdkEventFocus *event,
		      WBCGtk *wbcg)
{
	if (!wbcg_is_editing (wbcg))
		if (!wbcg_edit_start (wbcg, FALSE, TRUE)) {
#if 0
			GtkEntry *entry = GTK_ENTRY (w);
#endif
			wbcg_focus_cur_scg (wbcg);
#warning GTK3: what can we do there for gtk3?
#if 0
			entry->in_drag = FALSE;
			/*
			 * ->button is private, ugh.  Since the text area
			 * never gets a release event, there seems to be
			 * no official way of returning the widget to its
			 * correct state.
			 */
			entry->button = 0;
#endif
			return TRUE;
		}

	return FALSE;
}

static void
cb_statusbox_activate (GtkEntry *entry, WBCGtk *wbcg)
{
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
dump_size_tree (GtkWidget *w, gpointer indent_)
{
	int indent = GPOINTER_TO_INT (indent_);
	int h1, h2;
	GtkAllocation a;

	g_printerr ("%*s", indent, "");
	if (gtk_widget_get_name (w))
		g_printerr ("\"%s\" ", gtk_widget_get_name (w));

	gtk_widget_get_preferred_height (w, &h1, &h2);
	gtk_widget_get_allocation (w, &a);

	g_printerr ("%s %p viz=%d act=%dx%d minheight=%d natheight=%d\n",
		    g_type_name_from_instance ((GTypeInstance *)w), w,
		    gtk_widget_get_visible (w),
		    a.width, a.height,
		    h1, h2);

	if (GTK_IS_CONTAINER (w)) {
		gtk_container_foreach (GTK_CONTAINER (w),
				       dump_size_tree,
				       GINT_TO_POINTER (indent + 2));
	}
}


static void
dump_colrow_sizes (Sheet *sheet)
{
	static const char *what[2] = { "col", "row" };
	int pass;
	for (pass = 0; pass < 2; pass++) {
		gboolean is_cols = (pass == 0);
		ColRowCollection *crc = is_cols ? &sheet->cols : &sheet->rows;
		int i;

		g_printerr ("Dumping %s sizes, max_used=%d\n",
			    what[pass], crc->max_used);
		for (i = -1; i <= crc->max_used; i++) {
			ColRowInfo const *cri = (i >= 0)
				? sheet_colrow_get (sheet, i, is_cols)
				: sheet_colrow_get_default (sheet, is_cols);
			g_printerr ("%s %5d : ", what[pass], i);
			if (cri == NULL) {
				g_printerr ("default\n");
			} else {
				g_printerr ("pts=%-6g  px=%-3d%s%s%s%s%s%s\n",
					    cri->size_pts, cri->size_pixels,
					    cri->is_default ? "  def" : "",
					    cri->is_collapsed ? "  clps" : "",
					    cri->hard_size ? "  hard" : "",
					    cri->visible ? "  viz" : "",
					    cri->in_filter ? "  filt" : "",
					    cri->in_advanced_filter ? "  afilt" : "");
			}
		}
	}
}


static void
cb_workbook_debug_info (WBCGtk *wbcg)
{
	Workbook *wb = wb_control_get_workbook (GNM_WBC (wbcg));

	if (gnm_debug_flag ("notebook-size"))
		dump_size_tree (GTK_WIDGET (wbcg_toplevel (wbcg)), GINT_TO_POINTER (0));

	if (gnm_debug_flag ("deps")) {
		dependents_dump (wb);
	}

	if (gnm_debug_flag ("colrow")) {
		dump_colrow_sizes (wbcg_cur_sheet (wbcg));
	}

	if (gnm_debug_flag ("expr-sharer")) {
		GnmExprSharer *es = workbook_share_expressions (wb, FALSE);
		gnm_expr_sharer_report (es);
		gnm_expr_sharer_unref (es);
	}

	if (gnm_debug_flag ("style-optimize")) {
		workbook_optimize_style (wb);
	}

	if (gnm_debug_flag ("sheet-conditions")) {
		WORKBOOK_FOREACH_SHEET(wb, sheet, {
			sheet_conditions_dump (sheet);
		});
	}

	if (gnm_debug_flag ("name-collections")) {
		gnm_named_expr_collection_dump (wb->names, "workbook");
		WORKBOOK_FOREACH_SHEET(wb, sheet, {
			gnm_named_expr_collection_dump (sheet->names,
							sheet->name_unquoted);
		});
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
					   gtk_entry_get_text_length (entry)-1);
	}
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
	if (focus && !gtk_window_get_focus (window))
		wbcg_focus_cur_scg (wbcg);
}

/***************************************************************************/

static void
do_scroll_zoom (WBCGtk *wbcg, gboolean go_back)
{
	SheetControlGUI *scg = wbcg_get_scg (wbcg, wbcg_focus_cur_scg (wbcg));
	Sheet		*sheet = scg_sheet (scg);
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
		cmd_zoom (GNM_WBC (wbcg), g_slist_append (NULL, sheet),
			  (double) (zoom + 10) / 100);
}

static gboolean
cb_scroll_wheel (GtkWidget *w, GdkEventScroll *event,
		 WBCGtk *wbcg)
{
	SheetControlGUI *scg = wbcg_get_scg (wbcg, wbcg_focus_cur_scg (wbcg));
	/* scroll always operates on pane 0 */
	GnmPane *pane = scg_pane (scg, 0);
	gboolean go_back = (event->direction == GDK_SCROLL_UP ||
			    event->direction == GDK_SCROLL_LEFT);
	gboolean qsmooth = (event->direction == GDK_SCROLL_SMOOTH);
	int drow = 0, dcol = 0;

	if (!pane || !gtk_widget_get_realized (w))
		return FALSE;

	if ((event->state & GDK_CONTROL_MASK)) {
		/* zoom */
		if (qsmooth)
			return FALSE;
		do_scroll_zoom (wbcg, go_back);
		return TRUE;
	}

	if (qsmooth) {
		gdouble dx, dy;
		gdouble scale = 10; // ???
		gdk_event_get_scroll_deltas ((GdkEvent*)event, &dx, &dy);
		dcol = (int)(dx * scale);
		drow = (int)(dy * scale);
	} else {
		gboolean go_horiz = (event->direction == GDK_SCROLL_LEFT ||
				     event->direction == GDK_SCROLL_RIGHT);

		if ((event->state & GDK_SHIFT_MASK))
			go_horiz = !go_horiz;

		if (go_horiz) {
			dcol = (pane->last_full.col - pane->first.col) / 4;
			dcol = MAX (1, dcol);
			if (go_back) dcol = -dcol;
		} else {
			drow = (pane->last_full.row - pane->first.row) / 4;
			drow = MAX (1, drow);
			if (go_back) drow = -drow;
		}
	}

	if (dcol) {
		int col = pane->first.col + dcol;
		scg_set_left_col (pane->simple.scg, col);
	}
	if (drow) {
		int row = pane->first.row + drow;
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
	GtkAllocation ta;

	g_return_if_fail (GTK_IS_WINDOW (toplevel));

	gtk_widget_get_allocation (GTK_WIDGET (toplevel), &ta);
	gtk_window_set_default_size (toplevel, ta.width, ta.height);

	/* if we are already initialized set the focus.  Without this loading a
	 * multpage book sometimes leaves focus on the last book rather than
	 * the current book.  Which leads to a slew of errors for keystrokes
	 * until focus is corrected.
	 */
	if (wbcg->snotebook) {
		wbcg_focus_cur_scg (wbcg);
		wbcg_update_menu_feedback (wbcg, wbcg_cur_sheet (wbcg));
	}
}

static void
cb_css_parse_error (GtkCssProvider *css, GtkCssSection *section, GError *err)
{
	if (g_error_matches (err, GTK_CSS_PROVIDER_ERROR,
			     GTK_CSS_PROVIDER_ERROR_DEPRECATED) &&
	    !gnm_debug_flag ("css"))
		return;

	g_warning ("Theme parsing error: %s", err->message);
}

struct css_provider_data {
	GtkCssProvider *css;
	GSList *screens;
};

static void
cb_unload_providers (gpointer data_)
{
	struct css_provider_data *data = data_;
	GSList *l;

	for (l = data->screens; l; l = l->next) {
		GdkScreen *screen = l->data;
		gtk_style_context_remove_provider_for_screen
			(screen, GTK_STYLE_PROVIDER (data->css));
	}
	g_slist_free (data->screens);
	g_object_unref (data->css);
	g_free (data);
}

static void
cb_screen_changed (GtkWidget *widget)
{
	GdkScreen *screen = gtk_widget_get_screen (widget);
	GObject *app = gnm_app_get_app ();
	const char *app_key = "css-provider";
	struct css_provider_data *data;

	data = g_object_get_data (app, app_key);
	if (!data) {
		gboolean debug = gnm_debug_flag ("css");
		gboolean q_dark = gnm_theme_is_dark (widget);
		const char *resource = "/org/gnumeric/gnumeric/ui/gnumeric.css";
		GBytes *cssbytes;
		char *csstext;
		GHashTable *vars = g_hash_table_new (g_str_hash, g_str_equal);

		cssbytes = g_resources_lookup_data (resource, 0, NULL);
		if (q_dark)
			g_hash_table_insert (vars,
					     (gpointer)"DARK",
					     (gpointer)"1");
		csstext = gnm_cpp (g_bytes_get_data (cssbytes, NULL), vars);

		g_hash_table_destroy (vars);

		data = g_new (struct css_provider_data, 1);
		data->css = gtk_css_provider_new ();
		data->screens = NULL;

		if (debug)
			g_printerr ("Loading style from resource %s\n", resource);
		else
			g_signal_connect (data->css, "parsing-error",
					  G_CALLBACK (cb_css_parse_error),
					  NULL);

		gtk_css_provider_load_from_data	(data->css, csstext, -1, NULL);
		g_object_set_data_full (app, app_key, data, cb_unload_providers);
		g_bytes_unref (cssbytes);
		g_free (csstext);
	}

	if (screen && !g_slist_find (data->screens, screen)) {
		gtk_style_context_add_provider_for_screen
			(screen,
			 GTK_STYLE_PROVIDER (data->css),
			 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		data->screens = g_slist_prepend (data->screens, screen);
	}
}

void
wbcg_set_status_text (WBCGtk *wbcg, char const *text)
{
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));
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
		gtk_widget_set_visible (w, visible);
	wbc_gtk_set_toggle_action_state (wbcg, action_name, visible);
}


void
wbcg_toggle_visibility (WBCGtk *wbcg, GtkToggleAction *action)
{
	if (!wbcg->updating_ui && wbcg_ui_update_begin (wbcg)) {
		char const *name = gtk_action_get_name (GTK_ACTION (action));
		set_visibility (wbcg, name,
				gtk_toggle_action_get_active (action));
		wbcg_ui_update_end (wbcg);
	}
}

static void
cb_visibility (char const *action, GtkWidget *orig_widget, WBCGtk *new_wbcg)
{
	set_visibility (new_wbcg, action, gtk_widget_get_visible (orig_widget));
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
	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	if (!wbcg->last_key_was_end != !flag) {
		const char *txt = flag ? _("END") : "";
		wbcg_set_status_text (wbcg, txt);
		wbcg->last_key_was_end = flag;
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
	SheetControlGUI *scg = get_scg (w);
	sc_resize ((SheetControl *)scg, TRUE);
}

static void
cb_desktop_font_changed (GtkSettings *settings, GParamSpec *pspec,
			 WBCGtk *wbcg)
{
	if (wbcg->font_desc)
		pango_font_description_free (wbcg->font_desc);
	wbcg->font_desc = settings_get_font_desc (settings);
	gtk_container_foreach (GTK_CONTAINER (wbcg->snotebook),
			       (GtkCallback)cb_update_item_bar_font, NULL);
}

static GdkScreen *
wbcg_get_screen (WBCGtk *wbcg)
{
	return gtk_widget_get_screen (wbcg->notebook_area);
}

static GtkSettings *
wbcg_get_gtk_settings (WBCGtk *wbcg)
{
	return gtk_settings_get_for_screen (wbcg_get_screen (wbcg));
}

/* ------------------------------------------------------------------------- */

static int
show_gui (WBCGtk *wbcg)
{
	SheetControlGUI *scg;
	WorkbookView *wbv = wb_control_view (GNM_WBC (wbcg));
	int sx, sy;
	gdouble fx, fy;
	GdkRectangle rect;
	GdkScreen *screen = wbcg_get_screen (wbcg);
	gboolean debug = gnm_debug_flag ("window-size");

	/* In a Xinerama setup, we want the geometry of the actual display
	 * unit, if available. See bug 59902.  */
	gdk_screen_get_monitor_geometry (screen, 0, &rect);
	sx = MAX (rect.width, 600);
	sy = MAX (rect.height, 200);
	if (debug)
		g_printerr ("Monitor geometry %dx%d\n", sx, sy);

	fx = gnm_conf_get_core_gui_window_x ();
	fy = gnm_conf_get_core_gui_window_y ();
	if (debug)
		g_printerr ("Configuration scale %gx%g\n", fx, fy);

	if (debug && wbcg->preferred_geometry)
		g_printerr ("Specified geometry \"%s\"\n", wbcg->preferred_geometry);

	/* Successfully parsed geometry string and urged WM to comply */
	if (NULL != wbcg->preferred_geometry && NULL != wbcg->toplevel &&
	    gtk_window_parse_geometry (GTK_WINDOW (wbcg->toplevel),
				       wbcg->preferred_geometry)) {
		g_free (wbcg->preferred_geometry);
		wbcg->preferred_geometry = NULL;
	} else if (wbcg->snotebook != NULL &&
		   wbv != NULL &&
		   (wbv->preferred_width > 0 || wbv->preferred_height > 0)) {
		/* Set grid size to preferred width */
		int swidth = gdk_screen_get_width (screen);
		int sheight = gdk_screen_get_height (screen);
		int pwidth = MIN (wbv->preferred_width, swidth);
		int pheight = MIN (wbv->preferred_height, sheight);
		GtkRequisition requisition;

		pwidth = pwidth > 0 ? pwidth : -1;
		pheight = pheight > 0 ? pheight : -1;
		gtk_widget_set_size_request (GTK_WIDGET (wbcg->notebook_area),
					     pwidth, pheight);
		gtk_widget_get_preferred_size (GTK_WIDGET (wbcg->toplevel),
					       &requisition, NULL);
		if (debug) {
			g_printerr ("Screen size %dx%d\n", swidth, sheight);
			g_printerr ("View's preferred size %dx%d\n",
				    wbv->preferred_width, wbv->preferred_height);
			g_printerr ("Toplevel's preferred size %dx%d\n",
				    requisition.width, requisition.height);
			g_printerr ("Size request %dx%d\n",
				    pwidth, pheight);
		}

		/* We want to test if toplevel is bigger than screen.
		 * gtk_widget_size_request tells us the space
		 * allocated to the toplevel proper, but not how much is
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
			if (debug)
				g_printerr ("Maximizing\n");
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

	scg = wbcg_cur_scg (wbcg);
	if (scg)
		wbcg_set_direction (scg);

	gtk_widget_show (GTK_WIDGET (wbcg_toplevel (wbcg)));

	/* rehide headers if necessary */
	if (NULL != scg && wbcg_cur_sheet (wbcg))
		scg_adjust_preferences (scg);

	gtk_widget_set_size_request (GTK_WIDGET (wbcg->notebook_area),
				     -1, -1);
	return FALSE;
}

static GtkWidget *
wbcg_get_label_for_position (WBCGtk *wbcg, GtkWidget *source,
			     gint x)
{
	guint n, i;
	GtkWidget *last_visible = NULL;

	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), NULL);

	n = wbcg_get_n_scg (wbcg);
	for (i = 0; i < n; i++) {
		GtkWidget *label = gnm_notebook_get_nth_label (wbcg->bnotebook, i);
		int x0, x1;
		GtkAllocation la;

		if (!gtk_widget_get_visible (label))
			continue;

		gtk_widget_get_allocation (label, &la);
		x0 = la.x;
		x1 = x0 + la.width;

		if (x <= x1) {
			/*
			 * We are left of this label's right edge.  Use it
			 * even if we are far left of the label.
			 */
			return label;
		}

		last_visible = label;
	}

	return last_visible;
}

static gboolean
wbcg_is_local_drag (WBCGtk *wbcg, GtkWidget *source_widget)
{
	GtkWidget *top = (GtkWidget *)wbcg_toplevel (wbcg);
	return GNM_IS_PANE (source_widget) &&
		gtk_widget_get_toplevel (source_widget) == top;
}
static gboolean
cb_wbcg_drag_motion (GtkWidget *widget, GdkDragContext *context,
		     gint x, gint y, guint time, WBCGtk *wbcg)
{
	GtkWidget *source_widget = gtk_drag_get_source_widget (context);

	if (GNM_IS_NOTEBOOK (gtk_widget_get_parent (source_widget))) {
		/* The user wants to reorder sheets. We simulate a
		 * drag motion over a label.
		 */
		GtkWidget *label = wbcg_get_label_for_position (wbcg, source_widget, x);
		return cb_sheet_label_drag_motion (label, context, x, y,
						   time, wbcg);
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

	g_return_if_fail (GNM_IS_WBC_GTK (wbcg));

	if (GNM_IS_NOTEBOOK (gtk_widget_get_parent (source_widget)))
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
	gchar *target_type = gdk_atom_name (gtk_selection_data_get_target (selection_data));

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
			g_printerr ("autoscroll complete - stop it\n");
		else
			scg_drag_data_received (wbcg_cur_scg (wbcg),
				source_widget, 0, 0, selection_data);
	}
	g_free (target_type);
}

static void cb_cs_go_up  (WBCGtk *wbcg)
{ wb_control_navigate_to_cell (GNM_WBC (wbcg), navigator_top); }
static void cb_cs_go_down  (WBCGtk *wbcg)
{ wb_control_navigate_to_cell (GNM_WBC (wbcg), navigator_bottom); }
static void cb_cs_go_left  (WBCGtk *wbcg)
{ wb_control_navigate_to_cell (GNM_WBC (wbcg), navigator_first); }
static void cb_cs_go_right  (WBCGtk *wbcg)
{ wb_control_navigate_to_cell (GNM_WBC (wbcg), navigator_last); }
static void cb_cs_go_to_cell  (WBCGtk *wbcg) { dialog_goto_cell (wbcg); }

static void
wbc_gtk_cell_selector_popup (G_GNUC_UNUSED GtkEntry *entry,
			     G_GNUC_UNUSED GtkEntryIconPosition icon_pos,
			     G_GNUC_UNUSED GdkEvent *event,
			     gpointer data)
{
	if (event->type == GDK_BUTTON_PRESS) {
		WBCGtk *wbcg = data;

		struct CellSelectorMenu {
			gchar const *text;
			void (*function) (WBCGtk *wbcg);
		} const cell_selector_actions [] = {
			{ N_("Go to Top"),      &cb_cs_go_up      },
			{ N_("Go to Bottom"),   &cb_cs_go_down    },
			{ N_("Go to First"),    &cb_cs_go_left    },
			{ N_("Go to Last"),     &cb_cs_go_right   },
			{ NULL, NULL },
			{ N_("Go to Cell..."),  &cb_cs_go_to_cell }
		};
		unsigned int ui;
		GtkWidget *item, *menu = gtk_menu_new ();
		gboolean active = (!wbcg_is_editing (wbcg) &&
				   NULL == wbc_gtk_get_guru (wbcg));

		for (ui = 0; ui < G_N_ELEMENTS (cell_selector_actions); ui++) {
			const struct CellSelectorMenu *it =
				cell_selector_actions + ui;
			if (it->text)
				item = gtk_image_menu_item_new_with_label
					(_(it->text));
			else
				item = gtk_separator_menu_item_new ();

			if (it->function)
				g_signal_connect_swapped
					(G_OBJECT (item), "activate",
					 G_CALLBACK (it->function), wbcg);
			gtk_widget_set_sensitive (item, active);
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);
		}

		gnumeric_popup_menu (GTK_MENU (menu), event);
	}
}

static void
wbc_gtk_create_edit_area (WBCGtk *wbcg)
{
	GtkToolItem *item;
	GtkEntry *entry;
	int len;
	GtkWidget *debug_button;

	wbc_gtk_init_editline (wbcg);
	entry = wbcg_get_entry (wbcg);

	/* Set a reasonable width for the selection box. */
	len = gnm_widget_measure_string
		(GTK_WIDGET (wbcg_toplevel (wbcg)),
		 cell_coord_name (GNM_MAX_COLS - 1, GNM_MAX_ROWS - 1));
	/*
	 * Add a little extra since font might be proportional and since
	 * we also put user defined names there.
	 */
	len = len * 3 / 2;
	gtk_widget_set_size_request (wbcg->selection_descriptor, len, -1);

	g_signal_connect_swapped (wbcg->cancel_button,
				  "clicked", G_CALLBACK (cb_cancel_input),
				  wbcg);

	g_signal_connect_swapped (wbcg->ok_button,
				  "clicked", G_CALLBACK (cb_accept_input),
				  wbcg);
	gtk_menu_tool_button_set_menu (GTK_MENU_TOOL_BUTTON (wbcg->ok_button),
				       gtk_menu_new ());
	gtk_menu_tool_button_set_arrow_tooltip_text
		(GTK_MENU_TOOL_BUTTON (wbcg->ok_button),
		 _("Accept change in multiple cells"));
	g_signal_connect (wbcg->ok_button,
			  "show-menu", G_CALLBACK (cb_accept_input_menu),
			  wbcg);

	g_signal_connect_swapped (wbcg->func_button,
				  "clicked", G_CALLBACK (cb_autofunction),
				  wbcg);

	/* Dependency debugger */
	debug_button = GET_GUI_ITEM ("debug_button");
	if (gnm_debug_flag ("notebook-size") ||
	    gnm_debug_flag ("deps") ||
	    gnm_debug_flag ("colrow") ||
	    gnm_debug_flag ("expr-sharer") ||
	    gnm_debug_flag ("style-optimize") ||
	    gnm_debug_flag ("sheet-conditions") ||
	    gnm_debug_flag ("name-collections")) {
		g_signal_connect_swapped (debug_button,
					  "clicked", G_CALLBACK (cb_workbook_debug_info),
					  wbcg);
	} else {
		gtk_widget_destroy (debug_button);
	}

	item = GET_GUI_ITEM ("edit_line_entry_item");
	gtk_container_add (GTK_CONTAINER (item),
			   GTK_WIDGET (wbcg->edit_line.entry));
	gtk_widget_show_all (GTK_WIDGET (item));

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

	gtk_entry_set_icon_from_icon_name
		(GTK_ENTRY (wbcg->selection_descriptor),
		 GTK_ENTRY_ICON_SECONDARY, "go-jump");
	gtk_entry_set_icon_sensitive
		(GTK_ENTRY (wbcg->selection_descriptor),
		 GTK_ENTRY_ICON_SECONDARY, TRUE);
	gtk_entry_set_icon_activatable
		(GTK_ENTRY (wbcg->selection_descriptor),
		 GTK_ENTRY_ICON_SECONDARY, TRUE);

	g_signal_connect (G_OBJECT (wbcg->selection_descriptor),
			  "icon-press",
			  G_CALLBACK
			  (wbc_gtk_cell_selector_popup),
			  wbcg);
}

static int
wbcg_validation_msg (WorkbookControl *wbc, ValidationStyle v,
		     char const *title, char const *msg)
{
	WBCGtk *wbcg = (WBCGtk *)wbc;
	ValidationStatus res0, res1 = GNM_VALIDATION_STATUS_VALID; /* supress warning */
	char const *btn0, *btn1;
	GtkMessageType  type;
	GtkWidget  *dialog;
	int response;

	switch (v) {
	case GNM_VALIDATION_STYLE_STOP:
		res0 = GNM_VALIDATION_STATUS_INVALID_EDIT;
		btn0 = _("_Re-Edit");
		res1 = GNM_VALIDATION_STATUS_INVALID_DISCARD;
		btn1 = _("_Discard");
		type = GTK_MESSAGE_ERROR;
		break;
	case GNM_VALIDATION_STYLE_WARNING:
		res0 = GNM_VALIDATION_STATUS_VALID;
		btn0 = _("_Accept");
		res1 = GNM_VALIDATION_STATUS_INVALID_DISCARD;
		btn1 = _("_Discard");
		type = GTK_MESSAGE_WARNING;
		break;
	case GNM_VALIDATION_STYLE_INFO:
		res0 = GNM_VALIDATION_STATUS_VALID;
		btn0 = GNM_STOCK_OK;
		btn1 = NULL;
		type = GTK_MESSAGE_INFO;
		break;
	case GNM_VALIDATION_STYLE_PARSE_ERROR:
		res0 = GNM_VALIDATION_STATUS_INVALID_EDIT;
		btn0 = _("_Re-Edit");
		res1 = GNM_VALIDATION_STATUS_VALID;
		btn1 = _("_Accept");
		type = GTK_MESSAGE_ERROR;
		break;

	default:
		g_assert_not_reached ();
	}

	dialog = gtk_message_dialog_new (wbcg_toplevel (wbcg),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		type, GTK_BUTTONS_NONE, "%s", msg);
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
	WorkbookControl *wbc = GNM_WBC (wbcg);
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
	DISCONNECT (wbcg->sig_wbv, sig_auto_expr_attrs);
	DISCONNECT (wbcg->sig_wbv, sig_show_horizontal_scrollbar);
	DISCONNECT (wbcg->sig_wbv, sig_show_vertical_scrollbar);
	DISCONNECT (wbcg->sig_wbv, sig_show_notebook_tabs);
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
			 "notify::auto-expr-value",
			 G_CALLBACK (wbcg_auto_expr_value_changed),
			 wbcg,
			 0);
		wbcg_auto_expr_value_changed (wbv, NULL, wbcg);

		wbcg->sig_show_horizontal_scrollbar =
			g_signal_connect_object
			(G_OBJECT (wbv),
			 "notify::show-horizontal-scrollbar",
			 G_CALLBACK (wbcg_scrollbar_visibility),
			 wbcg,
			 0);
		wbcg->sig_show_vertical_scrollbar =
			g_signal_connect_object
			(G_OBJECT (wbv),
			 "notify::show-vertical-scrollbar",
			 G_CALLBACK (wbcg_scrollbar_visibility),
			 wbcg,
			 0);
		wbcg->sig_show_notebook_tabs =
			g_signal_connect_object
			(G_OBJECT (wbv),
			 "notify::show-notebook-tabs",
			 G_CALLBACK (wbcg_notebook_tabs_visibility),
			 wbcg,
			 0);
		wbcg_notebook_tabs_visibility (wbv, NULL, wbcg);
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

/****************************************************************************/

static void
set_font_name_feedback (GtkAction *act, const char *family)
{
	PangoFontDescription *desc = pango_font_description_new ();
	pango_font_description_set_family (desc, family);
	wbcg_font_action_set_font_desc (act, desc);
	pango_font_description_free (desc);
}

static void
set_font_size_feedback (GtkAction *act, double size)
{
	PangoFontDescription *desc = pango_font_description_new ();
	pango_font_description_set_size (desc, size * PANGO_SCALE);
	wbcg_font_action_set_font_desc (act, desc);
	pango_font_description_free (desc);
}

/****************************************************************************/

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
		wbcg_set_action_feedback (wbcg, wbcg->font.bold,
			gnm_style_get_font_bold (changes));
	if (gnm_style_is_element_set (changes, MSTYLE_FONT_ITALIC))
		wbcg_set_action_feedback (wbcg, wbcg->font.italic,
			gnm_style_get_font_italic (changes));
	if (gnm_style_is_element_set (changes, MSTYLE_FONT_UNDERLINE)) {
		wbcg_set_action_feedback (wbcg, wbcg->font.underline,
			gnm_style_get_font_uline (changes) == UNDERLINE_SINGLE);
		wbcg_set_action_feedback (wbcg, wbcg->font.d_underline,
			gnm_style_get_font_uline (changes) == UNDERLINE_DOUBLE);
		wbcg_set_action_feedback (wbcg, wbcg->font.sl_underline,
			gnm_style_get_font_uline (changes) == UNDERLINE_SINGLE_LOW);
		wbcg_set_action_feedback (wbcg, wbcg->font.dl_underline,
			gnm_style_get_font_uline (changes) == UNDERLINE_DOUBLE_LOW);
	}
	if (gnm_style_is_element_set (changes, MSTYLE_FONT_STRIKETHROUGH))
		wbcg_set_action_feedback (wbcg, wbcg->font.strikethrough,
			gnm_style_get_font_strike (changes));

	if (gnm_style_is_element_set (changes, MSTYLE_FONT_SCRIPT)) {
		wbcg_set_action_feedback (wbcg, wbcg->font.superscript,
			gnm_style_get_font_script (changes) == GO_FONT_SCRIPT_SUPER);
		wbcg_set_action_feedback (wbcg, wbcg->font.subscript,
			gnm_style_get_font_script (changes) == GO_FONT_SCRIPT_SUB);
	} else {
		wbcg_set_action_feedback (wbcg, wbcg->font.superscript, FALSE);
		wbcg_set_action_feedback (wbcg, wbcg->font.subscript, FALSE);
	}

	if (gnm_style_is_element_set (changes, MSTYLE_ALIGN_H)) {
		GnmHAlign align = gnm_style_get_align_h (changes);
		wbcg_set_action_feedback (wbcg, wbcg->h_align.left,
			align == GNM_HALIGN_LEFT);
		wbcg_set_action_feedback (wbcg, wbcg->h_align.center,
			align == GNM_HALIGN_CENTER);
		wbcg_set_action_feedback (wbcg, wbcg->h_align.right,
			align == GNM_HALIGN_RIGHT);
		wbcg_set_action_feedback (wbcg, wbcg->h_align.center_across_selection,
			align == GNM_HALIGN_CENTER_ACROSS_SELECTION);
		go_action_combo_pixmaps_select_id (wbcg->halignment, align);
	}
	if (gnm_style_is_element_set (changes, MSTYLE_ALIGN_V)) {
		GnmVAlign align = gnm_style_get_align_v (changes);
		wbcg_set_action_feedback (wbcg, wbcg->v_align.top,
			align == GNM_VALIGN_TOP);
		wbcg_set_action_feedback (wbcg, wbcg->v_align.bottom,
			align == GNM_VALIGN_BOTTOM);
		wbcg_set_action_feedback (wbcg, wbcg->v_align.center,
			align == GNM_VALIGN_CENTER);
		go_action_combo_pixmaps_select_id (wbcg->valignment, align);
	}

	if (gnm_style_is_element_set (changes, MSTYLE_FONT_SIZE)) {
		set_font_size_feedback (wbcg->font_name_haction,
					gnm_style_get_font_size (changes));
		set_font_size_feedback (wbcg->font_name_vaction,
					gnm_style_get_font_size (changes));
	}

	if (gnm_style_is_element_set (changes, MSTYLE_FONT_NAME)) {
		set_font_name_feedback (wbcg->font_name_haction,
					gnm_style_get_font_name (changes));
		set_font_name_feedback (wbcg->font_name_vaction,
					gnm_style_get_font_name (changes));
	}

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
	gtk_toolbar_set_show_arrow (toolbar, attached);
}

static char const *
get_accel_label (GtkMenuItem *item, guint *key)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (item));
	GList *l;
	char const *res = NULL;

	*key = GDK_KEY_VoidSymbol;
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

		if (key != GDK_KEY_VoidSymbol) {
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
	char *basename = GO_DOC (wb)->uri
		? go_basename_from_uri (GO_DOC (wb)->uri)
		: NULL;

	/* How many controls are there?  */
	count = 0;
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc, {
		if (GNM_IS_WBC_GTK (wbc))
			count++;
	});

	k = 1;
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc, {
		if (i >= 20)
			return i;
		if (GNM_IS_WBC_GTK (wbc) && basename) {
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
			i++;
		}});
	g_free (basename);
	return i;
}

static void
cb_regenerate_window_menu (WBCGtk *gtk)
{
	Workbook *wb = wb_control_get_workbook (GNM_WBC (gtk));
	GList const *ptr;
	unsigned i;

	/* This can happen during exit.  */
	if (!wb)
		return;

	if (gtk->windows.merge_id != 0)
		gtk_ui_manager_remove_ui (gtk->ui, gtk->windows.merge_id);
	gtk->windows.merge_id = gtk_ui_manager_new_merge_id (gtk->ui);

	if (gtk->windows.actions != NULL) {
		gtk_ui_manager_remove_action_group (gtk->ui,
			gtk->windows.actions);
		g_object_unref (gtk->windows.actions);
	}
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
			"/menubar/View/Windows", name, name,
			GTK_UI_MANAGER_AUTO, TRUE);
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

	g_return_if_fail (action != NULL);
	g_return_if_fail (action->handler != NULL);

	action->handler (action, wbc, action->data);
}

static void
cb_add_custom_ui (G_GNUC_UNUSED GnmApp *app,
		  GnmAppExtraUI *extra_ui, WBCGtk *gtk)
{
	CustomUIHandle  *details;
	GSList		*ptr;
	GError          *error = NULL;
	const char *ui_substr;

	details = g_new0 (CustomUIHandle, 1);
	details->actions = gtk_action_group_new (extra_ui->group_name);

	for (ptr = extra_ui->actions; ptr != NULL ; ptr = ptr->next) {
		GnmAction *action = ptr->data;
		GtkAction *res;
		GtkActionEntry entry;

		entry.name = action->id;
		entry.stock_id = action->icon_name;
		entry.label = action->label;
		entry.accelerator = NULL;
		entry.tooltip = NULL;
		entry.callback = G_CALLBACK (cb_custom_ui_handler);
		gtk_action_group_add_actions (details->actions, &entry, 1, gtk);
		res = gtk_action_group_get_action (details->actions, action->id);
		g_object_set_data (G_OBJECT (res), "GnmAction", action);
	}
	gtk_ui_manager_insert_action_group (gtk->ui, details->actions, 0);

	ui_substr = strstr (extra_ui->layout, "<ui>");
	if (ui_substr == extra_ui->layout)
		ui_substr = NULL;

	details->merge_id = gtk_ui_manager_add_ui_from_string
		(gtk->ui, extra_ui->layout, -1, ui_substr ? NULL : &error);
	if (details->merge_id == 0 && ui_substr) {
		/* Work around bug 569724.  */
		details->merge_id = gtk_ui_manager_add_ui_from_string
			(gtk->ui, ui_substr, -1, &error);
	}

	if (error) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
		gtk_ui_manager_remove_action_group (gtk->ui, details->actions);
		g_object_unref (details->actions);
		g_free (details);
	} else {
		g_hash_table_insert (gtk->custom_uis, extra_ui, details);
	}
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
	GtkWidget *box = gtk_widget_get_parent (GTK_WIDGET (tb));

	static const GtkOrientation orientations[] = {
		GTK_ORIENTATION_VERTICAL, GTK_ORIENTATION_VERTICAL,
		GTK_ORIENTATION_HORIZONTAL, GTK_ORIENTATION_HORIZONTAL
	};

	gtk_orientable_set_orientation (GTK_ORIENTABLE (tb),
					orientations[pos]);

	if (GTK_IS_HANDLE_BOX (box)) {
		static const GtkPositionType hdlpos[] = {
			GTK_POS_TOP, GTK_POS_TOP,
			GTK_POS_LEFT, GTK_POS_LEFT
		};

		gtk_handle_box_set_handle_position (GTK_HANDLE_BOX (box),
						    hdlpos[pos]);
	}
	if (pos == GTK_POS_TOP || pos == GTK_POS_BOTTOM)
		g_object_set (G_OBJECT (tb), "hexpand", TRUE, "vexpand", FALSE, NULL);
	else
		g_object_set (G_OBJECT (tb), "vexpand", TRUE, "hexpand", FALSE, NULL);
}

static void
set_toolbar_position (GtkToolbar *tb, GtkPositionType pos, WBCGtk *gtk)
{
	GtkWidget *box = gtk_widget_get_parent (GTK_WIDGET (tb));
	GtkContainer *zone = GTK_CONTAINER (gtk_widget_get_parent (GTK_WIDGET (box)));
	GtkContainer *new_zone = GTK_CONTAINER (gtk->toolbar_zones[pos]);
	char const *name = g_object_get_data (G_OBJECT (box), "name");
	const char *key = "toolbar-order";
	int n = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (box), key));
	GList *children, *l;
	int cpos = 0;

	if (zone == new_zone)
		return;

	g_object_ref (box);
	if (zone)
		gtk_container_remove (zone, box);
	set_toolbar_style_for_position (tb, pos);

	children = gtk_container_get_children (new_zone);
	for (l = children; l; l = l->next) {
		GObject *child = l->data;
		int nc = GPOINTER_TO_INT (g_object_get_data (child, key));
		if (nc < n) cpos++;
	}
	g_list_free (children);

	gtk_container_add (new_zone, box);
	gtk_container_child_set (new_zone, box, "position", cpos, NULL);

	g_object_unref (box);

	if (zone && name)
		gnm_conf_set_toolbar_position (name, pos);
}

static void
cb_set_toolbar_position (GtkMenuItem *item, WBCGtk *gtk)
{
	GtkToolbar *tb = g_object_get_data (G_OBJECT (item), "toolbar");
	GtkPositionType side = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "side"));

	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (item)))
		set_toolbar_position (tb, side, gtk);
}

static void
cb_tcm_hide (GtkWidget *widget, GtkWidget *box)
{
	gtk_widget_hide (box);
}

static void
toolbar_context_menu (GtkToolbar *tb, WBCGtk *gtk, GdkEvent *event)
{
	GtkWidget *box = gtk_widget_get_parent (GTK_WIDGET (tb));
	GtkWidget *zone = gtk_widget_get_parent (GTK_WIDGET (box));
	GtkWidget *menu = gtk_menu_new ();
	GtkWidget *item;
	GSList *group = NULL;
	size_t ui;

	static const struct {
		char const *text;
		GtkPositionType pos;
	} pos_items[] = {
		{ N_("Display toolbar above sheets"), GTK_POS_TOP },
		{ N_("Display toolbar to the left of sheets"), GTK_POS_LEFT },
		{ N_("Display toolbar to the right of sheets"), GTK_POS_RIGHT }
	};

	if (gnm_debug_flag ("toolbar-size"))
		dump_size_tree (GTK_WIDGET (tb), GINT_TO_POINTER (0));

	for (ui = 0; ui < G_N_ELEMENTS (pos_items); ui++) {
		char const *text = _(pos_items[ui].text);
		GtkPositionType pos = pos_items[ui].pos;

		item = gtk_radio_menu_item_new_with_label (group, text);
		group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));

		gtk_check_menu_item_set_active
			(GTK_CHECK_MENU_ITEM (item),
			 (zone == gtk->toolbar_zones[pos]));

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_object_set_data (G_OBJECT (item), "toolbar", tb);
		g_object_set_data (G_OBJECT (item), "side", GINT_TO_POINTER (pos));
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (cb_set_toolbar_position),
				  gtk);
	}

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);

	item = gtk_menu_item_new_with_label (_("Hide"));
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate",
			  G_CALLBACK (cb_tcm_hide),
			  box);

	gtk_widget_show_all (menu);
	gnumeric_popup_menu (GTK_MENU (menu), event);
}

static gboolean
cb_toolbar_button_press (GtkToolbar *tb, GdkEvent *event, WBCGtk *gtk)
{
	if (event->type == GDK_BUTTON_PRESS &&
	    event->button.button == 3) {
		toolbar_context_menu (tb, gtk, event);
		return TRUE;
	}

	return FALSE;
}

static gboolean
cb_handlebox_button_press (GtkHandleBox *hdlbox, GdkEvent *event, WBCGtk *gtk)
{
	if (event->type == GDK_BUTTON_PRESS &&
	    event->button.button == 3) {
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
	gboolean visible = gtk_widget_get_visible (box);

	gtk_toggle_action_set_active (toggle_action, visible);
	if (!wbcg->is_fullscreen) {
		/*
		 * We do not persist changes made going-to/while-in/leaving
		 * fullscreen mode.
		 */
		gnm_conf_set_toolbar_visible (name, visible);
	}
}

static struct ToolbarInfo {
	const char *name;
	const char *menu_text;
	const char *accel;
} toolbar_info[] = {
	{ "StandardToolbar", N_("Standard Toolbar"), "<control>7" },
	{ "FormatToolbar", N_("Format Toolbar"), NULL },
	{ "ObjectToolbar", N_("Object Toolbar"), NULL },
	{ NULL, NULL, NULL }
};


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
		gboolean visible = gnm_conf_get_toolbar_visible (name);
		int n = g_hash_table_size (wbcg->visibility_widgets);
		GtkWidget *vw;
		const struct ToolbarInfo *ti;
		GtkWidget *box;
		GtkPositionType pos = gnm_conf_get_toolbar_position (name);

		// See bug 761142.  This isn't supposed to be necessary.
		gtk_style_context_invalidate (gtk_widget_get_style_context (w));

		if (gnm_conf_get_detachable_toolbars ()) {
			box = gtk_handle_box_new ();
			g_object_connect (box,
				"signal::child_attached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (TRUE),
				"signal::child_detached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (FALSE),
				NULL);
		} else
			box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
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
		g_object_set_data (G_OBJECT (box), "toolbar-order",
				   GINT_TO_POINTER (n));
		set_toolbar_position (GTK_TOOLBAR (w), pos, gtk);

		g_signal_connect (box,
				  "notify::visible",
				  G_CALLBACK (cb_toolbar_box_visible),
				  gtk);
		g_object_set_data_full (G_OBJECT (box), "name",
					g_strdup (name),
					(GDestroyNotify)g_free);

		vw = box;
		g_hash_table_insert (wbcg->visibility_widgets,
				     g_strdup (toggle_name),
				     g_object_ref (vw));

		gtk_toolbar_set_show_arrow (GTK_TOOLBAR (w), TRUE);
		gtk_toolbar_set_style (GTK_TOOLBAR (w), GTK_TOOLBAR_ICONS);
		gtk_toolbar_set_icon_size (GTK_TOOLBAR (w), GTK_ICON_SIZE_SMALL_TOOLBAR);

		entry.name = toggle_name;
		entry.stock_id = NULL;
		entry.label = name;
		entry.accelerator = NULL;
		entry.tooltip = tooltip;
		entry.callback = G_CALLBACK (cb_toolbar_activate);
		entry.is_active = visible;

		for (ti = toolbar_info; ti->name; ti++) {
			if (strcmp (name, ti->name) == 0) {
				entry.label = _(ti->menu_text);
				entry.accelerator = ti->accel;
				break;
			}
		}

		gtk_action_group_add_toggle_actions (gtk->toolbar.actions,
			&entry, 1, wbcg);
		g_object_set_data (G_OBJECT (box), "toggle_action",
			gtk_action_group_get_action (gtk->toolbar.actions, toggle_name));
		gtk_ui_manager_add_ui (gtk->ui, gtk->toolbar.merge_id,
			"/menubar/View/Toolbars", toggle_name, toggle_name,
			GTK_UI_MANAGER_AUTO, FALSE);
		wbcg->hide_for_fullscreen =
			g_slist_prepend (wbcg->hide_for_fullscreen,
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
cb_post_activate (G_GNUC_UNUSED GtkUIManager *manager, GtkAction *action, WBCGtk *wbcg)
{
	if (!wbcg_is_editing (wbcg) && strcmp(gtk_action_get_name (action), "EditGotoCellIndicator") != 0)
		wbcg_focus_cur_scg (wbcg);
}

static void
cb_wbcg_window_state_event (GtkWidget           *widget,
			    GdkEventWindowState *event,
			    WBCGtk  *wbcg)
{
	gboolean new_val = (event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;
	if (!(event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) ||
	    new_val == wbcg->is_fullscreen ||
	    wbcg->updating_ui)
		return;

	wbc_gtk_set_toggle_action_state (wbcg, "ViewFullScreen", new_val);

	if (new_val) {
		GSList *l;

		wbcg->is_fullscreen = TRUE;
		for (l = wbcg->hide_for_fullscreen; l; l = l->next) {
			GtkToggleAction *ta = l->data;
			GOUndo *u;
			gboolean active = gtk_toggle_action_get_active (ta);
			u = go_undo_binary_new
				(ta, GUINT_TO_POINTER (active),
				 (GOUndoBinaryFunc)gtk_toggle_action_set_active,
				 NULL, NULL);
			wbcg->undo_for_fullscreen =
				go_undo_combine (wbcg->undo_for_fullscreen, u);
			gtk_toggle_action_set_active (ta, FALSE);
		}
	} else {
		if (wbcg->undo_for_fullscreen) {
			go_undo_undo (wbcg->undo_for_fullscreen);
			g_object_unref (wbcg->undo_for_fullscreen);
			wbcg->undo_for_fullscreen = NULL;
		}
		wbcg->is_fullscreen = FALSE;
	}
}

/****************************************************************************/

static void
cb_auto_expr_cell_changed (GtkWidget *item, WBCGtk *wbcg)
{
	WorkbookView *wbv = wb_control_view (GNM_WBC (wbcg));
	const GnmEvalPos *ep;
	GnmExprTop const *texpr;
	GnmValue const *v;

	if (wbcg->updating_ui)
		return;

	ep = g_object_get_data (G_OBJECT (item), "evalpos");

	g_object_set (wbv,
		      "auto-expr-func", NULL,
		      "auto-expr-descr", NULL,
		      "auto-expr-eval-pos", ep,
		      NULL);

	/* Now we have the expression set.  */
	texpr = wbv->auto_expr.dep.base.texpr;
	v = gnm_expr_top_get_constant (texpr);
	if (v)
		g_object_set (wbv,
			      "auto-expr-descr", value_peek_string (v),
			      NULL);
}

static void
cb_auto_expr_changed (GtkWidget *item, WBCGtk *wbcg)
{
	const GnmFunc *func;
	const char *descr;
	WorkbookView *wbv = wb_control_view (GNM_WBC (wbcg));

	if (wbcg->updating_ui)
		return;

	func = g_object_get_data (G_OBJECT (item), "func");
	descr = g_object_get_data (G_OBJECT (item), "descr");

	g_object_set (wbv,
		      "auto-expr-func", func,
		      "auto-expr-descr", descr,
		      "auto-expr-eval-pos", NULL,
		      NULL);
}

static void
cb_auto_expr_precision_toggled (GtkWidget *item, WBCGtk *wbcg)
{
	WorkbookView *wbv = wb_control_view (GNM_WBC (wbcg));
	if (wbcg->updating_ui)
		return;

	go_object_toggle (wbv, "auto-expr-max-precision");
}

static void
cb_auto_expr_insert_formula (WBCGtk *wbcg, gboolean below)
{
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	GnmRange const *selection = selection_first_range (scg_view (scg), NULL, NULL);
	GnmRange output;
	GnmRange *input;
	gboolean multiple, use_last_cr;
	data_analysis_output_t *dao;
	analysis_tools_data_auto_expression_t *specs;

	g_return_if_fail (selection != NULL);

	if (below) {
		multiple = (range_width (selection) > 1);
		output = *selection;
		range_normalize (&output);
		output.start.row = output.end.row;
		use_last_cr = (range_height (selection) > 1) && sheet_is_region_empty (scg_sheet (scg), &output);
		if (!use_last_cr) {
			if (range_translate (&output, scg_sheet (scg), 0, 1))
				return;
			if (multiple && gnm_sheet_get_last_col (scg_sheet (scg)) > output.end.col)
				output.end.col++;
		}
		input = gnm_range_dup (selection);
		range_normalize (input);
		if (use_last_cr)
			input->end.row--;
	} else {
		multiple = (range_height (selection) > 1);
		output = *selection;
		range_normalize (&output);
		output.start.col = output.end.col;
		use_last_cr = (range_width (selection) > 1) && sheet_is_region_empty (scg_sheet (scg), &output);
		if (!use_last_cr) {
			if (range_translate (&output, scg_sheet (scg), 1, 0))
				return;
			if (multiple && gnm_sheet_get_last_row (scg_sheet (scg)) > output.end.row)
				output.end.row++;
		}
		input = gnm_range_dup (selection);
		range_normalize (input);
		if (use_last_cr)
			input->end.col--;
	}


	dao = dao_init (NULL, RangeOutput);
	dao->start_col         = output.start.col;
	dao->start_row         = output.start.row;
	dao->cols              = range_width (&output);
	dao->rows              = range_height (&output);
	dao->sheet             = scg_sheet (scg);
	dao->autofit_flag      = FALSE;
	dao->put_formulas      = TRUE;

	specs = g_new0 (analysis_tools_data_auto_expression_t, 1);
	specs->base.wbc = GNM_WBC (wbcg);
	specs->base.input = g_slist_prepend (NULL, value_new_cellrange_r (scg_sheet (scg), input));
	g_free (input);
	specs->base.group_by = below ? GROUPED_BY_COL : GROUPED_BY_ROW;
	specs->base.labels = FALSE;
	specs->multiple = multiple;
	specs->below = below;
	specs->func = NULL;
	g_object_get (G_OBJECT (wb_control_view (GNM_WBC (wbcg))),
		      "auto-expr-func", &(specs->func), NULL);
	if (specs->func == NULL) {
		specs->func =  gnm_func_lookup_or_add_placeholder ("sum");
		gnm_func_inc_usage (specs->func);
	}

	cmd_analysis_tool (GNM_WBC (wbcg), scg_sheet (scg),
			   dao, specs, analysis_tool_auto_expression_engine,
			   TRUE);
}

static void
cb_auto_expr_insert_formula_below (G_GNUC_UNUSED GtkWidget *item, WBCGtk *wbcg)
{
	cb_auto_expr_insert_formula (wbcg, TRUE);
}

static void
cb_auto_expr_insert_formula_to_side (G_GNUC_UNUSED GtkWidget *item, WBCGtk *wbcg)
{
	cb_auto_expr_insert_formula (wbcg, FALSE);
}


static gboolean
cb_select_auto_expr (GtkWidget *widget, GdkEvent *event, WBCGtk *wbcg)
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

	WorkbookView *wbv = wb_control_view (GNM_WBC (wbcg));
	Sheet *sheet = wb_view_cur_sheet (wbv);
	GtkWidget *item, *menu;
	int i;
	char *cell_item;
	GnmCellPos const *pos;
	GnmEvalPos ep;

	if (event->button.button != 3)
		return FALSE;

	menu = gtk_menu_new ();

	for (i = 0; quick_compute_routines[i].displayed_name; i++) {
		GnmParsePos pp;
		char const *fname = quick_compute_routines[i].function;
		char const *dispname =
			_(quick_compute_routines[i].displayed_name);
		GnmExprTop const *new_auto_expr;
		GtkWidget *item;
		char *expr_txt;

		/* Test the expression...  */
		parse_pos_init (&pp, sheet->workbook, sheet, 0, 0);
		expr_txt = g_strconcat (fname, "(",
					parsepos_as_string (&pp),
					")",  NULL);
		new_auto_expr = gnm_expr_parse_str
			(expr_txt, &pp, GNM_EXPR_PARSE_DEFAULT,
			 sheet_get_conventions (sheet), NULL);
		g_free (expr_txt);
		if (!new_auto_expr)
			continue;
		gnm_expr_top_unref (new_auto_expr);

		item = gtk_menu_item_new_with_label (dispname);
		g_object_set_data (G_OBJECT (item),
				   "func", gnm_func_lookup (fname, NULL));
		g_object_set_data (G_OBJECT (item),
				   "descr", (gpointer)dispname);
		g_signal_connect (G_OBJECT (item),
			"activate",
			G_CALLBACK (cb_auto_expr_changed), wbcg);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	pos = &(scg_view (wbcg_cur_scg (wbcg)))->edit_pos;
	eval_pos_init_pos (&ep, sheet, pos);
	cell_item = g_strdup_printf (_("Content of %s"), cellpos_as_string (pos));
	item = gtk_menu_item_new_with_label (cell_item);
	g_free (cell_item);
	g_object_set_data_full (G_OBJECT (item),
				"evalpos", go_memdup (&ep, sizeof (ep)),
				(GDestroyNotify)g_free);
	g_signal_connect (G_OBJECT (item), "activate",
		G_CALLBACK (cb_auto_expr_cell_changed), wbcg);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_check_menu_item_new_with_label (_("Use Maximum Precision"));
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
		wbv->auto_expr.use_max_precision);
	g_signal_connect (G_OBJECT (item), "activate",
		G_CALLBACK (cb_auto_expr_precision_toggled), wbcg);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_menu_item_new_with_label (_("Insert Formula Below"));
	g_signal_connect (G_OBJECT (item), "activate",
		G_CALLBACK (cb_auto_expr_insert_formula_below), wbcg);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_menu_item_new_with_label (_("Insert Formula to Side"));
	g_signal_connect (G_OBJECT (item), "activate",
		G_CALLBACK (cb_auto_expr_insert_formula_to_side), wbcg);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	gnumeric_popup_menu (GTK_MENU (menu), event);
	return TRUE;
}

static void
wbc_gtk_create_status_area (WBCGtk *wbcg)
{
	GtkWidget *ebox;

	g_object_ref (wbcg->auto_expr_label);
	gtk_label_set_max_width_chars (GTK_LABEL (wbcg->auto_expr_label),
				       strlen (AUTO_EXPR_SAMPLE));
	gtk_widget_set_size_request
		(wbcg->auto_expr_label,
		 gnm_widget_measure_string (GTK_WIDGET (wbcg->toplevel),
					    AUTO_EXPR_SAMPLE),
		 -1);

	gtk_widget_set_size_request
		(wbcg->status_text,
		 gnm_widget_measure_string (GTK_WIDGET (wbcg->toplevel),
					    "W") * 5,
		 -1);
	ebox = GET_GUI_ITEM ("auto_expr_event_box");
	gtk_style_context_add_class (gtk_widget_get_style_context (ebox),
				     "auto-expr");
	g_signal_connect (G_OBJECT (ebox),
		"button_press_event",
		G_CALLBACK (cb_select_auto_expr), wbcg);

	g_hash_table_insert (wbcg->visibility_widgets,
			     g_strdup ("ViewStatusbar"),
			     g_object_ref (wbcg->status_area));

	/* disable statusbar by default going to fullscreen */
	wbcg->hide_for_fullscreen =
		g_slist_prepend (wbcg->hide_for_fullscreen,
				 wbcg_find_action (wbcg, "ViewStatusbar"));
	g_assert (wbcg->hide_for_fullscreen->data);
}

/****************************************************************************/

static void
cb_file_history_activate (GObject *action, WBCGtk *wbcg)
{
	gui_file_read (wbcg, g_object_get_data (action, "uri"), NULL, NULL);
}

static void
wbc_gtk_reload_recent_file_menu (WBCGtk *wbcg)
{
	WBCGtk *gtk = (WBCGtk *)wbcg;
	GSList *history, *ptr;
	unsigned i;
	gboolean any_history;
	GtkAction *full_history;

	if (gtk->file_history.merge_id != 0)
		gtk_ui_manager_remove_ui (gtk->ui, gtk->file_history.merge_id);
	gtk->file_history.merge_id = gtk_ui_manager_new_merge_id (gtk->ui);

	if (gtk->file_history.actions != NULL) {
		gtk_ui_manager_remove_action_group (gtk->ui,
			gtk->file_history.actions);
		g_object_unref (gtk->file_history.actions);
	}
	gtk->file_history.actions = gtk_action_group_new ("FileHistory");

	/* create the actions */
	history = gnm_app_history_get_list (3);
	any_history = (history != NULL);
	for (i = 1, ptr = history; ptr != NULL ; ptr = ptr->next, i++) {
		GtkActionEntry entry;
		GtkAction *action;
		char const *uri = ptr->data;
		char *name = g_strdup_printf ("FileHistoryEntry%d", i);
		char *label = gnm_history_item_label (uri, i);
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
	g_slist_free_full (history, (GDestroyNotify)g_free);

	gtk_ui_manager_insert_action_group (gtk->ui, gtk->file_history.actions, 0);

	/* merge them in */
	while (i-- > 1) {
		char *name = g_strdup_printf ("FileHistoryEntry%d", i);
		gtk_ui_manager_add_ui (gtk->ui, gtk->file_history.merge_id,
			"/menubar/File/FileHistory", name, name,
			GTK_UI_MANAGER_AUTO, TRUE);
		g_free (name);
	}

	full_history = wbcg_find_action (wbcg, "FileHistoryFull");
	g_object_set (G_OBJECT (full_history), "sensitive", any_history, NULL);
}

static void
cb_new_from_template (GObject *action, WBCGtk *wbcg)
{
	const char *uri = g_object_get_data (action, "uri");
	gnm_gui_file_template (wbcg, uri);
}

static void
add_template_dir (const char *path, GHashTable *h)
{
	GDir *dir;
	const char *name;

	dir = g_dir_open (path, 0, NULL);
	if (!dir)
		return;

	while ((name = g_dir_read_name (dir))) {
		char *fullname = g_build_filename (path, name, NULL);

		/*
		 * Unconditionally remove, so we can link to /dev/null
		 * and cause a system file to be hidden.
		 */
		g_hash_table_remove (h, name);

		if (g_file_test (fullname, G_FILE_TEST_IS_REGULAR)) {
			char *uri = go_filename_to_uri (fullname);
			g_hash_table_insert (h, g_strdup (name), uri);
		}
		g_free (fullname);
	}
	g_dir_close (dir);
}

static void
wbc_gtk_reload_templates (WBCGtk *gtk)
{
	unsigned i;
	GSList *l, *names;
	char *path;
	GHashTable *h;

	if (gtk->templates.merge_id != 0)
		gtk_ui_manager_remove_ui (gtk->ui, gtk->templates.merge_id);
	gtk->templates.merge_id = gtk_ui_manager_new_merge_id (gtk->ui);

	if (gtk->templates.actions != NULL) {
		gtk_ui_manager_remove_action_group (gtk->ui,
			gtk->templates.actions);
		g_object_unref (gtk->templates.actions);
	}
	gtk->templates.actions = gtk_action_group_new ("TemplateList");

	gtk_ui_manager_insert_action_group (gtk->ui, gtk->templates.actions, 0);

	h = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	path = g_build_filename (gnm_sys_data_dir (), "templates", NULL);
	add_template_dir (path, h);
	g_free (path);

	/* Possibly override the above with user templates without version.  */
	path = g_build_filename (gnm_usr_dir (FALSE), "templates", NULL);
	add_template_dir (path, h);
	g_free (path);

	/* Possibly override the above with user templates with version.  */
	path = g_build_filename (gnm_usr_dir (TRUE), "templates", NULL);
	add_template_dir (path, h);
	g_free (path);

	names = g_slist_sort (go_hash_keys (h), (GCompareFunc)g_utf8_collate);

	for (i = 1, l = names; l; l = l->next) {
		const char *uri = g_hash_table_lookup (h, l->data);
		GString *label = g_string_new (NULL);
		GtkActionEntry entry;
		char *gname;
		const char *gpath;
		char *basename = go_basename_from_uri (uri);
		const char *s;
		GtkAction *action;

		if (i < 10) g_string_append_c (label, '_');
		g_string_append_printf (label, "%d ", i);

		for (s = basename; *s; s++) {
			if (*s == '_') g_string_append_c (label, '_');
			g_string_append_c (label, *s);
		}

		entry.name = gname = g_strdup_printf ("Template%d", i);
		entry.stock_id = NULL;
		entry.label = label->str;
		entry.accelerator = NULL;
		entry.tooltip = NULL;
		entry.callback = G_CALLBACK (cb_new_from_template);

		gtk_action_group_add_actions (gtk->templates.actions,
					      &entry, 1, gtk);

		action = gtk_action_group_get_action (gtk->templates.actions,
						      entry.name);

		g_object_set_data_full (G_OBJECT (action), "uri",
			g_strdup (uri), (GDestroyNotify)g_free);


		gpath = "/menubar/File/Templates";
		gtk_ui_manager_add_ui (gtk->ui, gtk->templates.merge_id,
				       gpath, gname, gname,
				       GTK_UI_MANAGER_AUTO, FALSE);

		g_string_free (label, TRUE);
		g_free (gname);
		g_free (basename);
		i++;
	}

	g_slist_free (names);
	g_hash_table_destroy (h);
}

gboolean
wbc_gtk_load_templates (WBCGtk *wbcg)
{
	if (wbcg->templates.merge_id == 0) {
		wbc_gtk_reload_templates (wbcg);
	}

	wbcg->template_loader_handler = 0;
	return FALSE;
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
		"resizable", TRUE,
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
	g_signal_connect (w, "screen-changed",
		G_CALLBACK (cb_screen_changed), NULL);
	cb_screen_changed (w);

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

	if (wbcg->template_loader_handler != 0) {
		g_source_remove (wbcg->template_loader_handler);
		wbcg->template_loader_handler = 0;
	}

	if (wbcg->file_history.merge_id != 0)
		gtk_ui_manager_remove_ui (wbcg->ui, wbcg->file_history.merge_id);
	g_clear_object (&wbcg->file_history.actions);

	if (wbcg->toolbar.merge_id != 0)
		gtk_ui_manager_remove_ui (wbcg->ui, wbcg->toolbar.merge_id);
	g_clear_object (&wbcg->toolbar.actions);

	if (wbcg->windows.merge_id != 0)
		gtk_ui_manager_remove_ui (wbcg->ui, wbcg->windows.merge_id);
	g_clear_object (&wbcg->windows.actions);

	if (wbcg->templates.merge_id != 0)
		gtk_ui_manager_remove_ui (wbcg->ui, wbcg->templates.merge_id);
	g_clear_object (&wbcg->templates.actions);

	{
		GSList *l, *uis = go_hash_keys (wbcg->custom_uis);
		for (l = uis; l; l = l->next) {
			GnmAppExtraUI *extra_ui = l->data;
			cb_remove_custom_ui (NULL, extra_ui, wbcg);
		}
		g_slist_free (uis);
	}

	g_hash_table_destroy (wbcg->custom_uis);
	wbcg->custom_uis = NULL;

	g_clear_object (&wbcg->zoom_vaction);
	g_clear_object (&wbcg->zoom_haction);
	g_clear_object (&wbcg->borders);
	g_clear_object (&wbcg->fore_color);
	g_clear_object (&wbcg->back_color);
	g_clear_object (&wbcg->font_name_haction);
	g_clear_object (&wbcg->font_name_vaction);
	g_clear_object (&wbcg->redo_haction);
	g_clear_object (&wbcg->redo_vaction);
	g_clear_object (&wbcg->undo_haction);
	g_clear_object (&wbcg->undo_vaction);
	g_clear_object (&wbcg->halignment);
	g_clear_object (&wbcg->valignment);
	g_clear_object (&wbcg->actions);
	g_clear_object (&wbcg->permanent_actions);
	g_clear_object (&wbcg->font_actions);
	g_clear_object (&wbcg->data_only_actions);
	g_clear_object (&wbcg->semi_permanent_actions);
	g_clear_object (&wbcg->ui);

	/* Disconnect signals that would attempt to change things during
	 * destruction.
	 */

	wbcg_autosave_cancel (wbcg);

	if (wbcg->bnotebook != NULL)
		g_signal_handlers_disconnect_by_func (
			G_OBJECT (wbcg->bnotebook),
			G_CALLBACK (cb_notebook_switch_page), wbcg);
	g_clear_object (&wbcg->bnotebook);

	g_signal_handlers_disconnect_by_func (
		G_OBJECT (wbcg->toplevel),
		G_CALLBACK (cb_set_focus), wbcg);

	wbcg_auto_complete_destroy (wbcg);

	gtk_window_set_focus (wbcg_toplevel (wbcg), NULL);

	if (wbcg->toplevel != NULL) {
		gtk_widget_destroy (wbcg->toplevel);
		wbcg->toplevel = NULL;
	}

	if (wbcg->font_desc) {
		pango_font_description_free (wbcg->font_desc);
		wbcg->font_desc = NULL;
	}

	g_clear_object (&wbcg->auto_expr_label);

	g_hash_table_destroy (wbcg->visibility_widgets);
	g_clear_object (&wbcg->undo_for_fullscreen);

	g_slist_free (wbcg->hide_for_fullscreen);
	wbcg->hide_for_fullscreen = NULL;


	g_free (wbcg->preferred_geometry);
	wbcg->preferred_geometry = NULL;

	g_clear_object (&wbcg->gui);

	parent_class->finalize (obj);
}

/***************************************************************************/

typedef struct {
	GnmExprEntry *entry;
	GogDataset *dataset;
	int dim_i;
	gboolean suppress_update;
	GogDataType data_type;
	gboolean changed;

	gulong dataset_changed_handler;
	gulong entry_update_handler;
	guint idle;
} GraphDimEditor;

static void
cb_graph_dim_editor_update (GnmExprEntry *gee,
			    G_GNUC_UNUSED gboolean user_requested,
			    GraphDimEditor *editor)
{
	GOData *data = NULL;
	Sheet *sheet;
	SheetControlGUI *scg;
	editor->changed = FALSE;

	/* Ignore changes while we are insensitive. useful for displaying
	 * values, without storing them as Data.  Also ignore updates if the
	 * dataset has been cleared via the weakref handler  */
	if (!gtk_widget_is_sensitive (GTK_WIDGET (gee)) ||
	    editor->dataset == NULL)
		return;

	scg = gnm_expr_entry_get_scg (gee);
	sheet = scg_sheet (scg);

	/* If we are setting something */
	if (!gnm_expr_entry_is_blank (editor->entry)) {
		GnmParsePos pos;
		GnmParseError  perr;
		GnmExprTop const *texpr;
		GnmExprParseFlags flags =
			(editor->data_type == GOG_DATA_VECTOR)?
				GNM_EXPR_PARSE_PERMIT_MULTIPLE_EXPRESSIONS |
				GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS:
				GNM_EXPR_PARSE_UNKNOWN_NAMES_ARE_STRINGS;

		parse_error_init (&perr);
		/* Setting start_sel=FALSE to avoid */
		/* https://bugzilla.gnome.org/show_bug.cgi?id=658223 */
		texpr = gnm_expr_entry_parse (editor->entry,
			parse_pos_init_sheet (&pos, sheet),
			&perr, FALSE, flags);

		/* TODO : add some error dialogs split out
		 * the code in workbook_edit to add parens.  */
		if (texpr == NULL) {
			if (editor->data_type == GOG_DATA_SCALAR)
				texpr = gnm_expr_top_new_constant (
					value_new_string (
						gnm_expr_entry_get_text (editor->entry)));
			else {
				g_return_if_fail (perr.err != NULL);

				wb_control_validation_msg (GNM_WBC (scg_wbcg (scg)),
					GNM_VALIDATION_STYLE_INFO, NULL, perr.err->message);
				parse_error_free (&perr);
				gtk_editable_select_region (GTK_EDITABLE (gnm_expr_entry_get_entry (editor->entry)), 0, G_MAXINT);
				editor->changed = TRUE;
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
	editor->suppress_update = TRUE;
	gog_dataset_set_dim (editor->dataset, editor->dim_i, data, NULL);
	editor->suppress_update = FALSE;
}

static gboolean
cb_update_idle (GraphDimEditor *editor)
{
	cb_graph_dim_editor_update (editor->entry, FALSE, editor);
	editor->idle = 0;
	return FALSE;
}

static void
graph_dim_cancel_idle (GraphDimEditor *editor)
{
	if (editor->idle) {
		g_source_remove (editor->idle);
		editor->idle = 0;
	}
}

static gboolean
cb_graph_dim_entry_focus_out_event (G_GNUC_UNUSED GtkEntry	*ignored,
				    G_GNUC_UNUSED GdkEventFocus	*event,
				    GraphDimEditor		*editor)
{
	if (!editor->changed)
		return FALSE;
	graph_dim_cancel_idle (editor);
	editor->idle = g_idle_add ((GSourceFunc) cb_update_idle, editor);

	return FALSE;
}

static void
cb_graph_dim_entry_changed (GraphDimEditor *editor)
{
	editor->changed = TRUE;
}

static void
set_entry_contents (GnmExprEntry *entry, GOData *val)
{
	if (GNM_IS_GO_DATA_SCALAR (val)) {
		GnmValue const *v = gnm_expr_top_get_constant (gnm_go_data_get_expr (val));
		if (v && VALUE_IS_NUMBER (v)) {
			double d = go_data_get_scalar_value (val);
			GODateConventions const *date_conv = go_data_date_conv (val);
			gog_data_editor_set_value_double (GOG_DATA_EDITOR (entry),
							  d, date_conv);
			return;
		}
	}

	if (GO_IS_DATA_SCALAR (val) && go_data_has_value (val)) {
		double d = go_data_get_scalar_value (val);
		GODateConventions const *date_conv = go_data_date_conv (val);
		gog_data_editor_set_value_double (GOG_DATA_EDITOR (entry),
		                                  d, date_conv);
			return;
	}

	{
		SheetControlGUI *scg = gnm_expr_entry_get_scg (entry);
		Sheet const *sheet = scg_sheet (scg);
		char *txt = go_data_serialize (val, (gpointer)sheet->convs);
		gnm_expr_entry_load_from_text (entry, txt);
		g_free (txt);
	}
}

static void
cb_dataset_changed (GogDataset *dataset,
		    gboolean resize,
		    GraphDimEditor *editor)
{
	GOData *val = gog_dataset_get_dim (dataset, editor->dim_i);
	if (val != NULL && !editor->suppress_update) {
		g_signal_handler_block (editor->entry,
					editor->entry_update_handler);
		set_entry_contents (editor->entry, val);
		g_signal_handler_unblock (editor->entry,
					  editor->entry_update_handler);
	}
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
	graph_dim_cancel_idle (editor);
	if (editor->dataset) {
		g_signal_handler_disconnect (editor->dataset, editor->dataset_changed_handler);
		g_object_weak_unref (G_OBJECT (editor->dataset),
			(GWeakNotify) cb_dim_editor_weakref_notify, editor);
	}
	g_free (editor);
}

static GogDataEditor *
wbcg_data_allocator_editor (GogDataAllocator *dalloc,
			    GogDataset *dataset, int dim_i, GogDataType data_type)
{
	WBCGtk *wbcg = WBC_GTK (dalloc);
	GraphDimEditor *editor;
	GOData *val;

	editor = g_new (GraphDimEditor, 1);
	editor->dataset		= dataset;
	editor->dim_i		= dim_i;
	editor->suppress_update = FALSE;
	editor->data_type	= data_type;
	editor->entry		= gnm_expr_entry_new (wbcg, TRUE);
	editor->idle            = 0;
	editor->changed		= FALSE;
	g_object_weak_ref (G_OBJECT (editor->dataset),
		(GWeakNotify) cb_dim_editor_weakref_notify, editor);

	gnm_expr_entry_set_update_policy (editor->entry,
		GNM_UPDATE_DISCONTINUOUS);

	val = gog_dataset_get_dim (dataset, dim_i);
	if (val != NULL) {
		set_entry_contents (editor->entry, val);
	}

	gnm_expr_entry_set_flags (editor->entry, GNM_EE_FORCE_ABS_REF, GNM_EE_MASK);

	editor->entry_update_handler = g_signal_connect (G_OBJECT (editor->entry),
		"update",
		G_CALLBACK (cb_graph_dim_editor_update), editor);
	g_signal_connect (G_OBJECT (gnm_expr_entry_get_entry (editor->entry)),
		"focus-out-event",
		G_CALLBACK (cb_graph_dim_entry_focus_out_event), editor);
	g_signal_connect_swapped (G_OBJECT (gnm_expr_entry_get_entry (editor->entry)),
		"changed",
		G_CALLBACK (cb_graph_dim_entry_changed), editor);
	editor->dataset_changed_handler = g_signal_connect (G_OBJECT (editor->dataset),
		"changed", G_CALLBACK (cb_dataset_changed), editor);
	g_object_set_data_full (G_OBJECT (editor->entry),
		"editor", editor, (GDestroyNotify) graph_dim_editor_free);

	return GOG_DATA_EDITOR (editor->entry);
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
			      GTK_MESSAGE_ERROR,
			      "%s", err->message);
}

static void
wbcg_error_error_info (GOCmdContext *cc, GOErrorInfo *error)
{
	gnm_go_error_info_dialog_show (
		wbcg_toplevel (WBC_GTK (cc)), error);
}

static void
wbcg_error_error_info_list (GOCmdContext *cc, GSList *errs)
{
	gnm_go_error_info_list_dialog_show
		(wbcg_toplevel (WBC_GTK (cc)), errs);
}

static void
wbcg_progress_set (GOCmdContext *cc, double val)
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
	iface->error.error_info_list	    = wbcg_error_error_info_list;
	iface->progress_set	    = wbcg_progress_set;
	iface->progress_message_set = wbcg_progress_message_set;
}

/*************************************************************************/

static void
wbc_gtk_class_init (GObjectClass *gobject_class)
{
	WorkbookControlClass *wbc_class =
		GNM_WBC_CLASS (gobject_class);

	g_return_if_fail (wbc_class != NULL);

	debug_tab_order = gnm_debug_flag ("tab-order");

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

        g_object_class_install_property (gobject_class,
		 WBG_GTK_PROP_AUTOSAVE_PROMPT,
		 g_param_spec_boolean ("autosave-prompt",
				       P_("Autosave prompt"),
				       P_("Ask about autosave?"),
				       FALSE,
				       GSF_PARAM_STATIC | G_PARAM_READWRITE));
        g_object_class_install_property (gobject_class,
		 WBG_GTK_PROP_AUTOSAVE_TIME,
		 g_param_spec_int ("autosave-time",
				   P_("Autosave time in seconds"),
				   P_("Seconds before autosave"),
				   0, G_MAXINT, 0,
				   GSF_PARAM_STATIC | G_PARAM_READWRITE));

	wbc_gtk_signals [WBC_GTK_MARKUP_CHANGED] = g_signal_new ("markup-changed",
		GNM_WBC_GTK_TYPE,
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (WBCGtkClass, markup_changed),
		NULL, NULL,
		g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE,
		0, G_TYPE_NONE);

	gtk_window_set_default_icon_name ("org.gnumeric.gnumeric");
}

static void
wbc_gtk_init (GObject *obj)
{
	WBCGtk		*wbcg = (WBCGtk *)obj;
	GError		*error = NULL;
	char		*uifile;
	unsigned	 i;
	GEnumClass      *posclass;
	GtkStyleContext *ctxt;
	guint            merge_id;

	wbcg->gui = gnm_gtk_builder_load ("res:ui/wbcg.ui", NULL, NULL);
	wbcg->cancel_button = GET_GUI_ITEM ("cancel_button");
	wbcg->ok_button = GET_GUI_ITEM ("ok_button");
	wbcg->func_button = GET_GUI_ITEM ("func_button");
	wbcg->progress_bar = GET_GUI_ITEM ("progress_bar");
	wbcg->auto_expr_label = GET_GUI_ITEM ("auto_expr_label");
	wbcg->status_text = GET_GUI_ITEM ("status_text");
	wbcg->tabs_paned = GET_GUI_ITEM ("tabs_paned");
	wbcg->status_area = GET_GUI_ITEM ("status_area");
	wbcg->notebook_area = GET_GUI_ITEM ("notebook_area");
	wbcg->snotebook = GET_GUI_ITEM ("snotebook");
	wbcg->selection_descriptor = GET_GUI_ITEM ("selection_descriptor");
	wbcg->menu_zone = GET_GUI_ITEM ("menu_zone");
	wbcg->toolbar_zones[GTK_POS_TOP] = GET_GUI_ITEM ("toolbar_zone_top");
	wbcg->toolbar_zones[GTK_POS_BOTTOM] = NULL;
	wbcg->toolbar_zones[GTK_POS_LEFT] = GET_GUI_ITEM ("toolbar_zone_left");
	wbcg->toolbar_zones[GTK_POS_RIGHT] = GET_GUI_ITEM ("toolbar_zone_right");
	wbcg->updating_ui = FALSE;

	posclass = G_ENUM_CLASS (g_type_class_peek (gtk_position_type_get_type ()));
	for (i = 0; i < posclass->n_values; i++) {
		GEnumValue const *ev = posclass->values + i;
		GtkWidget *zone = wbcg->toolbar_zones[ev->value];
		GtkStyleContext *ctxt;
		if (!zone)
			continue;
		ctxt = gtk_widget_get_style_context (zone);
		gtk_style_context_add_class (ctxt, "toolbarzone");
		gtk_style_context_add_class (ctxt, ev->value_nick);
	}

	wbcg->visibility_widgets = g_hash_table_new_full (g_str_hash,
		g_str_equal, (GDestroyNotify)g_free, (GDestroyNotify)g_object_unref);
	wbcg->undo_for_fullscreen = NULL;
	wbcg->hide_for_fullscreen = NULL;

	wbcg->autosave_prompt = FALSE;
	wbcg->autosave_time = 0;
	wbcg->autosave_timer = 0;

	/* We are not in edit mode */
	wbcg->editing	    = FALSE;
	wbcg->editing_sheet = NULL;
	wbcg->editing_cell  = NULL;

	wbcg->new_object = NULL;

	wbcg->idle_update_style_feedback = 0;

	wbcg_set_toplevel (wbcg, GET_GUI_ITEM ("toplevel"));
	ctxt = gtk_widget_get_style_context (GTK_WIDGET (wbcg_toplevel (wbcg)));
	gtk_style_context_add_class (ctxt, "gnumeric");

	g_signal_connect (wbcg_toplevel (wbcg), "window_state_event",
			  G_CALLBACK (cb_wbcg_window_state_event),
			  wbcg);

	wbc_gtk_init_actions (wbcg);
	wbcg->ui = gtk_ui_manager_new ();
	g_object_connect (wbcg->ui,
		"signal::add_widget",	 G_CALLBACK (cb_add_menus_toolbars), wbcg,
		"signal::connect_proxy",    G_CALLBACK (cb_connect_proxy), wbcg,
		"signal::disconnect_proxy", G_CALLBACK (cb_disconnect_proxy), wbcg,
		"signal::post_activate", G_CALLBACK (cb_post_activate), wbcg,
		NULL);
	if (extra_actions)
		gnm_action_group_add_actions (wbcg->actions, extra_actions,
		                              extra_actions_nb, wbcg);
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->permanent_actions, 0);
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->actions, 0);
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->font_actions, 0);
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->data_only_actions, 0);
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->semi_permanent_actions, 0);
	gtk_window_add_accel_group (wbcg_toplevel (wbcg),
		gtk_ui_manager_get_accel_group (wbcg->ui));

	if (uifilename) {
		if (strncmp (uifilename, "res:", 4) == 0)
			uifile = g_strdup (uifilename);
		else
			uifile = g_build_filename (gnm_sys_data_dir (),
						   uifilename,
						   NULL);
	} else
		uifile = g_strdup ("res:/org/gnumeric/gnumeric/ui/GNOME_Gnumeric-gtk.xml");

	if (strncmp (uifile, "res:", 4) == 0)
		merge_id = gtk_ui_manager_add_ui_from_resource
			(wbcg->ui, uifile + 4, &error);
	else
		merge_id = gtk_ui_manager_add_ui_from_file
			(wbcg->ui, uifile, &error);
	if (!merge_id) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}

	g_free (uifile);

	wbcg->custom_uis = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						 NULL, g_free);

	wbcg->file_history.actions = NULL;
	wbcg->file_history.merge_id = 0;

	wbcg->toolbar.merge_id = gtk_ui_manager_new_merge_id (wbcg->ui);
	wbcg->toolbar.actions = gtk_action_group_new ("Toolbars");
	gtk_ui_manager_insert_action_group (wbcg->ui, wbcg->toolbar.actions, 0);

	wbcg->windows.actions = NULL;
	wbcg->windows.merge_id = 0;

	wbcg->templates.actions = NULL;
	wbcg->templates.merge_id = 0;

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

	/* updates the undo/redo menu labels before check_underlines
	 * to avoid problems like #324692. */
	wb_control_undo_redo_labels (GNM_WBC (wbcg), NULL, NULL);
	if (GNM_VERSION_MAJOR % 2 != 0 ||
	    gnm_debug_flag ("underlines")) {
		gtk_container_foreach (GTK_CONTAINER (wbcg->menu_zone),
				       (GtkCallback)check_underlines,
				       (gpointer)"");
	}

	wbcg_set_autosave_time (wbcg, gnm_conf_get_core_workbook_autosave_time ());
}

GSF_CLASS_FULL (WBCGtk, wbc_gtk, NULL, NULL, wbc_gtk_class_init, NULL,
	wbc_gtk_init, GNM_WBC_TYPE, 0,
	GSF_INTERFACE (wbcg_go_plot_data_allocator_init, GOG_TYPE_DATA_ALLOCATOR);
	GSF_INTERFACE (wbcg_gnm_cmd_context_init, GO_TYPE_CMD_CONTEXT))

/******************************************************************************/

void
wbc_gtk_markup_changer (WBCGtk *wbcg)
{
	g_signal_emit (G_OBJECT (wbcg), wbc_gtk_signals [WBC_GTK_MARKUP_CHANGED], 0);
}

/******************************************************************************/
/**
 * wbc_gtk_new:
 * @optional_view: (allow-none): #WorkbookView
 * @optional_wb: (allow-none) (transfer full): #Workbook
 * @optional_screen: (allow-none): #GdkScreen.
 * @optional_geometry: (allow-none): string.
 *
 * Returns: (transfer none): the new #WBCGtk.
 **/
WBCGtk *
wbc_gtk_new (WorkbookView *optional_view,
	     Workbook *optional_wb,
	     GdkScreen *optional_screen,
	     const gchar *optional_geometry)
{
	Sheet *sheet;
	WorkbookView *wbv;
	WBCGtk *wbcg = g_object_new (wbc_gtk_get_type (), NULL);
	WorkbookControl *wbc = (WorkbookControl *)wbcg;

	wbcg->preferred_geometry = g_strdup (optional_geometry);

	wbc_gtk_create_edit_area (wbcg);
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

	wbc_gtk_create_notebook_area (wbcg);

	wbcg_view_changed (wbcg, NULL, NULL);

	if (optional_screen)
		gtk_window_set_screen (wbcg_toplevel (wbcg), optional_screen);

	/* Postpone showing the GUI, so that we may resize it freely. */
	g_idle_add ((GSourceFunc)show_gui, wbcg);

	/* Load this later when thing have calmed down.  If this does not
	   trigger by the time the file menu is activated, then the UI is
	   updated right then -- and that looks sub-optimal because the
	   "Templates" menu is empty (and thus not shown) until the
	   update is done. */
	wbcg->template_loader_handler =
		g_timeout_add (1000, (GSourceFunc)wbc_gtk_load_templates, wbcg);

	wb_control_init_state (wbc);
	return wbcg;
}

/**
 * wbcg_toplevel:
 * @wbcg: #WBCGtk
 *
 * Returns: (transfer none): the toplevel #GtkWindow.
 **/
GtkWindow *
wbcg_toplevel (WBCGtk *wbcg)
{
	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), NULL);
	return GTK_WINDOW (wbcg->toplevel);
}

void
wbcg_set_transient (WBCGtk *wbcg, GtkWindow *window)
{
	go_gtk_window_set_transient (wbcg_toplevel (wbcg), window);
}

int
wbcg_get_n_scg (WBCGtk const *wbcg)
{
	return (GTK_IS_NOTEBOOK (wbcg->snotebook))?
		gtk_notebook_get_n_pages (wbcg->snotebook): -1;
}

/**
 * wbcg_get_nth_scg:
 * @wbcg: #WBCGtk
 * @i:
 *
 * Returns: (transfer none):  the scg associated with the @i-th tab in
 * @wbcg's notebook.
 * NOTE : @i != scg->sv->sheet->index_in_wb
 **/
SheetControlGUI *
wbcg_get_nth_scg (WBCGtk *wbcg, int i)
{
	SheetControlGUI *scg;
	GtkWidget *w;

	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), NULL);

	if (NULL != wbcg->snotebook &&
	    NULL != (w = gtk_notebook_get_nth_page (wbcg->snotebook, i)) &&
	    NULL != (scg = get_scg (w)) &&
	    NULL != scg->grid &&
	    NULL != scg_sheet (scg) &&
	    NULL != scg_view (scg))
		return scg;

	return NULL;
}

#warning merge these and clarfy whether we want the visible scg, or the logical (view) scg
/**
 * wbcg_focus_cur_scg:
 * @wbcg: The workbook control to operate on.
 *
 * A utility routine to safely ensure that the keyboard focus
 * is attached to the item-grid.  This is required when a user
 * edits a combo-box or and entry-line which grab focus.
 *
 * It is called for zoom, font name/size, and accept/cancel for the editline.
 * Returns: (transfer none): the sheet.
 **/
Sheet *
wbcg_focus_cur_scg (WBCGtk *wbcg)
{
	SheetControlGUI *scg;

	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), NULL);

	if (wbcg->snotebook == NULL)
		return NULL;

	scg = wbcg_get_nth_scg (wbcg,
		gtk_notebook_get_current_page (wbcg->snotebook));

	g_return_val_if_fail (scg != NULL, NULL);

	scg_take_focus (scg);
	return scg_sheet (scg);
}

/**
 * wbcg_cur_scg:
 * @wbcg: #WBCGtk
 *
 * Returns: (transfer none): the current #SheetControlGUI.
 **/
SheetControlGUI *
wbcg_cur_scg (WBCGtk *wbcg)
{
	return wbcg_get_scg (wbcg, wbcg_cur_sheet (wbcg));
}

/**
 * wbcg_cur_sheet:
 * @wbcg: #WBCGtk
 *
 * Returns: (transfer none): the current #Sheet.
 **/
Sheet *
wbcg_cur_sheet (WBCGtk *wbcg)
{
	return wb_control_cur_sheet (GNM_WBC (wbcg));
}

PangoFontDescription *
wbcg_get_font_desc (WBCGtk *wbcg)
{
	g_return_val_if_fail (GNM_IS_WBC_GTK (wbcg), NULL);

	if (!wbcg->font_desc) {
		GtkSettings *settings = wbcg_get_gtk_settings (wbcg);
		wbcg->font_desc = settings_get_font_desc (settings);
		g_signal_connect_object (settings, "notify::gtk-font-name",
					 G_CALLBACK (cb_desktop_font_changed),
					 wbcg, 0);
	}
	return wbcg->font_desc;
}

/**
 * wbcg_find_for_workbook:
 * @wb: #Workbook
 * @candidate: a candidate #WBCGtk
 * @pref_screen: the preferred screen.
 * @pref_display: the preferred display.
 *
 * Returns: (transfer none): the found #WBCGtk or %NULL.
 **/
WBCGtk *
wbcg_find_for_workbook (Workbook *wb,
			WBCGtk *candidate,
			GdkScreen *pref_screen,
			GdkDisplay *pref_display)
{
	gboolean has_screen, has_display;

	g_return_val_if_fail (GNM_IS_WORKBOOK (wb), NULL);
	g_return_val_if_fail (candidate == NULL || GNM_IS_WBC_GTK (candidate), NULL);

	if (candidate && wb_control_get_workbook (GNM_WBC (candidate)) == wb)
		return candidate;

	if (!pref_screen && candidate)
		pref_screen = wbcg_get_screen (candidate);

	if (!pref_display && pref_screen)
		pref_display = gdk_screen_get_display (pref_screen);

	candidate = NULL;
	has_screen = FALSE;
	has_display = FALSE;
	WORKBOOK_FOREACH_CONTROL(wb, wbv, wbc, {
		if (GNM_IS_WBC_GTK (wbc)) {
			WBCGtk *wbcg = WBC_GTK (wbc);
			GdkScreen *screen = wbcg_get_screen (wbcg);
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

void
wbcg_focus_current_cell_indicator (WBCGtk const *wbcg)
{
	gtk_widget_grab_focus (GTK_WIDGET (wbcg->selection_descriptor));
	gtk_editable_select_region (GTK_EDITABLE (wbcg->selection_descriptor), 0, -1);
}

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * wbc-bonobo.c: A gtk based WorkbookControl with bonobo hooks
 *
 * Copyright (C) 2000-2003 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "workbook-control-gui-priv.h"
#include "gui-util.h"
#include "sheet-object-container.h"

#include <bonobo.h>
#include <bonobo/bonobo-ui-component.h>
#include <gsf/gsf-impl-utils.h>

typedef struct {
	WorkbookControlGUI base;

	BonoboUIComponent *uic;

	GtkWidget *undo_combo, *redo_combo;
	GtkWidget *font_name_selector;
	GtkWidget *font_size_selector;
	GtkWidget *zoom_entry;
	GtkWidget *fore_color, *back_color;

} WBCbonobo;

static GObjectClass *parent_class;
static void
wbcb_finalize (GObject *obj)
{
	WCBbonobo *wbcb = (WCBbonono *)obj;
	bonobo_object_unref (BONOBO_OBJECT (wbcb->uic));
	wbcb->uic = NULL;

	(parent_class)->finalize (obj);
}

static void
inject_widget_into_bonoboui (WorkbookControlGUI *wbcg,
			     GtkWidget *widget, char const *path);
{
	BonoboControl *control;
	CORBA_Environment ev;

	gtk_widget_show_all (widget);
	control = bonobo_control_new (widget);

	CORBA_exception_init (&ev);
	bonobo_ui_component_object_set (
		wbcg->uic, path,
		BONOBO_OBJREF (control),
		&ev);
	/* if there was a problem injecting the widget then nothing is refing
	 * the control and the widget will go away under our feet */
	if (!BONOBO_EX (&ev))
		bonobo_object_unref (BONOBO_OBJECT (control));
	CORBA_exception_free (&ev);
}

static GtkWidget *
ur_stack (WorkbookControl *wbc, gboolean is_undo)
{
	WCBbonobo *wbcb = (WCBbonono *)wbc;
	return GNM_COMBO_STACK (is_undo ? wbcb->undo_combo : wbcb->redo_combo);
}

static void
wbcg_undo_redo_clear (WorkbookControl *wbc, gboolean is_undo)
{
	gnm_combo_stack_truncate (ur_stack (wbc, is_undo), 0);
}

static void
wbcg_undo_redo_truncate (WorkbookControl *wbc, int n, gboolean is_undo)
{
	gnm_combo_stack_truncate (ur_stack (wbc, is_undo), n);
}

static void
wbcg_undo_redo_pop (WorkbookControl *wbc, gboolean is_undo)
{
	gnm_combo_stack_pop (ur_stack (wbc, is_undo), 1);
}

static void
wbcb_undo_redo_push (WorkbookControl *wbc, gboolean is_undo,
		     char const *text, gpointer key)
{
	gnm_combo_stack_push (ur_stack (wbc, is_undo), text, key);
}

static void
wbcb_set_action_sensitivity (WorkbookControlGUI *wbcg, gboolean sensitive)
{
	CORBA_Environment ev;

	/* Don't disable/enable again (prevent toolbar flickering) */
	if (wbcg->toolbar_is_sensitive == sensitive)
		return;
	wbcg->toolbar_is_sensitive = sensitive;

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
}

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
wbcb_set_action_sensitivity (WorkbookControlGUI const *wbcg,
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
wbcb_set_action_label (WorkbookControlGUI const *wbcg, char const *verb_path,
		       char const *prefix, char const *suffix,
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

TOGGLE_HANDLER (display_formulas, sheet_toggle_show_formula (sheet);)
TOGGLE_HANDLER (hide_zero, sheet_toggle_hide_zeros (sheet); )
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

static void
wbcg_add_custom_ui (WorkbookControlGUI *wbcg, CustomXmlUI *ui)
{
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
}

static void
cb_custom_ui_shutdown (gpointer data)
{
	BonoboUIComponent *uic = data;
	bonobo_ui_component_unset_container (uic, NULL);
	bonobo_object_unref (BONOBO_OBJECT (uic));
}

static void
wbcb_setup_status_area (WorkbookControlGUI *wbcg)
{
	inject_widget_into_bonoboui (wbcg, progress, "/status/Progress");
	inject_widget_into_bonoboui (wbcg, frame2,   "/status/Status");
	inject_widget_into_bonoboui (wbcg, frame1,   "/status/AutoExpr");
}

static void
wbcb_set_toggle_action_state (WBCbonobo *wbcb, char const *path, gboolean state)
{
	gchar const *new_val = state ? "1" : "0";

	/* Ick,  This should be in bonobo */
	gchar *old_val = bonobo_ui_component_get_prop (wbcg->uic, path, "state", NULL);
	gboolean same = (old_val != NULL && !strcmp (new_val, old_val));
	g_free (old_val);
	if (same)
		return;

	bonobo_ui_component_set_prop (wbcg->uic, path, "state", new_val, NULL);
}

static void
workbook_create_standard_toolbar (WorkbookControlGUI *wbcg)
{
	int i, len;
	GtkWidget *zoom, *entry, *undo, *redo;

	/* Undo dropdown list */
	undo = wbcg->undo_combo = gnm_combo_stack_new (GTK_STOCK_UNDO, TRUE);
	gnm_combo_box_set_title (GO_COMBO_BOX (undo), _("Undo"));
	g_signal_connect (G_OBJECT (undo),
		"pop",
		G_CALLBACK (cb_undo_combo), wbcg);

	/* Redo dropdown list */
	redo = wbcg->redo_combo = gnm_combo_stack_new (GTK_STOCK_REDO, TRUE);
	gnm_combo_box_set_title (GO_COMBO_BOX (redo), _("Redo"));
	g_signal_connect (G_OBJECT (redo),
		"pop",
		G_CALLBACK (cb_redo_combo), wbcg);

	gnumeric_inject_widget_into_bonoboui (wbcg, undo, "/StandardToolbar/EditUndo");
	gnumeric_inject_widget_into_bonoboui (wbcg, redo, "/StandardToolbar/EditRedo");
	gnumeric_inject_widget_into_bonoboui (wbcg, zoom, "/StandardToolbar/SheetZoom");
}

static void
wbcb_style_feedback (WorkbookControl *wbc, GnmStyle const *changes)
{
	WBCbonobo	*wbcb;
	GnmStyle 	*style;
	GnmComboText    *fontsel, *fontsize;
	WorkbookView	*wb_view = wb_control_view (wbc);

	g_return_if_fail (IS_WORKBOOK_CONTROL_GUI (wbcg));
	g_return_if_fail (wb_view != NULL);

	if (!wbcg_ui_update_begin (wbcg))
		return;

	style = wb_view->current_format;

	g_return_if_fail (style != NULL);

	if (mstyle_is_element_set (style, MSTYLE_FONT_BOLD))
		wbcb_set_toggle_action_state (wbcg, "/commands/FontBold",
			mstyle_get_font_bold (style));
	if (mstyle_is_element_set (style, MSTYLE_FONT_ITALIC))
		wbcb_set_toggle_action_state (wbcg, "/commands/FontItalic",
			mstyle_get_font_italic (style));
	if (mstyle_is_element_set (style, MSTYLE_FONT_UNDERLINE))
		wbcb_set_toggle_action_state (wbcg, "/commands/FontUnderline",
			mstyle_get_font_uline (style) == UNDERLINE_SINGLE);

	if (mstyle_is_element_set (style, MSTYLE_ALIGN_H)) {
		wbcb_set_toggle_action_state (wbcg, "/commands/AlignLeft",
			h_align == HALIGN_LEFT);
		wbcb_set_toggle_action_state (wbcg, "/commands/AlignCenter",
			h_align == HALIGN_CENTER);
		wbcb_set_toggle_action_state (wbcg, "/commands/AlignRight",
			h_align == HALIGN_RIGHT);
		wbcb_set_toggle_action_state (wbcg, "/commands/CenterAcrossSelection",
			h_align == HALIGN_CENTER_ACROSS_SELECTION);
	}

	if (mstyle_is_element_set (style, MSTYLE_FONT_SIZE)) {
		char *size_str = g_strdup_printf ("%d", (int)mstyle_get_font_size (style));
		gnm_combo_text_set_text (GNM_COMBO_TEXT (wbcg->font_size_selector),
			size_str, GNM_COMBO_TEXT_FROM_TOP);
		g_free (size_str);
	}

	if (mstyle_is_element_set (style, MSTYLE_FONT_NAME))
		gnm_combo_text_set_text (GNM_COMBO_TEXT (wbcg->font_name_selector),
			mstyle_get_font_name (style), GNM_COMBO_TEXT_FROM_TOP);

	wbcg_ui_update_end (wbcg);
}

static void
wbcb_set_zoom_label (WorkbookControlGUI const *wbcg, char const *label)
{
	WCBbonobo const *wbcb = (WCBbonono const *)wbcg;
	gnm_combo_text_set_text (GNM_COMBO_TEXT (wbcb->zoom_entry),
		label, GNM_COMBO_TEXT_CURRENT);
}

static void
wbc_bonobo_create_status_area (WorkbookControlGUI *wbcg,
			       GtkWidget *status, GtkWidget *autoexpr)
{
	inject_widget_into_bonoboui (wbcg, status, "/status/Status");
	inject_widget_into_bonoboui (wbcg, autoexpr, "/status/AutoExpr");
}

static void
wbc_bonobo_class_init (GObjectClass *object_class)
{
	GnmCmdContextClass  *cc_class =
		GNM_CMD_CONTEXT_CLASS (object_class);
	WorkbookControlClass *wbc_class =
		WORKBOOK_CONTROL_CLASS (object_class);
	WorkbookControlGUIClass *wbcg_class =
		WORKBOOK_CONTROL_GUI_CLASS (object_class);

	parent_class = g_type_class_peek_parent (object_class);
	object_class->finalize		= wbcb_finalize;

	wbc_class->undo_redo.truncate	= wbcb_undo_redo_truncate;
	wbc_class->undo_redo.pop	= wbcb_undo_redo_pop;
	wbc_class->undo_redo.push	= wbcb_undo_redo_push;
	wbc_class->style_feedback	= wbcb_style_feedback;

	wbcg_class->create_status_area	    = wbcb_create_status_area;
	wbcg_class->set_zoom_label	    = wbcb_set_zoom_label;
	wbcg_class->set_action_sensitivity  = wbcb_set_action_sensitivity;
	wbcg_class->set_action_label        = wbcb_set_action_label;
	wbcg_class->set_toggle_action_state = wbcb_set_toggle_action_state;
}

static void
wbc_bonobo_init (GObject *obj)
{
	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)obj;
	BonoboUIContainer *ui_container;

	wbcg->toplevel = bonobo_window_new ("Gnumeric", "Gnumeric");
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
}

GSF_CLASS (WBCbonobo, wbc_bonobo,
	   wbc_bonobo_class_init, wbc_bonobo_init,
	   WORKBOOK_CONTROL_GUI_TYPE)

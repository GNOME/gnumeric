/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * wbc-gtk.c: A raw gtk based WorkbookControl
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
#include "wbc-gtk.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook.h"
#include "gui-util.h"
#include "gui-file.h"
#include "sheet.h"
#include "sheet-style.h"
#include "commands.h"
#include "application.h"
#include "history.h"
#include "style-color.h"
#include "global-gnome-font.h"
#include "workbook-edit.h"

#include <goffice/gui-utils/go-action-combo-stack.h>
#include <goffice/gui-utils/go-action-combo-color.h>
#include <goffice/gui-utils/go-action-combo-text.h>
#include <goffice/gui-utils/go-action-combo-pixmaps.h>
#include <goffice/utils/go-color.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkactiongroup.h>
#include <gtk/gtkuimanager.h>
#include <gtk/gtkstatusbar.h>
#include <gtk/gtkaccellabel.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktoggleaction.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhandlebox.h>
#include <gtk/gtkcheckmenuitem.h>
#include <glib/gi18n.h>
#include <errno.h>

#define CHECK_MENU_UNDERLINES


struct _WBCgtk {
	WorkbookControlGUI base;

	GtkWidget	 *status_area;
	GtkUIManager     *ui;
	GtkActionGroup   *permanent_actions, *actions, *font_actions;
	struct {
		GtkActionGroup   *actions;
		guint		  merge_id;
	} file_history;

	GOActionComboStack	*undo_action, *redo_action;
	GOActionComboColor	*fore_color, *back_color;
	GOActionComboText	*font_name, *font_size, *zoom;
	GOActionComboPixmaps	*borders;
	struct {
		GtkToggleAction	 *bold, *italic, *underline, *strikethrough;
	} font;
	struct {
		GtkToggleAction	 *left, *center, *right, *center_across_selection;
	} h_align;
	struct {
		GtkToggleAction	 *top, *center, *bottom;
	} v_align;

	GtkWidget *menu_zone, *toolbar_zone, *everything;
};
typedef WorkbookControlGUIClass WBCgtkClass;

static GObjectClass *parent_class = NULL;

/*****************************************************************************/

static void
wbc_gtk_actions_sensitive (WorkbookControlGUI *wbcg,
			   gboolean actions, gboolean font_actions)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	g_object_set (G_OBJECT (gtk->actions), "sensitive", actions, NULL);
	g_object_set (G_OBJECT (gtk->font_actions), "sensitive", font_actions, NULL);
}

static void
wbc_gtk_create_status_area (WorkbookControlGUI *wbcg, GtkWidget *progress,
			    GtkWidget *status, GtkWidget *autoexpr)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	wbcg->statusbar = gtk->status_area = gtk_hbox_new (FALSE, 2);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), status, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), autoexpr, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), progress, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->everything),
		gtk->status_area, FALSE, TRUE, 0);
	gtk_widget_show_all (gtk->status_area);
}

/*****************************************************************************/

static GNM_ACTION_DEF (cb_zoom_activated)
{
	WorkbookControl *wbc = (WorkbookControl *)wbcg;
	WBCgtk *gtk  = (WBCgtk *)wbcg;
	Sheet *sheet = wb_control_cur_sheet (wbc);
	char const *new_zoom = go_action_combo_text_get_entry (gtk->zoom);
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
	else
		wb_control_zoom_feedback (wbc);
}

static void
wbc_gtk_init_zoom (WBCgtk *gtk)
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

	gtk->zoom = g_object_new (go_action_combo_text_get_type (),
				  "name", "Zoom",
				  NULL);
	go_action_combo_text_set_width (gtk->zoom,  "10000%");
	for (i = 0; preset_zoom[i] != NULL ; ++i)
		go_action_combo_text_add_item (gtk->zoom, preset_zoom[i]);

#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Foreground"));
#endif
	g_signal_connect (G_OBJECT (gtk->zoom),
		"activate",
		G_CALLBACK (cb_zoom_activated), gtk);
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->zoom));
}

static void
wbc_gtk_set_zoom_label (WorkbookControlGUI const *wbcg, char const *label)
{
	go_action_combo_text_set_entry (((WBCgtk const *)wbcg)->zoom, label,
		GO_ACTION_COMBO_SEARCH_CURRENT);
}

/****************************************************************************/

#include <goffice/gui-utils/go-combo-pixmaps.h>
#include "pixmaps/gnumeric-stock-pixbufs.h"
#include "style-border.h"

static GOActionComboPixmapsElement const border_combo_info[] = {
	{ N_("Left"),			gnm_border_left,		11 },
	{ N_("Clear Borders"),		gnm_border_none,		12 },
	{ N_("Right"),			gnm_border_right,		13 },

	{ N_("All Borders"),		gnm_border_all,			21 },
	{ N_("Outside Borders"),	gnm_border_outside,		22 },
	{ N_("Thick Outside Borders"),	gnm_border_thick_outside,	23 },

	{ N_("Bottom"),			gnm_border_bottom,		31 },
	{ N_("Double Bottom"),		gnm_border_double_bottom,	32 },
	{ N_("Thick Bottom"),		gnm_border_thick_bottom,	33 },

	{ N_("Top and Bottom"),		gnm_border_top_n_bottom,	41 },
	{ N_("Top and Double Bottom"),	gnm_border_top_n_double_bottom,	42 },
	{ N_("Top and Thick Bottom"),	gnm_border_top_n_thick_bottom,	43 },

	{ NULL, NULL}
};

static void
cb_border_activated (GOActionComboPixmaps *a, WorkbookControl *wbc)
{
	Sheet *sheet = wb_control_cur_sheet (wbc);
	GnmBorder *borders[STYLE_BORDER_EDGE_MAX];
	int i;
	int index = go_action_combo_pixmaps_get_selection (a);
	
	/* Init the list */
	for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
		borders[i] = NULL;

	switch (index) {
	case 11 : /* left */
		borders[STYLE_BORDER_LEFT] = style_border_fetch (STYLE_BORDER_THIN,
			 sheet_style_get_auto_pattern_color (sheet),
			 style_border_get_orientation (MSTYLE_BORDER_LEFT));
		break;

	case 12 : /* none */
		for (i = STYLE_BORDER_TOP; i < STYLE_BORDER_EDGE_MAX; i++)
			borders[i] = style_border_ref (style_border_none ());
		break;

	case 13 : /* right */
		borders[STYLE_BORDER_RIGHT] = style_border_fetch (STYLE_BORDER_THIN,
			 sheet_style_get_auto_pattern_color (sheet),
			 style_border_get_orientation (MSTYLE_BORDER_RIGHT));
		break;

	case 21 : /* all */
		for (i = STYLE_BORDER_HORIZ; i <= STYLE_BORDER_VERT; ++i)
			borders[i] = style_border_fetch (STYLE_BORDER_THIN,
				sheet_style_get_auto_pattern_color (sheet),
				style_border_get_orientation (i));
		/* fall through */

	case 22 : /* outside */
		for (i = STYLE_BORDER_TOP; i <= STYLE_BORDER_RIGHT; ++i)
			borders[i] = style_border_fetch (STYLE_BORDER_THIN,
				sheet_style_get_auto_pattern_color (sheet),
				style_border_get_orientation (i));
		break;

	case 23 : /* thick_outside */
		for (i = STYLE_BORDER_TOP; i <= STYLE_BORDER_RIGHT; ++i)
			borders[i] = style_border_fetch (STYLE_BORDER_THICK,
				sheet_style_get_auto_pattern_color (sheet),
				style_border_get_orientation (i));
		break;

	case 41 : /* top_n_bottom */
	case 42 : /* top_n_double_bottom */
	case 43 : /* top_n_thick_bottom */
		borders[STYLE_BORDER_TOP] = style_border_fetch (STYLE_BORDER_THIN,
			sheet_style_get_auto_pattern_color (sheet),
			style_border_get_orientation (STYLE_BORDER_TOP));
	    /* Fall through */

	case 31 : /* bottom */
	case 32 : /* double_bottom */
	case 33 : /* thick_bottom */
	{
		int const tmp = index % 10;
		StyleBorderType const t =
		    (tmp == 1) ? STYLE_BORDER_THIN :
		    (tmp == 2) ? STYLE_BORDER_DOUBLE
		    : STYLE_BORDER_THICK;

		borders[STYLE_BORDER_BOTTOM] = style_border_fetch (t,
			sheet_style_get_auto_pattern_color (sheet),
			style_border_get_orientation (STYLE_BORDER_BOTTOM));
		break;
	}

	default :
		g_warning ("Unknown border preset selected (%d)", index);
		return;
	}

	cmd_selection_format (wbc, NULL, borders, _("Set Borders"));
}

static void
wbc_gtk_init_borders (WBCgtk *gtk)
{
	gtk->borders = go_action_combo_pixmaps_new ("BorderSelector", border_combo_info, 3, 4);
	g_object_set (G_OBJECT (gtk->borders),
		      "label", _("Borders"),
		      "tooltip", _("Borders"),
		      NULL);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Foreground"));
	go_combo_pixmaps_select (gtk->borders, 1); /* default to none */
#endif
	g_signal_connect (G_OBJECT (gtk->borders),
		"activate",
		G_CALLBACK (cb_border_activated), gtk);
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->borders));
}

/****************************************************************************/

static GOActionComboStack *
ur_stack (WorkbookControl *wbc, gboolean is_undo)
{
	WBCgtk *gtk = (WBCgtk *)wbc;
	return is_undo ? gtk->undo_action : gtk->redo_action;
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

static GOActionComboStack *
create_undo_redo (WBCgtk *gtk, char const *name, char const *tooltip,
		  char const *stock_id, char const *accel)
{
	GOActionComboStack *res = g_object_new (go_action_combo_stack_get_type (),
		"name",		name,
		"tooltip",  	_(tooltip),
		"stock_id",	stock_id,
		"sensitive",	FALSE,
		NULL);
	gtk_action_group_add_action_with_accel (gtk->actions,
		GTK_ACTION (res), accel);
	return res;
}


static void
cb_undo_activated (GOActionComboStack *a, WorkbookControl *wbc)
{
	unsigned n = workbook_find_command (wb_control_workbook (wbc), TRUE,
		go_action_combo_stack_selection (a));
	while (n-- > 0)
		command_undo (wbc);
}

static void
cb_redo_activated (GOActionComboStack *a, WorkbookControl *wbc)
{
	unsigned n = workbook_find_command (wb_control_workbook (wbc), FALSE,
		go_action_combo_stack_selection (a));
	while (n-- > 0)
		command_redo (wbc);
}

static void
wbc_gtk_init_undo_redo (WBCgtk *gtk)
{
	gtk->undo_action = create_undo_redo (gtk, N_("Undo"),
		N_("Undo the last action"), GTK_STOCK_UNDO, "<control>z");
	g_signal_connect (G_OBJECT (gtk->undo_action),
		"activate",
		G_CALLBACK (cb_undo_activated), gtk);

	gtk->redo_action = create_undo_redo (gtk, N_("Redo"),
		N_("Redo the undone action"), GTK_STOCK_REDO, "<control>y");
	g_signal_connect (G_OBJECT (gtk->redo_action),
		"activate",
		G_CALLBACK (cb_redo_activated), gtk);
}

/****************************************************************************/

static void
cb_custom_color_created (GOActionComboColor *caction, GtkWidget *dialog, WorkbookControlGUI *wbcg)
{
	wbcg_edit_attach_guru (wbcg, dialog);
	gnumeric_set_transient (wbcg_toplevel (wbcg), GTK_WINDOW (dialog));
	g_signal_connect_object (dialog,
		"destroy",
		G_CALLBACK (wbcg_edit_detach_guru), wbcg, G_CONNECT_SWAPPED);
}

static void
cb_fore_color_changed (GOActionComboColor *a, WorkbookControlGUI *wbcg)
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
			color->color.red, color->color.green, color->color.blue));
		style_color_unref (color);
		return;
	}

	mstyle = mstyle_new ();
	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE, is_default
		? style_color_auto_font ()
		: style_color_new_go (c));
	cmd_selection_format (wbc, mstyle, NULL, _("Set Foreground Color"));
}

static void
wbc_gtk_init_color_fore (WBCgtk *gtk)
{
	GnmColor *sc_auto_font = style_color_auto_font ();
	GOColor   default_color = GDK_TO_UINT(sc_auto_font->color);
	style_color_unref (sc_auto_font);

	gtk->fore_color = go_action_combo_color_new ("ColorFore", "font",
		_("Automatic"),	default_color, NULL); /* set group to view */
	g_object_set (G_OBJECT (gtk->fore_color),
		      "label", _("Foreground"),
		      "tooltip", _("Foreground"),
		      NULL);
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
cb_back_color_changed (GOActionComboColor *a, WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmStyle *mstyle;
	GOColor   c;
	gboolean  is_default;

	if (wbcg->updating_ui)
		return;

	c = go_action_combo_color_get_color (a, &is_default);

	mstyle = mstyle_new ();
	if (!is_default) {
		/* We need to have a pattern of at least solid to draw a background colour */
		if (!mstyle_is_element_set  (mstyle, MSTYLE_PATTERN) ||
		    mstyle_get_pattern (mstyle) < 1)
			mstyle_set_pattern (mstyle, 1);

		mstyle_set_color (mstyle, MSTYLE_COLOR_BACK,
			style_color_new_go (c));
	} else
		mstyle_set_pattern (mstyle, 0);	/* Set background to NONE */
	cmd_selection_format (wbc, mstyle, NULL, _("Set Background Color"));
}

static void
wbc_gtk_init_color_back (WBCgtk *gtk)
{
	gtk->back_color = go_action_combo_color_new ("ColorBack", "bucket",
		_("Clear Background"), 0, NULL);
	g_object_set (G_OBJECT (gtk->back_color),
		      "label", _("Background"),
		      "tooltip", _("Background"),
		      NULL);
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
cb_font_name_changed (GOActionComboText *a, WBCgtk *gtk)
{
	char const *new_name = go_action_combo_text_get_entry (gtk->font_name);

	while (g_ascii_isspace (*new_name))
		++new_name;

	if (*new_name) {
		if (wbcg_is_editing (WORKBOOK_CONTROL_GUI (gtk))) {
			wbcg_edit_add_markup (WORKBOOK_CONTROL_GUI (gtk),
				pango_attr_family_new (new_name));
		} else {
			GnmStyle *style = mstyle_new ();
			char *title = g_strdup_printf (_("Font Name %s"), new_name);
			mstyle_set_font_name (style, new_name);
			cmd_selection_format (WORKBOOK_CONTROL (gtk), style, NULL, title);
			g_free (title);
		}
	} else
		wb_control_style_feedback (WORKBOOK_CONTROL (gtk), NULL);

}

static void
wbc_gtk_init_font_name (WBCgtk *gtk)
{
	GList *ptr;

	gtk->font_name = g_object_new (go_action_combo_text_get_type (),
		"name",     "FontName",
		NULL);
	for (ptr = gnumeric_font_family_list; ptr != NULL; ptr = ptr->next)
		if (ptr->data) 
			go_action_combo_text_add_item (gtk->font_name, ptr->data);
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
cb_font_size_changed (GOActionComboText *a, WBCgtk *gtk)
{
	char const *new_size = go_action_combo_text_get_entry (gtk->font_size);
	char *end;
	float size;

	errno = 0; /* strtol sets errno, but does not clear it.  */
	size = strtod (new_size, &end);
	size = ((int)floor ((size * 20.) + .5)) / 20.;	/* round .05 */

	if (new_size != end && errno != ERANGE && 1. <= size && size <= 400.) {
		if (wbcg_is_editing (WORKBOOK_CONTROL_GUI (gtk))) {
			wbcg_edit_add_markup (WORKBOOK_CONTROL_GUI (gtk),
				pango_attr_size_new (size * PANGO_SCALE));
		} else {
			GnmStyle *style = mstyle_new ();
			char *title = g_strdup_printf (_("Font Size %f"), size);
			mstyle_set_font_size (style, size);
			cmd_selection_format (WORKBOOK_CONTROL (gtk), style, NULL, title);
			g_free (title);
		}
	} else
		wb_control_style_feedback (WORKBOOK_CONTROL (gtk), NULL);
}

static void
wbc_gtk_init_font_size (WBCgtk *gtk)
{
	unsigned i;

	gtk->font_size = g_object_new (go_action_combo_text_get_type (),
		"name",     "FontSize",
		NULL);
	for (i = 0; gnumeric_point_sizes[i] != 0; i++) {
		char *buffer = g_strdup_printf ("%d", gnumeric_point_sizes[i]);
		go_action_combo_text_add_item (gtk->font_size, buffer);
		g_free (buffer);
	}
	go_action_combo_text_set_width (gtk->font_size, "888");
	g_signal_connect (G_OBJECT (gtk->font_size),
		"activate",
		G_CALLBACK (cb_font_size_changed), gtk);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Foreground"));
#endif
	gtk_action_group_add_action (gtk->font_actions,
		GTK_ACTION (gtk->font_size));
}
/****************************************************************************/
/* Command callback called on activation of a file history menu item. */

static void
cb_file_history_activate (GObject *action, WorkbookControlGUI *wbcg)
{
	const char *filename = g_object_get_data (action, "filename");
	gui_file_read (wbcg, filename, NULL, NULL);
}

static void
wbc_gtk_reload_recent_file_menu (WorkbookControlGUI const *wbcg)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	GSList const *ptr;
	unsigned i;

	if (gtk->file_history.merge_id != 0)
		gtk_ui_manager_remove_ui (gtk->ui, gtk->file_history.merge_id);
	gtk->file_history.merge_id = gtk_ui_manager_new_merge_id (gtk->ui);

	if (gtk->file_history.actions != NULL)
		g_object_unref (gtk->file_history.actions);
	gtk->file_history.actions = gtk_action_group_new ("FileHistory");

	/* create the actions */
	ptr = gnm_app_history_get_list (FALSE);
	for (i = 1; ptr != NULL ; ptr = ptr->next, i++) {
		GtkActionEntry entry;
		GtkAction *action;
		const char *filename = ptr->data;
		char *name = g_strdup_printf ("FileHistoryEntry%d", i);
		char *label = history_item_label (filename, i);
		char *filename_utf8 = g_filename_to_utf8 (filename, -1, NULL, NULL, NULL);
		char *tooltip = filename_utf8
			? g_strdup_printf (_("Open %s"), filename_utf8)
			: NULL;

		entry.name = name;
		entry.stock_id = NULL;
		entry.label = label;
		entry.accelerator = NULL;
		entry.tooltip = tooltip;
		entry.callback = G_CALLBACK (cb_file_history_activate);
		gtk_action_group_add_actions (gtk->file_history.actions,
			&entry, 1, (WorkbookControlGUI *)wbcg);
		action = gtk_action_group_get_action (gtk->file_history.actions,
						      name);
		/* We just put it in there -- I had better come back out.  */
		g_assert (action != NULL);
		g_object_set_data_full (G_OBJECT (action), "filename",
					g_strdup (filename), (GDestroyNotify)g_free);

		g_free (name);
		g_free (label);
		g_free (filename_utf8);
		g_free (tooltip);		
	}

	gtk_ui_manager_insert_action_group (gtk->ui, gtk->file_history.actions, 0);

	/* merge them in */
	while (i-- > 1) {
		char *name = g_strdup_printf ("FileHistoryEntry%d", i);
		gtk_ui_manager_add_ui (gtk->ui, gtk->file_history.merge_id,
			"/menubar/File/FileHistory", 
			name, name, GTK_UI_MANAGER_AUTO, TRUE);
		g_free (name);
	}
}

/****************************************************************************/

static void
wbc_gtk_set_action_sensitivity (WorkbookControlGUI const *wbcg,
				char const *action, gboolean sensitive)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	GtkAction *a = gtk_action_group_get_action (gtk->actions, action);
	if (a == NULL)
		a = gtk_action_group_get_action (gtk->permanent_actions, action);
	g_object_set (G_OBJECT (a), "sensitive", sensitive, NULL);
}

/* NOTE : The semantics of prefix and suffix seem contrived.  Why are we
 * handling it at this end ?  That stuff should be done in the undo/redo code
 **/
static void
wbc_gtk_set_action_label (WorkbookControlGUI const *wbcg,
			  char const *action,
			  char const *prefix,
			  char const *suffix,
			  char const *new_tip)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	GtkAction *a = gtk_action_group_get_action (gtk->actions, action);

	if (prefix != NULL) {
		gchar    *text;
		gboolean  sensitive = TRUE;

		if (suffix == NULL) {
			suffix = _("Nothing");
			sensitive = FALSE;
		}

		text = g_strdup_printf ("%s : %s", prefix, suffix);
		g_object_set (G_OBJECT (a),
			      "label",	   text,
			      "sensitive", sensitive,
			      NULL);
		g_free (text);
	} else
		g_object_set (G_OBJECT (a), "label", suffix, NULL);

	if (new_tip != NULL)
		g_object_set (G_OBJECT (a), "tooltip", suffix, NULL);
}

static void
wbc_gtk_set_toggle_action_state (WorkbookControlGUI const *wbcg,
				 char const *action, gboolean state)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	GtkAction *a = gtk_action_group_get_action (gtk->actions, action);
	if (a == NULL)
		a = gtk_action_group_get_action (gtk->font_actions, action);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (a), state);
}

/****************************************************************************/

static WorkbookControl *
wbc_gtk_control_new (G_GNUC_UNUSED WorkbookControl *wbc,
		     WorkbookView *wbv,
		     Workbook *wb,
		     gpointer extra)
{
	return workbook_control_gui_new (wbv, wb, extra ? GDK_SCREEN (extra) : NULL);
}

static void
wbc_gtk_init_state (WorkbookControl *wbc)
{
	WorkbookView *wbv  = wb_control_view (wbc);
	WBCgtk       *wbcg = WBC_GTK (wbc);

	/* Share a colour history for all a view's controls */
	go_action_combo_color_set_group (wbcg->back_color, wbv);
	go_action_combo_color_set_group (wbcg->fore_color, wbv);
}

static void
wbc_gtk_style_feedback (WorkbookControl *wbc, GnmStyle const *changes)
{
	WorkbookView	*wb_view = wb_control_view (wbc);
	WBCgtk		*wbcg = (WBCgtk *)wbc;

	g_return_if_fail (wb_view != NULL);

	if (!wbcg_ui_update_begin (WORKBOOK_CONTROL_GUI (wbc)))
		return;

	if (changes == NULL)
		changes = wb_view->current_format;

	if (mstyle_is_element_set (changes, MSTYLE_FONT_BOLD))
		gtk_toggle_action_set_active (wbcg->font.bold,
			mstyle_get_font_bold (changes));
	if (mstyle_is_element_set (changes, MSTYLE_FONT_ITALIC))
		gtk_toggle_action_set_active (wbcg->font.italic,
			mstyle_get_font_italic (changes));
	if (mstyle_is_element_set (changes, MSTYLE_FONT_UNDERLINE))
		gtk_toggle_action_set_active (wbcg->font.underline,
			mstyle_get_font_uline (changes) == UNDERLINE_SINGLE);
	if (mstyle_is_element_set (changes, MSTYLE_FONT_STRIKETHROUGH))
		gtk_toggle_action_set_active (wbcg->font.strikethrough,
			mstyle_get_font_strike (changes));

	if (mstyle_is_element_set (changes, MSTYLE_ALIGN_H)) {
		StyleHAlignFlags align = mstyle_get_align_h (changes);
		gtk_toggle_action_set_active (wbcg->h_align.left,
			align == HALIGN_LEFT);
		gtk_toggle_action_set_active (wbcg->h_align.center,
			align == HALIGN_CENTER);
		gtk_toggle_action_set_active (wbcg->h_align.right,
			align == HALIGN_RIGHT);
		gtk_toggle_action_set_active (wbcg->h_align.center_across_selection,
			align == HALIGN_CENTER_ACROSS_SELECTION);
	}
	if (mstyle_is_element_set (changes, MSTYLE_ALIGN_V)) {
		StyleVAlignFlags align = mstyle_get_align_v (changes);
		gtk_toggle_action_set_active (wbcg->v_align.top,
			align == VALIGN_TOP);
		gtk_toggle_action_set_active (wbcg->v_align.bottom,
			align == VALIGN_BOTTOM);
		gtk_toggle_action_set_active (wbcg->v_align.center,
			align == VALIGN_CENTER);
	}

	if (mstyle_is_element_set (changes, MSTYLE_FONT_SIZE)) {
		char *size_str = g_strdup_printf ("%d", (int)mstyle_get_font_size (changes));
		go_action_combo_text_set_entry (wbcg->font_size,
			size_str, GO_ACTION_COMBO_SEARCH_FROM_TOP);
		g_free (size_str);
	}

	if (mstyle_is_element_set (changes, MSTYLE_FONT_NAME))
		go_action_combo_text_set_entry (wbcg->font_name,
			mstyle_get_font_name (changes), GO_ACTION_COMBO_SEARCH_FROM_TOP);

	wbcg_ui_update_end (WORKBOOK_CONTROL_GUI (wbc));
}

extern void wbcg_register_actions (WorkbookControlGUI *wbcg,
				   GtkActionGroup *menu_group,
				   GtkActionGroup *group,
				   GtkActionGroup *font_group);

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

#ifdef CHECK_MENU_UNDERLINES

static const char *
get_accel_label (GtkMenuItem *item, char *key)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (item));
	GList *l;
	const char *res = NULL;

	*key = 0;
	for (l = children; l; l = l->next) {
		GtkWidget *w = l->data;

		if (GTK_IS_ACCEL_LABEL (w)) {
			const char *label = gtk_label_get_label (GTK_LABEL (w));
			*key = 0;
			while (*label) {
				if (*label == '_') {
					*key = g_ascii_toupper (label[1]);
					break;
				}
				label++;
			}
			res = gtk_label_get_text (GTK_LABEL (w));
			break;
		}
	}

	g_list_free (children);
	return res;
}

static void
check_underlines (GtkWidget *w, const char *path)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (w));
	GHashTable *used = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)g_free);
	GList *l;

	for (l = children; l; l = l->next) {
		GtkMenuItem *item = GTK_MENU_ITEM (l->data);
		GtkWidget *sub = gtk_menu_item_get_submenu (item);
		char key;
		const char *label = get_accel_label (item, &key);

		if (sub) {
			char *newpath = g_strconcat (path, *path ? "->" : "", label, NULL);
			check_underlines (sub, newpath);
			g_free (newpath);
		}

		if (key) {
			const char *prev = g_hash_table_lookup (used, GINT_TO_POINTER ((int)key));
			if (prev)
				g_warning ("In the `%s' menu, the key `%c' is used for both `%s' and `%s'.",
					   path, key, prev, label);
			else
				g_hash_table_insert (used, GINT_TO_POINTER ((int)key), g_strdup (label));
		}
	}

	g_list_free (children);
	g_hash_table_destroy (used);
}

#endif



static void
cb_add_menus_toolbars (G_GNUC_UNUSED GtkUIManager *ui,
		       GtkWidget *w, WBCgtk *gtk)
{
	if (GTK_IS_TOOLBAR (w)) {
		WorkbookControlGUI *wbcg = (WorkbookControlGUI *)gtk;
		GtkWidget *box = gtk_handle_box_new ();
		const char *name;

		gtk_container_add (GTK_CONTAINER (box), w);
		g_object_connect (box,
			"signal::child_attached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (TRUE),
			"signal::child_detached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (FALSE),
			NULL);
		gtk_toolbar_set_show_arrow (GTK_TOOLBAR (w), TRUE);
		gtk_toolbar_set_style (GTK_TOOLBAR (w), GTK_TOOLBAR_ICONS);
		gtk_box_pack_start (GTK_BOX (gtk->toolbar_zone), box, FALSE, FALSE, 0);

		name = gtk_widget_get_name (w);
		if (name && strcmp (name, "StandardToolbar") == 0)
			wbcg->standard_toolbar = box;
		else if (name && strcmp (name, "FormatToolbar") == 0)
			wbcg->format_toolbar = box;
		else if (name && strcmp (name, "ObjectToolbar") == 0)
			wbcg->object_toolbar = box;
	} else
		gtk_box_pack_start (GTK_BOX (gtk->menu_zone), w, FALSE, TRUE, 0);
	gtk_widget_show_all (w);
}

static void
cb_show_menu_tip (GtkWidget *proxy, GnmCmdContext *cc)
{
	GtkAction *action = g_object_get_data (G_OBJECT (proxy), "GtkAction");
	char *tip;
	g_object_get (action, "tooltip", &tip, NULL);
	if (tip == NULL) tip = g_strdup (" "); /* empty has no height */
	cmd_context_progress_message_set (cc, _(tip));
	g_free (tip);
}

static void
cb_clear_menu_tip (GnmCmdContext *cc)
{
	cmd_context_progress_message_set (cc, " ");
}

static void
cb_connect_proxy (G_GNUC_UNUSED GtkUIManager *ui,
		  GtkAction    *action,
		  GtkWidget    *proxy, 
		  GnmCmdContext *cc)
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
		     GnmCmdContext *cc)
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
cb_post_activate (WorkbookControlGUI *wbcg)
{
	if (!wbcg_is_editing (wbcg))
		wbcg_focus_cur_scg (wbcg);
}

static void
wbc_gtk_init (GObject *obj)
{
	static struct {
		char const *name;
		gboolean    is_font;
		unsigned    offset;
	} const toggles[] = {
		{ "FontBold",		   TRUE, G_STRUCT_OFFSET (WBCgtk, font.bold) },
		{ "FontItalic",		   TRUE, G_STRUCT_OFFSET (WBCgtk, font.italic) },
		{ "FontUnderline",	   TRUE, G_STRUCT_OFFSET (WBCgtk, font.underline) },
		{ "FontStrikeThrough",	   TRUE, G_STRUCT_OFFSET (WBCgtk, font.strikethrough) },

		{ "AlignLeft",		   FALSE, G_STRUCT_OFFSET (WBCgtk, h_align.left) },
		{ "AlignCenter",	   FALSE, G_STRUCT_OFFSET (WBCgtk, h_align.center) },
		{ "AlignRight",		   FALSE, G_STRUCT_OFFSET (WBCgtk, h_align.right) },
		{ "CenterAcrossSelection", FALSE, G_STRUCT_OFFSET (WBCgtk, h_align.center_across_selection) },
		{ "AlignTop",		   FALSE, G_STRUCT_OFFSET (WBCgtk, v_align.top) },
		{ "AlignVCenter",	   FALSE, G_STRUCT_OFFSET (WBCgtk, v_align.center) },
		{ "AlignBottom",	   FALSE, G_STRUCT_OFFSET (WBCgtk, v_align.bottom) }
	};

	WorkbookControlGUI *wbcg = (WorkbookControlGUI *)obj;
	WBCgtk		   *gtk = (WBCgtk *)obj;
	GtkAction	   *act;
	char		   *uifile;
	unsigned	    i;
	GError *error = NULL;

	wbcg_set_toplevel (wbcg, gtk_window_new (GTK_WINDOW_TOPLEVEL));
	gtk_window_set_title (wbcg->toplevel, "Gnumeric");
	gtk_window_set_wmclass (wbcg->toplevel, "Gnumeric", "Gnumeric");
	gtk->menu_zone = gtk_vbox_new (TRUE, 0);
	gtk->toolbar_zone = gtk_vbox_new (FALSE, 0);
	gtk->everything = gtk_vbox_new (FALSE, 0);

	gtk_box_pack_start (GTK_BOX (gtk->everything),
		gtk->menu_zone, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (gtk->everything),
		gtk->toolbar_zone, FALSE, TRUE, 0);
	gtk_widget_show_all (gtk->everything);
#if 0
	bonobo_dock_set_client_area (BONOBO_DOCK (gtk->dock), wbcg->table);
#endif
	gtk_box_pack_start (GTK_BOX (gtk->everything),
		wbcg->table, TRUE, TRUE, 0);

#warning "TODO split into smaller chunks"
	gtk->permanent_actions = gtk_action_group_new ("PermanentActions");
	gtk_action_group_set_translation_domain (gtk->permanent_actions, NULL);
	gtk->actions = gtk_action_group_new ("Actions");
	gtk_action_group_set_translation_domain (gtk->actions, NULL);
	gtk->font_actions = gtk_action_group_new ("FontActions");
	gtk_action_group_set_translation_domain (gtk->font_actions, NULL);

	wbcg_register_actions (wbcg, gtk->permanent_actions, gtk->actions, gtk->font_actions);

	for (i = G_N_ELEMENTS (toggles); i-- > 0 ; ) {
		act = gtk_action_group_get_action (
			(toggles[i].is_font ? gtk->font_actions : gtk->actions),
			toggles[i].name);
		G_STRUCT_MEMBER (GtkToggleAction *, gtk, toggles[i].offset) = GTK_TOGGLE_ACTION (act);
	}

	wbc_gtk_init_undo_redo (gtk);
	wbc_gtk_init_color_fore (gtk);
	wbc_gtk_init_color_back (gtk);
	wbc_gtk_init_font_name (gtk);
	wbc_gtk_init_font_size (gtk);
	wbc_gtk_init_zoom (gtk);
	wbc_gtk_init_borders (gtk);

	gtk->ui = gtk_ui_manager_new ();
	g_object_connect (gtk->ui,
		"signal::add_widget",	 G_CALLBACK (cb_add_menus_toolbars), gtk,
		"signal::connect_proxy",    G_CALLBACK (cb_connect_proxy), gtk,
		"signal::disconnect_proxy", G_CALLBACK (cb_disconnect_proxy), gtk,
		"swapped_object_signal::post_activate", G_CALLBACK (cb_post_activate), gtk,
		NULL);
	gtk_ui_manager_insert_action_group (gtk->ui, gtk->permanent_actions, 0);
	gtk_ui_manager_insert_action_group (gtk->ui, gtk->actions, 0);
	gtk_ui_manager_insert_action_group (gtk->ui, gtk->font_actions, 0);

	gtk_window_add_accel_group (wbcg->toplevel, 
		gtk_ui_manager_get_accel_group (gtk->ui));
	uifile = g_build_filename (gnumeric_sys_data_dir (NULL),
				   "GNOME_Gnumeric-gtk.xml", NULL);
	if (!gtk_ui_manager_add_ui_from_file (gtk->ui, uifile, &error)) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}
	g_free (uifile);

	gtk->file_history.actions = NULL;
	gtk->file_history.merge_id = 0;
	wbc_gtk_reload_recent_file_menu (wbcg);

	gtk_ui_manager_ensure_update (gtk->ui);
	gtk_widget_show_all (gtk->everything);
	gtk_container_add (GTK_CONTAINER (wbcg->toplevel), gtk->everything);

#ifdef CHECK_MENU_UNDERLINES
	gtk_container_foreach (GTK_CONTAINER (gtk->menu_zone),
			       (GtkCallback)check_underlines,
			       (gpointer)"");
#endif
}

static void
wbc_gtk_finalize (GObject *obj)
{
	WBCgtk *gtk = (WBCgtk *)obj;

	if (gtk->file_history.merge_id != 0)
		gtk_ui_manager_remove_ui (gtk->ui, gtk->file_history.merge_id);
	if (gtk->file_history.actions != NULL)
		g_object_unref (gtk->file_history.actions);
	g_object_unref (gtk->ui);

	parent_class->finalize (obj);
}

static void
wbc_gtk_class_init (GObjectClass *object_class)
{
	WorkbookControlClass *wbc_class =
		WORKBOOK_CONTROL_CLASS (object_class);
	WorkbookControlGUIClass *wbcg_class =
		WORKBOOK_CONTROL_GUI_CLASS (object_class);

	parent_class = g_type_class_peek_parent (object_class);

	wbc_class->control_new		= wbc_gtk_control_new;
	wbc_class->undo_redo.truncate	= wbc_gtk_undo_redo_truncate;
	wbc_class->undo_redo.pop	= wbc_gtk_undo_redo_pop;
	wbc_class->undo_redo.push	= wbc_gtk_undo_redo_push;
	wbc_class->init_state		= wbc_gtk_init_state;
	wbc_class->style_feedback	= wbc_gtk_style_feedback;

	wbcg_class->actions_sensitive		= wbc_gtk_actions_sensitive;
	wbcg_class->create_status_area		= wbc_gtk_create_status_area;
	wbcg_class->set_zoom_label		= wbc_gtk_set_zoom_label;
	wbcg_class->reload_recent_file_menu	= wbc_gtk_reload_recent_file_menu;
	wbcg_class->set_action_sensitivity	= wbc_gtk_set_action_sensitivity;
	wbcg_class->set_action_label		= wbc_gtk_set_action_label;
	wbcg_class->set_toggle_action_state	= wbc_gtk_set_toggle_action_state;

	object_class->finalize = wbc_gtk_finalize;
}

GSF_CLASS (WBCgtk, wbc_gtk,
	   wbc_gtk_class_init, wbc_gtk_init,
	   WORKBOOK_CONTROL_GUI_TYPE)

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
#include "gui-util.h"
#include "gui-file.h"
#include "sheet.h"
#include "sheet-style.h"
#include "commands.h"
#include "style-color.h"
#include "global-gnome-font.h"

#include <goffice/gui-utils/go-action-combo-stack.h>
#include <goffice/gui-utils/go-action-combo-color.h>
#include <goffice/gui-utils/go-action-combo-text.h>
#include <goffice/gui-utils/go-action-combo-pixmaps.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkactiongroup.h>
#include <gtk/gtkuimanager.h>
#include <gtk/gtkstatusbar.h>
#include <gtk/gtklabel.h>
#include <gtk/gtktoggleaction.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkhandlebox.h>
#include <gtk/gtkcheckmenuitem.h>
#include <glib/gi18n.h>
#include <errno.h>

struct _WBCgtk {
	WorkbookControlGUI base;

	GtkWidget	 *status_area;
	GtkUIManager     *ui;
	GtkActionGroup   *actions;

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

/*****************************************************************************/

static void
wbc_gtk_actions_sensitive (WorkbookControlGUI *wbcg, gboolean sensitive)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	g_object_set (G_OBJECT (gtk->actions), "sensitive", sensitive, NULL);
}

static void
wbc_gtk_create_status_area (WorkbookControlGUI *wbcg, GtkWidget *progress,
			    GtkWidget *status, GtkWidget *autoexpr)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	gtk->status_area = gtk_hbox_new (FALSE, 2);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), status, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), autoexpr, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), progress, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->everything),
		gtk->status_area, FALSE, TRUE, 0);
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
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->zoom));

	g_signal_connect (G_OBJECT (gtk->zoom),
		"activate",
		G_CALLBACK (cb_zoom_activated), gtk);

#if 0
	/* Set a reasonable default width */
	entry = GNM_COMBO_TEXT (zoom)->entry;
	len = gnm_measure_string (
		gtk_widget_get_pango_context (GTK_WIDGET (wbcg->toplevel)),
		entry->style->font_desc,
		"%10000");
	gtk_widget_set_size_request (entry, len, -1);
#endif

	for (i = 0; preset_zoom[i] != NULL ; ++i)
		go_action_combo_text_add_item (gtk->zoom, preset_zoom[i]);
}

static void
wbc_gtk_set_zoom_label (WorkbookControlGUI const *wbcg, char const *label)
{
	go_action_combo_text_set_entry (((WBCgtk const *)wbcg)->zoom, label,
		GO_ACTION_COMBO_SEARCH_CURRENT);
}

/****************************************************************************/

#include "widgets/widget-pixmap-combo.h"
#include "pixmaps/gnumeric-stock-pixbufs.h"
#include "style-border.h"

static PixmapComboElement const border_combo_info[] = {
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
cb_border_changed (PixmapCombo *pixmap_combo, int index, WorkbookControlGUI *wbcg)
{
	Sheet *sheet = wb_control_cur_sheet (WORKBOOK_CONTROL (wbcg));
	GnmBorder *borders[STYLE_BORDER_EDGE_MAX];
	int i;

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

	cmd_selection_format (WORKBOOK_CONTROL (wbcg), NULL, borders,
			      _("Set Borders"));
}

static void
wbc_gtk_init_borders (WBCgtk *gtk)
{
	gtk->borders = go_action_combo_pixmaps_new ("BorderSelector", border_combo_info, 3, 4);
	g_object_set (G_OBJECT (gtk->borders),
		      "label", _("Borders"),
		      "tooltip", _("Borders"),
		      NULL);
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->borders));

#if 0
	/* Border combo box */

	/* default to none */
	pixmap_combo_select_pixmap (PIXMAP_COMBO (border_combo), 1);
	g_signal_connect (G_OBJECT (border_combo),
		"changed",
		G_CALLBACK (cb_border_changed), wbcg);
	disable_focus (border_combo, NULL);
#endif
}

/****************************************************************************/
static GOActionComboStack *
wbc_gtk_init_undo_redo (WBCgtk *gtk, char const *name, char const *tooltip,
			char const *stock_id)
{
	GOActionComboStack *res = g_object_new (go_action_combo_stack_get_type (),
		"name",		name,
		"tooltip",  	_(tooltip),
		"stock_id",	stock_id,
		NULL);
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (res));
#warning Create gtk_action_group_add_action_with_accel
	return res;
}

/****************************************************************************/

static void
cb_fore_color_changed (GOActionComboColor *combo, GdkColor *c,
		       gboolean is_custom, gboolean by_user, gboolean is_default,
		       WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmStyle *mstyle;

	if (wbcg->updating_ui)
		return;

	g_return_if_fail (c != NULL);

	mstyle = mstyle_new ();
	mstyle_set_color (mstyle, MSTYLE_COLOR_FORE, is_default
		? style_color_auto_font ()
		: style_color_new (c->red, c->green, c->blue));
	cmd_selection_format (wbc, mstyle, NULL, _("Set Foreground Color"));
}

static void
wbc_gtk_init_color_fore (WBCgtk *gtk)
{
	GnmColor *sc_auto_font = style_color_auto_font ();

	gtk->fore_color = go_action_combo_color_new ("ColorFore", "font",
		_("Automatic"),	&sc_auto_font->color, NULL);
	g_object_set (G_OBJECT (gtk->fore_color),
		      "label", _("Foreground"),
		      "tooltip", _("Foreground"),
		      NULL);
	style_color_unref (sc_auto_font);
#if 0
	g_signal_connect (G_OBJECT (fore_combo),
		"color_changed",
		G_CALLBACK (cb_fore_color_changed), wbcg);
	disable_focus (fore_combo, NULL);
	gnm_combo_box_set_title (GNM_COMBO_BOX (fore_combo),
				 _("Foreground"));

	/* Sync the color of the font color combo with the other views */
	WORKBOOK_FOREACH_CONTROL (wb_control_workbook (WORKBOOK_CONTROL (wbcg)), view, control,
				  if (control != WORKBOOK_CONTROL (wbcg)) {
					  GdkColor *color = color_combo_get_color (
						  COLOR_COMBO (WORKBOOK_CONTROL_GUI (control)->fore_color), NULL);
					  if (color) {
						  color_combo_set_color (
							  COLOR_COMBO (wbcg->fore_color), color);
						  gdk_color_free (color);
						  break;
					  }
				  });
#endif
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->fore_color));
}
/****************************************************************************/

static void
cb_back_color_changed (GOActionComboColor *combo, GdkColor *c,
		       gboolean is_custom, gboolean by_user, gboolean is_default,
		       WorkbookControlGUI *wbcg)
{
	WorkbookControl *wbc = WORKBOOK_CONTROL (wbcg);
	GnmStyle *mstyle;

	if (wbcg->updating_ui)
		return;

	mstyle = mstyle_new ();
	if (c != NULL) {
		/* We need to have a pattern of at least solid to draw a background colour */
		if (!mstyle_is_element_set  (mstyle, MSTYLE_PATTERN) ||
		    mstyle_get_pattern (mstyle) < 1)
			mstyle_set_pattern (mstyle, 1);

		mstyle_set_color (mstyle, MSTYLE_COLOR_BACK,
				  style_color_new (c->red, c->green, c->blue));
	} else
		mstyle_set_pattern (mstyle, 0);	/* Set background to NONE */
	cmd_selection_format (wbc, mstyle, NULL, _("Set Background Color"));
}

static void
wbc_gtk_init_color_back (WBCgtk *gtk)
{
	gtk->back_color = go_action_combo_color_new ("ColorBack", "bucket",
		_("Clear Background"), NULL, NULL);
	g_object_set (G_OBJECT (gtk->back_color),
		      "label", _("Background"),
		      "tooltip", _("Background"),
		      NULL);
#if 0
	g_signal_connect (G_OBJECT (back_combo),
		"color_changed",
		G_CALLBACK (cb_back_color_changed), wbcg);
	disable_focus (back_combo, NULL);
	gnm_combo_box_set_title (GNM_COMBO_BOX (back_combo),
				 _("Background"));

	/* Sync the color of the background color combo with the other views */
	WORKBOOK_FOREACH_CONTROL (wb_control_workbook (WORKBOOK_CONTROL (wbcg)), view, control,
				  if (control != WORKBOOK_CONTROL (wbcg)) {
					  GdkColor *color = color_combo_get_color (
						  COLOR_COMBO (WORKBOOK_CONTROL_GUI (control)->back_color), NULL);
					  if (color) {
						  color_combo_set_color (
							  COLOR_COMBO (wbcg->back_color), color);
						  gdk_color_free (color);
						  break;
					  }
				  });
#endif

	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->back_color));
}
/****************************************************************************/
static void
wbc_gtk_init_font_name (WBCgtk *gtk)
{
	GList *ptr;

	gtk->font_name = g_object_new (go_action_combo_text_get_type (),
				       "name",     "FontName",
				       NULL);
#if 0
	g_signal_connect (G_OBJECT (fontsel),
			  "entry_changed",
			  G_CALLBACK (cb_font_name_changed), wbcg);
	/* gtk_container_set_border_width (GTK_CONTAINER (fontsel), 0); */
#endif
	for (ptr = gnumeric_font_family_list; ptr != NULL; ptr = ptr->next)
		if (ptr->data) 
			go_action_combo_text_add_item (gtk->font_name, ptr->data);

	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->font_name));
}
/****************************************************************************/
static void
wbc_gtk_init_font_size (WBCgtk *gtk)
{
	unsigned i;

	gtk->font_size = g_object_new (go_action_combo_text_get_type (), "name",     "FontSize", NULL);
	for (i = 0; gnumeric_point_sizes[i] != 0; i++) {
		char *buffer = g_strdup_printf ("%d", gnumeric_point_sizes[i]);
		go_action_combo_text_add_item (gtk->font_size, buffer);
		g_free (buffer);
	}
	go_action_combo_text_set_width (gtk->font_size, "888");
#if 0
	g_signal_connect (G_OBJECT (fontsize),
			  "entry_changed",
			  G_CALLBACK (cb_font_size_changed), wbcg);
#endif

	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->font_size));
}
/****************************************************************************/
/* Command callback called on activation of a file history menu item. */
#define UGLY_GNOME_UI_KEY "HistoryFilename"

static void
file_history_cmd (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	char *filename = g_object_get_data (G_OBJECT (widget), UGLY_GNOME_UI_KEY);
	gui_file_read (wbcg, filename, NULL, NULL);
}

static void
wbc_gtk_reload_recent_file_menu (WorkbookControlGUI const *wbcg)
{
#if 0
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
#endif
}

/****************************************************************************/

static void
wbc_gtk_set_action_sensitivity (WorkbookControlGUI const *wbcg,
				char const *action, gboolean sensitive)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	GtkAction *a = gtk_action_group_get_action (gtk->actions, action);
	g_object_set (G_OBJECT (a), "sensitive", sensitive, NULL);
}

#warning These semantics seem crufty
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

static GOActionComboStack *
ur_stack (WorkbookControl *wbc, gboolean is_undo)
{
	WBCgtk *gtk = (WBCgtk *)wbc;
	return is_undo ? gtk->undo_action : gtk->redo_action;
}

static void
wbc_gtk_undo_redo_truncate (WorkbookControl *wbc, int n, gboolean is_undo)
{
	go_action_combo_stack_trunc (ur_stack (wbc, is_undo), n);
}

static void
wbc_gtk_undo_redo_pop (WorkbookControl *wbc, gboolean is_undo)
{
	go_action_combo_stack_pop (ur_stack (wbc, is_undo), 1);
}

static void
wbc_gtk_undo_redo_push (WorkbookControl *wbc, char const *text, gboolean is_undo)
{
	go_action_combo_stack_push (ur_stack (wbc, is_undo), text);
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
	GnmStyle	*style;
	WorkbookView	*wb_view = wb_control_view (wbc);
	WBCgtk		*wbcg = (WBCgtk *)wbc;

	g_return_if_fail (wb_view != NULL);

	if (!wbcg_ui_update_begin (WORKBOOK_CONTROL_GUI (wbc)))
		return;

	style = wb_view->current_format;

	g_return_if_fail (style != NULL);

	if (changes == NULL || mstyle_is_element_set (changes, MSTYLE_FONT_BOLD))
		gtk_toggle_action_set_active (wbcg->font.bold,
			mstyle_get_font_bold (style));
	if (changes == NULL || mstyle_is_element_set (changes, MSTYLE_FONT_ITALIC));
		gtk_toggle_action_set_active (wbcg->font.italic,
			mstyle_get_font_italic (style));
	if (changes == NULL || mstyle_is_element_set (changes, MSTYLE_FONT_UNDERLINE))
		gtk_toggle_action_set_active (wbcg->font.underline,
			mstyle_get_font_uline (style) == UNDERLINE_SINGLE);
	if (changes == NULL || mstyle_is_element_set (changes, MSTYLE_FONT_STRIKETHROUGH))
		gtk_toggle_action_set_active (wbcg->font.strikethrough,
			mstyle_get_font_strike (style));

	if (changes == NULL || mstyle_is_element_set (changes, MSTYLE_ALIGN_H)) {
		StyleHAlignFlags align = mstyle_get_align_h (style);
		gtk_toggle_action_set_active (wbcg->h_align.left,
			align == HALIGN_LEFT);
		gtk_toggle_action_set_active (wbcg->h_align.center,
			align == HALIGN_CENTER);
		gtk_toggle_action_set_active (wbcg->h_align.right,
			align == HALIGN_RIGHT);
		gtk_toggle_action_set_active (wbcg->h_align.center_across_selection,
			align == HALIGN_CENTER_ACROSS_SELECTION);
	}
	if (changes == NULL || mstyle_is_element_set (changes, MSTYLE_ALIGN_V)) {
		StyleVAlignFlags align = mstyle_get_align_v (style);
		gtk_toggle_action_set_active (wbcg->v_align.top,
			align == VALIGN_TOP);
		gtk_toggle_action_set_active (wbcg->v_align.bottom,
			align == VALIGN_BOTTOM);
		gtk_toggle_action_set_active (wbcg->v_align.center,
			align == VALIGN_CENTER);
	}

	if (changes == NULL || mstyle_is_element_set (changes, MSTYLE_FONT_SIZE)) {
		char *size_str = g_strdup_printf ("%d", (int)mstyle_get_font_size (style));
		go_action_combo_text_set_entry (wbcg->font_size,
			size_str, GO_ACTION_COMBO_SEARCH_FROM_TOP);
		g_free (size_str);
	}

	if (changes == NULL || mstyle_is_element_set (changes, MSTYLE_FONT_NAME))
		go_action_combo_text_set_entry (wbcg->font_name,
			mstyle_get_font_name (style), GO_ACTION_COMBO_SEARCH_FROM_TOP);

	wbcg_ui_update_end (WORKBOOK_CONTROL_GUI (wbc));
}

extern void wbcg_register_actions (WorkbookControlGUI *wbcg, GtkActionGroup *group);;

static void
cb_handlebox_dock_status (G_GNUC_UNUSED GtkHandleBox *hb,
			  GtkToolbar *toolbar, gpointer attached)
{
	gtk_toolbar_set_show_arrow (toolbar, GPOINTER_TO_INT (attached));
}

static void
cb_show_hide_toolbar (GtkWidget *w, char const *label)
{
	g_warning ("%s %s", gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (w)) ?
		   "show" : "hide", label);
}

static gboolean
cb_handlebox_popup (GtkWidget *widget, GdkEventButton *event, WBCgtk *gtk)
{
#warning make this dynamic when we have uimanager_get_toolbars
	static char const *toolbars[] = {
		N_("Standard"),
		N_("Formatting"),
		N_("Objects")
	};
	GtkWidget *menu, *item;
	unsigned i;
	gboolean is_visible;

	if (event->type != GDK_BUTTON_PRESS || event->button != 3)
		return FALSE;

	menu = gtk_menu_new ();
	for (i = 0; i < G_N_ELEMENTS (toolbars); i++) {
		item = gtk_check_menu_item_new_with_label (_(toolbars[i]));
		is_visible =
#if 0
			pull it from the toolbar
#else
			TRUE;
#endif
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
			is_visible);
		g_signal_connect (G_OBJECT (item),
			"activate",
			G_CALLBACK (cb_show_hide_toolbar), (gpointer) toolbars[i]);
		
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
	}

	gnumeric_popup_menu (GTK_MENU (menu), event);
	return TRUE;
}

static void
cb_add_menus_toolbars (G_GNUC_UNUSED GtkUIManager *ui,
		       GtkWidget *w, WBCgtk *gtk)
{
	if (GTK_IS_TOOLBAR (w)) {
		GtkWidget *box = gtk_handle_box_new ();
		gtk_container_add (GTK_CONTAINER (box), w);
		g_object_connect (box,
			"signal::child_attached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (TRUE),
			"signal::child_detached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (FALSE),
			"signal_after::button_press_event", G_CALLBACK (cb_handlebox_popup), gtk,
			NULL);
		gtk_toolbar_set_show_arrow (GTK_TOOLBAR (w), TRUE);
		gtk_toolbar_set_style (GTK_TOOLBAR (w), GTK_TOOLBAR_ICONS);
		gtk_box_pack_start (GTK_BOX (gtk->toolbar_zone), box, FALSE, FALSE, 0);
	} else
		gtk_box_pack_start (GTK_BOX (gtk->menu_zone), w, FALSE, TRUE, 0);
	gtk_widget_show_all (w);
}

static void
cb_show_menu_tip (GtkWidget *proxy, GnmCmdContext *cc)
{
	GtkAction *action = g_object_get_data (G_OBJECT (proxy), "GtkAction");
	char const *tip;
	g_object_get (action, "tooltip", &tip, NULL);
	if (tip == NULL) tip = " "; /* empty has no height */
	cmd_context_progress_message_set (cc, _(tip));
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
wbc_gtk_init (GObject *obj)
{
	static struct {
		char const *name;
		unsigned    offset;
	} const toggles[] = {
		{ "FontBold",		G_STRUCT_OFFSET (WBCgtk, font.bold) },
		{ "FontItalic",		G_STRUCT_OFFSET (WBCgtk, font.italic) },
		{ "FontUnderline",	G_STRUCT_OFFSET (WBCgtk, font.underline) },
		{ "FontStrikeThrough",	G_STRUCT_OFFSET (WBCgtk, font.strikethrough) },
		{ "AlignLeft",		G_STRUCT_OFFSET (WBCgtk, h_align.left) },
		{ "AlignCenter",	G_STRUCT_OFFSET (WBCgtk, h_align.center) },
		{ "AlignRight",		G_STRUCT_OFFSET (WBCgtk, h_align.right) },
		{ "CenterAcrossSelection", G_STRUCT_OFFSET (WBCgtk, h_align.center_across_selection) },
		{ "AlignTop",		G_STRUCT_OFFSET (WBCgtk, v_align.top) },
		{ "AlignVCenter",	G_STRUCT_OFFSET (WBCgtk, v_align.center) },
		{ "AlignBottom",	G_STRUCT_OFFSET (WBCgtk, v_align.bottom) }
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

#warning TODO split into smaller chunks
	gtk->actions = gtk_action_group_new ("Actions");

	wbcg_register_actions (wbcg, gtk->actions);

	for (i = G_N_ELEMENTS (toggles); i-- > 0 ; ) {
		act = gtk_action_group_get_action (gtk->actions, toggles[i].name);
		G_STRUCT_MEMBER (GtkToggleAction *, gtk, toggles[i].offset) = GTK_TOGGLE_ACTION (act);
	}

	gtk->undo_action = wbc_gtk_init_undo_redo (gtk, N_("Undo"),
		N_("Undo the last action"), GTK_STOCK_UNDO);
	gtk->redo_action = wbc_gtk_init_undo_redo (gtk, N_("Redo"),
		N_("Redo the undone action"), GTK_STOCK_REDO);
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
		"swapped_signal::post_activate", G_CALLBACK (wbcg_focus_cur_scg), gtk,
		NULL);
	gtk_ui_manager_insert_action_group (gtk->ui, gtk->actions, 0);

	gtk_window_add_accel_group (wbcg->toplevel, 
		gtk_ui_manager_get_accel_group (gtk->ui));
	uifile = g_build_filename (gnumeric_sys_data_dir (NULL),
				   "GNOME_Gnumeric-gtk.xml", NULL);
	if (!gtk_ui_manager_add_ui_from_file (gtk->ui, uifile, &error)) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}
	g_free (uifile);

	gtk_ui_manager_ensure_update (gtk->ui);
	gtk_widget_show (gtk->everything);
	gtk_container_add (GTK_CONTAINER (wbcg->toplevel), gtk->everything);
}

static void
wbc_gtk_class_init (GObjectClass *object_class)
{
	WorkbookControlClass *wbc_class =
		WORKBOOK_CONTROL_CLASS (object_class);
	WorkbookControlGUIClass *wbcg_class =
		WORKBOOK_CONTROL_GUI_CLASS (object_class);

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
}

GSF_CLASS (WBCgtk, wbc_gtk,
	   wbc_gtk_class_init, wbc_gtk_init,
	   WORKBOOK_CONTROL_GUI_TYPE)

/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * wbc-gtk.c: A raw gtk based WorkbookControl
 *
 * Copyright (C) 2000-2004 Jody Goldberg (jody@gnome.org)
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
 * 	Eduardo Lima  (eduardo.lima@indt.org.br)
 * 	Renato Araujo (renato.filho@indt.org.br)
 */
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "wbc-gtk.h"
#include "workbook-control-gui-priv.h"
#include "workbook-view.h"
#include "workbook-priv.h"
#include "gui-util.h"
#include "gui-file.h"
#include "sheet.h"
#include "sheet-style.h"
#include "commands.h"
#include "application.h"
#include "history.h"
#include "style-color.h"
#include "workbook-edit.h"
#include "gnumeric-gconf.h"

#include <goffice/gtk/go-action-combo-stack.h>
#include <goffice/gtk/go-action-combo-color.h>
#include <goffice/gtk/go-action-combo-text.h>
#include <goffice/gtk/go-action-combo-pixmaps.h>
#include <goffice/utils/go-color.h>
#include <goffice/utils/go-font.h>
#include <goffice/utils/go-file.h>
#include <goffice/app/go-cmd-context-impl.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtk.h>
#include "gdk/gdkkeysyms.h"
#include <glib/gi18n.h>
#include <errno.h>
#include <string.h>

#ifdef USE_HILDON
	#include <goffice/gtk/go-combo-text.h>
	#include <hildon-lgpl/hildon-widgets/hildon-app.h>
	#include <hildon-lgpl/hildon-widgets/hildon-appview.h>
#endif

#define CHECK_MENU_UNDERLINES

struct _WBCgtk {
	WorkbookControlGUI base;

	GtkWidget	 *status_area;
	GtkUIManager     *ui;
	GtkActionGroup   *permanent_actions, *actions, *font_actions;
	struct {
		GtkActionGroup   *actions;
		guint		  merge_id;
	} file_history, toolbar, windows;

	GOActionComboStack	*undo_haction, *redo_haction;
	GtkAction		*undo_vaction, *redo_vaction;
	GOActionComboColor	*fore_color, *back_color;
	GOActionComboText	*font_name, *font_size, *zoom;
	GOActionComboPixmaps	*borders, *halignment, *valignment;
	struct {
		GtkToggleAction	 *bold, *italic, *underline, *d_underline;
		GtkToggleAction	 *superscript, *subscript, *strikethrough;
	} font;
	struct {
		GtkToggleAction	 *left, *center, *right, *center_across_selection;
	} h_align;
	struct {
		GtkToggleAction	 *top, *center, *bottom;
	} v_align;

	GtkWidget *menu_zone, *everything, *toolbar_zones[4];
	GHashTable *custom_uis;
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
	gtk->status_area = gtk_hbox_new (FALSE, 2);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), status, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), autoexpr, FALSE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->status_area), progress, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (gtk->everything),
		gtk->status_area, FALSE, TRUE, 0);
	gtk_widget_show_all (gtk->status_area);

	g_hash_table_insert (wbcg->visibility_widgets,
			     g_strdup ("ViewStatusbar"),
			     g_object_ref (gtk->status_area));
	/* disable statusbar by default going to fullscreen */
	g_hash_table_insert (wbcg->toggle_for_fullscreen,
		g_strdup ("ViewStatusbar"),
		gtk_action_group_get_action (gtk->actions, "ViewStatusbar"));
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
				  "label", _("_Zoom"),
				  "visible-vertical", FALSE,
				  NULL);
#ifdef USE_HILDON
	go_action_combo_text_set_width (gtk->zoom,  "100000000%");
#else
	go_action_combo_text_set_width (gtk->zoom,  "10000%");
#endif
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

#include <goffice/gtk/go-combo-pixmaps.h>
#include "pixmaps/gnumeric-stock-pixbufs.h"
#include "style-border.h"

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
	GnmBorder *borders[STYLE_BORDER_EDGE_MAX];
	int i;
	int index = go_action_combo_pixmaps_get_selected (a, NULL);
	
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
		      "visible-vertical", FALSE,
		      NULL);
	/* TODO: Create vertical version.  */
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

static GOActionComboPixmapsElement const halignment_combo_info[] = {
	{ N_("Align left"),		GTK_STOCK_JUSTIFY_LEFT,		HALIGN_LEFT },
	{ N_("Center horizontally"),	GTK_STOCK_JUSTIFY_CENTER,	HALIGN_CENTER },
	{ N_("Align right"),		GTK_STOCK_JUSTIFY_RIGHT,	HALIGN_RIGHT },
	{ N_("Fill Horizontally"),	"Gnumeric_HAlignFill",		HALIGN_FILL },
	{ N_("Justify Horizontally"),	GTK_STOCK_JUSTIFY_FILL,		HALIGN_JUSTIFY },
	{ N_("Center horizontally across the selection"),
					"Gnumeric_CenterAcrossSelection", HALIGN_CENTER_ACROSS_SELECTION },
	{ N_("Align numbers right, and text left"),
					"Gnumeric_HAlignGeneral",	HALIGN_GENERAL },
	{ NULL, NULL }
};
static GOActionComboPixmapsElement const valignment_combo_info[] = {
	{ N_("Align Top"),		"stock_alignment-top",			VALIGN_TOP },
	{ N_("Center Vertically"),	"stock_alignment-centered-vertically",	VALIGN_CENTER },
	{ N_("Align Bottom"),		"stock_alignment-bottom",		VALIGN_BOTTOM },
	{ NULL, NULL}
};

static void
cb_halignment_activated (GOActionComboPixmaps *a, WorkbookControlGUI *wbcg)
{
	wbcg_set_selection_halign (wbcg,
		go_action_combo_pixmaps_get_selected (a, NULL));
}
static void
cb_valignment_activated (GOActionComboPixmaps *a, WorkbookControlGUI *wbcg)
{
	wbcg_set_selection_valign (wbcg,
		go_action_combo_pixmaps_get_selected (a, NULL));
}
static void
wbc_gtk_init_alignments (WBCgtk *gtk)
{
	gtk->halignment = go_action_combo_pixmaps_new ("HAlignmentSelector",
						       halignment_combo_info, 3, 1);
	g_object_set (G_OBJECT (gtk->halignment),
		      "label", _("Horizontal Alignment"),
		      "tooltip", _("Horizontal Alignment"),
		      NULL);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Horizontal Alignment"));
	go_combo_pixmaps_select (gtk->halignment, 1); /* default to none */
#endif
	g_signal_connect (G_OBJECT (gtk->halignment),
		"activate",
		G_CALLBACK (cb_halignment_activated), gtk);
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->halignment));

	gtk->valignment = go_action_combo_pixmaps_new ("VAlignmentSelector",
						       valignment_combo_info, 1, 3);
	g_object_set (G_OBJECT (gtk->valignment),
		      "label", _("Vertical Alignment"),
		      "tooltip", _("Vertical Alignment"),
		      NULL);
#if 0
	gnm_combo_box_set_title (GO_COMBO_BOX (fore_combo), _("Horizontal Alignment"));
	go_combo_pixmaps_select (gtk->valignment, 1); /* default to none */
#endif
	g_signal_connect (G_OBJECT (gtk->valignment),
		"activate",
		G_CALLBACK (cb_valignment_activated), gtk);
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (gtk->valignment));
}

/****************************************************************************/

static GOActionComboStack *
ur_stack (WorkbookControl *wbc, gboolean is_undo)
{
	WBCgtk *gtk = (WBCgtk *)wbc;
	return is_undo ? gtk->undo_haction : gtk->redo_haction;
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
	gboolean old_val, new_val = gtk_action_is_sensitive (src);

	g_return_if_fail (action != NULL);
	g_object_get (action, "sensitive", &old_val, NULL);

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
		  WBCgtk *gtk,
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
#ifdef USE_HILDON
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (*haction));
#else
	gtk_action_group_add_action_with_accel (gtk->actions,
		GTK_ACTION (*haction), accel);
#endif
	g_signal_connect (G_OBJECT (*haction), "activate", hcb, gtk);

	*vaction = gtk_action_new (vname, NULL, tooltip, stock_id);
	g_object_set (G_OBJECT (*vaction),
		      "sensitive", FALSE,
		      "visible-horizontal", FALSE,
		      NULL);
	gtk_action_group_add_action (gtk->actions, GTK_ACTION (*vaction));
	g_signal_connect_swapped (G_OBJECT (*vaction), "activate", vcb, gtk);

	g_signal_connect (G_OBJECT (*haction),
			  "notify::sensitive",
			  G_CALLBACK (cb_chain_sensitivity),
			  *vaction);
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
	GtkAction *repeat =
		gtk_action_group_get_action (gtk->permanent_actions, "Repeat");

	create_undo_redo (&gtk->undo_haction, "Undo",
			  G_CALLBACK (cb_undo_activated),
			  &gtk->undo_vaction, "VUndo",
			  G_CALLBACK (command_undo),
			  gtk,
			  _("Undo the last action"),
			  GTK_STOCK_UNDO, "<control>z");
	g_signal_connect (G_OBJECT (gtk->undo_haction),
			  "notify::sensitive",
			  G_CALLBACK (cb_chain_sensitivity),
			  repeat);

	create_undo_redo (&gtk->redo_haction, "Redo",
			  G_CALLBACK (cb_redo_activated),
			  &gtk->redo_vaction, "VRedo",
			  G_CALLBACK (command_redo),
			  gtk,
			  _("Redo the undone action"),
			  GTK_STOCK_REDO, "<control>y");
}

/****************************************************************************/

static void
cb_custom_color_created (GOActionComboColor *caction, GtkWidget *dialog, WorkbookControlGUI *wbcg)
{
	wbcg_edit_attach_guru (wbcg, dialog);
	wbcg_set_transient_for (wbcg, GTK_WINDOW (dialog));
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
wbc_gtk_init_color_fore (WBCgtk *gtk)
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
cb_back_color_changed (GOActionComboColor *a, WorkbookControlGUI *wbcg)
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
wbc_gtk_init_color_back (WBCgtk *gtk)
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
wbc_gtk_init_font_name (WBCgtk *gtk)
{
	PangoContext *context;
	GSList *ptr, *families;

	gtk->font_name = g_object_new (go_action_combo_text_get_type (),
				       "name", "FontName",
				       "case-sensitive", FALSE,
				       "stock-id", GTK_STOCK_SELECT_FONT,
				       "visible-vertical", FALSE,
				       NULL);

	/* TODO: Create vertical version of this.  */

	context = gtk_widget_get_pango_context
		(GTK_WIDGET (wbcg_toplevel (WORKBOOK_CONTROL_GUI (gtk))));
	families = go_fonts_list_families (context);
	for (ptr = families; ptr != NULL; ptr = ptr->next)
		go_action_combo_text_add_item (gtk->font_name, ptr->data);
	g_slist_foreach (families, (GFunc)g_free, NULL);
	g_slist_free (families);

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
wbc_gtk_init_font_size (WBCgtk *gtk)
{
	GSList *ptr, *font_sizes;

	gtk->font_size = g_object_new (go_action_combo_text_get_type (),
				       "name", "FontSize",
				       "stock-id", GTK_STOCK_SELECT_FONT,
				       "visible-vertical", FALSE,
				       "label", _("Font Size"),
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
	gui_file_read (wbcg,
		g_object_get_data (action, "uri"), NULL, NULL);
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
		const char *uri = ptr->data;
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
			&entry, 1, (WorkbookControlGUI *)wbcg);
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
#ifndef USE_HILDON
			suffix = _("Nothing");
#endif
			sensitive = FALSE;
		}

#ifdef USE_HILDON
		text = g_strdup (prefix);

		{
			/* Set hildon toolbuttons (in)sensitive */
			GtkToolbar * toolbar = hildon_appview_get_toolbar (HILDON_APPVIEW (wbcg->toplevel));
			GList * l;
			
			for (l = gtk_container_get_children (GTK_CONTAINER (toolbar)); l; l = l->next) {
				if (GTK_IS_TOOL_BUTTON (l->data)) {
					gchar * label;
					g_object_get (l->data, "label", &label, NULL);
					
					if (strstr (label, action) != NULL) {
						g_object_set (G_OBJECT (l->data), "sensitive", sensitive, NULL);
						g_free(label);
						break;
					}

					g_free(label);
				}
			}
		}
#else
		text = g_strdup_printf ("%s : %s", prefix, suffix);
#endif
		g_object_set (G_OBJECT (a),
			      "label",	   text,
			      "sensitive", sensitive,
			      NULL);
		g_free (text);
	} else
		g_object_set (G_OBJECT (a), "label", suffix, NULL);

	if (new_tip != NULL)
		g_object_set (G_OBJECT (a), "tooltip", new_tip, NULL);
}

static void
wbc_gtk_set_toggle_action_state (WorkbookControlGUI const *wbcg,
				 char const *action, gboolean state)
{
	WBCgtk *gtk = (WBCgtk *)wbcg;
	GtkAction *a = gtk_action_group_get_action (gtk->actions, action);
	if (a == NULL)
		a = gtk_action_group_get_action (gtk->font_actions, action);
	if (a == NULL)
		a = gtk_action_group_get_action (gtk->toolbar.actions, action);
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
get_accel_label (GtkMenuItem *item, guint *key)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (item));
	GList *l;
	const char *res = NULL;

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
check_underlines (GtkWidget *w, const char *path)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (w));
	GHashTable *used = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify)g_free);
	GList *l;

	for (l = children; l; l = l->next) {
		GtkMenuItem *item = GTK_MENU_ITEM (l->data);
		GtkWidget *sub = gtk_menu_item_get_submenu (item);
		guint key;
		const char *label = get_accel_label (item, &key);

		if (sub) {
			char *newpath = g_strconcat (path, *path ? "->" : "", label, NULL);
			check_underlines (sub, newpath);
			g_free (newpath);
		}

		if (key != GDK_VoidSymbol) {
			const char *prev = g_hash_table_lookup (used, GUINT_TO_POINTER (key));
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

#endif

/****************************************************************************/
/* window list menu */

static void
cb_window_menu_activate (GObject *action, WorkbookControlGUI *wbcg)
{
	gtk_window_present (wbcg_toplevel (wbcg));
}

static unsigned
regenerate_window_menu (WBCgtk *gtk, Workbook *wb, unsigned i)
{
	int k, count;

	/* How many controls are there?  */
	count = 0;
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc, {
		if (IS_WORKBOOK_CONTROL_GUI (wbc))
			count++;
	});

	k = 1;
	WORKBOOK_FOREACH_CONTROL (wb, wbv, wbc, {
		char *basename;
		if (i >= 20)
			return i;
		if (IS_WORKBOOK_CONTROL_GUI (wbc) &&
			(basename = go_basename_from_uri (wb->uri)) != NULL) {
			GString *label = g_string_new (NULL);
			char *name;
			const char *s;
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
cb_regenerate_window_menu (WBCgtk *gtk)
{
	Workbook *wb = wb_control_workbook (WORKBOOK_CONTROL (gtk));
	GList const *ptr;
	unsigned i;

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
			"/menubar/View/Windows", 
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
		  GnmAppExtraUI *extra_ui, WBCgtk *gtk)
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
		     GnmAppExtraUI *extra_ui, WBCgtk *gtk)
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
cb_init_extra_ui (GnmAppExtraUI *extra_ui, WBCgtk *gtk)
{
	cb_add_custom_ui (NULL, extra_ui, gtk);
}

/****************************************************************************/
/* Toolbar menu */

static void
set_toolbar_style_for_position (GtkToolbar *tb, GtkPositionType pos)
{
	GtkHandleBox *hdlbox = GTK_HANDLE_BOX (GTK_WIDGET (tb)->parent);

	static const GtkPositionType hdlpos[] = {
		GTK_POS_TOP, GTK_POS_TOP,
		GTK_POS_LEFT, GTK_POS_LEFT
	};
	static const GtkOrientation orientations[] = {
		GTK_ORIENTATION_VERTICAL, GTK_ORIENTATION_VERTICAL,
		GTK_ORIENTATION_HORIZONTAL, GTK_ORIENTATION_HORIZONTAL
	};

	gtk_toolbar_set_orientation (tb, orientations[pos]);
	gtk_handle_box_set_handle_position (hdlbox, hdlpos[pos]);
}


static void
set_toolbar_position (GtkToolbar *tb, GtkPositionType pos, WBCgtk *gtk)
{
	GtkWidget *hdlbox = GTK_WIDGET (tb)->parent;
	GtkWidget *zone = GTK_WIDGET (hdlbox)->parent;
	GtkWidget *new_zone = gtk->toolbar_zones[pos];
	const char *name = g_object_get_data (G_OBJECT (hdlbox), "name");

	if (zone == new_zone)
		return;

	g_object_ref (hdlbox);
	gtk_container_remove (GTK_CONTAINER (zone), hdlbox);
	set_toolbar_style_for_position (tb, pos);
	gtk_container_add (GTK_CONTAINER (new_zone), hdlbox);
	g_object_unref (hdlbox);

	gnm_gconf_set_toolbar_position (name, pos);
}

static void
cb_set_toolbar_position (GtkMenuItem *item, WBCgtk *gtk)
{
	GtkToolbar *tb = g_object_get_data (G_OBJECT (item), "toolbar");
	GtkPositionType side = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "side"));

	set_toolbar_position (tb, side, gtk);
}

static void
cb_reattach (GtkWidget *widget, GtkHandleBox *hdlbox)
{
	GdkEvent *event = gdk_event_new (GDK_DELETE);
	event->any.type = GDK_DELETE;
	event->any.window = g_object_ref (hdlbox->float_window);
	event->any.send_event = TRUE;
	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void
toolbar_context_menu (GtkToolbar *tb, WBCgtk *gtk, GdkEventButton *event_button)
{
	GtkHandleBox *hdlbox = GTK_HANDLE_BOX (GTK_WIDGET (tb)->parent);
	GtkWidget *zone = GTK_WIDGET (hdlbox)->parent;
	GtkWidget *menu = gtk_menu_new ();

	static const struct {
		const char *text;
		GtkPositionType pos;
	} pos_items[] = {
		{ N_("Display above sheets"), GTK_POS_TOP },
		{ N_("Display to the left of sheets"), GTK_POS_LEFT },
		{ N_("Display to the right of sheets"), GTK_POS_RIGHT }
	};

	if (hdlbox->child_detached) {
		GtkWidget *item =
			gtk_menu_item_new_with_label (_("Reattach to main window"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);
		g_signal_connect (G_OBJECT (item), "activate",
				  G_CALLBACK (cb_reattach),
				  hdlbox);
	} else {
		size_t ui;
		GSList *group = NULL;

		for (ui = 0; ui < G_N_ELEMENTS (pos_items); ui++) {
			const char *text = _(pos_items[ui].text);
			GtkPositionType pos = pos_items[ui].pos;
			GtkWidget *item;

			item = gtk_radio_menu_item_new_with_label (group, text);
			group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));

			GTK_CHECK_MENU_ITEM (item)->active =
				(zone == gtk->toolbar_zones[pos]);

			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);
			g_object_set_data (G_OBJECT (item), "toolbar", tb);
			g_object_set_data (G_OBJECT (item), "side", GINT_TO_POINTER (pos));
			g_signal_connect (G_OBJECT (item), "activate",
					  G_CALLBACK (cb_set_toolbar_position),
					  gtk);
		}
	}

	gnumeric_popup_menu (GTK_MENU (menu), event_button);
}

static gboolean
cb_toolbar_button_press (GtkToolbar *tb, GdkEventButton *event, WBCgtk *gtk)
{
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		toolbar_context_menu (tb, gtk, event);
		return TRUE;
	}

	return FALSE;
}

static gboolean
cb_handlebox_button_press (GtkHandleBox *hdlbox, GdkEventButton *event, WBCgtk *gtk)
{
	if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
		GtkToolbar *tb = GTK_TOOLBAR (gtk_bin_get_child (GTK_BIN (hdlbox)));
		toolbar_context_menu (tb, gtk, event);
		return TRUE;
	}

	return FALSE;
}


static void
cb_toolbar_activate (GtkToggleAction *action, WorkbookControlGUI *wbcg)
{
	wbcg_toggle_visibility (wbcg, action);
}

static void
cb_handlebox_visible (GtkWidget *box, G_GNUC_UNUSED GParamSpec *pspec,
		      WorkbookControlGUI *wbcg)
{
	GtkToggleAction *toggle_action = g_object_get_data (
		G_OBJECT (box), "toggle_action");
	const char *name = g_object_get_data (G_OBJECT (box), "name");
	gboolean visible = GTK_WIDGET_VISIBLE (box);

	gtk_toggle_action_set_active (toggle_action, visible);
	gnm_gconf_set_toolbar_visible (name, visible);
}

static void
cb_add_menus_toolbars (G_GNUC_UNUSED GtkUIManager *ui,
		       GtkWidget *w, WBCgtk *gtk)
{
	if (GTK_IS_TOOLBAR (w)) {
		WorkbookControlGUI *wbcg = (WorkbookControlGUI *)gtk;
		char const *name = gtk_widget_get_name (w);
#ifdef USE_HILDON
		if (0 == strcmp (name, "HildonToolbar")) {
			hildon_appview_set_toolbar (HILDON_APPVIEW (wbcg->toplevel), GTK_TOOLBAR (w));
			gtk_widget_show_all (w);
		} else if (0 == strcmp (name, "HildonTopToolbar")) {
			gtk_table_attach (GTK_TABLE (wbcg->table), w, 1, 2, 0, 1,
					  GTK_FILL | GTK_EXPAND , 0, 0, 0);
		}
#else
		GtkWidget *box = gtk_handle_box_new ();
		GtkToggleActionEntry entry;
		char *toggle_name = g_strdup_printf ("ViewMenuToolbar%s", name);
		char *tooltip = g_strdup_printf (_("Show/Hide toolbar %s"), _(name));
		gboolean visible = gnm_gconf_get_toolbar_visible (name);
		GtkPositionType pos = gnm_gconf_get_toolbar_position (name);

		g_signal_connect (G_OBJECT (w),
				  "button_press_event",
				  G_CALLBACK (cb_toolbar_button_press),
				  gtk);
		g_signal_connect (G_OBJECT (box),
				  "button_press_event",
				  G_CALLBACK (cb_handlebox_button_press),
				  gtk);

		gtk_container_add (GTK_CONTAINER (box), w);
		gtk_toolbar_set_show_arrow (GTK_TOOLBAR (w), TRUE);
		gtk_toolbar_set_style (GTK_TOOLBAR (w), GTK_TOOLBAR_ICONS);
		gtk_widget_show_all (box);
		if (!visible)
			gtk_widget_hide (box);
		gtk_box_pack_start (GTK_BOX (gtk->toolbar_zones[pos]), box, FALSE, FALSE, 0);
		set_toolbar_style_for_position (GTK_TOOLBAR (w), pos);
		g_object_connect (box,
			"signal::notify::visible", G_CALLBACK (cb_handlebox_visible), wbcg,
			"signal::child_attached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (TRUE),
			"signal::child_detached", G_CALLBACK (cb_handlebox_dock_status), GINT_TO_POINTER (FALSE),
			NULL);

		entry.name = toggle_name;
		entry.stock_id = NULL;
		entry.label = _(name);
		entry.accelerator = (0 == strcmp (name, "StandardToolbar")) ? "<control>7" : NULL;
		entry.tooltip = tooltip;
		entry.callback = G_CALLBACK (cb_toolbar_activate);
		entry.is_active = visible;
		gtk_action_group_add_toggle_actions (gtk->toolbar.actions,
			&entry, 1, (WorkbookControlGUI *)wbcg);
		gtk_ui_manager_add_ui (gtk->ui, gtk->toolbar.merge_id,
			"/menubar/View/Toolbars", 
			toggle_name, toggle_name, GTK_UI_MANAGER_AUTO, FALSE);
		g_object_set_data (G_OBJECT (box), "toggle_action",
			gtk_action_group_get_action (gtk->toolbar.actions, toggle_name));
		g_object_set_data_full (G_OBJECT (box), "name",
					g_strdup (name),
					(GDestroyNotify)g_free);

		g_hash_table_insert (wbcg->visibility_widgets,
			g_strdup (toggle_name), g_object_ref (box));
		g_hash_table_insert (wbcg->toggle_for_fullscreen,
			g_strdup (toggle_name),
			gtk_action_group_get_action (gtk->toolbar.actions,
						     toggle_name));

		g_free (tooltip);
		g_free (toggle_name);
#endif
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
cb_post_activate (WorkbookControlGUI *wbcg)
{
	if (!wbcg_is_editing (wbcg))
		wbcg_focus_cur_scg (wbcg);
}

static void
cb_toggle_visibility (char const *name,
		      GtkToggleAction *action, WorkbookControlGUI *wbcg)
{
	wbc_gtk_set_toggle_action_state (wbcg, name,
		!gtk_toggle_action_get_active (action));
}

static void
cb_wbcg_window_state_event (GtkWidget           *widget,
			    GdkEventWindowState *event,
			    WorkbookControlGUI  *wbcg)
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
		{ "FontDoubleUnderline",   TRUE, G_STRUCT_OFFSET (WBCgtk, font.d_underline) },
		{ "FontSuperscript",	   TRUE, G_STRUCT_OFFSET (WBCgtk, font.superscript) },
		{ "FontSubscript",	   TRUE, G_STRUCT_OFFSET (WBCgtk, font.subscript) },
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
	GtkWidget *hbox;

	gtk->menu_zone = gtk_vbox_new (TRUE, 0);
	gtk->everything = gtk_vbox_new (FALSE, 0);

	gtk->toolbar_zones[GTK_POS_TOP] = gtk_vbox_new (FALSE, 0);
	gtk->toolbar_zones[GTK_POS_BOTTOM] = NULL;
	gtk->toolbar_zones[GTK_POS_LEFT] = gtk_hbox_new (FALSE, 0);
	gtk->toolbar_zones[GTK_POS_RIGHT] = gtk_hbox_new (FALSE, 0);

#ifdef USE_HILDON
	{ GtkWidget *appview = hildon_appview_new ("Gnumeric");
	wbcg->hildon_app = hildon_app_new_with_appview (HILDON_APPVIEW (appview));
	wbcg_set_toplevel (wbcg, appview); }
#else
	wbcg_set_toplevel (wbcg, gtk_window_new (GTK_WINDOW_TOPLEVEL));
	g_signal_connect (wbcg_toplevel (wbcg), "window_state_event",
			  G_CALLBACK (cb_wbcg_window_state_event),
			  wbcg);
	gtk_box_pack_start (GTK_BOX (gtk->everything),
		gtk->menu_zone, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (gtk->everything),
		gtk->toolbar_zones[GTK_POS_TOP], FALSE, TRUE, 0);
#endif

	gtk_window_set_title (wbcg_toplevel (wbcg), "Gnumeric");
	gtk_window_set_wmclass (wbcg_toplevel (wbcg), "Gnumeric", "Gnumeric");

#ifdef USE_HILDON
	gtk_widget_show_all (GTK_WIDGET (wbcg_toplevel (wbcg)));
#endif
#if 0
	bonobo_dock_set_client_area (BONOBO_DOCK (gtk->dock), wbcg->table);
#endif

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), gtk->toolbar_zones[GTK_POS_LEFT], FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), wbcg->table, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (hbox), gtk->toolbar_zones[GTK_POS_RIGHT], FALSE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (gtk->everything), hbox, TRUE, TRUE, 0);
	gtk_widget_show_all (gtk->everything);

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
	wbc_gtk_init_alignments (gtk);

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
	gtk_window_add_accel_group (wbcg_toplevel (wbcg), 
		gtk_ui_manager_get_accel_group (gtk->ui));

#ifdef USE_HILDON	
	uifile = g_build_filename (gnm_sys_data_dir (), "HILDON_Gnumeric-gtk.xml", NULL);
#else
	uifile = g_build_filename (gnm_sys_data_dir (), "GNOME_Gnumeric-gtk.xml", NULL);
#endif
	if (!gtk_ui_manager_add_ui_from_file (gtk->ui, uifile, &error)) {
		g_message ("building menus failed: %s", error->message);
		g_error_free (error);
	}
	g_free (uifile);

	gtk->custom_uis = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						 NULL, g_free);

	gtk->file_history.actions = NULL;
	gtk->file_history.merge_id = 0;
	wbc_gtk_reload_recent_file_menu (wbcg);

	gtk->toolbar.merge_id = gtk_ui_manager_new_merge_id (gtk->ui);
	gtk->toolbar.actions = gtk_action_group_new ("Toolbars");
	gtk_ui_manager_insert_action_group (gtk->ui, gtk->toolbar.actions, 0);

	gtk->windows.actions = NULL;
	gtk->windows.merge_id = 0;
	gnm_app_foreach_extra_ui ((GFunc) cb_init_extra_ui, gtk);
	g_object_connect ((GObject *) gnm_app_get_app (),
		"swapped-object-signal::window-list-changed",
			G_CALLBACK (cb_regenerate_window_menu), gtk,
		"object-signal::custom-ui-added",
			G_CALLBACK (cb_add_custom_ui), gtk,
		"object-signal::custom-ui-removed",
			G_CALLBACK (cb_remove_custom_ui), gtk,
		NULL);

	gtk_ui_manager_ensure_update (gtk->ui);

#ifdef USE_HILDON
	{
		GtkWidget *hildonMenu = GTK_WIDGET (hildon_appview_get_menu (HILDON_APPVIEW (wbcg->toplevel)));
		GList *list = gtk_container_get_children (GTK_CONTAINER (gtk->menu_zone));
		
		for (list = gtk_container_get_children (GTK_CONTAINER (list->data)); list; list = list->next) {
			gtk_widget_reparent (GTK_WIDGET (list->data), hildonMenu);
		}

		gtk_widget_show_all (GTK_WIDGET (hildonMenu));
	}
#endif

	gtk_container_add (GTK_CONTAINER (wbcg->toplevel), gtk->everything);

	/*
	 * The following line updates the menus before the check so we
	 * avoid problems like #324692.  We could move it inside the
	 * conditional, but I don't like doing active things like this
	 * in there.
	 */
	wb_control_undo_redo_labels (WORKBOOK_CONTROL (wbcg), "", "");

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
	if (gtk->toolbar.merge_id != 0)
		gtk_ui_manager_remove_ui (gtk->ui, gtk->toolbar.merge_id);
	if (gtk->toolbar.actions != NULL)
		g_object_unref (gtk->toolbar.actions);
	g_object_unref (gtk->ui);

	g_hash_table_destroy (gtk->custom_uis);

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

/* vim: set sw=8: */

/*
 * workbook-object-toolbar.c: Toolbar for adding objects
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
#include <glib.h>
#include <libgnome/gnome-defs.h>
#include <libgnome/gnome-i18n.h>
#include "sheet.h"
#include "sheet-control-gui.h"
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "workbook-object-toolbar.h"
#include "gnumeric-toolbar.h"
#include "sheet-object-widget.h"
#include "sheet-object-graphic.h"
#ifdef ENABLE_BONOBO
#include <bonobo.h>
#endif

static void
create_object_command (SheetControlGUI *scg, SheetObject *so)
{
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));

	scg_mode_create_object (scg, so);
	workbook_recalc (sheet->workbook);
	sheet_update (sheet);
}

static void
cmd_create_label (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));
	SheetObject *so = sheet_widget_label_new (sheet);
		
	create_object_command (scg, so);
}
static void
cmd_create_frame (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));
	SheetObject *so = sheet_widget_frame_new (sheet);
	create_object_command (scg, so);
}

static void
cmd_create_button (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));
	SheetObject *so = sheet_widget_button_new (sheet);
	create_object_command (scg, so);
}
static void
cmd_create_checkbox (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));
	SheetObject *so = sheet_widget_checkbox_new (sheet);
	create_object_command (scg, so);
}
static void
cmd_create_radiobutton (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));
	SheetObject *so = sheet_widget_radio_button_new	(sheet);
	create_object_command (scg, so);
}
static void
cmd_create_list (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));
	SheetObject *so = sheet_widget_list_new (sheet);
	create_object_command (scg, so);
}
static void
cmd_create_combobox (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));
	SheetObject *so = sheet_widget_combo_new (sheet);
	create_object_command (scg, so);
}

static void
cmd_create_line (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	SheetObject *so = sheet_object_line_new (FALSE);
	create_object_command (scg, so);
}
static void
cmd_create_arrow (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	SheetObject *so = sheet_object_line_new (TRUE);
	create_object_command (scg, so);
}
static void
cmd_create_rectangle (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	SheetObject *so = sheet_object_box_new (FALSE);
	create_object_command (scg, so);
}
static void
cmd_create_ellipse (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	SheetControlGUI *scg = wb_control_gui_cur_sheet (wbcg);
	SheetObject *so = sheet_object_box_new (TRUE);
	create_object_command (scg, so);
}


#ifndef ENABLE_BONOBO
static GnomeUIInfo workbook_object_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("Label"), N_("Creates a label"),
		&cmd_create_label, "Gnumeric_Label"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Frame"), N_("Creates a frame"),
		&cmd_create_frame, "Gnumeric_Frame"),
#if 0
	/* not useful until we have scripts */
	GNOMEUIINFO_ITEM_STOCK (
		N_("Button"), N_("Creates a button"),
		&cmd_create_button, "Gnumeric_Button"),
#endif
	GNOMEUIINFO_ITEM_STOCK (
		N_("Checkbox"), N_("Creates a checkbox"),
		&cmd_create_checkbox, "Gnumeric_Checkbutton"),
#if 0
	/* need to think about how to manage groups */
	GNOMEUIINFO_ITEM_STOCK (
		N_("RadioButton"), N_("Creates a radio button"),
		&cmd_create_radiobutton, "Gnumeric_Radiobutton"),
#endif
	GNOMEUIINFO_ITEM_STOCK (
		N_("List"), N_("Creates a list"),
		&cmd_create_list, "Gnumeric_List"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Combo"), N_("Creates a combobox"),
		&cmd_create_combobox, "Gnumeric_Combo"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Line"), N_("Creates a line object"),
		cmd_create_line, "Gnumeric_Line"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Arrow"), N_("Creates an arrow object"),
		cmd_create_arrow, "Gnumeric_Arrow"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Rectangle"), N_("Creates a rectangle object"),
		cmd_create_rectangle, "Gnumeric_Rectangle"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Ellipse"), N_("Creates an ellipse object"),
		cmd_create_ellipse, "Gnumeric_Oval"),

	GNOMEUIINFO_END
};

void
workbook_create_object_toolbar (WorkbookControlGUI *wbcg)
{
	GtkWidget *toolbar = NULL;

	static char const * const name = "ObjectToolbar";
	GnomeDockItemBehavior behavior;
	GnomeApp *app;

	app = GNOME_APP (wbcg->toplevel);

	g_return_if_fail (app != NULL);

	toolbar = gnumeric_toolbar_new (workbook_object_toolbar,
					app->accel_group, wbcg);

	behavior = GNOME_DOCK_ITEM_BEH_NORMAL;
	if (!gnome_preferences_get_toolbar_detachable ())
		behavior |= GNOME_DOCK_ITEM_BEH_LOCKED;

	gnome_app_add_toolbar (app, GTK_TOOLBAR (toolbar), name, behavior,
			       GNOME_DOCK_TOP, 3, 0, 0);
	gtk_widget_show (toolbar);
	wbcg->object_toolbar = toolbar;
}
#else
static BonoboUIVerb verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("CreateLabel", cmd_create_label),
	BONOBO_UI_UNSAFE_VERB ("CreateFrame", cmd_create_frame),
	BONOBO_UI_UNSAFE_VERB ("CreateButton", cmd_create_button),
	BONOBO_UI_UNSAFE_VERB ("CreateCheckbox", cmd_create_checkbox),
	BONOBO_UI_UNSAFE_VERB ("CreateRadiobutton", cmd_create_radiobutton),
	BONOBO_UI_UNSAFE_VERB ("CreateList", cmd_create_list),
	BONOBO_UI_UNSAFE_VERB ("CreateCombobox", cmd_create_combobox),
	BONOBO_UI_UNSAFE_VERB ("CreateLine", cmd_create_line),
	BONOBO_UI_UNSAFE_VERB ("CreateArrow", cmd_create_arrow),
	BONOBO_UI_UNSAFE_VERB ("CreateRectangle", cmd_create_rectangle),
	BONOBO_UI_UNSAFE_VERB ("CreateEllipse", cmd_create_ellipse),
	BONOBO_UI_VERB_END
};

void
workbook_create_object_toolbar (WorkbookControlGUI *wbcg)
{
	bonobo_ui_component_add_verb_list_with_data (wbcg->uic, verbs, wbcg);
}
#endif

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
#include <gnome.h>
#include "sheet.h"
#include "workbook.h"
#include "workbook-private.h"
#include "workbook-object-toolbar.h"
#include "gnumeric-toolbar.h"
#include "sheet-object-widget.h"
#include "sheet-object-graphic.h"
#ifdef ENABLE_BONOBO
#include <bonobo.h>
#endif

static void
create_object_command (Sheet *sheet, SheetObject *so)
{
	sheet_mode_create_object (so);
	workbook_recalc (sheet->workbook);
	sheet_update (sheet);
}

static void
cmd_create_label (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_widget_label_new (sheet);
	create_object_command (sheet, so);
}
static void
cmd_create_frame (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_widget_frame_new (sheet);
	create_object_command (sheet, so);
}

static void
cmd_create_button (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_widget_button_new (sheet);
	create_object_command (sheet, so);
}
static void
cmd_create_checkbox (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_widget_checkbox_new (sheet);
	create_object_command (sheet, so);
}
static void
cmd_create_radiobutton (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_widget_radio_button_new	(sheet);
	create_object_command (sheet, so);
}
static void
cmd_create_list (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_widget_list_new (sheet);
	create_object_command (sheet, so);
}
static void
cmd_create_combobox (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_widget_combo_new (sheet);
	create_object_command (sheet, so);
}

static void
cmd_create_line (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_object_create_line (sheet, FALSE, "black", 1);
	create_object_command (sheet, so);
}
static void
cmd_create_arrow (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_object_create_line (sheet, TRUE, "black", 1);
	create_object_command (sheet, so);
}
static void
cmd_create_rectangle (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_object_create_filled (sheet, SHEET_OBJECT_BOX,
						      NULL, "black", 1);
	create_object_command (sheet, so);
}
static void
cmd_create_ellipse (GtkWidget *widget, Workbook *wb)
{
	Sheet *sheet = wb->current_sheet;
	SheetObject *so = sheet_object_create_filled (sheet, SHEET_OBJECT_OVAL,
						      NULL, "black", 1);
	create_object_command (sheet, so);
}


#ifndef ENABLE_BONOBO
static GnomeUIInfo workbook_object_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("Label"), N_("Creates a label"),
		&cmd_create_label, "Gnumeric_Label"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Frame"), N_("Creates a frame"),
		&cmd_create_frame, "Gnumeric_Frame"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Button"), N_("Creates a button"),
		&cmd_create_button, "Gnumeric_Button"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Checkbox"), N_("Creates a checkbox"),
		&cmd_create_checkbox, "Gnumeric_Checkbutton"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("RadioButton"), N_("Creates a radio button"),
		&cmd_create_radiobutton, "Gnumeric_Radiobutton"),
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
#endif

GtkWidget *
workbook_create_object_toolbar (Workbook *wb)
{
	GtkWidget *toolbar = NULL;

#ifndef ENABLE_BONOBO
	static char const * const name = "ObjectToolbar";
	GnomeDockItemBehavior behavior;
	GnomeApp *app;

	app = GNOME_APP (wb->toplevel);

	g_return_val_if_fail (app != NULL, NULL);

	toolbar = gnumeric_toolbar_new (workbook_object_toolbar,
					app->accel_group, wb);

	behavior = GNOME_DOCK_ITEM_BEH_NORMAL;
	if (!gnome_preferences_get_menubar_detachable ())
		behavior |= GNOME_DOCK_ITEM_BEH_LOCKED;

	gnome_app_add_toolbar (
		app,
		GTK_TOOLBAR (toolbar),
		name,
		behavior,
		GNOME_DOCK_TOP, 3, 0, 0);
#else
	bonobo_ui_component_add_verb_list_with_data (wb->priv->uic, verbs, wb);
#endif

	return toolbar;
}

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
#define GNUMERIC_TEST_ACTIVE_OBJECT

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

#ifdef GNUMERIC_TEST_ACTIVE_OBJECT
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
#endif

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

#include "pixmaps/label.xpm"
#include "pixmaps/frame.xpm"
#ifdef GNUMERIC_TEST_ACTIVE_OBJECT
#include "pixmaps/button.xpm"
#include "pixmaps/checkbutton.xpm"
#include "pixmaps/radiobutton.xpm"
#include "pixmaps/list.xpm"
#include "pixmaps/combo.xpm"
#endif
#include "pixmaps/rect.xpm"
#include "pixmaps/line.xpm"
#include "pixmaps/arrow.xpm"
#include "pixmaps/oval.xpm"


static GnomeUIInfo workbook_object_toolbar [] = {
	GNOMEUIINFO_ITEM_DATA (
		N_("Label"), N_("Creates a label"),
		&cmd_create_label, NULL, label_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Frame"), N_("Creates a frame"),
		&cmd_create_frame, NULL, frame_xpm),
#ifdef GNUMERIC_TEST_ACTIVE_OBJECT
	GNOMEUIINFO_ITEM_DATA (
		N_("Button"), N_("Creates a button"),
		&cmd_create_button, NULL, button_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Checkbox"), N_("Creates a checkbox"),
		&cmd_create_checkbox, NULL, checkbutton_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("RadioButton"), N_("Creates a radio button"),
		&cmd_create_radiobutton, NULL, radiobutton_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("List"), N_("Creates a list"),
		&cmd_create_list, NULL, list_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Combo"), N_("Creates a combobox"),
		&cmd_create_combobox, NULL, combo_xpm),
#endif
	GNOMEUIINFO_ITEM_DATA (
		N_("Line"), N_("Creates a line object"),
		cmd_create_line, NULL, line_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Arrow"), N_("Creates an arrow object"),
		cmd_create_arrow, NULL, arrow_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Rectangle"), N_("Creates a rectangle object"),
		cmd_create_rectangle, NULL, rect_xpm),
	GNOMEUIINFO_ITEM_DATA (
		N_("Ellipse"), N_("Creates an ellipse object"),
		cmd_create_ellipse, NULL, oval_xpm),

	GNOMEUIINFO_END
};

GtkWidget *
workbook_create_object_toolbar (Workbook *wb)
{
	GtkWidget *toolbar;

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
		GNOME_DOCK_TOP, 2, 0, 0);
#else
	toolbar = gnumeric_toolbar_new (
		workbook_object_toolbar,
		bonobo_win_get_accel_group (BONOBO_WIN (wb->toplevel)), wb);

#warning FIXME; the toolbar should be bonoboized properly.
	gtk_box_pack_start (GTK_BOX (wb->priv->main_vbox), toolbar,
			    FALSE, FALSE, 0);
#endif

	return toolbar;
}

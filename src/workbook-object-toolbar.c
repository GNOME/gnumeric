/* vim: set sw=8: */

/*
 * workbook-object-toolbar.c: Toolbar for adding objects
 *
 * Copyright (C) 2000 Jody Goldberg (jody@gnome.org)
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
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include "gnumeric.h"
#include "workbook-object-toolbar.h"

#include "sheet.h"
#include "sheet-control-gui.h"
#include "workbook-control-gui-priv.h"
#include "workbook.h"
#include "sheet-object-widget.h"
#include "sheet-object-graphic.h"
#include "sheet-object-graph.h"
#include "gui-util.h"

#ifdef NEW_GRAPHS
#include <goffice-graph/go-graph-guru.h>
#endif
#ifdef WITH_BONOBO
#include <bonobo.h>
#endif

static void
create_object_command (WorkbookControlGUI *wbcg, SheetObject *so)
{
	SheetControlGUI *scg = wbcg_cur_scg (wbcg);
	Sheet *sheet = sc_sheet (SHEET_CONTROL (scg));

	scg_mode_create_object (scg, so);
	workbook_recalc (sheet->workbook);
	sheet_update (sheet);
}

static void
cmd_create_label (GtkWidget *ignored, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg,
		g_object_new (sheet_object_text_get_type (), NULL));
}
static void
cmd_create_frame (GtkWidget *ignored, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg,
		g_object_new (sheet_widget_frame_get_type(), NULL));
}

#if 0
	/* not useful until we have scripts */
static void
cmd_create_button (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg,
		g_object_new (sheet_widget_button_get_type(), NULL));
}
#endif
static void
cmd_create_scrollbar (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg,
		g_object_new (sheet_widget_scrollbar_get_type(), NULL));
}
static void
cmd_create_checkbox (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg,
		g_object_new (sheet_widget_checkbox_get_type(), NULL));
}
#if 0
static void
cmd_create_radiobutton (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg,
		g_object_new (sheet_widget_radiobutton_get_type(), NULL));
}
#endif
static void
cmd_create_list (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg,
		g_object_new (sheet_widget_list_get_type(), NULL));
}
static void
cmd_create_combo (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg,
		g_object_new (sheet_widget_combo_get_type(), NULL));
}

static void
cmd_create_line (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg, sheet_object_line_new (FALSE));
}
static void
cmd_create_arrow (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg, sheet_object_line_new (TRUE));
}
static void
cmd_create_rectangle (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg, sheet_object_box_new (FALSE));
}
static void
cmd_create_ellipse (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	create_object_command (wbcg, sheet_object_box_new (TRUE));
}

#ifdef NEW_GRAPHS
static void
cmd_create_graph (GtkWidget *widget, WorkbookControlGUI *wbcg)
{
	go_graph_guru (NULL, wbcg);
}
#endif

#ifndef WITH_BONOBO
static GnomeUIInfo workbook_object_toolbar [] = {
	GNOMEUIINFO_ITEM_STOCK (
		N_("Label"), N_("Create a label"),
		&cmd_create_label, "Gnumeric_ObjectLabel"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Frame"), N_("Create a frame"),
		&cmd_create_frame, "Gnumeric_ObjectFrame"),
#if 0
	/* not useful until we have scripts */
	GNOMEUIINFO_ITEM_STOCK (
		N_("Button"), N_("Create a button"),
		&cmd_create_button, "Gnumeric_ObjectButton"),
#endif
	GNOMEUIINFO_ITEM_STOCK (
		N_("Scrollbar"), N_("Create a scrollbar"),
		&cmd_create_scrollbar, "Gnumeric_ObjectScrollbar"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Checkbox"), N_("Create a checkbox"),
		&cmd_create_checkbox, "Gnumeric_ObjectCheckbox"),
#if 0
	/* need to think about how to manage groups */
	GNOMEUIINFO_ITEM_STOCK (
		N_("RadioButton"), N_("Create a radio button"),
		&cmd_create_radiobutton, "Gnumeric_ObjectRadiobutton"),
#endif
	GNOMEUIINFO_ITEM_STOCK (
		N_("List"), N_("Create a list"),
		&cmd_create_list, "Gnumeric_ObjectList"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Combo Box"), N_("Create a combo box"),
		&cmd_create_combo, "Gnumeric_ObjectCombo"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Line"), N_("Create a line object"),
		cmd_create_line, "Gnumeric_ObjectLine"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Arrow"), N_("Create an arrow object"),
		cmd_create_arrow, "Gnumeric_ObjectArrow"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Rectangle"), N_("Create a rectangle object"),
		cmd_create_rectangle, "Gnumeric_ObjectRectangle"),
	GNOMEUIINFO_ITEM_STOCK (
		N_("Ellipse"), N_("Create an ellipse object"),
		cmd_create_ellipse, "Gnumeric_ObjectEllipse"),
#ifdef NEW_GRAPHS
	GNOMEUIINFO_ITEM_STOCK (
		N_("Graph"), N_("Create a graph"),
		cmd_create_graph, "Gnumeric_GraphGuru"),
#endif

	GNOMEUIINFO_END
};

void
workbook_create_object_toolbar (WorkbookControlGUI *wbcg)
{
	wbcg->object_toolbar = gnumeric_toolbar_new (wbcg,
		workbook_object_toolbar, "ObjectToolbar", 3, 0, 0);
	gtk_widget_show (wbcg->object_toolbar);
}
#else
static BonoboUIVerb verbs [] = {
	BONOBO_UI_UNSAFE_VERB ("CreateLabel", cmd_create_label),
	BONOBO_UI_UNSAFE_VERB ("CreateFrame", cmd_create_frame),
	/* BONOBO_UI_UNSAFE_VERB ("CreateButton", cmd_create_button), */
	BONOBO_UI_UNSAFE_VERB ("CreateScrollbar", cmd_create_scrollbar),
	BONOBO_UI_UNSAFE_VERB ("CreateCheckbox", cmd_create_checkbox),
	/* BONOBO_UI_UNSAFE_VERB ("CreateRadiobutton", cmd_create_radiobutton), */
	BONOBO_UI_UNSAFE_VERB ("CreateList", cmd_create_list),
	BONOBO_UI_UNSAFE_VERB ("CreateCombo", cmd_create_combo),
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

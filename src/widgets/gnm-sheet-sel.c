/*
 * gnm-sheet-sel.c: A selector for sheets.
 *
 * Copyright (c) 2018 Morten Welinder
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 **/

#include <gnumeric-config.h>
#include <widgets/gnm-sheet-sel.h>
#include <gnm-i18n.h>
#include <sheet.h>
#include <workbook.h>
#include <application.h>

#define SHEET_KEY "__sheet"

struct GnmSheetSel_ {
	GOOptionMenu parent;

	Sheet *sheet;
};

typedef struct {
	GOOptionMenuClass parent_klass;
} GnmSheetSelClass;

enum {
	PROP_0,
	PROP_SHEET
};

static GOOptionMenuClass *gnm_sheet_sel_parent_class;

/**
 * gnm_sheet_sel_set_sheet:
 * @ss: #GnmSheetSel
 * @sheet: (transfer none): #Sheet
 */
void
gnm_sheet_sel_set_sheet (GnmSheetSel *ss, Sheet *sheet)
{
	GtkWidget *menu;

	g_return_if_fail (GNM_IS_SHEET_SEL (ss));

	if (sheet == ss->sheet)
		return;

	menu = go_option_menu_get_menu (&ss->parent);
	if (menu) {
		GList *children =
			gtk_container_get_children (GTK_CONTAINER (menu));
		GList *l;

		for (l = children; l; l = l->next) {
			GtkMenuItem *item = l->data;
			Sheet *this_sheet =
				g_object_get_data (G_OBJECT (item), SHEET_KEY);
			if (this_sheet == sheet) {
				go_option_menu_select_item (&ss->parent, item);
				break;
			}
		}
		g_list_free (children);
	}

	ss->sheet = sheet;

	g_object_notify (G_OBJECT (ss), "sheet");
}

/**
 * gnm_sheet_sel_get_sheet:
 * @ss: #GnmSheetSel
 *
 * Returns: (transfer none): Selected #Sheet
 */
Sheet *
gnm_sheet_sel_get_sheet (GnmSheetSel *ss)
{
	g_return_val_if_fail (GNM_IS_SHEET_SEL (ss), NULL);
	return ss->sheet;
}

static void
cb_changed (GOOptionMenu *om, GnmSheetSel *ss)
{
	GtkWidget *item = go_option_menu_get_history (om);
	Sheet *sheet = g_object_get_data (G_OBJECT (item), SHEET_KEY);
	gnm_sheet_sel_set_sheet (ss, sheet);
}

static void
gnm_sheet_sel_init (GnmSheetSel *ss)
{
	g_signal_connect (G_OBJECT (&ss->parent), "changed",
                          G_CALLBACK (cb_changed), ss);
}

static void
gnm_sheet_sel_set_property (GObject *object, guint property_id,
			    const GValue *value, GParamSpec *pspec)
{
	GnmSheetSel *ss = (GnmSheetSel *)object;

	switch (property_id) {
	case PROP_SHEET:
		gnm_sheet_sel_set_sheet (ss, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sheet_sel_get_property (GObject *object, guint property_id,
			    GValue *value, GParamSpec *pspec)
{
	GnmSheetSel *ss = (GnmSheetSel *)object;

	switch (property_id) {
	case PROP_SHEET:
		g_value_set_object (value, ss->sheet);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_sheet_sel_class_init (GObjectClass *klass)
{
	gnm_sheet_sel_parent_class = g_type_class_peek (GO_TYPE_OPTION_MENU);

	klass->set_property	= gnm_sheet_sel_set_property;
	klass->get_property	= gnm_sheet_sel_get_property;

	g_object_class_install_property
		(klass, PROP_SHEET,
		 g_param_spec_object ("sheet",
				      P_("Sheet"),
				      P_("The current sheet"),
				      GNM_SHEET_TYPE,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));
}


GSF_CLASS (GnmSheetSel, gnm_sheet_sel,
	   gnm_sheet_sel_class_init, gnm_sheet_sel_init,
	   GO_TYPE_OPTION_MENU)

GtkWidget *
gnm_sheet_sel_new (void)
{
	return g_object_new (GNM_TYPE_SHEET_SEL, NULL);
}

/**
 * gnm_sheet_sel_set_sheets:
 * @ss: #GnmSheetSel
 * @sheets: (element-type Sheet) (transfer none): sheets
 */
void
gnm_sheet_sel_set_sheets (GnmSheetSel *ss, GPtrArray *sheets)
{
	GtkMenu *menu;
	unsigned ui;

	g_return_if_fail (GNM_IS_SHEET_SEL (ss));

        menu = GTK_MENU (gtk_menu_new ());

	for (ui = 0; ui < sheets->len; ui++) {
		Sheet *sheet = g_ptr_array_index (sheets, ui);
		GtkWidget *item =
			gtk_check_menu_item_new_with_label
			(sheet->name_unquoted);
		gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), FALSE);
		g_object_set_data (G_OBJECT (item), SHEET_KEY, sheet);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	}

	gtk_widget_show_all (GTK_WIDGET (menu));
	go_option_menu_set_menu (&ss->parent, GTK_WIDGET (menu));

	if (sheets->len > 0)
		gnm_sheet_sel_set_sheet (ss, g_ptr_array_index (sheets, 0));
}

static void
cb_wb_changed (GnmWorkbookSel *wbs,
	       G_GNUC_UNUSED GParamSpec *pspec,
	       GnmSheetSel *ss)
{
	Workbook *wb = gnm_workbook_sel_get_workbook (wbs);
	GPtrArray *sheets = wb ? workbook_sheets (wb) : NULL;
	// FIXME: sort?
	gnm_sheet_sel_set_sheets (ss, sheets);
	g_ptr_array_unref (sheets);
}

void
gnm_sheet_sel_link (GnmSheetSel *ss, GnmWorkbookSel *wbs)
{
	g_return_if_fail (GNM_IS_SHEET_SEL (ss));
	g_return_if_fail (GNM_IS_WORKBOOK_SEL (wbs));

	g_signal_connect_object
		(wbs,
		 "notify::workbook", G_CALLBACK (cb_wb_changed),
		 ss, 0);
	cb_wb_changed (wbs, NULL, ss);
}

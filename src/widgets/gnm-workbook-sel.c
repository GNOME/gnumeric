/*
 * gnm-workbook-sel.c: A selector for workbooks.
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
#include <widgets/gnm-workbook-sel.h>
#include <gnm-i18n.h>
#include <workbook.h>
#include <application.h>

#define WB_KEY "__wb"

struct GnmWorkbookSel_ {
	GOOptionMenu parent;

	Workbook *wb;
};

typedef struct {
	GOOptionMenuClass parent_klass;
} GnmWorkbookSelClass;

enum {
	PROP_0,
	PROP_WORKBOOK
};

static GOOptionMenuClass *gnm_workbook_sel_parent_class;

/**
 * gnm_workbook_sel_set_workbook:
 * @wbs: #GnmWorkbookSel
 * @wb: (transfer none): #Workbook
 */
void
gnm_workbook_sel_set_workbook (GnmWorkbookSel *wbs, Workbook *wb)
{
	GtkWidget *menu;

	g_return_if_fail (GNM_IS_WORKBOOK_SEL (wbs));

	if (wb == wbs->wb)
		return;

	menu = go_option_menu_get_menu (&wbs->parent);
	if (menu) {
		GList *children =
			gtk_container_get_children (GTK_CONTAINER (menu));
		GList *l;

		for (l = children; l; l = l->next) {
			GtkMenuItem *item = l->data;
			Workbook *this_wb =
				g_object_get_data (G_OBJECT (item), WB_KEY);
			if (this_wb == wb) {
				go_option_menu_select_item (&wbs->parent, item);
				break;
			}
		}
		g_list_free (children);
	}

	wbs->wb = wb;

	g_object_notify (G_OBJECT (wbs), "workbook");
}

/**
 * gnm_workbook_sel_get_workbook:
 * @wbs: #GnmWorkbookSel
 *
 * Returns: (transfer none): Selected workbook
 */
Workbook *
gnm_workbook_sel_get_workbook (GnmWorkbookSel *wbs)
{
	g_return_val_if_fail (GNM_IS_WORKBOOK_SEL (wbs), NULL);
	return wbs->wb;
}

static void
cb_changed (GOOptionMenu *om, GnmWorkbookSel *wbs)
{
	GtkWidget *item = go_option_menu_get_history (om);
	Workbook *wb = g_object_get_data (G_OBJECT (item), WB_KEY);
	gnm_workbook_sel_set_workbook (wbs, wb);
}

static gint
doc_order (gconstpointer a_, gconstpointer b_)
{
	GODoc *a = (GODoc *)a_;
	GODoc *b = (GODoc *)b_;

	return go_str_compare (go_doc_get_uri (a), go_doc_get_uri (b));
}

static void
gnm_workbook_sel_init (GnmWorkbookSel *wbs)
{
	GtkMenu *menu;
	GList *l, *wb_list;

        menu = GTK_MENU (gtk_menu_new ());

	wb_list = g_list_copy (gnm_app_workbook_list ());
	wb_list = g_list_sort (wb_list, doc_order);

	for (l = wb_list; l; l = l->next) {
		Workbook *wb = l->data;
		GtkWidget *item, *child;
		const char *uri;
		char *markup, *shortname, *filename, *dirname, *longname, *duri;

		uri = go_doc_get_uri (GO_DOC (wb));
		filename = go_filename_from_uri (uri);
		if (filename) {
			shortname = g_filename_display_basename (filename);
		} else {
			shortname = g_filename_display_basename (uri);
		}

		dirname = g_path_get_dirname (filename);
		duri = g_uri_unescape_string (dirname, NULL);
		longname = duri
			? g_filename_display_name (duri)
			: g_strdup (uri);

		markup = g_markup_printf_escaped
			(_("%s\n<small>%s</small>"),
			 shortname, longname);

		item = gtk_check_menu_item_new_with_label ("");
		gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (item), TRUE);
		child = gtk_bin_get_child (GTK_BIN (item));
		gtk_label_set_markup (GTK_LABEL (child), markup);
		gtk_label_set_ellipsize (GTK_LABEL (child), PANGO_ELLIPSIZE_MIDDLE);

		g_free (markup);
		g_free (shortname);
		g_free (dirname);
		g_free (longname);
		g_free (duri);
		g_free (filename);

		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		g_object_set_data (G_OBJECT (item), WB_KEY, wb);
	}

	gtk_widget_show_all (GTK_WIDGET (menu));
	go_option_menu_set_menu (&wbs->parent, GTK_WIDGET (menu));

	if (wb_list)
		gnm_workbook_sel_set_workbook (wbs, wb_list->data);

	g_list_free (wb_list);

	g_signal_connect (G_OBJECT (&wbs->parent), "changed",
                          G_CALLBACK (cb_changed), wbs);
}

static void
gnm_workbook_sel_set_property (GObject *object, guint property_id,
			       const GValue *value, GParamSpec *pspec)
{
	GnmWorkbookSel *wbs = (GnmWorkbookSel *)object;

	switch (property_id) {
	case PROP_WORKBOOK:
		gnm_workbook_sel_set_workbook (wbs, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_workbook_sel_get_property (GObject *object, guint property_id,
			       GValue *value, GParamSpec *pspec)
{
	GnmWorkbookSel *wbs = (GnmWorkbookSel *)object;

	switch (property_id) {
	case PROP_WORKBOOK:
		g_value_set_object (value, wbs->wb);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_workbook_sel_class_init (GObjectClass *klass)
{
	gnm_workbook_sel_parent_class = g_type_class_peek (GO_TYPE_OPTION_MENU);

	klass->set_property	= gnm_workbook_sel_set_property;
	klass->get_property	= gnm_workbook_sel_get_property;

	g_object_class_install_property
		(klass, PROP_WORKBOOK,
		 g_param_spec_object ("workbook",
				      P_("Workbook"),
				      P_("The current workbook"),
				      GNM_WORKBOOK_TYPE,
				      GSF_PARAM_STATIC | G_PARAM_READWRITE));
}


GSF_CLASS (GnmWorkbookSel, gnm_workbook_sel,
	   gnm_workbook_sel_class_init, gnm_workbook_sel_init,
	   GO_TYPE_OPTION_MENU)

GtkWidget *
gnm_workbook_sel_new (void)
{
	return g_object_new (GNM_TYPE_WORKBOOK_SEL, NULL);
}

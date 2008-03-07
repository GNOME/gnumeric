/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/**
 * gnm-dao.c:  Implements a widget to specify tool output location.
 *
 * Copyright (c) 2003 Andreas J. Guelzow <aguelzow@taliesin.ca>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 **/

#include <gnumeric-config.h>
#include "gnm-dao.h"
#include "gnumeric-expr-entry.h"
#include <tools/dao.h>
#include <value.h>
#include <workbook-control.h>

#include <goffice/gtk/goffice-gtk.h>
#include <gsf/gsf-impl-utils.h>
#include <gtk/gtkcombobox.h>
#include <gtk/gtktable.h>
#include <gtk/gtkhbox.h>
#include <gtk/gtktogglebutton.h>
#include <glib/gi18n-lib.h>

struct  _GnmDao {
	GtkHBox 	box;
	GladeXML 	*gui;

	GtkWidget *new_sheet;
	GtkWidget *new_workbook;
	GtkWidget *output_range;
	GtkWidget *in_place;
	GnmExprEntry *output_entry;
        GtkWidget *clear_outputrange_button;
        GtkWidget *retain_format_button;
        GtkWidget *retain_comments_button;
        GtkWidget *put_menu;

	WBCGtk *wbcg;
};

typedef struct {
	GtkHBoxClass parent_class;

	void (*gnm_dao_changed) (GnmDao *gdao);
	void (*gnm_dao_activate) (GnmDao *gdao);
} GnmDaoClass;

static GtkHBoxClass *gnm_dao_parent_class;

/* Signals we emit */
enum {
	GNM_DAO_CHANGED,
	GNM_DAO_ACTIVATE,
	LAST_SIGNAL
};

static guint gnm_dao_signals[LAST_SIGNAL] = { 0 };

static void
gnm_dao_init (GnmDao *gdao)
{
	GtkWidget *toplevel;
	GtkWidget *old_parent;

	gdao->gui = gnm_glade_xml_new (NULL, "dao.glade", NULL, NULL);
	if (gdao->gui == NULL)
		return;

	toplevel = glade_xml_get_widget (gdao->gui, "dao_box");

	gdao->new_sheet  = glade_xml_get_widget (gdao->gui,
						 "newsheet-button");
	gdao->new_workbook  = glade_xml_get_widget (gdao->gui,
						    "newworkbook-button");
	gdao->output_range  = glade_xml_get_widget (gdao->gui,
						    "outputrange-button");
	gdao->in_place  = glade_xml_get_widget (gdao->gui,
						    "inplace-button");
	gdao->clear_outputrange_button = glade_xml_get_widget
		(gdao->gui, "clear_outputrange_button");
	gdao->retain_format_button = glade_xml_get_widget
		(gdao->gui, "retain_format_button");
	gdao->retain_comments_button = glade_xml_get_widget
		(gdao->gui, "retain_comments_button");
	gdao->put_menu = glade_xml_get_widget
		(gdao->gui, "put_menu");
	gtk_combo_box_set_active
		(GTK_COMBO_BOX (gdao->put_menu), 1);
	gdao->output_entry = NULL;
	gdao->wbcg = NULL;

	old_parent = gtk_widget_get_toplevel (toplevel);
	gtk_widget_reparent (toplevel, GTK_WIDGET (gdao));
	gtk_widget_destroy (old_parent);
	gtk_widget_queue_resize (toplevel);
}

static void
gnm_dao_destroy (GtkObject *object)
{
  	GnmDao *gdao = GNM_DAO (object);

	if (gdao->gui) {
		g_object_unref (G_OBJECT (gdao->gui));
		gdao->gui = NULL;
	}

	((GtkObjectClass *)gnm_dao_parent_class)->destroy (object);
}

static void
gnm_dao_class_init (GtkObjectClass *klass)
{
	klass->destroy = gnm_dao_destroy;

	gnm_dao_parent_class = g_type_class_peek (gtk_hbox_get_type ());

	gnm_dao_signals[GNM_DAO_CHANGED] =
		g_signal_new ("readiness-changed",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmDaoClass, gnm_dao_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	gnm_dao_signals[GNM_DAO_ACTIVATE] =
		g_signal_new ("activate",
			      G_OBJECT_CLASS_TYPE (klass),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (GnmDaoClass, gnm_dao_activate),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

GSF_CLASS (GnmDao, gnm_dao,
	   gnm_dao_class_init, gnm_dao_init, GTK_TYPE_HBOX)

static void
tool_set_focus_output_range (G_GNUC_UNUSED GtkWidget *widget,
			     G_GNUC_UNUSED GdkEventFocus *event,
			     GnmDao *gdao)
{
	    gtk_toggle_button_set_active
		    (GTK_TOGGLE_BUTTON (gdao->output_range), TRUE);
}

static void
cb_focus_on_entry (GtkWidget *widget, GtkWidget *entry)
{
        if (GTK_TOGGLE_BUTTON (widget)->active)
		gtk_widget_grab_focus
			(GTK_WIDGET (gnm_expr_entry_get_entry
				     (GNM_EXPR_ENTRY (entry))));
}

const char *dao_group[] = {
	"newsheet-button",
	"newworkbook-button",
	"outputrange-button",
	"inplace-button",
	NULL
};

static void
cb_emit_readiness_changed (G_GNUC_UNUSED GtkWidget *dummy, GnmDao *gdao)
{
	g_signal_emit_by_name (G_OBJECT (gdao), "readiness-changed");
}

static void
cb_emit_activate (G_GNUC_UNUSED GtkWidget *dummy, GnmDao *gdao)
{
	g_signal_emit_by_name (G_OBJECT (gdao), "activate");
}

static void
cb_set_sensitivity (G_GNUC_UNUSED GtkWidget *dummy, GnmDao *gdao)
{
	int grp_val = gnumeric_glade_group_value (gdao->gui, dao_group);

	gtk_widget_set_sensitive (gdao->clear_outputrange_button,
				  (grp_val == 2));
	gtk_widget_set_sensitive (gdao->retain_format_button,
				  (grp_val == 2));
	gtk_widget_set_sensitive (gdao->retain_comments_button,
				  (grp_val == 2));

}

static void
gnm_dao_setup_signals (GnmDao *gdao)
{
	g_signal_connect (G_OBJECT (gdao->output_range),
			  "toggled",
			  G_CALLBACK (cb_focus_on_entry),
			  gdao->output_entry);
	g_signal_connect
		(G_OBJECT (gnm_expr_entry_get_entry
			   (GNM_EXPR_ENTRY (gdao->output_entry))),
		 "focus-in-event",
		 G_CALLBACK (tool_set_focus_output_range), gdao);
	g_signal_connect_after (G_OBJECT (gdao->output_entry),
				"changed",
				G_CALLBACK (cb_set_sensitivity), gdao);
	g_signal_connect_after (G_OBJECT (gdao->output_entry),
				"changed",
				G_CALLBACK (cb_emit_readiness_changed),
				gdao);
	g_signal_connect (G_OBJECT (gdao->output_entry),
				  "activate",
			  G_CALLBACK (cb_emit_activate), gdao);
	g_signal_connect_after (G_OBJECT (gdao->output_range),
				"toggled",
				G_CALLBACK (cb_set_sensitivity), gdao);
	g_signal_connect_after (G_OBJECT (gdao->output_range),
				"toggled",
				G_CALLBACK (cb_emit_readiness_changed),
				gdao);
}

GtkWidget *
gnm_dao_new (WBCGtk *wbcg, gchar *inplace_str)
{
	GnmDao *gdao = GNM_DAO (g_object_new (GNM_DAO_TYPE, NULL));
	GtkTable *table;

	g_return_val_if_fail (wbcg != NULL, NULL);
	gdao->wbcg = wbcg;

	/* Create the output range expression entry */
	table = GTK_TABLE (glade_xml_get_widget (gdao->gui, "output-table"));
	gdao->output_entry = gnm_expr_entry_new (wbcg, TRUE);
	gnm_expr_entry_set_flags (gdao->output_entry,
				  GNM_EE_SINGLE_RANGE, GNM_EE_MASK);
	gtk_table_attach (table, GTK_WIDGET (gdao->output_entry),
			  2, 3, 3, 4,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
	go_atk_setup_label (gdao->output_range,
			     GTK_WIDGET (gdao->output_entry));
	gtk_widget_show (GTK_WIDGET (gdao->output_entry));
	/* Finished creating the output range expression entry */

	gnm_dao_set_inplace (gdao, inplace_str);
	gnm_dao_setup_signals (gdao);
	cb_set_sensitivity (NULL, gdao);

	return GTK_WIDGET (gdao);
}

void
gnm_dao_set_inplace (GnmDao *gdao, gchar *inplace_str)
{
	g_return_if_fail (gdao != NULL);

	if (inplace_str) {
		gtk_button_set_label (GTK_BUTTON (gdao->in_place),
				      inplace_str);
		gtk_widget_show (gdao->in_place);
	} else
		gtk_widget_hide (gdao->in_place);
}

gboolean
gnm_dao_get_data (GnmDao *gdao, data_analysis_output_t **dao)
{
	gboolean dao_ready  = FALSE;
	int grp_val;

	g_return_val_if_fail (gdao != NULL, FALSE);

	grp_val = gnumeric_glade_group_value (gdao->gui, dao_group);

	dao_ready =  ((grp_val  != 2) ||
		      gnm_expr_entry_is_cell_ref
		      (GNM_EXPR_ENTRY (gdao->output_entry),
		       wb_control_cur_sheet (WORKBOOK_CONTROL (gdao->wbcg)),
		       TRUE));

	if (dao_ready && NULL != dao) {
		GtkWidget *button;
		GnmValue *output_range = NULL;

		switch (grp_val) {
		case 0:
		default:
			*dao = dao_init (*dao, NewSheetOutput);
			break;
		case 1:
			*dao = dao_init (*dao, NewWorkbookOutput);
			break;
		case 2:
			output_range = gnm_expr_entry_parse_as_value
				(GNM_EXPR_ENTRY (gdao->output_entry),
				 wb_control_cur_sheet (WORKBOOK_CONTROL
						       (gdao->wbcg)));
			*dao = dao_init (*dao, RangeOutput);
			dao_load_from_value (*dao, output_range);
			value_release (output_range);
			break;
		case 3:
			(*dao) = dao_init ((*dao), InPlaceOutput);
			/* It is the callers responsibility to fill the */
			/* dao with the appropriate range. */
			break;
		}

		button = glade_xml_get_widget (gdao->gui, "autofit_button");
		(*dao)->autofit_flag = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (button));

		(*dao)->clear_outputrange = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (gdao->clear_outputrange_button));
		(*dao)->retain_format = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (gdao->retain_format_button));
		(*dao)->retain_comments = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (gdao->retain_comments_button));

		(*dao)->put_formulas
			= (gtk_combo_box_get_active
			   (GTK_COMBO_BOX (gdao->put_menu))
			   != 0);
	}

	return dao_ready;
}

gboolean
gnm_dao_is_ready (GnmDao *gdao)
{
	return gnm_dao_get_data (gdao, NULL);
}

gboolean
gnm_dao_is_finite (GnmDao *gdao)
{
	int grp_val;

	g_return_val_if_fail (gdao != NULL, FALSE);

	grp_val = gnumeric_glade_group_value (gdao->gui, dao_group);
	return ((grp_val == 2) || (grp_val == 3));
}


void
gnm_dao_set_put (GnmDao *gdao, gboolean show_put, gboolean put_formulas)
{
	g_return_if_fail (gdao != NULL);

	gtk_combo_box_set_active
		(GTK_COMBO_BOX (gdao->put_menu), put_formulas ? 1 : 0);
	gtk_widget_set_sensitive (GTK_WIDGET (gdao->put_menu), show_put);
}

void
gnm_dao_load_range (GnmDao *gdao, GnmRange const *range)
{
	g_return_if_fail (gdao != NULL);

	gnm_expr_entry_load_from_range
		(gdao->output_entry,
		 wb_control_cur_sheet (WORKBOOK_CONTROL (gdao->wbcg)),
		 range);
}

void
gnm_dao_focus_output_range (GnmDao *gdao)
{
	gtk_toggle_button_set_active
		    (GTK_TOGGLE_BUTTON (gdao->output_range), TRUE);
}

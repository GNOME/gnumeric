/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/**
 * dialog-autofilter.c:  A pair of dialogs for autofilter conditions
 *
 * (c) Copyright 2002 Jody Goldberg <jody@gnome.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <commands.h>
#include <workbook-control.h>
#include <workbook.h>
#include <workbook-edit.h>
#include <sheet.h>
#include <sheet-view.h>
#include <workbook-cmd-format.h>
#include <sheet-filter.h>

#include <glade/glade.h>

typedef struct {
	GladeXML           *gui;
	WorkbookControlGUI *wbcg;
	GtkWidget          *dialog;
	GnmFilter	   *filter;
	unsigned	    field;
	gboolean	    is_expr;
} AutoFilterState;

#define DIALOG_KEY "autofilter"

static void
cb_autofilter_destroy (AutoFilterState *state)
{
	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}

	state->dialog = NULL;
	g_free (state);
}

static void
cb_autofilter_ok (G_GNUC_UNUSED GtkWidget *button,
		  AutoFilterState *state)
{
	GnmFilterCondition *cond = NULL;
	GtkWidget *w;

	if (state->is_expr) {
	} else {
		int bottom, percentage, count;

		w = glade_xml_get_widget (state->gui, "top_vs_bottom_option_menu");
		bottom = gtk_option_menu_get_history (GTK_OPTION_MENU (w));

		w = glade_xml_get_widget (state->gui, "item_vs_percentage_option_menu");
		percentage = gtk_option_menu_get_history (GTK_OPTION_MENU (w));

		w = glade_xml_get_widget (state->gui, "item_count");
		count = gtk_spin_button_get_value (GTK_SPIN_BUTTON (w));
		if (bottom >= 0 && percentage >= 0)
			cond = gnm_filter_condition_new_bucket (
					!bottom, !percentage, count);
	}
	if (cond != NULL) {
		gnm_filter_set_condition (state->filter, state->field,
				 cond, TRUE);
		sheet_update (state->filter->dep.sheet);
	}

	gtk_widget_destroy (state->dialog);
}

static void
cb_autofilter_cancel (G_GNUC_UNUSED GtkWidget *button,
		      AutoFilterState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_top10_type_changed (GtkOptionMenu *menu,
		       AutoFilterState *state)
{
	GtkWidget *spin = glade_xml_get_widget (state->gui, "item_count");

	gtk_spin_button_set_range (GTK_SPIN_BUTTON (spin), 1.,
		(gtk_option_menu_get_history (menu) > 0) ? 100. : 500.);
}

void
dialog_auto_filter (WorkbookControlGUI *wbcg,
		    GnmFilter *filter, int field,
		    gboolean is_expr, GnmFilterCondition *cond)
{
	AutoFilterState *state;
	GtkWidget *w;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, DIALOG_KEY))
		return;

	state = g_new (AutoFilterState, 1);
	state->wbcg	= wbcg;
	state->filter	= filter;
	state->field	= field;
	state->is_expr	= is_expr;

	state->gui = gnumeric_glade_xml_new (wbcg,
			is_expr ? "autofilter-expression.glade" : "autofilter-top10.glade");

	g_return_if_fail (state->gui != NULL);

	if (is_expr) {
	} else {
		w = glade_xml_get_widget (state->gui, "item_vs_percentage_option_menu");
		g_signal_connect (G_OBJECT (w),
			"changed",
			G_CALLBACK (cb_top10_type_changed), state);
	}

	if (cond != NULL) {
		GnmFilterOp const op = cond->op[0];
		if (is_expr && 0 == (op & GNM_FILTER_OP_TYPE_MASK)) {
		} else if (!is_expr &&
			   GNM_FILTER_OP_TOP_N == (op & GNM_FILTER_OP_TYPE_MASK)) {
			w = glade_xml_get_widget (state->gui, "top_vs_bottom_option_menu");
			gtk_option_menu_set_history (GTK_OPTION_MENU (w), (op & 1) ? 1 : 0);
			w = glade_xml_get_widget (state->gui, "item_vs_percentage_option_menu");
			gtk_option_menu_set_history (GTK_OPTION_MENU (w), (op & 2) ? 1 : 0);
			w = glade_xml_get_widget (state->gui, "item_count");
			gtk_spin_button_set_value (GTK_SPIN_BUTTON (w),
						   cond->count);
		}
	}

	state->dialog = glade_xml_get_widget (state->gui, "dialog");

	w = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_autofilter_ok), state);
	w = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_autofilter_cancel), state);

	/* a candidate for merging into attach guru */
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"autofilter.html");
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_autofilter_destroy);
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gnumeric_keyed_dialog (wbcg, GTK_WINDOW (state->dialog), DIALOG_KEY);
	gtk_widget_show (state->dialog);
}

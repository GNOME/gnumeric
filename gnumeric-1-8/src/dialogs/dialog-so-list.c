/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-so-list.c: A property dialog for lists and combos
 *
 * Copyright (C) 2006 Jody Goldberg (jody@gnome.org)
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
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "help.h"

#include <expr.h>
#include <selection.h>
#include <sheet.h>
#include <sheet-view.h>
#include <sheet-object-widget.h>
#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <gui-util.h>
#include <parse-util.h>
#include <commands.h>
#include <widgets/gnumeric-expr-entry.h>
#include <gtk/gtktable.h>
#include <glib/gi18n.h>

#define DIALOG_SO_LIST_KEY "so-list"

typedef struct {
	GladeXML	*gui;
	GtkWidget	*dialog;
	GnmExprEntry	*content_entry, *link_entry;

	WBCGtk	*wbcg;
	SheetObject		*so;
} GnmDialogSOList;

static void
cb_so_list_destroy (GnmDialogSOList *state)
{
	if (state->gui != NULL)
		g_object_unref (G_OBJECT (state->gui));
	g_free (state);
}

static GnmExprEntry *
init_entry (GnmDialogSOList *state, char const *name,
	    GnmDependent const *dep)
{
	GnmExprEntry *gee;
	GtkWidget *w = glade_xml_get_widget (state->gui, name);

	g_return_val_if_fail (w != NULL, NULL);

	gee = GNM_EXPR_ENTRY (w);
	g_object_set (G_OBJECT (w),
		"scg", wbcg_cur_scg (state->wbcg),
		"with-icon", TRUE,
		NULL);
	gnm_expr_entry_set_flags (gee,
		GNM_EE_FORCE_ABS_REF | GNM_EE_SHEET_OPTIONAL | GNM_EE_SINGLE_RANGE,
		GNM_EE_MASK);
	gnm_expr_entry_load_from_dep (gee, dep);
	return gee;
}

static void
cb_so_list_response (GtkWidget *dialog, gint response_id, GnmDialogSOList *state)
{
	if (response_id == GTK_RESPONSE_HELP)
		return;
	if (response_id == GTK_RESPONSE_OK) {
	}

	gtk_object_destroy (GTK_OBJECT (dialog));
}

static gboolean
so_list_init (GnmDialogSOList *state, WBCGtk *wbcg, SheetObject *so)
{
	GtkTable *table;

	state->gui = gnm_glade_xml_new (GO_CMD_CONTEXT (wbcg),
		"so-list.glade", NULL, NULL);
        if (state->gui == NULL)
                return TRUE;

	state->wbcg   = wbcg;
	state->so     = so;
	state->dialog = glade_xml_get_widget (state->gui, "SOList");
	table = GTK_TABLE (glade_xml_get_widget (state->gui, "table"));

	state->content_entry = init_entry (state, "content-entry",
		sheet_widget_list_base_get_content_dep (so));
	state->link_entry = init_entry (state, "link-entry",
		sheet_widget_list_base_get_result_dep (so));

	g_signal_connect (G_OBJECT (state->dialog), "response",
		G_CALLBACK (cb_so_list_response), state);
	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help"),
		GNUMERIC_HELP_LINK_SO_LIST);

	/* a candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
		DIALOG_SO_LIST_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify)cb_so_list_destroy);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (state->dialog));
	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show_all (GTK_WIDGET (state->dialog));

	return FALSE;
}

void
dialog_so_list (WBCGtk *wbcg, GObject *so)
{
	GnmDialogSOList *state;

	g_return_if_fail (wbcg != NULL);

	if (wbc_gtk_get_guru (wbcg) ||
	    gnumeric_dialog_raise_if_exists (wbcg, DIALOG_SO_LIST_KEY))
		return;

	state = g_new0 (GnmDialogSOList, 1);
	if (so_list_init (state, wbcg, SHEET_OBJECT (so))) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
			_("Could not create the List Property dialog."));
		g_free (state);
	}
}

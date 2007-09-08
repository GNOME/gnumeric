/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-so-styled.c: Pref dialog for objects with a GogStyle 'style' property
 *
 * Copyright (C) 2004 Jody Goldberg (jody@gnome.org)
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
#include "gnumeric.h"
#include "dialogs.h"

#include "gui-gnumeric.h"
#include "gui-util.h"
#include "dialogs/help.h"
#include "wbc-gtk.h"
#include "commands.h"
#include "sheet-object.h"
#include <goffice/app/go-cmd-context.h>
#include <goffice/graph/gog-style.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkbox.h>

typedef struct {
	GObject			*so;
	WBCGtk	*wbcg;
	GogStyle		*orig_style;
} DialogSOStyled;

#define GNM_SO_STYLED_KEY "gnm-so-styled-key"

static void
dialog_so_styled_free (DialogSOStyled *pref)
{
	if (pref->orig_style != NULL) {
		g_object_set (G_OBJECT (pref->so), "style", pref->orig_style, NULL);
		g_object_unref (pref->orig_style);
	}
	g_free (pref);
}

static void
cb_dialog_so_styled_response (GtkWidget *dialog,
			      gint response_id, DialogSOStyled *pref)
{
	if (response_id == GTK_RESPONSE_HELP)
		return;
	if (response_id == GTK_RESPONSE_OK) {
		cmd_object_format (WORKBOOK_CONTROL (pref->wbcg),
			SHEET_OBJECT (pref->so), pref->orig_style);
		g_object_unref (pref->orig_style);
		pref->orig_style = NULL;
	}
	gtk_object_destroy (GTK_OBJECT (dialog));
}

void
dialog_so_styled (WBCGtk *wbcg,
		  GObject *so, GogStyle *orig, GogStyle *default_style,
		  char const *title)
{
	DialogSOStyled *state;
	GtkWidget	*dialog, *help;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, GNM_SO_STYLED_KEY))
		return;

	state = g_new0 (DialogSOStyled, 1);
	state->so    = G_OBJECT (so);
	state->wbcg  = wbcg;
	state->orig_style = gog_style_dup (orig);
	dialog = gtk_dialog_new_with_buttons (title,
		wbcg_toplevel (state->wbcg),
		GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);

	help = gtk_dialog_add_button (GTK_DIALOG (dialog),
		GTK_STOCK_HELP,		GTK_RESPONSE_HELP);
	gnumeric_init_help_button (help, "sect-graphics-drawings");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
		GTK_STOCK_CANCEL,	GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK,		GTK_RESPONSE_OK,
		NULL);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		gog_style_get_editor (orig, default_style,
			GO_CMD_CONTEXT (wbcg), G_OBJECT (so)),
		TRUE, TRUE, TRUE);
	g_object_unref (default_style);
	g_signal_connect (G_OBJECT (dialog), "response",
		G_CALLBACK (cb_dialog_so_styled_response), state);
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (dialog),
		GNM_SO_STYLED_KEY);
	g_object_set_data_full (G_OBJECT (dialog),
		"state", state, (GDestroyNotify) dialog_so_styled_free);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (dialog));
	wbc_gtk_attach_guru (state->wbcg, dialog);
	gtk_widget_show (dialog);
}

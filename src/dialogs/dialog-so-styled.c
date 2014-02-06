/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-so-styled.c: Pref dialog for objects with a GOStyle 'style' property
 *
 * Copyright (C) 2004 Jody Goldberg (jody@gnome.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) version 3.
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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "dialogs.h"

#include "gui-util.h"
#include "dialogs/help.h"
#include "wbc-gtk.h"
#include "commands.h"
#include "sheet-object.h"
#include <goffice/goffice.h>
#include <gtk/gtk.h>
#include <widgets/gnumeric-text-view.h>

typedef struct {
	GObject *so;
	WBCGtk *wbcg;
	GOStyle *orig_style;
	char *orig_text;
	PangoAttrList *orig_attributes;
	so_styled_t extent;
} DialogSOStyled;

#define GNM_SO_STYLED_KEY "gnm-so-styled-key"

static void
dialog_so_styled_free (DialogSOStyled *pref)
{
	if (pref->orig_style != NULL) {
		g_object_set (G_OBJECT (pref->so), "style", pref->orig_style, NULL);
		g_object_unref (pref->orig_style);
	}
	if ((pref->extent & SO_STYLED_TEXT) != 0) {
		g_object_set (G_OBJECT (pref->so), "text", pref->orig_text, NULL);
		g_free (pref->orig_text);
		g_object_set (G_OBJECT (pref->so), "markup", pref->orig_attributes, NULL);
		pango_attr_list_unref (pref->orig_attributes);
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
				   SHEET_OBJECT (pref->so), pref->orig_style,
				   pref->orig_text, pref->orig_attributes);
		g_object_unref (pref->orig_style);
		pref->orig_style = NULL;
		g_free (pref->orig_text);
		pref->orig_text = NULL;
		pango_attr_list_unref (pref->orig_attributes);
		pref->orig_attributes = NULL;
		pref->extent = 0;
	}
	gtk_widget_destroy (dialog);
}

static void
cb_dialog_so_styled_text_widget_changed (GnmTextView *gtv, DialogSOStyled *state)
{
	gchar *text;
	PangoAttrList *attr;

	g_object_get (gtv, "text", &text, NULL);
	g_object_set (state->so, "text", text, NULL);
	g_free (text);

	g_object_get (gtv, "attributes", &attr, NULL);
	g_object_set (state->so, "markup", attr, NULL);
	pango_attr_list_unref (attr);
}



static GtkWidget *
dialog_so_styled_text_widget (DialogSOStyled *state)
{
	GnmTextView *gtv = gnm_text_view_new ();
	char *strval;
	PangoAttrList  *markup;

	g_object_get (state->so, "text", &strval, NULL);
	g_object_set (gtv, "text", (strval == NULL) ? "" : strval, NULL);
	state->orig_text = strval;

	g_object_get (state->so, "markup", &markup, NULL);
	state->orig_attributes = markup;
	pango_attr_list_ref (state->orig_attributes);
	g_object_set (gtv, "attributes", markup, NULL);

	g_signal_connect (G_OBJECT (gtv), "changed",
			  G_CALLBACK (cb_dialog_so_styled_text_widget_changed), state);

	return GTK_WIDGET (gtv);
}

void
dialog_so_styled (WBCGtk *wbcg,
		  GObject *so, GOStyle *orig, GOStyle *default_style,
		  char const *title, so_styled_t extent)
{
	DialogSOStyled *state;
	GtkWidget	*dialog, *help, *editor;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, GNM_SO_STYLED_KEY)) {
		g_object_unref (default_style);
		return;
	}

	state = g_new0 (DialogSOStyled, 1);
	state->so    = G_OBJECT (so);
	state->wbcg  = wbcg;
	state->orig_style = go_style_dup (orig);
	state->orig_text = NULL;
	dialog = gtk_dialog_new_with_buttons
		(title,
		 wbcg_toplevel (state->wbcg),
		 GTK_DIALOG_DESTROY_WITH_PARENT,
		 NULL, NULL);
	state->extent = extent;

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	help = gtk_dialog_add_button (GTK_DIALOG (dialog),
		GTK_STOCK_HELP,		GTK_RESPONSE_HELP);
	gnumeric_init_help_button (help, "sect-graphics-drawings");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
		GTK_STOCK_CANCEL,	GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK,		GTK_RESPONSE_OK,
		NULL);

	editor = go_style_get_editor (orig, default_style,
				      GO_CMD_CONTEXT (wbcg), G_OBJECT (so));

	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
		editor, TRUE, TRUE, TRUE);
	g_object_unref (default_style);

	if (extent == SO_STYLED_TEXT) {
		GtkWidget *text_w = dialog_so_styled_text_widget (state);
		gtk_widget_show_all (text_w);
		if (GTK_IS_NOTEBOOK (editor))
			gtk_notebook_append_page (GTK_NOTEBOOK (editor),
						  text_w,
						  gtk_label_new (_("Content")));
		else
			gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
					    text_w, TRUE, TRUE, TRUE);
	}

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

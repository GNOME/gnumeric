/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-hyperlink.c: Add or edit a hyperlink
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */
#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"

#include <gui-util.h>
#include <hlink.h>
#include <sheet-control.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <workbook-edit.h>
#include <gnumeric-i18n.h>

typedef struct {
	WorkbookControlGUI  *wbcg;
	Workbook  *wb;

	GladeXML  *gui;
	GtkWidget *dialog;

	GtkImage  *type_image;
	GtkLabel  *type_descriptor;
	GnmHLink  *link;
} HyperlinkState;

static GType last_link_type = 0;

static void
dialog_hyperlink_free (HyperlinkState *state)
{
	wbcg_edit_detach_guru (state->wbcg);

	if (state->gui != NULL) {
		g_object_unref (G_OBJECT (state->gui));
		state->gui = NULL;
	}
	if (state->link != NULL) {
		g_object_unref (G_OBJECT (state->link));
		state->link = NULL;
	}
	state->dialog = NULL;
	g_free (state);
}

static void
cb_cancel (GtkWidget *button, HyperlinkState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
cb_ok (GtkWidget *button, HyperlinkState *state)
{
#warning todo assign the result
	gtk_widget_destroy (state->dialog);
}

static struct {
	char const *label;
	char const *image_name;
	char const *name;
	char const *widget_name;
	char const *descriptor;
} const type [] = {
	{ N_("_Internal Link"), "Gnumeric_Link_Internal",
	  "GnmHLinkCurWB",	"internal-link-box",
	  N_("Jump to specific cells or named range in the current workbook") },
	{ N_("_External Link"), "Gnumeric_Link_External",
	  "GnmHLinkExternal",	"external-link-box" ,
	  N_("Open an external file with the specified name") },
	{ N_("Send _Email"),	"Gnumeric_Link_EMail",
	  "GnmHLinkEMail",	"email-box" ,
	  N_("Prepare an email") },
	{ N_("_URL"),		"Gnumeric_Link_URL",
	  "GnmHLinkURL",	"url-box" ,
	  N_("Browse to the specified URL") },
};

static void
dialog_hyperlink_setup_type (HyperlinkState *state)
{
	GtkWidget *w;
	char const *name = G_OBJECT_TYPE_NAME (state->link);
	unsigned i;

	for (i = 0 ; i < G_N_ELEMENTS (type); i++) {
		w = glade_xml_get_widget (state->gui, type[i].widget_name);

		if (!strcmp (name, type[i].name)) {
			gtk_widget_show_all (w);
			gtk_image_set_from_stock (state->type_image,
				type[i].image_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
			gtk_label_set_text (state->type_descriptor,
				_(type[i].descriptor));
		} else
			gtk_widget_hide (w);
	}
}

static void
dialog_hyperlink_set_type (HyperlinkState *state, GType type)
{
	GnmHLink *old = state->link;

	last_link_type = type;
	state->link = g_object_new (type, NULL);
	if (old != NULL) {
		gnm_hlink_set_target (state->link, gnm_hlink_get_target (old));
		gnm_hlink_set_tip (state->link, gnm_hlink_get_tip (old));
	}
	dialog_hyperlink_setup_type (state);
}

static void
cb_menu_activate (GObject *elem, HyperlinkState *state)
{
	gpointer tmp = g_object_get_data (elem, "type-index");
	dialog_hyperlink_set_type (state, g_type_from_name (
		type [GPOINTER_TO_INT (tmp)].name));
}

static gboolean
dialog_hyperlink_init (HyperlinkState *state)
{
	static char const * const label[] = {
		"internal-link-label",
		"external-link-label",
		"email-address-label",
		"email-subject-label",
		"url-label",
		"tip-label"
	};
	static char const * const entry[] = {
		"external-link",
		"email-address",
		"email-subject",
		"url",
		"tip-entry",
	};
	GtkWidget *w, *menu;
	GtkSizeGroup *size_group;
	GnumericExprEntry *expr_entry;
	unsigned i, select = 0;

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	for (i = 0 ; i < G_N_ELEMENTS (label); i++)
		gtk_size_group_add_widget (size_group,
			glade_xml_get_widget (state->gui, label[i]));

	w  = glade_xml_get_widget (state->gui, "link-type-image");
	state->type_image = GTK_IMAGE (w);
	w  = glade_xml_get_widget (state->gui, "link-type-descriptor");
	state->type_descriptor = GTK_LABEL (w);

	w = glade_xml_get_widget (state->gui, "internal-link-box");
	expr_entry = gnumeric_expr_entry_new (state->wbcg, TRUE);
	gtk_box_pack_end (GTK_BOX (w), GTK_WIDGET (expr_entry), TRUE, TRUE, 0);
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
		GTK_WIDGET (expr_entry));
	gnm_expr_entry_set_scg (expr_entry, wbcg_cur_scg (state->wbcg));

	for (i = 0 ; i < G_N_ELEMENTS (entry); i++)
		gnumeric_editable_enters (GTK_WINDOW (state->dialog),
			glade_xml_get_widget (state->gui, entry[i]));

	w = glade_xml_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_cancel), state);

	w  = glade_xml_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (w),
		"clicked",
		G_CALLBACK (cb_ok), state);
	gtk_window_set_default (GTK_WINDOW (state->dialog), w);

	gnumeric_init_help_button (
		glade_xml_get_widget (state->gui, "help_button"),
		"hyperlink.html");

	menu = gtk_menu_new ();
	for (i = 0 ; i < G_N_ELEMENTS (type); i++) {
		GtkWidget *elem = gtk_image_menu_item_new_with_mnemonic (_(type[i].label));
		GtkWidget *image = gtk_image_new_from_stock (type[i].image_name,
			GTK_ICON_SIZE_MENU);
		gtk_widget_show (image);
		gtk_image_menu_item_set_image (
			GTK_IMAGE_MENU_ITEM (elem),
			image);
		g_object_set_data (G_OBJECT (elem), "type-index", GINT_TO_POINTER(i));
		g_signal_connect (G_OBJECT (elem), "activate",
			G_CALLBACK (cb_menu_activate),
			state);
		gtk_menu_append (GTK_MENU (menu), elem);
		if (last_link_type == g_type_from_name (type [i].name))
			select = i;
	}
	gtk_menu_set_active (GTK_MENU (menu), select);
	gtk_widget_show_all (menu);
	w  = glade_xml_get_widget (state->gui, "link-type-menu");
	gtk_option_menu_set_menu (GTK_OPTION_MENU (w), menu);

	return FALSE;
}

#define GLADE_FILE "hyperlink.glade"
#define DIALOG_KEY "hyperlink-dialog"
void
dialog_hyperlink (WorkbookControlGUI *wbcg, SheetControl *sc)
{
	HyperlinkState* state;
	GnmHLink	*link = NULL;
	Sheet		*sheet;
	GList		*ptr;

	g_return_if_fail (wbcg != NULL);

	if (gnumeric_dialog_raise_if_exists (wbcg, DIALOG_KEY))
		return;

	/* make sure that all hlink types are registered */
	gnm_hlink_cur_wb_get_type ();
	gnm_hlink_url_get_type ();
	gnm_hlink_email_get_type ();
	gnm_hlink_external_get_type ();

	state = g_new (HyperlinkState, 1);
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));

	/* Get the dialog and check for errors */
	state->gui = gnumeric_glade_xml_new (wbcg, GLADE_FILE);
        if (state->gui == NULL) {
		g_warning ("glade file missing or corrupted");
		g_free (state);
                return;
	}

        state->dialog = glade_xml_get_widget (state->gui, "hyperlink-dialog");

	sheet = sc_sheet (sc);
	for (ptr = sc_view (sc)->selections; ptr != NULL; ptr = ptr->next)
		if (NULL != (link = sheet_style_region_contains_link (sheet, ptr->data)))
			break;
	
	state->link = NULL;
	if (link != NULL)
		last_link_type = G_OBJECT_TYPE (link);
	else if (last_link_type == 0)
		last_link_type = gnm_hlink_url_get_type ();

	if (dialog_hyperlink_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the hyperlink dialog."));
		g_free (state);
		return;
	}

	dialog_hyperlink_set_type (state, last_link_type);

	/* a candidate for merging into attach guru */
	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       DIALOG_KEY);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) dialog_hyperlink_free);
	gnumeric_non_modal_dialog (state->wbcg, GTK_WINDOW (state->dialog));
	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	gtk_widget_show (state->dialog);
}

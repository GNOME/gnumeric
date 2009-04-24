/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 * dialog-so-styled.c: Pref dialog for objects with a GOStyle 'style' property
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
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "dialogs.h"

#include "gui-gnumeric.h"
#include "gui-util.h"
#include "dialogs/help.h"
#include "wbc-gtk.h"
#include "commands.h"
#include "sheet-object.h"
#include <goffice/app/go-cmd-context.h>
#include <goffice/utils/go-style.h>
#include <gtk/gtk.h>

typedef struct {
	GObject			*so;
	WBCGtk	*wbcg;
	GOStyle		*orig_style;
	char    *orig_text;
	PangoAttrList *orig_attributes;
	GtkTextBuffer *buffer;
	
	GtkToggleToolButton *italic;
	GtkToggleToolButton *strikethrough;
	GtkToolButton *bold;
} DialogSOStyled;

#define GNM_SO_STYLED_KEY "gnm-so-styled-key"

static void
dialog_so_styled_free (DialogSOStyled *pref)
{
	if (pref->orig_style != NULL) {
		g_object_set (G_OBJECT (pref->so), "style", pref->orig_style, NULL);
		g_object_unref (pref->orig_style);
	}
	if (pref->orig_text) {
		g_object_set (G_OBJECT (pref->so), "text", pref->orig_text, NULL);
		g_free (pref->orig_text);
	}
	if (pref->orig_attributes != NULL) {
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
	}
	gtk_object_destroy (GTK_OBJECT (dialog));
}

static void
cb_dialog_so_styled_text_widget_changed (GtkTextBuffer *buffer, DialogSOStyled *state)
{
	gchar *text = gnumeric_textbuffer_get_text (buffer);
	PangoAttrList *attr = gnm_get_pango_attributes_from_buffer (buffer);

	g_object_set (state->so, "text", text, NULL);
	g_free (text);
	
	g_object_set (state->so, "markup", attr, NULL);
	pango_attr_list_unref (attr);
}

static void
gnm_toggle_tool_button_set_active_no_signal (GtkToggleToolButton *button,
					     gboolean is_active,
					     DialogSOStyled *state)
{
	gulong handler_id = g_signal_handler_find (button, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, state);

	g_signal_handler_block (button, handler_id);
	gtk_toggle_tool_button_set_active (button, is_active);
	g_signal_handler_unblock (button, handler_id);
}

static void
cb_dialog_so_styled_text_widget_mark_set (GtkTextBuffer *buffer,
					  G_GNUC_UNUSED GtkTextIter   *location,
					  G_GNUC_UNUSED GtkTextMark   *mark,
					  DialogSOStyled *state)
{
	GtkTextIter start, end;
	gtk_text_buffer_get_selection_bounds (state->buffer, &start, &end);
	
	{ /* Handling italic button */
		GtkTextTag *tag_italic = gtk_text_tag_table_lookup 
			(gtk_text_buffer_get_tag_table (state->buffer), "PANGO_STYLE_ITALIC");
		gnm_toggle_tool_button_set_active_no_signal
			(state->italic, 
			 (tag_italic != NULL) && gtk_text_iter_has_tag (&start, tag_italic), state);
	}
	{ /* Handling strikethrough button */
		GtkTextTag *tag_strikethrough = gtk_text_tag_table_lookup 
			(gtk_text_buffer_get_tag_table (state->buffer), "PANGO_STRIKETHROUGH_TRUE");
		gnm_toggle_tool_button_set_active_no_signal
			(state->strikethrough, 
			 (tag_strikethrough != NULL) && gtk_text_iter_has_tag (&start, tag_strikethrough), 
			 state);
	}
}

static void
cb_dialog_so_styled_text_widget_set_italic (GtkToggleToolButton *toolbutton, DialogSOStyled *state)
{
	GtkTextIter start, end;

	if (gtk_text_buffer_get_selection_bounds (state->buffer, &start, &end)) {
		GtkTextTag *tag_italic = gtk_text_tag_table_lookup 
			(gtk_text_buffer_get_tag_table (state->buffer), "PANGO_STYLE_ITALIC");
		GtkTextTag *tag_normal = gtk_text_tag_table_lookup 
			(gtk_text_buffer_get_tag_table (state->buffer), "PANGO_STYLE_NORMAL");
		if (tag_italic == NULL)
			tag_italic = gtk_text_buffer_create_tag (state->buffer, "PANGO_STYLE_ITALIC", 
								 "style", PANGO_STYLE_ITALIC, 
								 "style-set", TRUE, NULL);
		if (tag_normal == NULL)
			tag_normal = gtk_text_buffer_create_tag (state->buffer, "PANGO_STYLE_NORMAL", 
								 "style", PANGO_STYLE_NORMAL, 
								 "style-set", TRUE, NULL);
		
		if (gtk_text_iter_has_tag (&start, tag_italic)) {
			gtk_text_buffer_remove_tag (state->buffer, tag_italic,
						    &start, &end);
			gtk_text_buffer_apply_tag (state->buffer, tag_normal, &start, &end);
		} else {
			gtk_text_buffer_remove_tag (state->buffer, tag_normal,
						    &start, &end);
			gtk_text_buffer_apply_tag (state->buffer, tag_italic, &start, &end);
		}
		cb_dialog_so_styled_text_widget_changed (state->buffer, state);
	}
}

static void
cb_dialog_so_styled_text_widget_set_strikethrough (GtkToggleToolButton *toolbutton, DialogSOStyled *state)
{
	GtkTextIter start, end;

	if (gtk_text_buffer_get_selection_bounds (state->buffer, &start, &end)) {
		GtkTextTag *tag_no_strikethrough = gtk_text_tag_table_lookup 
			(gtk_text_buffer_get_tag_table (state->buffer), "PANGO_STRIKETHROUGH_FALSE");
		GtkTextTag *tag_strikethrough = gtk_text_tag_table_lookup 
			(gtk_text_buffer_get_tag_table (state->buffer), "PANGO_STRIKETHROUGH_TRUE");
		if (tag_no_strikethrough == NULL)
			tag_no_strikethrough = gtk_text_buffer_create_tag (state->buffer, "PANGO_STRIKETHROUGH_FALSE", 
								 "strikethrough", FALSE, 
								 "strikethrough-set", TRUE, NULL);
		if (tag_strikethrough == NULL)
			tag_strikethrough = gtk_text_buffer_create_tag (state->buffer, "PANGO_STRIKETHROUGH_TRUE", 
								 "strikethrough", TRUE, 
								 "strikethrough-set", TRUE, NULL);
		
		if (gtk_text_iter_has_tag (&start, tag_strikethrough)) {
			gtk_text_buffer_remove_tag (state->buffer, tag_strikethrough,
						    &start, &end);
			gtk_text_buffer_apply_tag (state->buffer, tag_no_strikethrough, &start, &end);
		} else {
			gtk_text_buffer_remove_tag (state->buffer, tag_no_strikethrough,
						    &start, &end);
			gtk_text_buffer_apply_tag (state->buffer, tag_strikethrough, &start, &end);
		}
		cb_dialog_so_styled_text_widget_changed (state->buffer, state);
	}
}

static GtkToggleToolButton *
dialog_so_styled_build_toggle_button (GtkWidget *tb, DialogSOStyled *state, char const *button_name, GCallback cb)
{
	GtkToolItem * tb_button;

	tb_button = gtk_toggle_tool_button_new_from_stock (button_name);
	gtk_toolbar_insert(GTK_TOOLBAR(tb), tb_button, -1);
	g_signal_connect (G_OBJECT (tb_button), "toggled", cb, state);
	return GTK_TOGGLE_TOOL_BUTTON (tb_button);
}

static void
dialog_so_styled_bold_button_activated (GtkMenuItem *menuitem, DialogSOStyled *state)
{
	gpointer val = g_object_get_data (G_OBJECT (menuitem), "boldvalue");
	if (val != NULL) {
		GtkTextIter start, end;
		if (gtk_text_buffer_get_selection_bounds (state->buffer, &start, &end)) {
			GtkTextTag *tag = gtk_text_buffer_create_tag (state->buffer, 
								      NULL, NULL);
			g_object_set (G_OBJECT (tag), "weight", GPOINTER_TO_INT (val),
				      "weight-set", TRUE, NULL);
			gtk_text_buffer_apply_tag (state->buffer, tag, &start, &end);
			cb_dialog_so_styled_text_widget_changed (state->buffer, state);
		}
		g_object_set_data (G_OBJECT (state->bold), "boldvalue", val);
	}
}

#define SETUPBPLDMENUITEM(string, value)                                       \
	child = gtk_menu_item_new_with_label (string);                         \
        gtk_widget_show (child);					       \
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), child);                  \
	g_signal_connect (G_OBJECT (child), "activate",                        \
			  G_CALLBACK (dialog_so_styled_bold_button_activated), \
                          state);                                              \
	g_object_set_data (G_OBJECT (child), "boldvalue",                      \
                           GINT_TO_POINTER (value)); 

static GtkToolButton *
dialog_so_styled_build_button_bold (GtkWidget *tb, DialogSOStyled *state)
{
	GtkToolItem * tb_button;
	GtkWidget *menu;
	GtkWidget *child;

	menu = gtk_menu_new ();
	
	SETUPBPLDMENUITEM(_("Thin"), PANGO_WEIGHT_THIN)
	SETUPBPLDMENUITEM(_("Ultralight"), PANGO_WEIGHT_ULTRALIGHT)
	SETUPBPLDMENUITEM(_("Light"), PANGO_WEIGHT_LIGHT)
	SETUPBPLDMENUITEM(_("Normal"), PANGO_WEIGHT_NORMAL)
	SETUPBPLDMENUITEM(_("Medium"), PANGO_WEIGHT_MEDIUM)
	SETUPBPLDMENUITEM(_("Semibold"), PANGO_WEIGHT_SEMIBOLD)
	SETUPBPLDMENUITEM(_("Bold"), PANGO_WEIGHT_BOLD)
	SETUPBPLDMENUITEM(_("Ultrabold"), PANGO_WEIGHT_ULTRABOLD)
	SETUPBPLDMENUITEM(_("Heavy"), PANGO_WEIGHT_HEAVY)
	SETUPBPLDMENUITEM(_("Ultraheavy"), PANGO_WEIGHT_ULTRAHEAVY)

	tb_button = gtk_menu_tool_button_new_from_stock (GTK_STOCK_BOLD);
	gtk_toolbar_insert(GTK_TOOLBAR(tb), tb_button, -1);
	gtk_menu_tool_button_set_menu (GTK_MENU_TOOL_BUTTON (tb_button), menu);
	g_object_set_data (G_OBJECT (tb_button), "boldvalue",
                           GINT_TO_POINTER (PANGO_WEIGHT_BOLD));
	g_signal_connect (G_OBJECT (tb_button), "clicked",                        \
			  G_CALLBACK (dialog_so_styled_bold_button_activated), \
                          state);
	return tb_button;
}

#undef SETUPBPLDMENUITEM

static GtkWidget *
dialog_so_styled_text_widget (DialogSOStyled *state)
{
	GtkWidget *vb = gtk_vbox_new (FALSE, 0);
	GtkWidget *tb = gtk_toolbar_new ();
	GtkWidget *tv = gtk_text_view_new ();
	char *strval;
	PangoAttrList  *markup;

	state->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));

	state->italic = dialog_so_styled_build_toggle_button 
		(tb, state, GTK_STOCK_ITALIC, 
		 G_CALLBACK (cb_dialog_so_styled_text_widget_set_italic));
	state->strikethrough = dialog_so_styled_build_toggle_button 
		(tb, state, GTK_STOCK_STRIKETHROUGH, 
		 G_CALLBACK (cb_dialog_so_styled_text_widget_set_strikethrough));
	gtk_toolbar_insert(GTK_TOOLBAR(tb), gtk_separator_tool_item_new (), -1);
	state->bold = dialog_so_styled_build_button_bold (tb, state);

	gtk_container_set_border_width (GTK_CONTAINER (tv), 5);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD_CHAR);

	g_object_get (state->so, "text", &strval, NULL);
	state->orig_text = g_strdup (strval);
	gtk_text_buffer_set_text (state->buffer, strval, -1);
	g_free (strval);

	g_object_get (state->so, "markup", &markup, NULL);
	state->orig_attributes = markup;
	pango_attr_list_ref (state->orig_attributes);
	gnm_load_pango_attributes_into_buffer (markup, state->buffer);

	g_signal_connect (G_OBJECT (state->buffer), "changed",
			  G_CALLBACK (cb_dialog_so_styled_text_widget_changed), state);
	g_signal_connect (G_OBJECT (state->buffer), "mark_set",
			  G_CALLBACK (cb_dialog_so_styled_text_widget_mark_set), state);

	gtk_box_pack_start (GTK_BOX (vb), tb, FALSE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vb), tv, TRUE, TRUE, 0);
	return vb;
}

void
dialog_so_styled (WBCGtk *wbcg,
		  GObject *so, GOStyle *orig, GOStyle *default_style,
		  char const *title, so_styled_t extent)
{
	DialogSOStyled *state;
	GtkWidget	*dialog, *help, *editor;

	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, GNM_SO_STYLED_KEY))
		return;

	state = g_new0 (DialogSOStyled, 1);
	state->so    = G_OBJECT (so);
	state->wbcg  = wbcg;
	state->orig_style = go_style_dup (orig);
	state->orig_text = NULL;
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

	editor = go_style_get_editor (orig, default_style,
				      GO_CMD_CONTEXT (wbcg), G_OBJECT (so));

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
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
			gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
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

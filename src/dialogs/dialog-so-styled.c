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
#include <gnumeric.h>
#include <dialogs/dialogs.h>

#include <gui-util.h>
#include <dialogs/help.h>
#include <wbc-gtk.h>
#include <commands.h>
#include <sheet-object.h>
#include <gnm-so-line.h>
#include <goffice/goffice.h>
#include <widgets/gnm-text-view.h>

typedef struct {
	GObject *so;
	WBCGtk *wbcg;
	GSList *orig_props;

	so_styled_t extent;
} DialogSOStyled;

#define GNM_SO_STYLED_KEY "gnm-so-styled-key"

static void
dialog_so_styled_free (DialogSOStyled *state)
{
	go_object_properties_apply (state->so, state->orig_props, TRUE);
	go_object_properties_free (state->orig_props);
	g_free (state);
}

static void
force_new_style (GObject *so)
{
	GOStyle *style;

	/* Ensure we have a new style object.  */
	g_object_get (so, "style", &style, NULL);
	g_object_set (so, "style", style, NULL);
	g_object_unref (style);
}

static void
cb_set_props (gpointer a, gpointer b, gpointer data)
{
	go_object_properties_apply (a, b, TRUE);
}

static GOUndo *
make_undo (GObject *so, GSList *props)
{
	return go_undo_binary_new (g_object_ref (so), props,
				   cb_set_props,
				   g_object_unref,
				   (GFreeFunc)go_object_properties_free);
}

static void
cb_dialog_so_styled_response (GtkWidget *dialog,
			      gint response_id, DialogSOStyled *state)
{
	if (response_id == GTK_RESPONSE_HELP)
		return;
	if (response_id == GTK_RESPONSE_OK) {
		GSList *new_props = go_object_properties_collect (state->so);
		force_new_style (state->so);
		cmd_generic (GNM_WBC (state->wbcg),
			     _("Format Object"),
			     make_undo (state->so, state->orig_props),
			     make_undo (state->so, new_props));
		state->orig_props = NULL;
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
	g_free (strval);

	g_object_get (state->so, "markup", &markup, NULL);
	g_object_set (gtv, "attributes", markup, NULL);
	/* unref? */

	g_signal_connect (G_OBJECT (gtv), "changed",
			  G_CALLBACK (cb_dialog_so_styled_text_widget_changed), state);

	return GTK_WIDGET (gtv);
}

static void
cb_arrow_changed (GOArrowSel *as,
		  G_GNUC_UNUSED GParamSpec *pspec,
		  DialogSOStyled *state)
{
	const char *prop = g_object_get_data (G_OBJECT (as), "prop");
	g_object_set (state->so,
		      prop, go_arrow_sel_get_arrow (as),
		      NULL);
}

static GtkWidget *
dialog_so_styled_line_widget (DialogSOStyled *state, const char *prop)
{
	GtkWidget *w = go_arrow_sel_new ();
	GOArrow *arrow;

	g_object_get (state->so, prop, &arrow, NULL);
	go_arrow_sel_set_arrow (GO_ARROW_SEL (w), arrow);
	g_free (arrow);

	g_object_set_data_full (G_OBJECT (w), "prop", g_strdup (prop), g_free);

	g_signal_connect (G_OBJECT (w),
			  "notify::arrow",
			  G_CALLBACK (cb_arrow_changed),
			  state);

	return w;
}

void
dialog_so_styled (WBCGtk *wbcg, GObject *so, GOStyle *default_style,
		  char const *title, so_styled_t extent)
{
	DialogSOStyled *state;
	GtkWidget	*dialog, *help, *editor;
	GOStyle *style;

	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, GNM_SO_STYLED_KEY)) {
		g_object_unref (default_style);
		return;
	}

	state = g_new0 (DialogSOStyled, 1);
	state->so    = G_OBJECT (so);
	state->wbcg  = wbcg;
	state->orig_props = go_object_properties_collect (so);
	force_new_style (state->so);

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
	gnm_init_help_button (help, "sect-graphics-drawings");

	gtk_dialog_add_buttons (GTK_DIALOG (dialog),
		GNM_STOCK_CANCEL,	GTK_RESPONSE_CANCEL,
		GNM_STOCK_OK,		GTK_RESPONSE_OK,
		NULL);

	g_object_get (so, "style", &style, NULL);
	editor = go_style_get_editor (style, default_style,
				      GO_CMD_CONTEXT (wbcg), G_OBJECT (so));
	g_object_unref (style);

	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
		editor, TRUE, TRUE, TRUE);
	g_object_unref (default_style);

	if (extent & SO_STYLED_TEXT) {
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

	if (extent & SO_STYLED_LINE) {
		GtkWidget *w = dialog_so_styled_line_widget (state, "end-arrow");
		gtk_widget_show_all (w);
		if (GTK_IS_NOTEBOOK (editor))
			gtk_notebook_append_page (GTK_NOTEBOOK (editor), w,
						  gtk_label_new (_("Head")));
		else
			gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
					    w, TRUE, TRUE, TRUE);
	}

	if (extent & SO_STYLED_LINE) {
		GtkWidget *w = dialog_so_styled_line_widget (state, "start-arrow");
		gtk_widget_show_all (w);
		if (GTK_IS_NOTEBOOK (editor))
			gtk_notebook_append_page (GTK_NOTEBOOK (editor), w,
						  gtk_label_new (_("Tail")));
		else
			gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
					    w, TRUE, TRUE, TRUE);
	}

	g_signal_connect (G_OBJECT (dialog), "response",
		G_CALLBACK (cb_dialog_so_styled_response), state);
	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (dialog),
		GNM_SO_STYLED_KEY);
	g_object_set_data_full (G_OBJECT (dialog),
		"state", state, (GDestroyNotify) dialog_so_styled_free);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
		GTK_WINDOW (dialog));
	wbc_gtk_attach_guru (state->wbcg, dialog);
	gtk_widget_show (dialog);
}

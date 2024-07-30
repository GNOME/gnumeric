/*
 * dialog-hyperlink.c: Add or edit a hyperlink
 *
 * Copyright (C) 2002 Jody Goldberg (jody@gnome.org)
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
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <commands.h>
#include <widgets/gnm-expr-entry.h>
#include <expr-name.h>
#include <expr.h>
#include <gui-util.h>
#include <hlink.h>
#include <mstyle.h>
#include <style-color.h>
#include <sheet-control.h>
#include <sheet-view.h>
#include <sheet-style.h>
#include <value.h>
#include <wbc-gtk.h>
#include <goffice/goffice.h>

#include <glib/gi18n-lib.h>

#include <string.h>


typedef struct {
	WBCGtk  *wbcg;
	Workbook  *wb;
	SheetControl *sc;
	Sheet *sheet;

	GtkBuilder *gui;
	GtkWidget *dialog;

	GtkImage  *type_image;
	GtkLabel  *type_descriptor;
	GnmExprEntry *internal_link_ee;
	GnmHLink  *link;
	gboolean   is_new;

	GtkWidget *use_def_widget;
} HyperlinkState;

static void
dhl_free (HyperlinkState *state)
{
	if (state->gui != NULL) {
		g_object_unref (state->gui);
		state->gui = NULL;
	}
	if (state->link != NULL) {
		g_object_unref (state->link);
		state->link = NULL;
	}
	state->dialog = NULL;
	g_free (state);
}

static char *
dhl_get_default_tip (char const * const target) {
	char *default_text = _("Left click once to follow this link.\n"
			       "Middle click once to select this cell");
	if (target == NULL)
		return g_strdup (default_text);
	else
		return g_strjoin ("\n", target, default_text, NULL);
}

static void
dhl_set_tip (HyperlinkState* state)
{
	char const *tip = gnm_hlink_get_tip (state->link);
	GtkTextBuffer *tb;
	GtkWidget *w;

	if (state->is_new) {
			w = go_gtk_builder_get_widget (state->gui, "use-default-tip");
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
			return;
	}

	if (tip != NULL) {
		char const * const target = gnm_hlink_get_target (state->link);
		char *default_tip = dhl_get_default_tip (target);
		gboolean is_default = (strcmp (tip, default_tip) == 0);
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->use_def_widget),
					      is_default);
		g_free (default_tip);
		if (is_default)
			return;
	}
	w = go_gtk_builder_get_widget (state->gui, "use-this-tip");
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);

	tb = gtk_text_view_get_buffer
		(GTK_TEXT_VIEW (go_gtk_builder_get_widget (state->gui, "tip-entry")));

	gtk_text_buffer_set_text (tb, (tip == NULL) ? "" : tip, -1);
}

static char *
dhl_get_tip (HyperlinkState *state, char const *target)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (state->use_def_widget)))
		return NULL;
	else {
		char *tip;
		GtkTextBuffer *tb = gtk_text_view_get_buffer
			(GTK_TEXT_VIEW (go_gtk_builder_get_widget (state->gui, "tip-entry")));
		GtkTextIter start_iter, end_iter;

		gtk_text_buffer_get_start_iter (tb, &start_iter);
		gtk_text_buffer_get_end_iter (tb, &end_iter);

		tip  = gtk_text_buffer_get_text (tb, &start_iter, &end_iter, FALSE);

		if (tip != NULL && *tip == 0) {
			g_free (tip);
			tip = NULL;
		}

		return tip;
	}
}

static void
dhl_set_target_cur_wb (HyperlinkState *state, const char* const target)
{
	gnm_expr_entry_load_from_text (state->internal_link_ee, target);
}

static char *
dhl_get_target_cur_wb (HyperlinkState *state, gboolean *success)
{
	char *ret = NULL;
	GnmExprEntry *gee = state->internal_link_ee;
	char const *target = gnm_expr_entry_get_text (gee);
	Sheet *sheet = sc_sheet (state->sc);
	GnmValue *val;

	*success = FALSE;
	if (*target == 0) {
		*success = TRUE;
	} else {
		val = gnm_expr_entry_parse_as_value (gee, sheet);
		if (!val) {
			/* not an address, is it a name ? */
			GnmParsePos pp;
			GnmNamedExpr *nexpr;

			parse_pos_init_sheet (&pp, sheet);
			nexpr = expr_name_lookup (&pp, target);
			if (nexpr != NULL)
				val = gnm_expr_top_get_range (nexpr->texpr);
		}
		if (val) {
			*success = TRUE;
			ret = g_strdup (target);
			value_release (val);
		} else {
			go_gtk_notice_dialog (GTK_WINDOW (state->dialog),
					 GTK_MESSAGE_ERROR,
					 _("Not a range or name"));
			gnm_expr_entry_grab_focus (gee, TRUE);
		}
	}
	return ret;
}

static void
dhl_set_target_external (HyperlinkState *state, const char* const target)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, "external-link");

	gtk_entry_set_text (GTK_ENTRY (w), target);
}

static char *
dhl_get_target_external (HyperlinkState *state, gboolean *success)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, "external-link");
	const char *target = gtk_entry_get_text (GTK_ENTRY (w));

	*success = TRUE;
	return target[0] ? g_strdup (target) : NULL;
}

static void
dhl_set_target_email (HyperlinkState *state, const char* const target)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, "email-address");
	GtkWidget *w2 = go_gtk_builder_get_widget (state->gui, "email-subject");
	gchar* cursor;
	gchar* subject;
	gchar* guitext;

	if (!target || *target == '\0')
		return;

	if (g_str_has_prefix (target, "mailto:"))
		return;

	cursor = g_strdup (target + strlen ("mailto:"));

	subject = strstr (cursor, "?subject=");
	if (subject) {
		guitext = g_uri_unescape_string (subject + strlen ("?subject="),
						 NULL);
		gtk_entry_set_text (GTK_ENTRY (w2), guitext);
		*subject = '\0';
		g_free (guitext);
	}

	guitext = g_uri_unescape_string (cursor, NULL);

	gtk_entry_set_text (GTK_ENTRY (w), guitext);

	g_free (guitext);
	g_free (cursor);
}

static char*
dhl_get_target_email (HyperlinkState *state, gboolean *success)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, "email-address");
	GtkWidget *w2 = go_gtk_builder_get_widget (state->gui, "email-subject");
	const char *address = gtk_entry_get_text (GTK_ENTRY (w));
	const char *subject = gtk_entry_get_text (GTK_ENTRY (w2));
	gchar* enc_subj, *enc_addr;
	gchar* result;

	*success = TRUE;
	if (!address || *address == '\0') {
		return NULL;
	}

	enc_addr = go_url_encode (address, 0);
	if (!subject || *subject == '\0') {
		result =  g_strconcat ("mailto:", enc_addr, NULL);
	} else {
		enc_subj = go_url_encode (subject, 0);

		result = g_strconcat ("mailto:", enc_addr,
				      "?subject=", enc_subj, NULL);
		g_free (enc_subj);
	}

	g_free (enc_addr);

	return result;
}

static void
dhl_set_target_url (HyperlinkState *state, const char* const target)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, "url");

	gtk_entry_set_text (GTK_ENTRY (w), target);
}

static char *
dhl_get_target_url (HyperlinkState *state, gboolean *success)
{
	GtkWidget *w = go_gtk_builder_get_widget (state->gui, "url");
	const char *target = gtk_entry_get_text (GTK_ENTRY (w));

	*success = TRUE;
	return target[0] ? g_strdup (target) : NULL;
}

static struct {
	char const *label;
	char const *icon_name;
	char const *name;
	char const *widget_name;
	char const *descriptor;
	void (*set_target) (HyperlinkState *state, const char* const target);
	char * (*get_target) (HyperlinkState *state, gboolean *success);
} const type[] = {
	{ N_("Internal Link"), "gnumeric-link-internal",
	  "GnmHLinkCurWB",	"internal-link-grid",
	  N_("Jump to specific cells or named range in the current workbook"),
	  dhl_set_target_cur_wb,
	  dhl_get_target_cur_wb },

	{ N_("External Link"), "gnumeric-link-external",
	  "GnmHLinkExternal",	"external-link-grid" ,
	  N_("Open an external file with the specified name"),
	  dhl_set_target_external,
	  dhl_get_target_external },
	{ N_("Email Link"),	"gnumeric-link-email",
	  "GnmHLinkEMail",	"email-grid" ,
	  N_("Prepare an email"),
	  dhl_set_target_email,
	  dhl_get_target_email },
	{ N_("Web Link"),		"gnumeric-link-url",
	  "GnmHLinkURL",	"url-grid" ,
	  N_("Browse to the specified URL"),
	  dhl_set_target_url,
	  dhl_get_target_url }
};

static void
dhl_set_target (HyperlinkState* state)
{
	unsigned i;
	char const * const target = gnm_hlink_get_target (state->link);
	char const * type_name;

	if (target) {
		type_name = G_OBJECT_TYPE_NAME (state->link);
		for (i = 0 ; i < G_N_ELEMENTS (type); i++) {
			if (strcmp (type_name, type[i].name) == 0) {
				if (type[i].set_target)
					(type[i].set_target) (state, target);
				break;
			}
		}
	}
}

static char *
dhl_get_target (HyperlinkState *state, gboolean *success)
{
	unsigned i;
	char const *type_name = G_OBJECT_TYPE_NAME (state->link);

	*success = FALSE;
	for (i = 0 ; i < G_N_ELEMENTS (type); i++) {
		if (strcmp (type_name, type[i].name) == 0) {
			if (type[i].get_target)
				return (type[i].get_target) (state, success);
			break;
		}
	}

	return NULL;
}


static void
dhl_cb_cancel (G_GNUC_UNUSED GtkWidget *button, HyperlinkState *state)
{
	gtk_widget_destroy (state->dialog);
}

static void
dhl_cb_ok (G_GNUC_UNUSED GtkWidget *button, HyperlinkState *state)
{
	GnmStyle *style;
	char *cmdname;
	char *target;
	char *tip;
	gboolean success;

	target = dhl_get_target (state, &success);
	if (!success)
		return;		/* Let user continue editing */

	wb_control_sheet_focus (GNM_WBC (state->wbcg), state->sheet);

	if (target) {
		GnmHLink *new_link = gnm_hlink_dup_to (state->link, state->sheet);
		gnm_hlink_set_target (new_link, target);
		tip = dhl_get_tip (state, target);
		gnm_hlink_set_tip (new_link, tip);
		g_free (tip);
		style = gnm_style_new ();
		gnm_style_set_hlink (style, new_link);
		gnm_style_set_font_uline (style, UNDERLINE_SINGLE);
		gnm_style_set_font_color (style, gnm_color_new_go (GO_COLOR_BLUE));

		if (state->is_new) {
			cmdname = _("Add Hyperlink");
			cmd_selection_hyperlink (GNM_WBC (state->wbcg),
						 style,
						 cmdname, target);
		} else {
			cmdname = _("Edit Hyperlink");
			cmd_selection_hyperlink (GNM_WBC (state->wbcg),
						 style,
						 cmdname, NULL);
			g_free (target);
		}
	} else if (!state->is_new) {
		style = gnm_style_new ();
		gnm_style_set_hlink (style, NULL);
		cmdname = _("Remove Hyperlink");
		cmd_selection_hyperlink (GNM_WBC (state->wbcg), style,
					 cmdname, NULL);
	}
	gtk_widget_destroy (state->dialog);
}

static void
dhl_setup_type (HyperlinkState *state)
{
	GtkWidget *w;
	char const *name = G_OBJECT_TYPE_NAME (state->link);
	unsigned i;

	for (i = 0 ; i < G_N_ELEMENTS (type); i++) {
		w = go_gtk_builder_get_widget (state->gui, type[i].widget_name);

		if (!strcmp (name, type[i].name)) {
			gtk_widget_show_all (w);
			gtk_image_set_from_icon_name
				(state->type_image, type[i].icon_name,
				 GTK_ICON_SIZE_DIALOG);
			gtk_label_set_text (state->type_descriptor,
					    _(type[i].descriptor));
		} else
			gtk_widget_hide (w);
	}
}

static void
dhl_set_type (HyperlinkState *state, GType type)
{
	GnmHLink *old = state->link;

	state->link = gnm_hlink_new (type, state->sheet);
	if (old != NULL) {
		gnm_hlink_set_target (state->link, gnm_hlink_get_target (old));
		gnm_hlink_set_tip (state->link, gnm_hlink_get_tip (old));
		g_object_unref (old);
	}
	dhl_setup_type (state);
}

static void
dhl_cb_menu_changed (GtkComboBox *box, HyperlinkState *state)
{
	int i = gtk_combo_box_get_active (box);
	dhl_set_type (state, g_type_from_name (
		type [i].name));
}

static gboolean
dhl_init (HyperlinkState *state)
{
	static char const * const label[] = {
		"internal-link-label",
		"external-link-label",
		"email-address-label",
		"email-subject-label",
		"url-label",
		"use-this-tip"
	};
	GtkWidget *w;
	GtkSizeGroup *size_group;
	GnmExprEntry *expr_entry;
	unsigned i, select = 0;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkCellRenderer *renderer;

#ifdef GNM_NO_MAILTO
	gtk_widget_hide (go_gtk_builder_get_widget (state->gui, "email-grid"));
#endif
	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	for (i = 0 ; i < G_N_ELEMENTS (label); i++)
		gtk_size_group_add_widget (size_group,
					   go_gtk_builder_get_widget (state->gui, label[i]));
	g_object_unref (size_group);

	w = go_gtk_builder_get_widget (state->gui, "link-type-image");
	state->type_image = GTK_IMAGE (w);
	w = go_gtk_builder_get_widget (state->gui, "link-type-descriptor");
	state->type_descriptor = GTK_LABEL (w);

	w = go_gtk_builder_get_widget (state->gui, "internal-link-grid");
	expr_entry = gnm_expr_entry_new (state->wbcg, TRUE);
	gtk_widget_set_hexpand (GTK_WIDGET (expr_entry), TRUE);
	gtk_container_add (GTK_CONTAINER (w), GTK_WIDGET (expr_entry));
	gtk_entry_set_activates_default
		(gnm_expr_entry_get_entry (expr_entry), TRUE);
	state->internal_link_ee = expr_entry;

	w = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (w),
			  "clicked",
			  G_CALLBACK (dhl_cb_cancel), state);

	w  = go_gtk_builder_get_widget (state->gui, "ok_button");
	g_signal_connect (G_OBJECT (w),
			  "clicked",
			  G_CALLBACK (dhl_cb_ok), state);
	gtk_window_set_default (GTK_WINDOW (state->dialog), w);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_HYPERLINK);

	store = gtk_list_store_new (2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	w = go_gtk_builder_get_widget (state->gui, "link-type-menu");
	gtk_combo_box_set_model (GTK_COMBO_BOX (w), GTK_TREE_MODEL (store));
	g_object_unref (store);

	for (i = 0 ; i < G_N_ELEMENTS (type); i++) {
		GdkPixbuf *pixbuf = go_gtk_widget_render_icon_pixbuf
			(GTK_WIDGET (wbcg_toplevel (state->wbcg)),
			 type[i].icon_name, GTK_ICON_SIZE_MENU);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
				    0, pixbuf,
				    1, _(type[i].label),
				    -1);
		g_object_unref (pixbuf);

		if (strcmp (G_OBJECT_TYPE_NAME (state->link),
			    type [i].name) == 0)
			select = i;
	}

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w),
				    renderer,
				    FALSE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer,
					"pixbuf", 0,
					NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w),
				    renderer,
				    TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer,
					"text", 1,
					NULL);
	gtk_combo_box_set_active (GTK_COMBO_BOX (w), select);

	g_signal_connect (G_OBJECT (w), "changed",
			  G_CALLBACK (dhl_cb_menu_changed),
			  state);

	gnm_link_button_and_entry (go_gtk_builder_get_widget (state->gui, "use-this-tip"),
				   go_gtk_builder_get_widget (state->gui, "tip-entry"));

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	return FALSE;
}

#define DIALOG_KEY "hyperlink-dialog"
void
dialog_hyperlink (WBCGtk *wbcg, SheetControl *sc)
{
	GtkBuilder *gui;
	HyperlinkState* state;
	GnmHLink	*link = NULL;
	GSList		*ptr;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, DIALOG_KEY))
		return;

	gui = gnm_gtk_builder_load ("res:ui/hyperlink.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
                return;

	state = g_new (HyperlinkState, 1);
	state->wbcg  = wbcg;
	state->wb   = wb_control_get_workbook (GNM_WBC (wbcg));
	state->sc   = sc;
	state->gui  = gui;
        state->dialog = go_gtk_builder_get_widget (state->gui, "hyperlink-dialog");

	state->use_def_widget = go_gtk_builder_get_widget (state->gui, "use-default-tip");

	state->sheet = sc_sheet (sc);
	for (ptr = sc_view (sc)->selections; ptr != NULL; ptr = ptr->next) {
		GnmRange const *r = ptr->data;
		link = sheet_style_region_contains_link (state->sheet, r);
		if (link)
			break;
	}

	/* We are creating a new link since the existing link */
	/* may be used in many places. */
	/* We are duplicating it here rather than in an ok handler in case */
	/* The link is changed for a differnet cell in a different view. */
	if (link == NULL) {
		state->link = gnm_hlink_new (gnm_hlink_url_get_type (), state->sheet);
		state->is_new = TRUE;
	} else {
		state->link = gnm_hlink_new (G_OBJECT_TYPE (link), state->sheet);
		state->is_new = FALSE;
		gnm_hlink_set_target (state->link, gnm_hlink_get_target (link));
		gnm_hlink_set_tip (state->link, gnm_hlink_get_tip (link));
	}

	if (dhl_init (state)) {
		go_gtk_notice_dialog (wbcg_toplevel (wbcg), GTK_MESSAGE_ERROR,
				 _("Could not create the hyperlink dialog."));
		g_free (state);
		return;
	}

	dhl_setup_type (state);

	dhl_set_target (state);
	dhl_set_tip (state);

	/* a candidate for merging into attach guru */
	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       DIALOG_KEY);
	go_gtk_nonmodal_dialog (wbcg_toplevel (state->wbcg),
				   GTK_WINDOW (state->dialog));

	wbc_gtk_attach_guru (state->wbcg, state->dialog);
	g_object_set_data_full (G_OBJECT (state->dialog),
		"state", state, (GDestroyNotify) dhl_free);
	gtk_widget_show (state->dialog);
}

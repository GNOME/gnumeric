/*
 * dialog-function-select.c:  Implements the function selector
 *
 * Authors:
 *  Michael Meeks <michael@ximian.com>
 *  Andreas J. Guelzow <aguelzow@pyrshep.ca>
 *
 * Copyright (C) 2003-2010 Andreas J. Guelzow <aguelzow@pyrshep.ca>
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
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <dialogs/dialogs.h>
#include <dialogs/help.h>

#include <gui-util.h>
#include <gutils.h>
#include <func.h>
#include <workbook.h>
#include <wbc-gtk.h>
#include <application.h>
#include <position.h>
#include <expr.h>
#include <value.h>
#include <sheet.h>
#include <gnumeric-conf.h>
#include <gnm-format.h>
#include <auto-format.h>

#include <gsf/gsf-impl-utils.h>
#include <string.h>

#define FUNCTION_SELECT_KEY "function-selector-dialog"
#define FUNCTION_SELECT_HELP_KEY "function-selector-dialog-help-mode"
#define FUNCTION_SELECT_PASTE_KEY "function-selector-dialog-paste-mode"
#define FUNCTION_SELECT_DIALOG_KEY "function-selector-dialog"

#define UNICODE_ELLIPSIS "\xe2\x80\xa6"

typedef enum {
	GURU_MODE = 0,
	HELP_MODE,
	PASTE_MODE
} DialogMode;

typedef struct {
	WBCGtk  *wbcg;
	Workbook *wb;
	Sheet *sheet;

	gboolean localized_function_names;

	GtkBuilder  *gui;
	GtkWidget *dialog;
	GtkWidget *ok_button;
	GtkWidget *paste_button;
	GtkListStore  *model;
	GtkComboBox   *cb;
	GtkListStore  *model_functions;
	GtkTreeModel  *model_filter;
	GtkTreeView   *treeview;
	GtkTextView   *description_view;
	GtkWidget     *search_entry;

	GSList *recent_funcs;

	struct {
		gint from;
		gint to;
		char *prefix;
	} paste;

	DialogMode      mode;
	char const *formula_guru_key;
} FunctionSelectState;

enum {
	CAT_NAME,
	CATEGORY,
	CAT_SEPARATOR,
	NUM_CAT_COLUMNS
};
enum {
	FUN_NAME,
	FUNCTION,
	FUNCTION_DESC,
	FUNCTION_PAL,
	FUNCTION_CAT,
	FUNCTION_VISIBLE,
	FUNCTION_RECENT,
	FUNCTION_USED,
	NUM_COLUMNS
};

/*************************************************************************/
/* Search Functions  */
/*************************************************************************/

typedef struct {
	char const *text;
	gboolean recent_only;
	gboolean used_only;
	GnmFuncGroup const * cat;
} search_t;

static gboolean
cb_dialog_function_select_search_all (GtkTreeModel *model,
				      G_GNUC_UNUSED GtkTreePath *path,
				      GtkTreeIter *iter, gpointer data)
{
	search_t *specs = data;
	gchar *name;
	gchar *desc;
	gboolean visible, was_visible, recent, used;
	GnmFuncGroup const * cat;

	gtk_tree_model_get (model, iter,
			    FUN_NAME, &name,
			    FUNCTION_DESC, &desc,
			    FUNCTION_VISIBLE, &was_visible,
			    FUNCTION_RECENT, &recent,
			    FUNCTION_USED, &used,
			    FUNCTION_CAT, &cat,
			    -1);

	if (specs->recent_only && !recent)
		visible = FALSE;
	else if (specs->used_only && !used)
		visible = FALSE;
	else if (specs->cat != NULL && specs->cat != cat)
		visible = FALSE;
	else if (specs->text == NULL)
		visible = TRUE;
	else {
		gchar *name_n, *name_cf, *text_n, *text_cf;

		text_n = g_utf8_normalize (specs->text, -1, G_NORMALIZE_ALL);
		text_cf = g_utf8_casefold(text_n, -1);

		name_n = g_utf8_normalize (name, -1, G_NORMALIZE_ALL);
		name_cf = g_utf8_casefold(name_n, -1);
		visible = (NULL != g_strstr_len (name_cf, -1, text_cf));
		g_free (name_n);
		g_free (name_cf);

		if (!visible) {
			name_n = g_utf8_normalize (desc, -1, G_NORMALIZE_ALL);
			name_cf = g_utf8_casefold(name_n, -1);
			visible = (NULL != g_strstr_len (name_cf, -1, text_cf));
			g_free (name_n);
			g_free (name_cf);
		}

		g_free (text_n);
		g_free (text_cf);
	}

	g_free (name);
	g_free (desc);

	if (visible != was_visible)
		gtk_list_store_set (GTK_LIST_STORE (model), iter,
				    FUNCTION_VISIBLE, visible,
				    -1);
	return FALSE;
}

static void
dialog_function_select_search (GtkEntry *entry, gpointer data)
{
	search_t specs = {NULL, FALSE, FALSE, NULL};
	FunctionSelectState *state = data;
	GtkTreeIter iter;

	if (0 != gtk_entry_get_text_length (entry))
		specs.text = gtk_entry_get_text (entry);

	if (gtk_combo_box_get_active_iter (state->cb, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (state->model), &iter,
				    CATEGORY, &specs.cat,
				    -1);
		specs.recent_only
			= (specs.cat != NULL &&
			   specs.cat == GINT_TO_POINTER(-1));
		specs.used_only
			= (specs.cat != NULL &&
			   specs.cat == GINT_TO_POINTER(-2));
		if (specs.recent_only || specs.used_only)
			specs.cat = NULL;
	}

	gtk_tree_model_foreach (GTK_TREE_MODEL (state->model_functions),
				cb_dialog_function_select_search_all,
				(gpointer) &specs);
}

static void
dialog_function_select_erase_search_entry (GtkEntry *entry,
			      G_GNUC_UNUSED GtkEntryIconPosition icon_pos,
			      G_GNUC_UNUSED GdkEvent *event,
			      gpointer data)
{
	gtk_entry_set_text (entry, "");
	dialog_function_select_search (entry, data);
}

static void
dialog_function_select_cat_changed (G_GNUC_UNUSED GtkComboBox *widget,
				    gpointer data)
{
	FunctionSelectState *state = data;

	dialog_function_select_search (GTK_ENTRY (state->search_entry),
				       data);
}

/*************************************************************************/

static gboolean
cb_dialog_function_load_recent_funcs(GtkTreeModel *model,
				     G_GNUC_UNUSED GtkTreePath *path,
				     GtkTreeIter *iter,
				     gpointer data)
{
	gpointer this;

	gtk_tree_model_get (model, iter,
			    FUNCTION, &this,
			    -1);
	if (this == data) {
		gtk_list_store_set (GTK_LIST_STORE (model), iter,
				    FUNCTION_RECENT, TRUE,
				    -1);
		return TRUE;
	}
	return FALSE;
}

static void
dialog_function_load_recent_funcs (FunctionSelectState *state)
{
	GSList const *recent_funcs;

	for (recent_funcs = gnm_conf_get_functionselector_recentfunctions ();
	     recent_funcs;
	     recent_funcs = recent_funcs->next) {
		char const *name = recent_funcs->data;
		GnmFunc *fd;

		if (name == NULL)
			continue;

		fd = gnm_func_lookup (name, NULL);
		if (fd) {
			state->recent_funcs = g_slist_prepend (state->recent_funcs, fd);
			gtk_tree_model_foreach (GTK_TREE_MODEL (state->model_functions),
						cb_dialog_function_load_recent_funcs,
						fd);
		}
	}
}

static void
dialog_function_write_recent_func (FunctionSelectState *state, GnmFunc const *fd)
{
	GSList *rec_funcs;
	GSList *gconf_value_list = NULL;
	guint ulimit = gnm_conf_get_functionselector_num_of_recent ();

	state->recent_funcs = g_slist_remove (state->recent_funcs, (gpointer) fd);
	state->recent_funcs = g_slist_prepend (state->recent_funcs, (gpointer) fd);

	while (g_slist_length (state->recent_funcs) > ulimit)
		state->recent_funcs = g_slist_remove (state->recent_funcs,
						      g_slist_last (state->recent_funcs)->data);

	for (rec_funcs = state->recent_funcs; rec_funcs; rec_funcs = rec_funcs->next) {
		gconf_value_list = g_slist_prepend
			(gconf_value_list,
			 g_strdup (gnm_func_get_name (rec_funcs->data,
						      state->localized_function_names)));
	}
	gnm_conf_set_functionselector_recentfunctions (gconf_value_list);
	g_slist_free_full (gconf_value_list, g_free);
}

static gboolean
cb_unref (GtkTreeModel *model, G_GNUC_UNUSED GtkTreePath *path,
	  GtkTreeIter *iter, G_GNUC_UNUSED gpointer data)
{
	GnmFunc *f;

	gtk_tree_model_get (model, iter,
			    FUNCTION, &f,
			    -1);
	gnm_func_dec_usage (f);
	return FALSE;
}

static void
cb_dialog_function_select_destroy (FunctionSelectState  *state)
{
	if (state->formula_guru_key &&
	    gnm_dialog_raise_if_exists (state->wbcg, state->formula_guru_key)) {
		/* The formula guru is waiting for us.*/
		state->formula_guru_key = NULL;
		dialog_formula_guru (state->wbcg, NULL);
	}

	if (state->gui != NULL)
		g_object_unref (state->gui);
	g_slist_free (state->recent_funcs);
	gtk_tree_model_foreach (GTK_TREE_MODEL (state->model_functions),
				cb_unref,
				NULL);
	g_free (state->paste.prefix);
	g_free (state);
}

/**
 * cb_dialog_function_select_cancel_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_function_select_cancel_clicked (G_GNUC_UNUSED GtkWidget *button,
					  FunctionSelectState *state)
{
	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * cb_dialog_function_select_ok_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_function_select_ok_clicked (G_GNUC_UNUSED GtkWidget *button,
				      FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	GnmFunc *func;
	GtkTreeSelection *the_selection = gtk_tree_view_get_selection (state->treeview);

	if (state->formula_guru_key != NULL &&
	    gtk_tree_selection_get_selected (the_selection, &model, &iter)) {
		WBCGtk *wbcg = state->wbcg;
		gtk_tree_model_get (model, &iter,
				    FUNCTION, &func,
				    -1);
		dialog_function_write_recent_func (state, func);
		state->formula_guru_key = NULL;
		gtk_widget_destroy (state->dialog);
		dialog_formula_guru (wbcg, func);
		return;
	}

	gtk_widget_destroy (state->dialog);
	return;
}

/**
 * cb_dialog_function_select_paste_clicked:
 * @button:
 * @state:
 *
 * Close (destroy) the dialog
 **/
static void
cb_dialog_function_select_paste_clicked (G_GNUC_UNUSED GtkWidget *button,
				      FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	GnmFunc *func;
	GtkTreeSelection *the_selection = gtk_tree_view_get_selection (state->treeview);

	if (gtk_tree_selection_get_selected (the_selection, &model, &iter) &&
	    wbcg_edit_start (state->wbcg, FALSE, FALSE)) {
		GtkEditable *entry
			= GTK_EDITABLE (wbcg_get_entry (state->wbcg));
		gint position;
		gtk_tree_model_get (model, &iter,
				    FUNCTION, &func,
				    -1);
		if (func != NULL) {
			dialog_function_write_recent_func (state, func);
			if (state->paste.from >= 0)
				gtk_editable_select_region
					(entry, state->paste.from,
					 state->paste.to);
			gtk_editable_delete_selection (entry);
			position = gtk_editable_get_position (entry);
			gtk_editable_insert_text
				(entry, func->name, -1, &position);
			gtk_editable_set_position (entry, position);
		}
	}

	gtk_widget_destroy (state->dialog);
	return;
}

static void
cb_dialog_function_row_activated (G_GNUC_UNUSED GtkTreeView *tree_view,
				  G_GNUC_UNUSED GtkTreePath       *path,
				  G_GNUC_UNUSED GtkTreeViewColumn *column,
				  FunctionSelectState *state)
{
	switch (state->mode) {
	case GURU_MODE:
		cb_dialog_function_select_ok_clicked (NULL, state);
		return;
	case PASTE_MODE:
		cb_dialog_function_select_paste_clicked (NULL, state);
		return;
	default:
		return;
	}
}

static gint
dialog_function_select_by_name (gconstpointer a_, gconstpointer b_,
				gpointer user)
{
	GnmFunc const * const a = (GnmFunc const * const)a_;
	GnmFunc const * const b = (GnmFunc const * const)b_;
	FunctionSelectState const *state = user;
	gboolean localized = state->localized_function_names;

	return g_utf8_collate (gnm_func_get_name (a, localized),
			       gnm_func_get_name (b, localized));
}

/*************************************************************************/
/* Functions related to the category selector                            */
/*************************************************************************/

typedef struct {
	char const *name;
	GtkTreeIter *iter;
} dialog_function_select_load_cb_t;

static gboolean
cb_dialog_function_select_load_cb (GtkTreeModel *model,
				   G_GNUC_UNUSED GtkTreePath *path,
				   GtkTreeIter *iter,
				   gpointer data)
{
	dialog_function_select_load_cb_t *specs = data;
	gchar *name;
	gpointer ptr;
	gboolean res;

	gtk_tree_model_get (model, iter,
			    CAT_NAME, &name,
			    CATEGORY, &ptr,
			    -1);

	if (ptr == NULL || ptr == GINT_TO_POINTER(-1)
	    || ptr == GINT_TO_POINTER(-2))
		res = FALSE;
	else if (go_utf8_collate_casefold (specs->name, name) < 0) {
		specs->iter = gtk_tree_iter_copy (iter);
		res = TRUE;
	} else
		res = FALSE;

	g_free (name);

	return res;
}

static void
dialog_function_select_load_cb (FunctionSelectState *state)
{
	int i = 0;
	GtkTreeIter p_iter;
	GnmFuncGroup const * cat;

	gtk_list_store_clear (state->model);

	gtk_list_store_insert_before (state->model, &p_iter, NULL);
	gtk_list_store_set (state->model, &p_iter,
			    CAT_NAME, _("All Functions"),
			    CATEGORY, NULL,
			    CAT_SEPARATOR, FALSE,
			    -1);
	gtk_list_store_insert_before (state->model, &p_iter, NULL);
	gtk_list_store_set (state->model, &p_iter,
			    CAT_NAME, _("Recently Used"),
			    CATEGORY, GINT_TO_POINTER(-1),
			    CAT_SEPARATOR, FALSE,
			    -1);
	gtk_list_store_insert_before (state->model, &p_iter, NULL);
	gtk_list_store_set (state->model, &p_iter,
			    CAT_NAME, _("In Use"),
			    CATEGORY, GINT_TO_POINTER(-2),
			    CAT_SEPARATOR, FALSE,
			    -1);

	gtk_list_store_insert_before (state->model, &p_iter, NULL);
	gtk_list_store_set (state->model, &p_iter,
			    CAT_NAME, "-",
			    CATEGORY, NULL,
			    CAT_SEPARATOR, TRUE,
			    -1);

	while ((cat = gnm_func_group_get_nth (i++)) != NULL) {
		dialog_function_select_load_cb_t specs;
		specs.name = _(cat->display_name->str);
		specs.iter = NULL;

		gtk_tree_model_foreach (GTK_TREE_MODEL (state->model),
					cb_dialog_function_select_load_cb,
					&specs);

		gtk_list_store_insert_before (state->model, &p_iter, specs.iter);
		gtk_list_store_set (state->model, &p_iter,
				    CAT_NAME, specs.name,
				    CATEGORY, cat,
				    CAT_SEPARATOR, FALSE,
				    -1);
		if (specs.iter != NULL)
			gtk_tree_iter_free (specs.iter);
	}
}

static gboolean
dialog_function_select_cat_row_separator (GtkTreeModel *model,
					  GtkTreeIter *iter,
					  G_GNUC_UNUSED gpointer data)
{
	gboolean sep;

	gtk_tree_model_get (model, iter,
			    CAT_SEPARATOR, &sep,
			    -1);

	return sep;
}

/*************************************************************************/
/* Functions related to the description field                            */
/*************************************************************************/

static GtkTextTag *
make_link (GtkTextBuffer *description, GtkWidget *target, const char *name,
	   GCallback cb, gpointer user)
{
	GtkTextTag *link =
		gtk_text_tag_table_lookup
		(gtk_text_buffer_get_tag_table (description), name);

	if (!link) {
		GdkRGBA link_color;
		char *link_color_text;

		gnm_get_link_color (target, &link_color);
		link_color_text = gdk_rgba_to_string (&link_color);

		link = gtk_text_buffer_create_tag
			(description, name,
			 "underline", PANGO_UNDERLINE_SINGLE,
			 "foreground", link_color_text,
			 NULL);

		g_free (link_color_text);

		if (cb)
			g_signal_connect (link, "event", cb, user);
	}

	return link;
}

static gboolean
cb_link_event (GtkTextTag *link, G_GNUC_UNUSED GObject *trigger,
	       GdkEvent *event, G_GNUC_UNUSED GtkTextIter *iter,
	       G_GNUC_UNUSED gpointer user)
{
	switch (event->type) {
	case GDK_BUTTON_PRESS:
	case GDK_2BUTTON_PRESS:
	case GDK_3BUTTON_PRESS: {
		GdkEventButton *eb = (GdkEventButton *)event;
		const char *uri = g_object_get_data (G_OBJECT (link), "uri");
		GError *error = NULL;

		if (eb->button != 1)
			break;
		if (event->type != GDK_BUTTON_PRESS)
			return TRUE;

		error = go_gtk_url_show (uri, gdk_event_get_screen (event));
		if (error) {
			g_printerr ("Failed to show %s\n(%s)\n",
				    uri,
				    error->message);
			g_error_free (error);
		}

		return TRUE;
	}

#if 0
	case GDK_ENTER_NOTIFY:
	case GDK_LEAVE_NOTIFY:
		/* We aren't getting these. */
#endif
	default:
		break;
	}

	return FALSE;
}

static char *
make_expr_example (Sheet *sheet, const char *text,
		   gboolean localized, gboolean consider_format)
{
	GnmLocale *oldlocale = NULL;
	GnmExprTop const *texpr;
	char *res;
	GnmParsePos pp;
	GnmEvalPos ep;
	GnmConventions const *convs = gnm_conventions_default;
	char *tmp_text = NULL;
	GOFormat const *fmt = NULL;

	if (consider_format &&
	    g_ascii_strncasecmp (text, "TEXT(", 5) == 0 &&
	    text[strlen(text) - 1] == ')') {
		char *p;
		tmp_text = g_strdup (text + 5);
		p = tmp_text + strlen (tmp_text) - 1;
		while (p >= tmp_text && p[0] != '"') p--;
		p[0] = 0;
		while (p >= tmp_text && p[0] != '"') p--;
		fmt = go_format_new_from_XL (p + 1);
		while (p >= tmp_text && p[0] != ',') p--;
		*p = 0;
	}

	eval_pos_init_sheet (&ep, sheet);
	parse_pos_init_evalpos (&pp, &ep);

	if (!localized)
		oldlocale = gnm_push_C_locale ();
	texpr = gnm_expr_parse_str (text, &pp,
				    GNM_EXPR_PARSE_DEFAULT,
				    convs,
				    NULL);
	if (!localized)
		gnm_pop_C_locale (oldlocale);

	if (texpr) {
		char *etxt = gnm_expr_top_as_string (texpr, &pp, convs);
		GnmValue *val = gnm_expr_top_eval
			(texpr, &ep, GNM_EXPR_EVAL_PERMIT_NON_SCALAR);
		char *vtxt;

		if (!fmt)
			fmt = gnm_auto_style_format_suggest (texpr, &ep);
		vtxt = format_value (fmt, val, -1, sheet_date_conv (sheet));

		gnm_expr_top_unref (texpr);
		value_release (val);

		res = g_strdup_printf (_("%s evaluates to %s."), etxt, vtxt);

		g_free (etxt);
		g_free (vtxt);
	} else {
		g_warning ("Failed to parse [%s]", text);
		res = g_strdup ("");
	}

	g_free (tmp_text);
	go_format_unref (fmt);

	return res;
}



#define ADD_LTEXT(text,len) gtk_text_buffer_insert (description, &ti, (text), (len))
#define ADD_TEXT(text) ADD_LTEXT((text),-1)
#define ADD_BOLD_TEXT(text,len) gtk_text_buffer_insert_with_tags (description, &ti, (text), (len), bold, NULL)
#define ADD_LINK_TEXT(text,len) gtk_text_buffer_insert_with_tags (description, &ti, (text), (len), link, NULL)
#define ADD_TEXT_WITH_ARGS(text) { const char *t = text; while (*t) { const char *at = strstr (t, "@{"); \
			if (at == NULL) { ADD_TEXT(t); break;} ADD_LTEXT(t, at - t); t = at + 2; at = strchr (t,'}'); \
			if (at != NULL) { ADD_BOLD_TEXT(t, at - t); t = at + 1; } else {ADD_TEXT (t); break;}}}
#define FINISH_ARGS if (seen_args && !args_finished) {\
	gint min, max; \
	gnm_func_count_args (func, &min, &max);\
		if (max == G_MAXINT) {	\
			ADD_BOLD_TEXT(UNICODE_ELLIPSIS, strlen(UNICODE_ELLIPSIS)); \
			ADD_LTEXT("\n",1);				\
			args_finished = TRUE;				\
		}							\
	}

static void
describe_new_style (GtkTextBuffer *description,
		    GtkWidget *target,
		    GnmFunc *func, Sheet *sheet)
{
	GnmFuncHelp const *help;
	GtkTextIter ti;
	GtkTextTag *bold =
		gtk_text_buffer_create_tag
		(description, NULL,
		 "weight", PANGO_WEIGHT_BOLD,
		 NULL);
	gboolean seen_args = FALSE;
	gboolean args_finished = FALSE;
	gboolean seen_examples = FALSE;
	gboolean seen_extref = FALSE;
	gboolean is_TEXT =
		g_ascii_strcasecmp (gnm_func_get_name (func, FALSE), "TEXT") == 0;
	int n;

	gtk_text_buffer_get_end_iter (description, &ti);

	help = gnm_func_get_help (func, &n);

	for (; n-- > 0; help++) {
		switch (help->type) {
		case GNM_FUNC_HELP_NAME: {
			const char *text = gnm_func_gettext (func, help->text);
			const char *colon = strchr (text, ':');
			if (!colon)
				break;
			ADD_BOLD_TEXT (text, colon - text);
			ADD_TEXT (": ");
			ADD_TEXT_WITH_ARGS (colon + 1);
			ADD_TEXT ("\n\n");
			break;
		}
		case GNM_FUNC_HELP_ARG: {
			const char *text = gnm_func_gettext (func, help->text);
			const char *colon = strchr (text, ':');
			if (!colon)
				break;

			if (!seen_args) {
				seen_args = TRUE;
				ADD_TEXT (_("Arguments:"));
				ADD_TEXT ("\n");
			}

			ADD_BOLD_TEXT (text, colon - text);
			ADD_TEXT (": ");
			ADD_TEXT_WITH_ARGS (colon + 1);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_DESCRIPTION: {
			const char *text = gnm_func_gettext (func, help->text);
			FINISH_ARGS;
			ADD_TEXT ("\n");
			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_NOTE: {
			const char *text = gnm_func_gettext (func, help->text);
			FINISH_ARGS;
			ADD_TEXT ("\n");
			ADD_TEXT (_("Note: "));
			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_EXAMPLES: {
			const char *text = gnm_func_gettext (func, help->text);
			gboolean was_translated = (text != help->text);

			FINISH_ARGS;
			if (!seen_examples) {
				seen_examples = TRUE;
				ADD_TEXT ("\n");
				ADD_TEXT (_("Examples:"));
				ADD_TEXT ("\n");
			}

			if (text[0] == '=') {
				char *example =
					make_expr_example (sheet, text + 1,
							   was_translated,
							   !is_TEXT);
				ADD_TEXT (example);
				g_free (example);
			} else {
				ADD_TEXT_WITH_ARGS (text);
			}
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_SEEALSO: {
			const char *text = help->text;  /* Not translated */
			const char *pre = _("See also: ");
			GtkTextTag *link =
				make_link (description, target, "LINK",
					   NULL, NULL);

			FINISH_ARGS;
			ADD_TEXT ("\n");

			while (*text) {
				const char *end = strchr (text, ',');
				if (!end) end = text + strlen (text);

				ADD_TEXT (pre);
				ADD_LINK_TEXT (text, end - text);

				text = *end ? end + 1 : end;

				pre = _(", ");
			}
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_EXTREF: {
			GtkTextTag *link;
			char *uri, *tagname;
			const char *text;

			FINISH_ARGS;
			/*
			 * We put in just one link and let the web page handle
			 * the rest.  In particular, we do not even look at
			 * what the help->text is here.
			 */
			if (seen_extref)
				break;

			uri = g_strdup_printf ("http://www.gnumeric.org/func-doc.shtml?%s", func->name);

			tagname = g_strdup_printf ("EXTLINK-%s", func->name);
			link = make_link
				(description, target, tagname,
				 G_CALLBACK (cb_link_event), NULL);

			g_object_set_data_full (G_OBJECT (link),
						"uri", uri,
						g_free);

			ADD_TEXT (_("Further information: "));

			text = _("online descriptions");
			ADD_LINK_TEXT (text, strlen (text));

			ADD_TEXT (".\n");

			seen_extref = TRUE;
			break;
		}
		case GNM_FUNC_HELP_EXCEL: {
			const char *text = gnm_func_gettext (func, help->text);
			FINISH_ARGS;
			ADD_TEXT ("\n");
			ADD_TEXT (_("Microsoft Excel: "));
			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		case GNM_FUNC_HELP_ODF: {
			const char *text = gnm_func_gettext (func, help->text);
			FINISH_ARGS;
			ADD_TEXT ("\n");
			ADD_TEXT (_("ODF (OpenFormula): "));
			ADD_TEXT_WITH_ARGS (text);
			ADD_TEXT ("\n");
			break;
		}
		default:
			break;
		}
	}
	FINISH_ARGS;
}

#undef ADD_TEXT_WITH_ARGS
#undef ADD_TEXT
#undef ADD_LTEXT
#undef ADD_BOLD_TEXT
#undef ADD_LINK_TEXT
#undef FINISH_ARGS

typedef struct {
	GnmFunc    *fd;
	FunctionSelectState *state;
	GtkTreePath *path;
} dialog_function_select_find_func_t;


static gboolean
dialog_function_select_search_func (GtkTreeModel *model,
				    GtkTreePath *path,
				    GtkTreeIter *iter,
				    gpointer dt)
{
	GnmFunc* fd;
	dialog_function_select_find_func_t *data = dt;

	gtk_tree_model_get (model, iter,
			    FUNCTION, &fd,
			    -1);
	if (fd == data->fd) {
		data->path = gtk_tree_path_copy (path);
		return TRUE;
	}
	return FALSE;
}

static void
dialog_function_select_find_func (FunctionSelectState *state, char* name)
{
	GnmFunc    *fd;

	if (name == NULL)
		return;

	fd = gnm_func_lookup (name, state->wb);
	if (fd != NULL) {
		dialog_function_select_find_func_t data = {fd, state, NULL};
		GtkTreeSelection *selection = gtk_tree_view_get_selection
			(state->treeview);
		GtkTreePath *path;

		gtk_tree_model_foreach (GTK_TREE_MODEL (state->model_functions),
					dialog_function_select_search_func,
					&data);
		if (data.path != NULL) {
			GtkTreeIter iter;
			if (gtk_tree_model_get_iter
			    (GTK_TREE_MODEL (state->model_functions), &iter,
                             data.path))
				gtk_list_store_set (state->model_functions,
						    &iter,
						    FUNCTION_VISIBLE, TRUE,
						    -1);

			path = gtk_tree_model_filter_convert_child_path_to_path
				(GTK_TREE_MODEL_FILTER (state->model_filter),
				 data.path);

			gtk_tree_selection_select_path (selection,
							path);
			gtk_tree_view_scroll_to_cell (state->treeview, path,
						      NULL, FALSE, 0., 0.);
			gtk_tree_path_free (path);
			gtk_tree_path_free (data.path);
		} else
			g_warning ("Function %s was not found in its category", name);

	} else
		g_warning ("Function %s was not found", name);
}

typedef struct {
	FunctionSelectState *state;
	gchar * name;
} cb_dialog_function_select_idle_handler_t;

static gboolean
cb_dialog_function_select_idle_handler (gpointer dt)
{
	cb_dialog_function_select_idle_handler_t *data = dt;

	dialog_function_select_find_func (data->state, data->name);

	g_free (data->name);
	g_free (data);

	return FALSE;
}

static void
cb_description_clicked (GtkTextBuffer *textbuffer,
			GtkTextIter   *location,
			GtkTextMark   *mark,
			FunctionSelectState *state)
{
	const char * mark_name;
	GtkTextTag *link;
	GtkTextIter   *start;
	GtkTextIter   *end;
	cb_dialog_function_select_idle_handler_t *data;

	if ((mark == NULL) || ((mark_name = gtk_text_mark_get_name (mark)) == NULL)
	    || (strcmp(mark_name, "selection_bound") != 0))
		return;

	link = gtk_text_tag_table_lookup
		(gtk_text_buffer_get_tag_table (textbuffer), "LINK");

	if ((link == NULL) || !gtk_text_iter_has_tag (location, link))
		return;

	start = gtk_text_iter_copy (location);
	end = gtk_text_iter_copy (location);

	if (!gtk_text_iter_begins_tag (start, link))
		gtk_text_iter_backward_to_tag_toggle (start, link);
	if (!gtk_text_iter_ends_tag (end, link))
		gtk_text_iter_forward_to_tag_toggle (end, link);

	data = g_new(cb_dialog_function_select_idle_handler_t, 1);

	data->name = gtk_text_buffer_get_text (textbuffer, start, end, FALSE);
	gtk_text_iter_free (start);
	gtk_text_iter_free (end);
	data->state = state;

	g_idle_add_full (G_PRIORITY_HIGH_IDLE, cb_dialog_function_select_idle_handler,
			 data, NULL);
}

static void
cb_dialog_function_select_fun_selection_changed (GtkTreeSelection *selection,
						 FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GtkTreeModel *model;
	GnmFunc *func;
	GtkTextBuffer *description;
	GtkTextMark *mark;
	gboolean active = FALSE;

	description = gtk_text_view_get_buffer (state->description_view);

	mark = gtk_text_buffer_get_mark (description, "start-mark");
	gtk_text_view_scroll_to_mark (state->description_view, mark,
				      0.1, TRUE, 0.0, 0.0);
	gtk_text_buffer_set_text (description, "", 0);

	if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
		gtk_tree_model_get (model, &iter,
				    FUNCTION, &func,
				    -1);

		gnm_func_load_if_stub (func);

		if (gnm_func_get_help (func, NULL) == NULL)
			gtk_text_buffer_set_text (description, "?", -1);
		else
			describe_new_style (description,
					    GTK_WIDGET (state->description_view),
					    func, state->sheet);
		active = TRUE;
	}
	gtk_widget_set_sensitive (state->ok_button, active);
	gtk_widget_set_sensitive (state->paste_button, active);

}

/**********************************************************************/
/* Setup Functions */
/**********************************************************************/

static const gchar *
dialog_function_select_peek_description (GnmFunc *func)
{
	GnmFuncHelp const *help;
	int n;

	gnm_func_load_if_stub (func);

	help = gnm_func_get_help (func, &n);
	if (help == NULL)
		return "";

	for (; n-- > 0; help++) {
		switch (help->type) {
		case GNM_FUNC_HELP_ARG:
		case GNM_FUNC_HELP_NOTE:
		case GNM_FUNC_HELP_EXAMPLES:
		case GNM_FUNC_HELP_SEEALSO:
		case GNM_FUNC_HELP_EXTREF:
		case GNM_FUNC_HELP_EXCEL:
		case GNM_FUNC_HELP_ODF:
		case GNM_FUNC_HELP_DESCRIPTION:
		default:
			break;
		case GNM_FUNC_HELP_NAME: {
			const char *text = gnm_func_gettext (func, help->text);
			const char *colon = strchr (text, ':');
			return (colon ? colon + 1 : text);
		}
		}
	}
	return "";
}


static gchar *
dialog_function_select_get_description (GnmFunc *func, PangoAttrList **pal)
{
	PangoAttribute *attr;
	char const *desc = dialog_function_select_peek_description (func);
	char const *here;
	GString* gstr = g_string_new (NULL);

	*pal = pango_attr_list_new ();

	if (desc != NULL) {
		while (*desc != '\0') {
			here =  strstr (desc, "@{");
			if (here == NULL) {
				g_string_append (gstr, desc);
				break;
			}
			g_string_append_len (gstr, desc, here - desc);
			attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
			attr->start_index = gstr->len;
			desc = here + 2;
			here = strchr (desc,'}');
			if (here == NULL) {
				g_string_append (gstr, desc);
				pango_attr_list_insert (*pal, attr);
				break;
			}
			g_string_append_len (gstr, desc, here - desc);
			attr->end_index = gstr->len;
			pango_attr_list_insert (*pal, attr);
			desc = here + 1;
		}
	}

	return g_string_free (gstr, FALSE);
}



static void
dialog_function_select_load_tree (FunctionSelectState *state)
{
	GtkTreeIter  iter;
	GnmFuncGroup const * cat;
	GSList *funcs = NULL, *ptr;
	GnmFunc *func;
	gint i = 0;
	PangoAttrList *pal;
	gchar *desc;

	gtk_list_store_clear (state->model_functions);

	while ((cat = gnm_func_group_get_nth (i++)) != NULL)
		funcs = g_slist_concat (funcs,
					g_slist_copy (cat->functions));

	funcs = g_slist_sort_with_data (funcs,
					dialog_function_select_by_name,
					state);

	for (ptr = funcs; ptr; ptr = ptr->next) {
		func = ptr->data;
		if (!(gnm_func_get_flags (func) &
		      (GNM_FUNC_INTERNAL | GNM_FUNC_IS_PLACEHOLDER))) {
			gboolean in_use = gnm_func_get_in_use (func);
			gtk_list_store_append (state->model_functions, &iter);
			gnm_func_inc_usage (func);
			desc = dialog_function_select_get_description (func, &pal);
			gtk_list_store_set
				(state->model_functions, &iter,
				 FUN_NAME, gnm_func_get_name (func, state->localized_function_names),
				 FUNCTION, func,
				 FUNCTION_DESC, desc,
				 FUNCTION_PAL, pal,
				 FUNCTION_CAT, gnm_func_get_function_group (func),
				 FUNCTION_VISIBLE, TRUE,
				 FUNCTION_RECENT, FALSE,
				 FUNCTION_USED, in_use,
				 -1);
			g_free (desc);
			pango_attr_list_unref (pal);

		}
	}

	g_slist_free (funcs);
}

static void
dialog_function_select_init (FunctionSelectState *state)
{
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	GtkTextIter where;
	GtkTextBuffer *description;
	GtkCellRenderer *cell;
	GtkWidget *cancel_button;
	GtkWidget *close_button;

	g_object_set_data (G_OBJECT (state->dialog), FUNCTION_SELECT_DIALOG_KEY,
			   state);

	/* Set-up combo box */
	state->cb = GTK_COMBO_BOX
		(go_gtk_builder_get_widget (state->gui, "category-box"));
	state->model = gtk_list_store_new
		(NUM_CAT_COLUMNS, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_BOOLEAN);

	gtk_combo_box_set_model (state->cb, GTK_TREE_MODEL (state->model));
	g_object_unref (state->model);
	cell = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (state->cb), cell, TRUE);
	gtk_cell_layout_add_attribute
		(GTK_CELL_LAYOUT (state->cb), cell, "text", CAT_NAME);
	dialog_function_select_load_cb (state);
	gtk_combo_box_set_row_separator_func
		(state->cb, dialog_function_select_cat_row_separator,
		 state, NULL);
	g_signal_connect (state->cb, "changed",
			  G_CALLBACK (dialog_function_select_cat_changed),
			  state);
	/* Finished set-up of combo box */

	/* Set-up treeview */

	state->model_functions = gtk_list_store_new
		(NUM_COLUMNS,
/* 	FUN_NAME, */
/* 	FUNCTION, */
/* 	FUNCTION_DESC, */
/* 	FUNCTION_PAL, */
/* 	FUNCTION_CAT, */
/* 	FUNCTION_VISIBLE, */
/* 	FUNCTION_RECENT, */
/* 	FUNCTION_USED, */
		 G_TYPE_STRING, G_TYPE_POINTER,
		 G_TYPE_STRING, PANGO_TYPE_ATTR_LIST,
		 G_TYPE_POINTER, G_TYPE_BOOLEAN,
		 G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);

	state->model_filter = gtk_tree_model_filter_new
		(GTK_TREE_MODEL (state->model_functions), NULL);
	g_object_unref (state->model_functions);
	gtk_tree_model_filter_set_visible_column
		(GTK_TREE_MODEL_FILTER (state->model_filter), FUNCTION_VISIBLE);

	state->treeview = GTK_TREE_VIEW
		(go_gtk_builder_get_widget (state->gui, "function-list"));
	gtk_tree_view_set_model (state->treeview,
				 state->model_filter);
	g_object_unref (state->model_filter);

	selection = gtk_tree_view_get_selection (state->treeview);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);
	g_signal_connect (selection, "changed",
			  G_CALLBACK
			  (cb_dialog_function_select_fun_selection_changed),
			  state);

	column = gtk_tree_view_column_new_with_attributes
		(_("Name"),
		 gtk_cell_renderer_text_new (),
		 "text", FUN_NAME, NULL);
	gtk_tree_view_append_column (state->treeview, column);
	column = gtk_tree_view_column_new_with_attributes
		(_("Description"),
		 gtk_cell_renderer_text_new (),
		 "text", FUNCTION_DESC,
		 "attributes", FUNCTION_PAL, NULL);
	gtk_tree_view_append_column (state->treeview, column);

	gtk_tree_view_set_headers_visible (state->treeview, FALSE);
	/* Finished set-up of treeview */

	dialog_function_select_load_tree (state);
	dialog_function_load_recent_funcs (state);

	state->search_entry = go_gtk_builder_get_widget (state->gui,
						    "search-entry");
	if (state->paste.prefix != NULL)
		gtk_entry_set_text (GTK_ENTRY (state->search_entry),
				    state->paste.prefix);

	g_signal_connect (G_OBJECT (state->search_entry),
			  "icon-press",
			  G_CALLBACK
			  (dialog_function_select_erase_search_entry),
			  state);

	g_signal_connect (G_OBJECT (state->search_entry),
			  "activate",
			  G_CALLBACK (dialog_function_select_search),
			  state);
	if (state->mode != HELP_MODE)
		g_signal_connect (G_OBJECT (state->treeview),
				  "row-activated",
				  G_CALLBACK (cb_dialog_function_row_activated),
				  state);

	gtk_paned_set_position (GTK_PANED (go_gtk_builder_get_widget
					   (state->gui, "vpaned1")), 300);

	state->description_view = GTK_TEXT_VIEW (go_gtk_builder_get_widget
						 (state->gui, "description"));
	gtk_style_context_add_class
		(gtk_widget_get_style_context (GTK_WIDGET (state->description_view)),
		 "function-help");
	description = gtk_text_view_get_buffer (state->description_view);
	gtk_text_buffer_get_start_iter (description, &where);
	gtk_text_buffer_create_mark (description, "start-mark", &where, TRUE);

	g_signal_connect_after (G_OBJECT (description),
		"mark-set",
		G_CALLBACK (cb_description_clicked), state);

	state->ok_button = go_gtk_builder_get_widget (state->gui, "ok_button");
	gtk_widget_set_sensitive (state->ok_button, FALSE);
	g_signal_connect (G_OBJECT (state->ok_button),
		"clicked",
		G_CALLBACK (cb_dialog_function_select_ok_clicked), state);
	state->paste_button = go_gtk_builder_get_widget (state->gui, "paste_button");
	gtk_widget_set_sensitive (state->paste_button, FALSE);
	g_signal_connect (G_OBJECT (state->paste_button),
		"clicked",
		G_CALLBACK (cb_dialog_function_select_paste_clicked), state);
	cancel_button = go_gtk_builder_get_widget (state->gui, "cancel_button");
	g_signal_connect (G_OBJECT (cancel_button), "clicked",
		G_CALLBACK (cb_dialog_function_select_cancel_clicked), state);
	close_button = go_gtk_builder_get_widget (state->gui, "close_button");
	g_signal_connect (G_OBJECT (close_button), "clicked",
		G_CALLBACK (cb_dialog_function_select_cancel_clicked), state);

	gnm_dialog_setup_destroy_handlers
		(GTK_DIALOG (state->dialog),
		 state->wbcg,
		 GNM_DIALOG_DESTROY_CURRENT_SHEET_REMOVED);

	gnm_init_help_button (
		go_gtk_builder_get_widget (state->gui, "help_button"),
		GNUMERIC_HELP_LINK_FUNCTION_SELECT);
	g_object_set_data_full
		(G_OBJECT (state->dialog),
		 "state", state,
		 (GDestroyNotify) cb_dialog_function_select_destroy);

	if (state->paste.prefix != NULL)
		dialog_function_select_search
			(GTK_ENTRY (state->search_entry), state);

	gtk_widget_set_visible (close_button, state->mode != GURU_MODE);
	gtk_widget_set_visible (go_gtk_builder_get_widget
				(state->gui, "help_button"),
				state->mode == GURU_MODE);
	gtk_widget_set_visible (cancel_button, state->mode == GURU_MODE);
	gtk_widget_set_visible (state->ok_button, state->mode == GURU_MODE);
	gtk_widget_set_visible (state->paste_button, state->mode == PASTE_MODE);
	gtk_widget_set_visible (go_gtk_builder_get_widget
				(state->gui, "title_label"),
				state->mode == GURU_MODE);
	gtk_combo_box_set_active (state->cb, state->mode == HELP_MODE ? 2 : 0);
	switch (state->mode) {
	case GURU_MODE:
		break;
	case HELP_MODE:
		gtk_window_set_title (GTK_WINDOW (state->dialog),
				      _("Gnumeric Function Help Browser"));
		break;
	case PASTE_MODE:
		gtk_window_set_title (GTK_WINDOW (state->dialog),
				      _("Paste Function Name dialog"));
		break;
	}
}

static void
dialog_function_select_full (WBCGtk *wbcg, char const *guru_key,
			     char const *key, DialogMode mode, gint from, gint to)
{
	FunctionSelectState* state;
	GtkBuilder *gui;

	g_return_if_fail (wbcg != NULL);

	if (gnm_dialog_raise_if_exists (wbcg, key))
		return;
	gui = gnm_gtk_builder_load ("res:ui/function-select.ui", NULL, GO_CMD_CONTEXT (wbcg));
        if (gui == NULL)
		return;

	state = g_new (FunctionSelectState, 1);
	state->wbcg  = wbcg;
	state->sheet = wb_control_cur_sheet (GNM_WBC (wbcg));
	state->localized_function_names = state->sheet->convs->localized_function_names;
	state->wb    = state->sheet->workbook;
        state->gui   = gui;
        state->dialog = go_gtk_builder_get_widget (state->gui, "selection_dialog");
	state->formula_guru_key = guru_key;
        state->recent_funcs = NULL;
	state->mode  = mode;
	state->paste.from = from;
	state->paste.to = to;

	gnm_dialog_setup_destroy_handlers (GTK_DIALOG (state->dialog),
					   state->wbcg,
					   GNM_DIALOG_DESTROY_SHEET_REMOVED);

	if (mode == PASTE_MODE && state->paste.from >= 0) {
		GtkEditable *entry
			= GTK_EDITABLE (wbcg_get_entry (state->wbcg));
		state->paste.prefix = gtk_editable_get_chars
			(entry, state->paste.from,
			 state->paste.to);
	} else
		state->paste.prefix = NULL;

	dialog_function_select_init (state);
	gnm_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       key);

	gtk_widget_show (state->dialog);
}

void
dialog_function_select (WBCGtk *wbcg, char const *key)
{
	dialog_function_select_full (wbcg, key,
				     FUNCTION_SELECT_KEY, GURU_MODE, -1, -1);
}

void
dialog_function_select_help (WBCGtk *wbcg)
{
	dialog_function_select_full (wbcg, NULL,
				     FUNCTION_SELECT_HELP_KEY, HELP_MODE,
				     -1, -1);
}

void
dialog_function_select_paste (WBCGtk *wbcg, gint from, gint to)
{
	dialog_function_select_full (wbcg, NULL,
				     FUNCTION_SELECT_PASTE_KEY, PASTE_MODE,
				     from, to);
}

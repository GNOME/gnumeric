/*
 * gnumeric-conf.c:
 *
 * Author:
 *	Andreas J. Guelzow <aguelzow@pyrshep.ca>
 *
 * (C) Copyright 2002-2005 Andreas J. Guelzow <aguelzow@pyrshep.ca>
 * (C) Copyright 2009-2011 Morten Welinder <terra@gnome.org>
 *
 * Introduced the concept of "node" and implemented the win32 backend
 * by Ivan, Wong Yat Cheung <email@ivanwong.info>, 2005
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
#include <gnumeric.h>
#include <application.h>
#include <gnumeric-conf.h>
#include <gutils.h>
#include <mstyle.h>
#include <goffice/goffice.h>
#include <value.h>
#include <number-match.h>
#include <string.h>
#include <sheet.h>
#include <print-info.h>
#include <glib/gi18n-lib.h>

#define NO_DEBUG_GCONF
#ifndef NO_DEBUG_GCONF
#define d(code)	{ code; }
#else
#define d(code)
#endif

#define GNM_CONF_DIR "gnumeric"

static gboolean persist_changes = TRUE;

static GOConfNode *root = NULL;

/*
 * Hashes to simplify ownership rules.  We use this so none of the getters
 * have to return memory that the callers need to free.
  */
static GHashTable *string_pool;
static GHashTable *string_list_pool;
static GHashTable *node_pool, *node_watch;

static gboolean debug_getters;
static gboolean debug_setters;
#define MAYBE_DEBUG_GET(key) do {				\
	if (debug_getters) g_printerr ("conf-get: %s\n", key);	\
} while (0)
#define MAYBE_DEBUG_SET(key) do {				\
	if (debug_setters) g_printerr ("conf-set: %s\n", key);	\
} while (0)


static guint sync_handler;

static gboolean
cb_sync (void)
{
	go_conf_sync (root);
	sync_handler = 0;
	return FALSE;
}

static void
schedule_sync (void)
{
	if (sync_handler)
		return;

	sync_handler = g_timeout_add (200, (GSourceFunc)cb_sync, NULL);
}

/* -------------------------------------------------------------------------- */

static GSList *watchers;

struct cb_watch_generic {
	guint handler;
	const char *key;
	const char *short_desc;
	const char *long_desc;
};

static void
free_watcher (struct cb_watch_generic *watcher)
{
	go_conf_remove_monitor (watcher->handler);
}

/* ---------------------------------------- */

/**
 * gnm_conf_get_root:
 *
 * Returns: (transfer none): the root config node.
 */
GOConfNode *
gnm_conf_get_root (void)
{
	return root;
}

static GOConfNode *
get_node (const char *key, gpointer watch)
{
	GOConfNode *res = g_hash_table_lookup (node_pool, key);
	if (!res) {
		res = go_conf_get_node (key[0] == '/' ? NULL : root, key);
		g_hash_table_insert (node_pool, (gpointer)key, res);
		if (watch)
			g_hash_table_insert (node_watch, res, watch);
	}
	return res;
}

static GOConfNode *
get_watch_node (gpointer watch_)
{
	struct cb_watch_generic *watch = watch_;
	return get_node (watch->key, watch);
}

/**
 * gnm_conf_get_short_desc:
 * @node: #GOConfNode
 *
 * Returns: (transfer none) (nullable): a brief description of @node.
 */
char const *
gnm_conf_get_short_desc (GOConfNode *node)
{
	struct cb_watch_generic *watch =
		g_hash_table_lookup (node_watch, node);
	const char *desc = watch ? watch->short_desc : NULL;
	return desc ? _(desc) : NULL;
}

/**
 * gnm_conf_get_long_desc:
 * @node: #GOConfNode
 *
 * Returns: (transfer none) (nullable): a description of @node.
 */
char const *
gnm_conf_get_long_desc (GOConfNode *node)
{
	struct cb_watch_generic *watch =
		g_hash_table_lookup (node_watch, node);
	const char *desc = watch ? watch->long_desc : NULL;
	return desc ? _(desc) : NULL;
}

/* -------------------------------------------------------------------------- */

struct cb_watch_bool {
	guint handler;
	const char *key;
	const char *short_desc;
	const char *long_desc;
	gboolean defalt;
	gboolean var;
};

static void
cb_watch_bool (GOConfNode *node, G_GNUC_UNUSED const char *key, gpointer user)
{
	struct cb_watch_bool *watch = user;
	watch->var = go_conf_load_bool (node, NULL, watch->defalt);
}

static void
watch_bool (struct cb_watch_bool *watch)
{
	GOConfNode *node = get_node (watch->key, watch);
	watch->handler = go_conf_add_monitor
		(node, NULL, cb_watch_bool, watch);
	watchers = g_slist_prepend (watchers, watch);
	cb_watch_bool (node, NULL, watch);
	MAYBE_DEBUG_GET (watch->key);
}

static void
set_bool (struct cb_watch_bool *watch, gboolean x)
{
	x = (x != FALSE);
	if (x == watch->var)
		return;

	MAYBE_DEBUG_SET (watch->key);
	watch->var = x;
	if (persist_changes) {
		go_conf_set_bool (root, watch->key, x);
		schedule_sync ();
	}
}

/* ---------------------------------------- */

struct cb_watch_int {
	guint handler;
	const char *key;
	const char *short_desc;
	const char *long_desc;
	int min, max, defalt;
	int var;
};

static void
cb_watch_int (GOConfNode *node, G_GNUC_UNUSED const char *key, gpointer user)
{
	struct cb_watch_int *watch = user;
	watch->var = go_conf_load_int (node, NULL,
				       watch->min, watch->max,
				       watch->defalt);
}

static void
watch_int (struct cb_watch_int *watch)
{
	GOConfNode *node = get_node (watch->key, watch);
	watch->handler = go_conf_add_monitor
		(node, NULL, cb_watch_int, watch);
	watchers = g_slist_prepend (watchers, watch);
	cb_watch_int (node, NULL, watch);
	MAYBE_DEBUG_GET (watch->key);
}

static void
set_int (struct cb_watch_int *watch, int x)
{
	x = CLAMP (x, watch->min, watch->max);

	if (x == watch->var)
		return;

	MAYBE_DEBUG_SET (watch->key);
	watch->var = x;
	if (persist_changes) {
		go_conf_set_int (root, watch->key, x);
		schedule_sync ();
	}
}

/* ---------------------------------------- */

struct cb_watch_double {
	guint handler;
	const char *key;
	const char *short_desc;
	const char *long_desc;
	double min, max, defalt;
	double var;
};

static void
cb_watch_double (GOConfNode *node, G_GNUC_UNUSED const char *key, gpointer user)
{
	struct cb_watch_double *watch = user;
	watch->var = go_conf_load_double (node, NULL,
					  watch->min, watch->max,
					  watch->defalt);
}

static void
watch_double (struct cb_watch_double *watch)
{
	GOConfNode *node = get_node (watch->key, watch);
	watch->handler = go_conf_add_monitor
		(node, NULL, cb_watch_double, watch);
	watchers = g_slist_prepend (watchers, watch);
	cb_watch_double (node, NULL, watch);
	MAYBE_DEBUG_GET (watch->key);
}

static void
set_double (struct cb_watch_double *watch, double x)
{
	x = CLAMP (x, watch->min, watch->max);

	if (x == watch->var)
		return;

	MAYBE_DEBUG_SET (watch->key);
	watch->var = x;
	if (persist_changes) {
		go_conf_set_double (root, watch->key, x);
		schedule_sync ();
	}
}

/* ---------------------------------------- */

struct cb_watch_string {
	guint handler;
	const char *key;
	const char *short_desc;
	const char *long_desc;
	const char *defalt;
	const char *var;
};

static void
cb_watch_string (GOConfNode *node, G_GNUC_UNUSED const char *key, gpointer user)
{
	struct cb_watch_string *watch = user;
	char *res = go_conf_load_string (node, NULL);
	if (!res) res = g_strdup (watch->defalt);
	g_hash_table_replace (string_pool, (gpointer)watch->key, res);
	watch->var = res;
}

static void
watch_string (struct cb_watch_string *watch)
{
	GOConfNode *node = get_node (watch->key, watch);
	watch->handler = go_conf_add_monitor
		(node, NULL, cb_watch_string, watch);
	watchers = g_slist_prepend (watchers, watch);
	cb_watch_string (node, NULL, watch);
	MAYBE_DEBUG_GET (watch->key);
}

static void
set_string (struct cb_watch_string *watch, const char *x)
{
	char *xc;

	if (!x || !watch->var || strcmp (x, watch->var) == 0)
		return;

	MAYBE_DEBUG_SET (watch->key);
	xc = g_strdup (x);
	watch->var = xc;
	/* Update pool before setting so monitors see the right value.  */
	g_hash_table_replace (string_pool, (gpointer)watch->key, xc);
	if (persist_changes) {
		go_conf_set_string (root, watch->key, xc);
		schedule_sync ();
	}
}

/* ---------------------------------------- */

struct cb_watch_string_list {
	guint handler;
	const char *key;
	const char *short_desc;
	const char *long_desc;
	GSList *var;
};

static void
cb_watch_string_list (GOConfNode *node, G_GNUC_UNUSED const char *key, gpointer user)
{
	struct cb_watch_string_list *watch = user;
	GSList *res = go_conf_load_str_list (node, NULL);
	g_hash_table_replace (string_list_pool, (gpointer)watch->key, res);
	watch->var = res;
}

static void
watch_string_list (struct cb_watch_string_list *watch)
{
	GOConfNode *node = get_node (watch->key, watch);
	watch->handler = go_conf_add_monitor
		(node, NULL, cb_watch_string_list, watch);
	watchers = g_slist_prepend (watchers, watch);
	cb_watch_string_list (node, NULL, watch);
	MAYBE_DEBUG_GET (watch->key);
}

static gboolean
string_list_equal (GSList *x, GSList *y)
{
	while (x && y) {
		if (strcmp (x->data, y->data) != 0)
			return FALSE;
		x = x->next;
		y = y->next;
	}

	return x == y;
}

static void
set_string_list (struct cb_watch_string_list *watch, GSList *x)
{
	if (string_list_equal (x, watch->var))
		return;

	x = go_string_slist_copy (x);

	MAYBE_DEBUG_SET (watch->key);
	watch->var = x;
	/* Update pool before setting so monitors see the right value.  */
	g_hash_table_replace (string_list_pool, (gpointer)watch->key, x);
	if (persist_changes) {
		go_conf_set_str_list (root, watch->key, x);
		schedule_sync ();
	}
}

/* ---------------------------------------- */

struct cb_watch_enum {
	guint handler;
	const char *key;
	const char *short_desc;
	const char *long_desc;
	int defalt;
	GType typ;
	int var;
};

static void
cb_watch_enum (GOConfNode *node, G_GNUC_UNUSED const char *key, gpointer user)
{
	struct cb_watch_enum *watch = user;
	watch->var = go_conf_load_enum (node, NULL,
					watch->typ, watch->defalt);
}

static void
watch_enum (struct cb_watch_enum *watch, GType typ)
{
	GOConfNode *node = get_node (watch->key, watch);
	watch->typ = typ;
	watch->handler = go_conf_add_monitor
		(node, NULL, cb_watch_enum, watch);
	watchers = g_slist_prepend (watchers, watch);
	cb_watch_enum (node, NULL, watch);
	MAYBE_DEBUG_GET (watch->key);
}

static void
set_enum (struct cb_watch_enum *watch, int x)
{
	if (x == watch->var)
		return;

	MAYBE_DEBUG_SET (watch->key);
	watch->var = x;
	if (persist_changes) {
		go_conf_set_enum (root, watch->key, watch->typ, x);
		schedule_sync ();
	}
}

/* -------------------------------------------------------------------------- */

static void
cb_free_string_list (GSList *l)
{
	g_slist_free_full (l, g_free);
}

/**
 * gnm_conf_init: (skip)
 */
void
gnm_conf_init (void)
{
	debug_getters = gnm_debug_flag ("conf-get");
	debug_setters = gnm_debug_flag ("conf-set");

	if (debug_getters || debug_setters)
		g_printerr ("gnm_conf_init\n");

	string_pool = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 NULL, g_free);
	string_list_pool = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 NULL, (GDestroyNotify)cb_free_string_list);
	node_pool = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 NULL, (GDestroyNotify)go_conf_free_node);
	node_watch = g_hash_table_new (g_direct_hash, g_direct_equal);

	root = go_conf_get_node (NULL, GNM_CONF_DIR);
	g_hash_table_insert (node_pool, (gpointer)"/", root);
}

/**
 * gnm_conf_shutdown: (skip)
 */
void
gnm_conf_shutdown (void)
{
	if (debug_getters || debug_setters)
		g_printerr ("gnm_conf_shutdown\n");

	//go_conf_sync (root);
	if (sync_handler) {
		g_source_remove (sync_handler);
		sync_handler = 0;
	}

	g_slist_free_full (watchers, (GDestroyNotify)free_watcher);
	watchers = NULL;

	g_hash_table_destroy (string_pool);
	string_pool = NULL;

	g_hash_table_destroy (string_list_pool);
	string_list_pool = NULL;

	g_hash_table_destroy (node_watch);
	node_watch = NULL;

	g_hash_table_destroy (node_pool);
	node_pool = NULL;

	root = NULL;
}

/**
 * gnm_conf_set_persistence:
 * @persist: whether to save changes
 *
 * If @persist is %TRUE, then changes from this point on will not be saved.
 */
void
gnm_conf_set_persistence (gboolean persist)
{
	persist_changes = persist;
}


/**
 * gnm_conf_get_page_setup:
 *
 * Returns: (transfer full): the default #GtkPageSetup.
 **/
GtkPageSetup *
gnm_conf_get_page_setup (void)
{
	GtkPageSetup *page_setup = gtk_page_setup_new ();

	page_setup_set_paper (page_setup,
			      gnm_conf_get_printsetup_paper ());

	gtk_page_setup_set_orientation
		(page_setup,
		 gnm_conf_get_printsetup_paper_orientation ());

	gtk_page_setup_set_top_margin
		(page_setup,
		 gnm_conf_get_printsetup_margin_gtk_top (),
		 GTK_UNIT_POINTS);
	gtk_page_setup_set_bottom_margin
		(page_setup,
		 gnm_conf_get_printsetup_margin_gtk_bottom (),
		 GTK_UNIT_POINTS);
	gtk_page_setup_set_left_margin
		(page_setup,
		 gnm_conf_get_printsetup_margin_gtk_left (),
		 GTK_UNIT_POINTS);
	gtk_page_setup_set_right_margin
		(page_setup,
		 gnm_conf_get_printsetup_margin_gtk_right (),
		 GTK_UNIT_POINTS);

	return page_setup;
}

void
gnm_conf_set_page_setup (GtkPageSetup *setup)
{
	char *paper;

	paper = page_setup_get_paper (setup);
	gnm_conf_set_printsetup_paper (paper);
	g_free (paper);

	gnm_conf_set_printsetup_paper_orientation
		(gtk_page_setup_get_orientation (setup));

	gnm_conf_set_printsetup_margin_gtk_top
		(gtk_page_setup_get_top_margin (setup, GTK_UNIT_POINTS));
	gnm_conf_set_printsetup_margin_gtk_bottom
		(gtk_page_setup_get_bottom_margin (setup, GTK_UNIT_POINTS));
	gnm_conf_set_printsetup_margin_gtk_left
		(gtk_page_setup_get_left_margin (setup, GTK_UNIT_POINTS));
	gnm_conf_set_printsetup_margin_gtk_right
		(gtk_page_setup_get_right_margin (setup, GTK_UNIT_POINTS));
}

/**
 * gnm_conf_get_printer_decoration_font:
 *
 * Returns: (transfer full): a style appropriate font for headers and
 * footers.
 */
GnmStyle *
gnm_conf_get_printer_decoration_font (void)
{
	GnmStyle *style = gnm_style_new ();

	gnm_style_set_font_name (style,
				 gnm_conf_get_printsetup_hf_font_name ());
	gnm_style_set_font_size (style,
				 gnm_conf_get_printsetup_hf_font_size ());
	gnm_style_set_font_bold (style,
				 gnm_conf_get_printsetup_hf_font_bold ());
	gnm_style_set_font_italic (style,
				   gnm_conf_get_printsetup_hf_font_italic ());

	return style;
}

#define TOOLBAR_TANGO(Object,Format,Standard)	\
	if (strcmp (name, "ObjectToolbar") == 0)		\
		Object						\
	else if (strcmp (name, "FormatToolbar") == 0)		\
		Format						\
	else if (strcmp (name, "StandardToolbar") == 0)		\
		Standard


gboolean
gnm_conf_get_toolbar_visible (const char *name)
{
	TOOLBAR_TANGO
		(return gnm_conf_get_core_gui_toolbars_object_visible ();,
		 return gnm_conf_get_core_gui_toolbars_format_visible ();,
		 return gnm_conf_get_core_gui_toolbars_standard_visible (););

	g_warning ("Unknown toolbar: %s", name);
	return FALSE;
}

void
gnm_conf_set_toolbar_visible (const char *name, gboolean x)
{
	TOOLBAR_TANGO
		(gnm_conf_set_core_gui_toolbars_object_visible (x);,
		 gnm_conf_set_core_gui_toolbars_format_visible (x);,
		 gnm_conf_set_core_gui_toolbars_standard_visible (x););
}

GtkPositionType
gnm_conf_get_toolbar_position (const char *name)
{
	TOOLBAR_TANGO
		(return gnm_conf_get_core_gui_toolbars_object_position ();,
		 return gnm_conf_get_core_gui_toolbars_format_position ();,
		 return gnm_conf_get_core_gui_toolbars_standard_position (););

	g_warning ("Unknown toolbar: %s", name);
	return GTK_POS_TOP;
}

void
gnm_conf_set_toolbar_position (const char *name, GtkPositionType x)
{
	TOOLBAR_TANGO
		(gnm_conf_set_core_gui_toolbars_object_position (x);,
		 gnm_conf_set_core_gui_toolbars_format_position (x);,
		 gnm_conf_set_core_gui_toolbars_standard_position (x););
}

#undef TOOLBAR_TANGO

/**
 * gnm_conf_get_print_settings:
 *
 * Returns: (transfer full): the default #GtkPrintSettings.
 **/
GtkPrintSettings *
gnm_conf_get_print_settings (void)
{
	GtkPrintSettings *settings =  gtk_print_settings_new ();
	GSList *list = gnm_conf_get_printsetup_gtk_setting ();

	while (list && list->next) {
		/* For historical reasons, value comes before key. */
		const char *value = list->data;
		const char *key = list->next->data;

		list = list->next->next;
		gtk_print_settings_set (settings, key, value);
	}

	return settings;
}

static void
gnm_gconf_set_print_settings_cb (const gchar *key, const gchar *value, gpointer user_data)
{
	GSList **list = user_data;

	/* For historical reasons, value comes before key. */
	*list = g_slist_prepend (*list, g_strdup (key));
	*list = g_slist_prepend (*list, g_strdup (value));
}

void
gnm_conf_set_print_settings (GtkPrintSettings *settings)
{
	GSList *list = NULL;

	gtk_print_settings_foreach (settings, gnm_gconf_set_print_settings_cb, &list);
	gnm_conf_set_printsetup_gtk_setting (list);
	g_slist_free_full (list, g_free);
}

gboolean
gnm_conf_get_detachable_toolbars (void)
{
#ifdef WIN32
	return FALSE;
#else
	return go_conf_get_bool
		(NULL,
		 "/desktop/interface/toolbar_detachable");
#endif
}

/* ------------------------------------------------------------------------- */
/*
 * The following code was generated by running
 *
 *     make update-gnumeric-conf
 */

/* ----------- AUTOMATICALLY GENERATED CODE BELOW -- DO NOT EDIT ----------- */

static struct cb_watch_bool watch_autocorrect_first_letter = {
	0, "autocorrect/first-letter",
	"Autocorrect first letter",
	"This variable determines whether to autocorrect first letters",
	TRUE,
};

gboolean
gnm_conf_get_autocorrect_first_letter (void)
{
	if (!watch_autocorrect_first_letter.handler)
		watch_bool (&watch_autocorrect_first_letter);
	return watch_autocorrect_first_letter.var;
}

void
gnm_conf_set_autocorrect_first_letter (gboolean x)
{
	if (!watch_autocorrect_first_letter.handler)
		watch_bool (&watch_autocorrect_first_letter);
	set_bool (&watch_autocorrect_first_letter, x);
}

/**
 * gnm_conf_get_autocorrect_first_letter_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autocorrect_first_letter_node (void)
{
	return get_watch_node (&watch_autocorrect_first_letter);
}

static struct cb_watch_string_list watch_autocorrect_first_letter_list = {
	0, "autocorrect/first-letter-list",
	"List of First Letter Exception",
	"The autocorrect engine does not capitalize the first letter of words following strings in this list.",
};

/**
 * gnm_conf_get_autocorrect_first_letter_list:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_autocorrect_first_letter_list (void)
{
	if (!watch_autocorrect_first_letter_list.handler)
		watch_string_list (&watch_autocorrect_first_letter_list);
	return watch_autocorrect_first_letter_list.var;
}

/**
 * gnm_conf_set_autocorrect_first_letter_list:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_autocorrect_first_letter_list (GSList *x)
{
	if (!watch_autocorrect_first_letter_list.handler)
		watch_string_list (&watch_autocorrect_first_letter_list);
	set_string_list (&watch_autocorrect_first_letter_list, x);
}

/**
 * gnm_conf_get_autocorrect_first_letter_list_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autocorrect_first_letter_list_node (void)
{
	return get_watch_node (&watch_autocorrect_first_letter_list);
}

static struct cb_watch_bool watch_autocorrect_init_caps = {
	0, "autocorrect/init-caps",
	"Autocorrect initial caps",
	"This variable determines whether to autocorrect initial caps",
	TRUE,
};

gboolean
gnm_conf_get_autocorrect_init_caps (void)
{
	if (!watch_autocorrect_init_caps.handler)
		watch_bool (&watch_autocorrect_init_caps);
	return watch_autocorrect_init_caps.var;
}

void
gnm_conf_set_autocorrect_init_caps (gboolean x)
{
	if (!watch_autocorrect_init_caps.handler)
		watch_bool (&watch_autocorrect_init_caps);
	set_bool (&watch_autocorrect_init_caps, x);
}

/**
 * gnm_conf_get_autocorrect_init_caps_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autocorrect_init_caps_node (void)
{
	return get_watch_node (&watch_autocorrect_init_caps);
}

static struct cb_watch_string_list watch_autocorrect_init_caps_list = {
	0, "autocorrect/init-caps-list",
	"List of initial caps exceptions",
	"The autocorrect engine does not correct the initial caps for words in this list.",
};

/**
 * gnm_conf_get_autocorrect_init_caps_list:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_autocorrect_init_caps_list (void)
{
	if (!watch_autocorrect_init_caps_list.handler)
		watch_string_list (&watch_autocorrect_init_caps_list);
	return watch_autocorrect_init_caps_list.var;
}

/**
 * gnm_conf_set_autocorrect_init_caps_list:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_autocorrect_init_caps_list (GSList *x)
{
	if (!watch_autocorrect_init_caps_list.handler)
		watch_string_list (&watch_autocorrect_init_caps_list);
	set_string_list (&watch_autocorrect_init_caps_list, x);
}

/**
 * gnm_conf_get_autocorrect_init_caps_list_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autocorrect_init_caps_list_node (void)
{
	return get_watch_node (&watch_autocorrect_init_caps_list);
}

static struct cb_watch_bool watch_autocorrect_names_of_days = {
	0, "autocorrect/names-of-days",
	"Autocorrect names of days",
	"This variable determines whether to autocorrect names of days",
	TRUE,
};

gboolean
gnm_conf_get_autocorrect_names_of_days (void)
{
	if (!watch_autocorrect_names_of_days.handler)
		watch_bool (&watch_autocorrect_names_of_days);
	return watch_autocorrect_names_of_days.var;
}

void
gnm_conf_set_autocorrect_names_of_days (gboolean x)
{
	if (!watch_autocorrect_names_of_days.handler)
		watch_bool (&watch_autocorrect_names_of_days);
	set_bool (&watch_autocorrect_names_of_days, x);
}

/**
 * gnm_conf_get_autocorrect_names_of_days_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autocorrect_names_of_days_node (void)
{
	return get_watch_node (&watch_autocorrect_names_of_days);
}

static struct cb_watch_bool watch_autocorrect_replace = {
	0, "autocorrect/replace",
	"Autocorrect replace",
	"Autocorrect replace",
	TRUE,
};

gboolean
gnm_conf_get_autocorrect_replace (void)
{
	if (!watch_autocorrect_replace.handler)
		watch_bool (&watch_autocorrect_replace);
	return watch_autocorrect_replace.var;
}

void
gnm_conf_set_autocorrect_replace (gboolean x)
{
	if (!watch_autocorrect_replace.handler)
		watch_bool (&watch_autocorrect_replace);
	set_bool (&watch_autocorrect_replace, x);
}

/**
 * gnm_conf_get_autocorrect_replace_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autocorrect_replace_node (void)
{
	return get_watch_node (&watch_autocorrect_replace);
}

static struct cb_watch_string_list watch_autoformat_extra_dirs = {
	0, "autoformat/extra-dirs",
	"List of Extra Autoformat Directories.",
	"This list contains all extra directories containing autoformat templates.",
};

/**
 * gnm_conf_get_autoformat_extra_dirs:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_autoformat_extra_dirs (void)
{
	if (!watch_autoformat_extra_dirs.handler)
		watch_string_list (&watch_autoformat_extra_dirs);
	return watch_autoformat_extra_dirs.var;
}

/**
 * gnm_conf_set_autoformat_extra_dirs:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_autoformat_extra_dirs (GSList *x)
{
	if (!watch_autoformat_extra_dirs.handler)
		watch_string_list (&watch_autoformat_extra_dirs);
	set_string_list (&watch_autoformat_extra_dirs, x);
}

/**
 * gnm_conf_get_autoformat_extra_dirs_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autoformat_extra_dirs_node (void)
{
	return get_watch_node (&watch_autoformat_extra_dirs);
}

static struct cb_watch_string watch_autoformat_sys_dir = {
	0, "autoformat/sys-dir",
	"System Directory for Autoformats",
	"This directory contains the pre-installed autoformat templates.",
	"autoformat-templates",
};

const char *
gnm_conf_get_autoformat_sys_dir (void)
{
	if (!watch_autoformat_sys_dir.handler)
		watch_string (&watch_autoformat_sys_dir);
	return watch_autoformat_sys_dir.var;
}

void
gnm_conf_set_autoformat_sys_dir (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_autoformat_sys_dir.handler)
		watch_string (&watch_autoformat_sys_dir);
	set_string (&watch_autoformat_sys_dir, x);
}

/**
 * gnm_conf_get_autoformat_sys_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autoformat_sys_dir_node (void)
{
	return get_watch_node (&watch_autoformat_sys_dir);
}

static struct cb_watch_string watch_autoformat_usr_dir = {
	0, "autoformat/usr-dir",
	"User Directory for Autoformats",
	"The main directory for user specific autoformat templates.",
	"autoformat-templates",
};

const char *
gnm_conf_get_autoformat_usr_dir (void)
{
	if (!watch_autoformat_usr_dir.handler)
		watch_string (&watch_autoformat_usr_dir);
	return watch_autoformat_usr_dir.var;
}

void
gnm_conf_set_autoformat_usr_dir (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_autoformat_usr_dir.handler)
		watch_string (&watch_autoformat_usr_dir);
	set_string (&watch_autoformat_usr_dir, x);
}

/**
 * gnm_conf_get_autoformat_usr_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autoformat_usr_dir_node (void)
{
	return get_watch_node (&watch_autoformat_usr_dir);
}

static struct cb_watch_bool watch_core_defaultfont_bold = {
	0, "core/defaultfont/bold",
	"The default font is bold.",
	"This value determines whether the default font for a new workbook is bold.",
	FALSE,
};

gboolean
gnm_conf_get_core_defaultfont_bold (void)
{
	if (!watch_core_defaultfont_bold.handler)
		watch_bool (&watch_core_defaultfont_bold);
	return watch_core_defaultfont_bold.var;
}

void
gnm_conf_set_core_defaultfont_bold (gboolean x)
{
	if (!watch_core_defaultfont_bold.handler)
		watch_bool (&watch_core_defaultfont_bold);
	set_bool (&watch_core_defaultfont_bold, x);
}

/**
 * gnm_conf_get_core_defaultfont_bold_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_defaultfont_bold_node (void)
{
	return get_watch_node (&watch_core_defaultfont_bold);
}

static struct cb_watch_bool watch_core_defaultfont_italic = {
	0, "core/defaultfont/italic",
	"The default font is italic.",
	"This value determines whether the default font for a new workbook is italic.",
	FALSE,
};

gboolean
gnm_conf_get_core_defaultfont_italic (void)
{
	if (!watch_core_defaultfont_italic.handler)
		watch_bool (&watch_core_defaultfont_italic);
	return watch_core_defaultfont_italic.var;
}

void
gnm_conf_set_core_defaultfont_italic (gboolean x)
{
	if (!watch_core_defaultfont_italic.handler)
		watch_bool (&watch_core_defaultfont_italic);
	set_bool (&watch_core_defaultfont_italic, x);
}

/**
 * gnm_conf_get_core_defaultfont_italic_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_defaultfont_italic_node (void)
{
	return get_watch_node (&watch_core_defaultfont_italic);
}

static struct cb_watch_string watch_core_defaultfont_name = {
	0, "core/defaultfont/name",
	"Default font name",
	"The default font name for new workbooks.",
	"Sans",
};

const char *
gnm_conf_get_core_defaultfont_name (void)
{
	if (!watch_core_defaultfont_name.handler)
		watch_string (&watch_core_defaultfont_name);
	return watch_core_defaultfont_name.var;
}

void
gnm_conf_set_core_defaultfont_name (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_core_defaultfont_name.handler)
		watch_string (&watch_core_defaultfont_name);
	set_string (&watch_core_defaultfont_name, x);
}

/**
 * gnm_conf_get_core_defaultfont_name_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_defaultfont_name_node (void)
{
	return get_watch_node (&watch_core_defaultfont_name);
}

static struct cb_watch_double watch_core_defaultfont_size = {
	0, "core/defaultfont/size",
	"Default Font Size",
	"The default font size for new workbooks.",
	1, 100, 10,
};

double
gnm_conf_get_core_defaultfont_size (void)
{
	if (!watch_core_defaultfont_size.handler)
		watch_double (&watch_core_defaultfont_size);
	return watch_core_defaultfont_size.var;
}

void
gnm_conf_set_core_defaultfont_size (double x)
{
	if (!watch_core_defaultfont_size.handler)
		watch_double (&watch_core_defaultfont_size);
	set_double (&watch_core_defaultfont_size, x);
}

/**
 * gnm_conf_get_core_defaultfont_size_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_defaultfont_size_node (void)
{
	return get_watch_node (&watch_core_defaultfont_size);
}

static struct cb_watch_bool watch_core_file_save_def_overwrite = {
	0, "core/file/save/def-overwrite",
	"Default To Overwriting Files",
	"Before an existing file is being overwritten, Gnumeric will present a warning dialog. Setting this option will make the overwrite button in that dialog the default button.",
	FALSE,
};

gboolean
gnm_conf_get_core_file_save_def_overwrite (void)
{
	if (!watch_core_file_save_def_overwrite.handler)
		watch_bool (&watch_core_file_save_def_overwrite);
	return watch_core_file_save_def_overwrite.var;
}

void
gnm_conf_set_core_file_save_def_overwrite (gboolean x)
{
	if (!watch_core_file_save_def_overwrite.handler)
		watch_bool (&watch_core_file_save_def_overwrite);
	set_bool (&watch_core_file_save_def_overwrite, x);
}

/**
 * gnm_conf_get_core_file_save_def_overwrite_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_file_save_def_overwrite_node (void)
{
	return get_watch_node (&watch_core_file_save_def_overwrite);
}

static struct cb_watch_string_list watch_core_file_save_extension_check_disabled = {
	0, "core/file/save/extension-check-disabled",
	"List of file savers with disabled extension check.",
	"This list contains the ids of the file savers for which the extension check is disabled.",
};

/**
 * gnm_conf_get_core_file_save_extension_check_disabled:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_core_file_save_extension_check_disabled (void)
{
	if (!watch_core_file_save_extension_check_disabled.handler)
		watch_string_list (&watch_core_file_save_extension_check_disabled);
	return watch_core_file_save_extension_check_disabled.var;
}

/**
 * gnm_conf_set_core_file_save_extension_check_disabled:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_core_file_save_extension_check_disabled (GSList *x)
{
	if (!watch_core_file_save_extension_check_disabled.handler)
		watch_string_list (&watch_core_file_save_extension_check_disabled);
	set_string_list (&watch_core_file_save_extension_check_disabled, x);
}

/**
 * gnm_conf_get_core_file_save_extension_check_disabled_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_file_save_extension_check_disabled_node (void)
{
	return get_watch_node (&watch_core_file_save_extension_check_disabled);
}

static struct cb_watch_bool watch_core_file_save_single_sheet = {
	0, "core/file/save/single-sheet",
	"Warn When Exporting Into Single Sheet Format",
	"Some file formats can contain only a single sheet. This variable determines whether the user will be warned if only a single sheet of a multi-sheet workbook is being saved.",
	TRUE,
};

gboolean
gnm_conf_get_core_file_save_single_sheet (void)
{
	if (!watch_core_file_save_single_sheet.handler)
		watch_bool (&watch_core_file_save_single_sheet);
	return watch_core_file_save_single_sheet.var;
}

void
gnm_conf_set_core_file_save_single_sheet (gboolean x)
{
	if (!watch_core_file_save_single_sheet.handler)
		watch_bool (&watch_core_file_save_single_sheet);
	set_bool (&watch_core_file_save_single_sheet, x);
}

/**
 * gnm_conf_get_core_file_save_single_sheet_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_file_save_single_sheet_node (void)
{
	return get_watch_node (&watch_core_file_save_single_sheet);
}

static struct cb_watch_bool watch_core_gui_cells_extension_markers = {
	0, "core/gui/cells/extension-markers",
	"Extension Markers",
	"This variable determines whether cells with truncated content are marked.",
	FALSE,
};

gboolean
gnm_conf_get_core_gui_cells_extension_markers (void)
{
	if (!watch_core_gui_cells_extension_markers.handler)
		watch_bool (&watch_core_gui_cells_extension_markers);
	return watch_core_gui_cells_extension_markers.var;
}

void
gnm_conf_set_core_gui_cells_extension_markers (gboolean x)
{
	if (!watch_core_gui_cells_extension_markers.handler)
		watch_bool (&watch_core_gui_cells_extension_markers);
	set_bool (&watch_core_gui_cells_extension_markers, x);
}

/**
 * gnm_conf_get_core_gui_cells_extension_markers_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_cells_extension_markers_node (void)
{
	return get_watch_node (&watch_core_gui_cells_extension_markers);
}

static struct cb_watch_bool watch_core_gui_cells_function_markers = {
	0, "core/gui/cells/function-markers",
	"Function Markers",
	"This variable determines whether cells containing spreadsheet function are marked.",
	FALSE,
};

gboolean
gnm_conf_get_core_gui_cells_function_markers (void)
{
	if (!watch_core_gui_cells_function_markers.handler)
		watch_bool (&watch_core_gui_cells_function_markers);
	return watch_core_gui_cells_function_markers.var;
}

void
gnm_conf_set_core_gui_cells_function_markers (gboolean x)
{
	if (!watch_core_gui_cells_function_markers.handler)
		watch_bool (&watch_core_gui_cells_function_markers);
	set_bool (&watch_core_gui_cells_function_markers, x);
}

/**
 * gnm_conf_get_core_gui_cells_function_markers_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_cells_function_markers_node (void)
{
	return get_watch_node (&watch_core_gui_cells_function_markers);
}

static struct cb_watch_bool watch_core_gui_editing_autocomplete = {
	0, "core/gui/editing/autocomplete",
	"Autocomplete",
	"This variable determines whether autocompletion is set on.",
	TRUE,
};

gboolean
gnm_conf_get_core_gui_editing_autocomplete (void)
{
	if (!watch_core_gui_editing_autocomplete.handler)
		watch_bool (&watch_core_gui_editing_autocomplete);
	return watch_core_gui_editing_autocomplete.var;
}

void
gnm_conf_set_core_gui_editing_autocomplete (gboolean x)
{
	if (!watch_core_gui_editing_autocomplete.handler)
		watch_bool (&watch_core_gui_editing_autocomplete);
	set_bool (&watch_core_gui_editing_autocomplete, x);
}

/**
 * gnm_conf_get_core_gui_editing_autocomplete_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_editing_autocomplete_node (void)
{
	return get_watch_node (&watch_core_gui_editing_autocomplete);
}

static struct cb_watch_int watch_core_gui_editing_autocomplete_min_chars = {
	0, "core/gui/editing/autocomplete-min-chars",
	"Minimum Number of Characters for Autocompletion",
	"This variable determines the minimum number of characters required for autocompletion.",
	1, 10, 3,
};

int
gnm_conf_get_core_gui_editing_autocomplete_min_chars (void)
{
	if (!watch_core_gui_editing_autocomplete_min_chars.handler)
		watch_int (&watch_core_gui_editing_autocomplete_min_chars);
	return watch_core_gui_editing_autocomplete_min_chars.var;
}

void
gnm_conf_set_core_gui_editing_autocomplete_min_chars (int x)
{
	if (!watch_core_gui_editing_autocomplete_min_chars.handler)
		watch_int (&watch_core_gui_editing_autocomplete_min_chars);
	set_int (&watch_core_gui_editing_autocomplete_min_chars, x);
}

/**
 * gnm_conf_get_core_gui_editing_autocomplete_min_chars_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_editing_autocomplete_min_chars_node (void)
{
	return get_watch_node (&watch_core_gui_editing_autocomplete_min_chars);
}

static struct cb_watch_enum watch_core_gui_editing_enter_moves_dir = {
	0, "core/gui/editing/enter-moves-dir",
	"Enter Direction",
	"Which direction pressing Enter will move the edit position.",
	GO_DIRECTION_DOWN,
};

GODirection
gnm_conf_get_core_gui_editing_enter_moves_dir (void)
{
	if (!watch_core_gui_editing_enter_moves_dir.handler)
		watch_enum (&watch_core_gui_editing_enter_moves_dir, GO_TYPE_DIRECTION);
	return watch_core_gui_editing_enter_moves_dir.var;
}

void
gnm_conf_set_core_gui_editing_enter_moves_dir (GODirection x)
{
	if (!watch_core_gui_editing_enter_moves_dir.handler)
		watch_enum (&watch_core_gui_editing_enter_moves_dir, GO_TYPE_DIRECTION);
	set_enum (&watch_core_gui_editing_enter_moves_dir, x);
}

/**
 * gnm_conf_get_core_gui_editing_enter_moves_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_editing_enter_moves_dir_node (void)
{
	return get_watch_node (&watch_core_gui_editing_enter_moves_dir);
}

static struct cb_watch_bool watch_core_gui_editing_function_argument_tooltips = {
	0, "core/gui/editing/function-argument-tooltips",
	"Show Function Argument Tooltips",
	"This variable determines whether to show function argument tooltips.",
	TRUE,
};

gboolean
gnm_conf_get_core_gui_editing_function_argument_tooltips (void)
{
	if (!watch_core_gui_editing_function_argument_tooltips.handler)
		watch_bool (&watch_core_gui_editing_function_argument_tooltips);
	return watch_core_gui_editing_function_argument_tooltips.var;
}

void
gnm_conf_set_core_gui_editing_function_argument_tooltips (gboolean x)
{
	if (!watch_core_gui_editing_function_argument_tooltips.handler)
		watch_bool (&watch_core_gui_editing_function_argument_tooltips);
	set_bool (&watch_core_gui_editing_function_argument_tooltips, x);
}

/**
 * gnm_conf_get_core_gui_editing_function_argument_tooltips_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_editing_function_argument_tooltips_node (void)
{
	return get_watch_node (&watch_core_gui_editing_function_argument_tooltips);
}

static struct cb_watch_bool watch_core_gui_editing_function_name_tooltips = {
	0, "core/gui/editing/function-name-tooltips",
	"Show Function Name Tooltips",
	"This variable determines whether to show function name tooltips.",
	TRUE,
};

gboolean
gnm_conf_get_core_gui_editing_function_name_tooltips (void)
{
	if (!watch_core_gui_editing_function_name_tooltips.handler)
		watch_bool (&watch_core_gui_editing_function_name_tooltips);
	return watch_core_gui_editing_function_name_tooltips.var;
}

void
gnm_conf_set_core_gui_editing_function_name_tooltips (gboolean x)
{
	if (!watch_core_gui_editing_function_name_tooltips.handler)
		watch_bool (&watch_core_gui_editing_function_name_tooltips);
	set_bool (&watch_core_gui_editing_function_name_tooltips, x);
}

/**
 * gnm_conf_get_core_gui_editing_function_name_tooltips_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_editing_function_name_tooltips_node (void)
{
	return get_watch_node (&watch_core_gui_editing_function_name_tooltips);
}

static struct cb_watch_int watch_core_gui_editing_recalclag = {
	0, "core/gui/editing/recalclag",
	"Auto Expression Recalculation Lag",
	"If `lag' is 0, Gnumeric recalculates all auto expressions immediately after every change.  Non-zero values of `lag' allow Gnumeric to accumulate more changes before each recalculation. If `lag' is positive, then whenever a change appears, Gnumeric waits `lag' milliseconds and then recalculates; if more changes appear during that period, they are also processed at that time. If `lag' is negative, then recalculation happens only after a quiet period of |lag| milliseconds.",
	-5000, 5000, 200,
};

int
gnm_conf_get_core_gui_editing_recalclag (void)
{
	if (!watch_core_gui_editing_recalclag.handler)
		watch_int (&watch_core_gui_editing_recalclag);
	return watch_core_gui_editing_recalclag.var;
}

void
gnm_conf_set_core_gui_editing_recalclag (int x)
{
	if (!watch_core_gui_editing_recalclag.handler)
		watch_int (&watch_core_gui_editing_recalclag);
	set_int (&watch_core_gui_editing_recalclag, x);
}

/**
 * gnm_conf_get_core_gui_editing_recalclag_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_editing_recalclag_node (void)
{
	return get_watch_node (&watch_core_gui_editing_recalclag);
}

static struct cb_watch_bool watch_core_gui_editing_transitionkeys = {
	0, "core/gui/editing/transitionkeys",
	"Transition Keys",
	"This variable determines whether transition keys are set on. Transition keys are a throw back to 1-2-3 style event handling. They turn Ctrl-arrow into page movement rather than jumping to the start/end of series.",
	FALSE,
};

gboolean
gnm_conf_get_core_gui_editing_transitionkeys (void)
{
	if (!watch_core_gui_editing_transitionkeys.handler)
		watch_bool (&watch_core_gui_editing_transitionkeys);
	return watch_core_gui_editing_transitionkeys.var;
}

void
gnm_conf_set_core_gui_editing_transitionkeys (gboolean x)
{
	if (!watch_core_gui_editing_transitionkeys.handler)
		watch_bool (&watch_core_gui_editing_transitionkeys);
	set_bool (&watch_core_gui_editing_transitionkeys, x);
}

/**
 * gnm_conf_get_core_gui_editing_transitionkeys_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_editing_transitionkeys_node (void)
{
	return get_watch_node (&watch_core_gui_editing_transitionkeys);
}

static struct cb_watch_double watch_core_gui_screen_horizontaldpi = {
	0, "core/gui/screen/horizontaldpi",
	"Horizontal DPI",
	"Screen resolution in the horizontal direction.",
	10, 1000, 96,
};

double
gnm_conf_get_core_gui_screen_horizontaldpi (void)
{
	if (!watch_core_gui_screen_horizontaldpi.handler)
		watch_double (&watch_core_gui_screen_horizontaldpi);
	return watch_core_gui_screen_horizontaldpi.var;
}

void
gnm_conf_set_core_gui_screen_horizontaldpi (double x)
{
	if (!watch_core_gui_screen_horizontaldpi.handler)
		watch_double (&watch_core_gui_screen_horizontaldpi);
	set_double (&watch_core_gui_screen_horizontaldpi, x);
}

/**
 * gnm_conf_get_core_gui_screen_horizontaldpi_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_screen_horizontaldpi_node (void)
{
	return get_watch_node (&watch_core_gui_screen_horizontaldpi);
}

static struct cb_watch_double watch_core_gui_screen_verticaldpi = {
	0, "core/gui/screen/verticaldpi",
	"Vertical DPI",
	"Screen resolution in the vertical direction.",
	10, 1000, 96,
};

double
gnm_conf_get_core_gui_screen_verticaldpi (void)
{
	if (!watch_core_gui_screen_verticaldpi.handler)
		watch_double (&watch_core_gui_screen_verticaldpi);
	return watch_core_gui_screen_verticaldpi.var;
}

void
gnm_conf_set_core_gui_screen_verticaldpi (double x)
{
	if (!watch_core_gui_screen_verticaldpi.handler)
		watch_double (&watch_core_gui_screen_verticaldpi);
	set_double (&watch_core_gui_screen_verticaldpi, x);
}

/**
 * gnm_conf_get_core_gui_screen_verticaldpi_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_screen_verticaldpi_node (void)
{
	return get_watch_node (&watch_core_gui_screen_verticaldpi);
}

static struct cb_watch_int watch_core_gui_toolbars_format_position = {
	0, "core/gui/toolbars/format-position",
	"Format toolbar position",
	"This variable determines where the format toolbar should be shown.  0 is left, 1 is right, 2 is top.",
	0, 3, 2,
};

GtkPositionType
gnm_conf_get_core_gui_toolbars_format_position (void)
{
	if (!watch_core_gui_toolbars_format_position.handler)
		watch_int (&watch_core_gui_toolbars_format_position);
	return watch_core_gui_toolbars_format_position.var;
}

void
gnm_conf_set_core_gui_toolbars_format_position (GtkPositionType x)
{
	if (!watch_core_gui_toolbars_format_position.handler)
		watch_int (&watch_core_gui_toolbars_format_position);
	set_int (&watch_core_gui_toolbars_format_position, x);
}

/**
 * gnm_conf_get_core_gui_toolbars_format_position_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_toolbars_format_position_node (void)
{
	return get_watch_node (&watch_core_gui_toolbars_format_position);
}

static struct cb_watch_bool watch_core_gui_toolbars_format_visible = {
	0, "core/gui/toolbars/format-visible",
	"Format toolbar visible",
	"This variable determines whether the format toolbar should be visible initially.",
	TRUE,
};

gboolean
gnm_conf_get_core_gui_toolbars_format_visible (void)
{
	if (!watch_core_gui_toolbars_format_visible.handler)
		watch_bool (&watch_core_gui_toolbars_format_visible);
	return watch_core_gui_toolbars_format_visible.var;
}

void
gnm_conf_set_core_gui_toolbars_format_visible (gboolean x)
{
	if (!watch_core_gui_toolbars_format_visible.handler)
		watch_bool (&watch_core_gui_toolbars_format_visible);
	set_bool (&watch_core_gui_toolbars_format_visible, x);
}

/**
 * gnm_conf_get_core_gui_toolbars_format_visible_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_toolbars_format_visible_node (void)
{
	return get_watch_node (&watch_core_gui_toolbars_format_visible);
}

static struct cb_watch_int watch_core_gui_toolbars_object_position = {
	0, "core/gui/toolbars/object-position",
	"Object toolbar position",
	"This variable determines where the object toolbar should be shown.  0 is left, 1 is right, 2 is top.",
	0, 3, 2,
};

GtkPositionType
gnm_conf_get_core_gui_toolbars_object_position (void)
{
	if (!watch_core_gui_toolbars_object_position.handler)
		watch_int (&watch_core_gui_toolbars_object_position);
	return watch_core_gui_toolbars_object_position.var;
}

void
gnm_conf_set_core_gui_toolbars_object_position (GtkPositionType x)
{
	if (!watch_core_gui_toolbars_object_position.handler)
		watch_int (&watch_core_gui_toolbars_object_position);
	set_int (&watch_core_gui_toolbars_object_position, x);
}

/**
 * gnm_conf_get_core_gui_toolbars_object_position_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_toolbars_object_position_node (void)
{
	return get_watch_node (&watch_core_gui_toolbars_object_position);
}

static struct cb_watch_bool watch_core_gui_toolbars_object_visible = {
	0, "core/gui/toolbars/object-visible",
	"Object toolbar visible",
	"This variable determines whether the object toolbar should be visible initially.",
	TRUE,
};

gboolean
gnm_conf_get_core_gui_toolbars_object_visible (void)
{
	if (!watch_core_gui_toolbars_object_visible.handler)
		watch_bool (&watch_core_gui_toolbars_object_visible);
	return watch_core_gui_toolbars_object_visible.var;
}

void
gnm_conf_set_core_gui_toolbars_object_visible (gboolean x)
{
	if (!watch_core_gui_toolbars_object_visible.handler)
		watch_bool (&watch_core_gui_toolbars_object_visible);
	set_bool (&watch_core_gui_toolbars_object_visible, x);
}

/**
 * gnm_conf_get_core_gui_toolbars_object_visible_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_toolbars_object_visible_node (void)
{
	return get_watch_node (&watch_core_gui_toolbars_object_visible);
}

static struct cb_watch_int watch_core_gui_toolbars_standard_position = {
	0, "core/gui/toolbars/standard-position",
	"Standard toolbar position",
	"This variable determines where the standard toolbar should be shown.  0 is left, 1 is right, 2 is top.",
	0, 3, 2,
};

GtkPositionType
gnm_conf_get_core_gui_toolbars_standard_position (void)
{
	if (!watch_core_gui_toolbars_standard_position.handler)
		watch_int (&watch_core_gui_toolbars_standard_position);
	return watch_core_gui_toolbars_standard_position.var;
}

void
gnm_conf_set_core_gui_toolbars_standard_position (GtkPositionType x)
{
	if (!watch_core_gui_toolbars_standard_position.handler)
		watch_int (&watch_core_gui_toolbars_standard_position);
	set_int (&watch_core_gui_toolbars_standard_position, x);
}

/**
 * gnm_conf_get_core_gui_toolbars_standard_position_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_toolbars_standard_position_node (void)
{
	return get_watch_node (&watch_core_gui_toolbars_standard_position);
}

static struct cb_watch_bool watch_core_gui_toolbars_standard_visible = {
	0, "core/gui/toolbars/standard-visible",
	"Standard toolbar visible",
	"This variable determines whether the standard toolbar should be visible initially.",
	TRUE,
};

gboolean
gnm_conf_get_core_gui_toolbars_standard_visible (void)
{
	if (!watch_core_gui_toolbars_standard_visible.handler)
		watch_bool (&watch_core_gui_toolbars_standard_visible);
	return watch_core_gui_toolbars_standard_visible.var;
}

void
gnm_conf_set_core_gui_toolbars_standard_visible (gboolean x)
{
	if (!watch_core_gui_toolbars_standard_visible.handler)
		watch_bool (&watch_core_gui_toolbars_standard_visible);
	set_bool (&watch_core_gui_toolbars_standard_visible, x);
}

/**
 * gnm_conf_get_core_gui_toolbars_standard_visible_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_toolbars_standard_visible_node (void)
{
	return get_watch_node (&watch_core_gui_toolbars_standard_visible);
}

static struct cb_watch_double watch_core_gui_window_x = {
	0, "core/gui/window/x",
	"Default Horizontal Window Size",
	"This number (between 0.25 and 1.00) gives the horizontal fraction of the screen size covered by the default window.",
	0.1, 1, 0.75,
};

double
gnm_conf_get_core_gui_window_x (void)
{
	if (!watch_core_gui_window_x.handler)
		watch_double (&watch_core_gui_window_x);
	return watch_core_gui_window_x.var;
}

void
gnm_conf_set_core_gui_window_x (double x)
{
	if (!watch_core_gui_window_x.handler)
		watch_double (&watch_core_gui_window_x);
	set_double (&watch_core_gui_window_x, x);
}

/**
 * gnm_conf_get_core_gui_window_x_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_window_x_node (void)
{
	return get_watch_node (&watch_core_gui_window_x);
}

static struct cb_watch_double watch_core_gui_window_y = {
	0, "core/gui/window/y",
	"Default Vertical Window Size",
	"This number (between 0.25 and 1.00) gives the vertical fraction of the screen size covered by the default window.",
	0.1, 1, 0.75,
};

double
gnm_conf_get_core_gui_window_y (void)
{
	if (!watch_core_gui_window_y.handler)
		watch_double (&watch_core_gui_window_y);
	return watch_core_gui_window_y.var;
}

void
gnm_conf_set_core_gui_window_y (double x)
{
	if (!watch_core_gui_window_y.handler)
		watch_double (&watch_core_gui_window_y);
	set_double (&watch_core_gui_window_y, x);
}

/**
 * gnm_conf_get_core_gui_window_y_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_window_y_node (void)
{
	return get_watch_node (&watch_core_gui_window_y);
}

static struct cb_watch_double watch_core_gui_window_zoom = {
	0, "core/gui/window/zoom",
	"Default Zoom Factor",
	"The initial zoom factor for new workbooks.",
	0.1, 5, 1,
};

double
gnm_conf_get_core_gui_window_zoom (void)
{
	if (!watch_core_gui_window_zoom.handler)
		watch_double (&watch_core_gui_window_zoom);
	return watch_core_gui_window_zoom.var;
}

void
gnm_conf_set_core_gui_window_zoom (double x)
{
	if (!watch_core_gui_window_zoom.handler)
		watch_double (&watch_core_gui_window_zoom);
	set_double (&watch_core_gui_window_zoom, x);
}

/**
 * gnm_conf_get_core_gui_window_zoom_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_window_zoom_node (void)
{
	return get_watch_node (&watch_core_gui_window_zoom);
}

static struct cb_watch_bool watch_core_sort_default_ascending = {
	0, "core/sort/default/ascending",
	"Sort Ascending",
	"This option determines the initial state of the sort-order button in the sort dialog.",
	TRUE,
};

gboolean
gnm_conf_get_core_sort_default_ascending (void)
{
	if (!watch_core_sort_default_ascending.handler)
		watch_bool (&watch_core_sort_default_ascending);
	return watch_core_sort_default_ascending.var;
}

void
gnm_conf_set_core_sort_default_ascending (gboolean x)
{
	if (!watch_core_sort_default_ascending.handler)
		watch_bool (&watch_core_sort_default_ascending);
	set_bool (&watch_core_sort_default_ascending, x);
}

/**
 * gnm_conf_get_core_sort_default_ascending_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_sort_default_ascending_node (void)
{
	return get_watch_node (&watch_core_sort_default_ascending);
}

static struct cb_watch_bool watch_core_sort_default_by_case = {
	0, "core/sort/default/by-case",
	"Sort is Case-Sensitive",
	"Setting this option will cause the sort buttons on the toolbar to perform a case-sensitive sort and determine the initial state of the case-sensitive checkbox in the sort dialog.",
	FALSE,
};

gboolean
gnm_conf_get_core_sort_default_by_case (void)
{
	if (!watch_core_sort_default_by_case.handler)
		watch_bool (&watch_core_sort_default_by_case);
	return watch_core_sort_default_by_case.var;
}

void
gnm_conf_set_core_sort_default_by_case (gboolean x)
{
	if (!watch_core_sort_default_by_case.handler)
		watch_bool (&watch_core_sort_default_by_case);
	set_bool (&watch_core_sort_default_by_case, x);
}

/**
 * gnm_conf_get_core_sort_default_by_case_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_sort_default_by_case_node (void)
{
	return get_watch_node (&watch_core_sort_default_by_case);
}

static struct cb_watch_bool watch_core_sort_default_retain_formats = {
	0, "core/sort/default/retain-formats",
	"Sorting Preserves Formats",
	"Setting this option will cause the sort buttons on the toolbar to preserve the cell formats while sorting and determines the initial state of the preserve-formats checkbox in the sort dialog.",
	TRUE,
};

gboolean
gnm_conf_get_core_sort_default_retain_formats (void)
{
	if (!watch_core_sort_default_retain_formats.handler)
		watch_bool (&watch_core_sort_default_retain_formats);
	return watch_core_sort_default_retain_formats.var;
}

void
gnm_conf_set_core_sort_default_retain_formats (gboolean x)
{
	if (!watch_core_sort_default_retain_formats.handler)
		watch_bool (&watch_core_sort_default_retain_formats);
	set_bool (&watch_core_sort_default_retain_formats, x);
}

/**
 * gnm_conf_get_core_sort_default_retain_formats_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_sort_default_retain_formats_node (void)
{
	return get_watch_node (&watch_core_sort_default_retain_formats);
}

static struct cb_watch_int watch_core_sort_dialog_max_initial_clauses = {
	0, "core/sort/dialog/max-initial-clauses",
	"Number of Automatic Clauses",
	"When selecting a sort region in the sort dialog, sort clauses are automatically added. This number determines the maximum number of clauses to be added automatically.",
	0, 256, 10,
};

int
gnm_conf_get_core_sort_dialog_max_initial_clauses (void)
{
	if (!watch_core_sort_dialog_max_initial_clauses.handler)
		watch_int (&watch_core_sort_dialog_max_initial_clauses);
	return watch_core_sort_dialog_max_initial_clauses.var;
}

void
gnm_conf_set_core_sort_dialog_max_initial_clauses (int x)
{
	if (!watch_core_sort_dialog_max_initial_clauses.handler)
		watch_int (&watch_core_sort_dialog_max_initial_clauses);
	set_int (&watch_core_sort_dialog_max_initial_clauses, x);
}

/**
 * gnm_conf_get_core_sort_dialog_max_initial_clauses_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_sort_dialog_max_initial_clauses_node (void)
{
	return get_watch_node (&watch_core_sort_dialog_max_initial_clauses);
}

static struct cb_watch_int watch_core_workbook_autosave_time = {
	0, "core/workbook/autosave-time",
	"Autosave frequency",
	"The number of seconds between autosaves.",
	0, 365 * 24 * 60 * 60, 0,
};

int
gnm_conf_get_core_workbook_autosave_time (void)
{
	if (!watch_core_workbook_autosave_time.handler)
		watch_int (&watch_core_workbook_autosave_time);
	return watch_core_workbook_autosave_time.var;
}

void
gnm_conf_set_core_workbook_autosave_time (int x)
{
	if (!watch_core_workbook_autosave_time.handler)
		watch_int (&watch_core_workbook_autosave_time);
	set_int (&watch_core_workbook_autosave_time, x);
}

/**
 * gnm_conf_get_core_workbook_autosave_time_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_workbook_autosave_time_node (void)
{
	return get_watch_node (&watch_core_workbook_autosave_time);
}

static struct cb_watch_int watch_core_workbook_n_cols = {
	0, "core/workbook/n-cols",
	"Default Number of columns in a sheet",
	"The number of columns in each sheet. This setting will be used only in a new Gnumeric session.",
	GNM_MIN_COLS, GNM_MAX_COLS, 256,
};

int
gnm_conf_get_core_workbook_n_cols (void)
{
	if (!watch_core_workbook_n_cols.handler)
		watch_int (&watch_core_workbook_n_cols);
	return watch_core_workbook_n_cols.var;
}

void
gnm_conf_set_core_workbook_n_cols (int x)
{
	if (!watch_core_workbook_n_cols.handler)
		watch_int (&watch_core_workbook_n_cols);
	set_int (&watch_core_workbook_n_cols, x);
}

/**
 * gnm_conf_get_core_workbook_n_cols_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_workbook_n_cols_node (void)
{
	return get_watch_node (&watch_core_workbook_n_cols);
}

static struct cb_watch_int watch_core_workbook_n_rows = {
	0, "core/workbook/n-rows",
	"Default Number of rows in a sheet",
	"The number of rows in each sheet. This setting will be used only in a new Gnumeric session.",
	GNM_MIN_ROWS, GNM_MAX_ROWS, 65536,
};

int
gnm_conf_get_core_workbook_n_rows (void)
{
	if (!watch_core_workbook_n_rows.handler)
		watch_int (&watch_core_workbook_n_rows);
	return watch_core_workbook_n_rows.var;
}

void
gnm_conf_set_core_workbook_n_rows (int x)
{
	if (!watch_core_workbook_n_rows.handler)
		watch_int (&watch_core_workbook_n_rows);
	set_int (&watch_core_workbook_n_rows, x);
}

/**
 * gnm_conf_get_core_workbook_n_rows_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_workbook_n_rows_node (void)
{
	return get_watch_node (&watch_core_workbook_n_rows);
}

static struct cb_watch_int watch_core_workbook_n_sheet = {
	0, "core/workbook/n-sheet",
	"Default Number of Sheets",
	"The number of sheets initially created in a new workbook.",
	1, 64, 3,
};

int
gnm_conf_get_core_workbook_n_sheet (void)
{
	if (!watch_core_workbook_n_sheet.handler)
		watch_int (&watch_core_workbook_n_sheet);
	return watch_core_workbook_n_sheet.var;
}

void
gnm_conf_set_core_workbook_n_sheet (int x)
{
	if (!watch_core_workbook_n_sheet.handler)
		watch_int (&watch_core_workbook_n_sheet);
	set_int (&watch_core_workbook_n_sheet, x);
}

/**
 * gnm_conf_get_core_workbook_n_sheet_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_workbook_n_sheet_node (void)
{
	return get_watch_node (&watch_core_workbook_n_sheet);
}

static struct cb_watch_int watch_core_xml_compression_level = {
	0, "core/xml/compression-level",
	"Default Compression Level For Gnumeric Files",
	"This integer (between 0 and 9) specifies the amount of compression performed by Gnumeric when saving files in the default file format. 0 is minimal compression while 9 is maximal compression.",
	0, 9, 9,
};

int
gnm_conf_get_core_xml_compression_level (void)
{
	if (!watch_core_xml_compression_level.handler)
		watch_int (&watch_core_xml_compression_level);
	return watch_core_xml_compression_level.var;
}

void
gnm_conf_set_core_xml_compression_level (int x)
{
	if (!watch_core_xml_compression_level.handler)
		watch_int (&watch_core_xml_compression_level);
	set_int (&watch_core_xml_compression_level, x);
}

/**
 * gnm_conf_get_core_xml_compression_level_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_xml_compression_level_node (void)
{
	return get_watch_node (&watch_core_xml_compression_level);
}

static struct cb_watch_bool watch_cut_and_paste_prefer_clipboard = {
	0, "cut-and-paste/prefer-clipboard",
	"Prefer CLIPBOARD over PRIMARY selection",
	"When TRUE, Gnumeric will prefer the modern CLIPBOARD selection over the legacy PRIMARY selections.  Set to FALSE if you have to deal with older applications, like Xterm or Emacs, which set only the PRIMARY selection.",
	TRUE,
};

gboolean
gnm_conf_get_cut_and_paste_prefer_clipboard (void)
{
	if (!watch_cut_and_paste_prefer_clipboard.handler)
		watch_bool (&watch_cut_and_paste_prefer_clipboard);
	return watch_cut_and_paste_prefer_clipboard.var;
}

void
gnm_conf_set_cut_and_paste_prefer_clipboard (gboolean x)
{
	if (!watch_cut_and_paste_prefer_clipboard.handler)
		watch_bool (&watch_cut_and_paste_prefer_clipboard);
	set_bool (&watch_cut_and_paste_prefer_clipboard, x);
}

/**
 * gnm_conf_get_cut_and_paste_prefer_clipboard_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_cut_and_paste_prefer_clipboard_node (void)
{
	return get_watch_node (&watch_cut_and_paste_prefer_clipboard);
}

static struct cb_watch_bool watch_dialogs_rs_unfocused = {
	0, "dialogs/rs/unfocused",
	"Allow Unfocused Range Selections",
	"Some dialogs contain only a single entry field that allows range selections in the workbook. Setting this variable to TRUE directs selections to this entry even if the entry does not have keyboard focus.",
	FALSE,
};

gboolean
gnm_conf_get_dialogs_rs_unfocused (void)
{
	if (!watch_dialogs_rs_unfocused.handler)
		watch_bool (&watch_dialogs_rs_unfocused);
	return watch_dialogs_rs_unfocused.var;
}

void
gnm_conf_set_dialogs_rs_unfocused (gboolean x)
{
	if (!watch_dialogs_rs_unfocused.handler)
		watch_bool (&watch_dialogs_rs_unfocused);
	set_bool (&watch_dialogs_rs_unfocused, x);
}

/**
 * gnm_conf_get_dialogs_rs_unfocused_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_dialogs_rs_unfocused_node (void)
{
	return get_watch_node (&watch_dialogs_rs_unfocused);
}

static struct cb_watch_int watch_functionselector_num_of_recent = {
	0, "functionselector/num-of-recent",
	"Maximum Length of Recently Used Functions List",
	"The function selector keeps a list of recently used functions. This is the maximum length of that list.",
	0, 40, 12,
};

int
gnm_conf_get_functionselector_num_of_recent (void)
{
	if (!watch_functionselector_num_of_recent.handler)
		watch_int (&watch_functionselector_num_of_recent);
	return watch_functionselector_num_of_recent.var;
}

void
gnm_conf_set_functionselector_num_of_recent (int x)
{
	if (!watch_functionselector_num_of_recent.handler)
		watch_int (&watch_functionselector_num_of_recent);
	set_int (&watch_functionselector_num_of_recent, x);
}

/**
 * gnm_conf_get_functionselector_num_of_recent_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_functionselector_num_of_recent_node (void)
{
	return get_watch_node (&watch_functionselector_num_of_recent);
}

static struct cb_watch_string_list watch_functionselector_recentfunctions = {
	0, "functionselector/recentfunctions",
	"List of recently used functions.",
	"The function selector keeps a list of recently used functions. This is that list.",
};

/**
 * gnm_conf_get_functionselector_recentfunctions:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_functionselector_recentfunctions (void)
{
	if (!watch_functionselector_recentfunctions.handler)
		watch_string_list (&watch_functionselector_recentfunctions);
	return watch_functionselector_recentfunctions.var;
}

/**
 * gnm_conf_set_functionselector_recentfunctions:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_functionselector_recentfunctions (GSList *x)
{
	if (!watch_functionselector_recentfunctions.handler)
		watch_string_list (&watch_functionselector_recentfunctions);
	set_string_list (&watch_functionselector_recentfunctions, x);
}

/**
 * gnm_conf_get_functionselector_recentfunctions_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_functionselector_recentfunctions_node (void)
{
	return get_watch_node (&watch_functionselector_recentfunctions);
}

static struct cb_watch_string watch_plugin_glpk_glpsol_path = {
	0, "plugin/glpk/glpsol-path",
	"Full path of glpsol program to use",
	"This is the full path to the glpsol binary that the lpsolve plugin should use.",
	"",
};

const char *
gnm_conf_get_plugin_glpk_glpsol_path (void)
{
	if (!watch_plugin_glpk_glpsol_path.handler)
		watch_string (&watch_plugin_glpk_glpsol_path);
	return watch_plugin_glpk_glpsol_path.var;
}

void
gnm_conf_set_plugin_glpk_glpsol_path (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_plugin_glpk_glpsol_path.handler)
		watch_string (&watch_plugin_glpk_glpsol_path);
	set_string (&watch_plugin_glpk_glpsol_path, x);
}

/**
 * gnm_conf_get_plugin_glpk_glpsol_path_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugin_glpk_glpsol_path_node (void)
{
	return get_watch_node (&watch_plugin_glpk_glpsol_path);
}

static struct cb_watch_bool watch_plugin_latex_use_utf8 = {
	0, "plugin/latex/use-utf8",
	"Use UTF-8 in LaTeX Export",
	"This setting determines whether created LaTeX files use UTF-8 (unicode) or ISO-8859-1 (Latin1). To use the UTF-8 files, you must have the ucs LaTeX package installed.",
	FALSE,
};

gboolean
gnm_conf_get_plugin_latex_use_utf8 (void)
{
	if (!watch_plugin_latex_use_utf8.handler)
		watch_bool (&watch_plugin_latex_use_utf8);
	return watch_plugin_latex_use_utf8.var;
}

void
gnm_conf_set_plugin_latex_use_utf8 (gboolean x)
{
	if (!watch_plugin_latex_use_utf8.handler)
		watch_bool (&watch_plugin_latex_use_utf8);
	set_bool (&watch_plugin_latex_use_utf8, x);
}

/**
 * gnm_conf_get_plugin_latex_use_utf8_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugin_latex_use_utf8_node (void)
{
	return get_watch_node (&watch_plugin_latex_use_utf8);
}

static struct cb_watch_string watch_plugin_lpsolve_lpsolve_path = {
	0, "plugin/lpsolve/lpsolve-path",
	"Full path of lp_solve program to use",
	"This is the full path to the lp_solve binary that the lpsolve plugin should use.",
	"",
};

const char *
gnm_conf_get_plugin_lpsolve_lpsolve_path (void)
{
	if (!watch_plugin_lpsolve_lpsolve_path.handler)
		watch_string (&watch_plugin_lpsolve_lpsolve_path);
	return watch_plugin_lpsolve_lpsolve_path.var;
}

void
gnm_conf_set_plugin_lpsolve_lpsolve_path (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_plugin_lpsolve_lpsolve_path.handler)
		watch_string (&watch_plugin_lpsolve_lpsolve_path);
	set_string (&watch_plugin_lpsolve_lpsolve_path, x);
}

/**
 * gnm_conf_get_plugin_lpsolve_lpsolve_path_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugin_lpsolve_lpsolve_path_node (void)
{
	return get_watch_node (&watch_plugin_lpsolve_lpsolve_path);
}

static struct cb_watch_bool watch_plugins_activate_newplugins = {
	0, "plugins/activate-newplugins",
	"Activate New Plugins",
	"This variable determines whether to activate every new encountered plugin.",
	TRUE,
};

gboolean
gnm_conf_get_plugins_activate_newplugins (void)
{
	if (!watch_plugins_activate_newplugins.handler)
		watch_bool (&watch_plugins_activate_newplugins);
	return watch_plugins_activate_newplugins.var;
}

void
gnm_conf_set_plugins_activate_newplugins (gboolean x)
{
	if (!watch_plugins_activate_newplugins.handler)
		watch_bool (&watch_plugins_activate_newplugins);
	set_bool (&watch_plugins_activate_newplugins, x);
}

/**
 * gnm_conf_get_plugins_activate_newplugins_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugins_activate_newplugins_node (void)
{
	return get_watch_node (&watch_plugins_activate_newplugins);
}

static struct cb_watch_string_list watch_plugins_active = {
	0, "plugins/active",
	"List of Active Plugins.",
	"This list contains all plugins that are supposed to be automatically activated.",
};

/**
 * gnm_conf_get_plugins_active:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_plugins_active (void)
{
	if (!watch_plugins_active.handler)
		watch_string_list (&watch_plugins_active);
	return watch_plugins_active.var;
}

/**
 * gnm_conf_set_plugins_active:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_plugins_active (GSList *x)
{
	if (!watch_plugins_active.handler)
		watch_string_list (&watch_plugins_active);
	set_string_list (&watch_plugins_active, x);
}

/**
 * gnm_conf_get_plugins_active_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugins_active_node (void)
{
	return get_watch_node (&watch_plugins_active);
}

static struct cb_watch_string_list watch_plugins_extra_dirs = {
	0, "plugins/extra-dirs",
	"List of Extra Plugin Directories.",
	"This list contains all extra directories containing plugins.",
};

/**
 * gnm_conf_get_plugins_extra_dirs:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_plugins_extra_dirs (void)
{
	if (!watch_plugins_extra_dirs.handler)
		watch_string_list (&watch_plugins_extra_dirs);
	return watch_plugins_extra_dirs.var;
}

/**
 * gnm_conf_set_plugins_extra_dirs:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_plugins_extra_dirs (GSList *x)
{
	if (!watch_plugins_extra_dirs.handler)
		watch_string_list (&watch_plugins_extra_dirs);
	set_string_list (&watch_plugins_extra_dirs, x);
}

/**
 * gnm_conf_get_plugins_extra_dirs_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugins_extra_dirs_node (void)
{
	return get_watch_node (&watch_plugins_extra_dirs);
}

static struct cb_watch_string_list watch_plugins_file_states = {
	0, "plugins/file-states",
	"List of Plugin File States.",
	"This list contains all plugin file states.",
};

/**
 * gnm_conf_get_plugins_file_states:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_plugins_file_states (void)
{
	if (!watch_plugins_file_states.handler)
		watch_string_list (&watch_plugins_file_states);
	return watch_plugins_file_states.var;
}

/**
 * gnm_conf_set_plugins_file_states:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_plugins_file_states (GSList *x)
{
	if (!watch_plugins_file_states.handler)
		watch_string_list (&watch_plugins_file_states);
	set_string_list (&watch_plugins_file_states, x);
}

/**
 * gnm_conf_get_plugins_file_states_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugins_file_states_node (void)
{
	return get_watch_node (&watch_plugins_file_states);
}

static struct cb_watch_string_list watch_plugins_known = {
	0, "plugins/known",
	"List of Known Plugins.",
	"This list contains all known plugins.",
};

/**
 * gnm_conf_get_plugins_known:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_plugins_known (void)
{
	if (!watch_plugins_known.handler)
		watch_string_list (&watch_plugins_known);
	return watch_plugins_known.var;
}

/**
 * gnm_conf_set_plugins_known:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_plugins_known (GSList *x)
{
	if (!watch_plugins_known.handler)
		watch_string_list (&watch_plugins_known);
	set_string_list (&watch_plugins_known, x);
}

/**
 * gnm_conf_get_plugins_known_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugins_known_node (void)
{
	return get_watch_node (&watch_plugins_known);
}

static struct cb_watch_bool watch_printsetup_across_then_down = {
	0, "printsetup/across-then-down",
	"Default Print Direction",
	"This value determines the default setting in the Print Setup dialog whether to print first right then down. Please use the Print Setup dialog to edit this value.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_across_then_down (void)
{
	if (!watch_printsetup_across_then_down.handler)
		watch_bool (&watch_printsetup_across_then_down);
	return watch_printsetup_across_then_down.var;
}

void
gnm_conf_set_printsetup_across_then_down (gboolean x)
{
	if (!watch_printsetup_across_then_down.handler)
		watch_bool (&watch_printsetup_across_then_down);
	set_bool (&watch_printsetup_across_then_down, x);
}

/**
 * gnm_conf_get_printsetup_across_then_down_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_across_then_down_node (void)
{
	return get_watch_node (&watch_printsetup_across_then_down);
}

static struct cb_watch_bool watch_printsetup_all_sheets = {
	0, "printsetup/all-sheets",
	"Apply print-setup to all sheets",
	"This value determines whether by default the print set-up dialog applies to all sheets simultaneously.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_all_sheets (void)
{
	if (!watch_printsetup_all_sheets.handler)
		watch_bool (&watch_printsetup_all_sheets);
	return watch_printsetup_all_sheets.var;
}

void
gnm_conf_set_printsetup_all_sheets (gboolean x)
{
	if (!watch_printsetup_all_sheets.handler)
		watch_bool (&watch_printsetup_all_sheets);
	set_bool (&watch_printsetup_all_sheets, x);
}

/**
 * gnm_conf_get_printsetup_all_sheets_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_all_sheets_node (void)
{
	return get_watch_node (&watch_printsetup_all_sheets);
}

static struct cb_watch_bool watch_printsetup_center_horizontally = {
	0, "printsetup/center-horizontally",
	"Default Horizontal Centering",
	"This value determines whether the default setting in the Print Setup dialog is to center pages horizontally.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_center_horizontally (void)
{
	if (!watch_printsetup_center_horizontally.handler)
		watch_bool (&watch_printsetup_center_horizontally);
	return watch_printsetup_center_horizontally.var;
}

void
gnm_conf_set_printsetup_center_horizontally (gboolean x)
{
	if (!watch_printsetup_center_horizontally.handler)
		watch_bool (&watch_printsetup_center_horizontally);
	set_bool (&watch_printsetup_center_horizontally, x);
}

/**
 * gnm_conf_get_printsetup_center_horizontally_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_center_horizontally_node (void)
{
	return get_watch_node (&watch_printsetup_center_horizontally);
}

static struct cb_watch_bool watch_printsetup_center_vertically = {
	0, "printsetup/center-vertically",
	"Default Vertical Centering",
	"This value determines whether the default setting in the Print Setup dialog is to center pages vertically.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_center_vertically (void)
{
	if (!watch_printsetup_center_vertically.handler)
		watch_bool (&watch_printsetup_center_vertically);
	return watch_printsetup_center_vertically.var;
}

void
gnm_conf_set_printsetup_center_vertically (gboolean x)
{
	if (!watch_printsetup_center_vertically.handler)
		watch_bool (&watch_printsetup_center_vertically);
	set_bool (&watch_printsetup_center_vertically, x);
}

/**
 * gnm_conf_get_printsetup_center_vertically_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_center_vertically_node (void)
{
	return get_watch_node (&watch_printsetup_center_vertically);
}

static struct cb_watch_string_list watch_printsetup_footer = {
	0, "printsetup/footer",
	"Page Footer",
	"The default page footer for new documents that can be modified using the\n	page setup dialog.",
};

/**
 * gnm_conf_get_printsetup_footer:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_printsetup_footer (void)
{
	if (!watch_printsetup_footer.handler)
		watch_string_list (&watch_printsetup_footer);
	return watch_printsetup_footer.var;
}

/**
 * gnm_conf_set_printsetup_footer:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_printsetup_footer (GSList *x)
{
	if (!watch_printsetup_footer.handler)
		watch_string_list (&watch_printsetup_footer);
	set_string_list (&watch_printsetup_footer, x);
}

/**
 * gnm_conf_get_printsetup_footer_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_footer_node (void)
{
	return get_watch_node (&watch_printsetup_footer);
}

static struct cb_watch_string_list watch_printsetup_gtk_setting = {
	0, "printsetup/gtk-setting",
	"GTKPrintSetting",
	"The configuration of GTKPrintSetting. Do not edit this variable.",
};

/**
 * gnm_conf_get_printsetup_gtk_setting:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_printsetup_gtk_setting (void)
{
	if (!watch_printsetup_gtk_setting.handler)
		watch_string_list (&watch_printsetup_gtk_setting);
	return watch_printsetup_gtk_setting.var;
}

/**
 * gnm_conf_set_printsetup_gtk_setting:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_printsetup_gtk_setting (GSList *x)
{
	if (!watch_printsetup_gtk_setting.handler)
		watch_string_list (&watch_printsetup_gtk_setting);
	set_string_list (&watch_printsetup_gtk_setting, x);
}

/**
 * gnm_conf_get_printsetup_gtk_setting_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_gtk_setting_node (void)
{
	return get_watch_node (&watch_printsetup_gtk_setting);
}

static struct cb_watch_string_list watch_printsetup_header = {
	0, "printsetup/header",
	"Page Header",
	"The default page header for new documents that can be modified using the\n	page setup dialog.",
};

/**
 * gnm_conf_get_printsetup_header:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_printsetup_header (void)
{
	if (!watch_printsetup_header.handler)
		watch_string_list (&watch_printsetup_header);
	return watch_printsetup_header.var;
}

/**
 * gnm_conf_set_printsetup_header:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_printsetup_header (GSList *x)
{
	if (!watch_printsetup_header.handler)
		watch_string_list (&watch_printsetup_header);
	set_string_list (&watch_printsetup_header, x);
}

/**
 * gnm_conf_get_printsetup_header_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_header_node (void)
{
	return get_watch_node (&watch_printsetup_header);
}

static struct cb_watch_bool watch_printsetup_hf_font_bold = {
	0, "printsetup/hf-font-bold",
	"The default header/footer font is bold.",
	"This value determines whether the default font for headers and footers is bold.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_hf_font_bold (void)
{
	if (!watch_printsetup_hf_font_bold.handler)
		watch_bool (&watch_printsetup_hf_font_bold);
	return watch_printsetup_hf_font_bold.var;
}

void
gnm_conf_set_printsetup_hf_font_bold (gboolean x)
{
	if (!watch_printsetup_hf_font_bold.handler)
		watch_bool (&watch_printsetup_hf_font_bold);
	set_bool (&watch_printsetup_hf_font_bold, x);
}

/**
 * gnm_conf_get_printsetup_hf_font_bold_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_hf_font_bold_node (void)
{
	return get_watch_node (&watch_printsetup_hf_font_bold);
}

static struct cb_watch_bool watch_printsetup_hf_font_italic = {
	0, "printsetup/hf-font-italic",
	"The default header/footer font is italic.",
	"This value determines whether the default font for headers and footers is italic.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_hf_font_italic (void)
{
	if (!watch_printsetup_hf_font_italic.handler)
		watch_bool (&watch_printsetup_hf_font_italic);
	return watch_printsetup_hf_font_italic.var;
}

void
gnm_conf_set_printsetup_hf_font_italic (gboolean x)
{
	if (!watch_printsetup_hf_font_italic.handler)
		watch_bool (&watch_printsetup_hf_font_italic);
	set_bool (&watch_printsetup_hf_font_italic, x);
}

/**
 * gnm_conf_get_printsetup_hf_font_italic_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_hf_font_italic_node (void)
{
	return get_watch_node (&watch_printsetup_hf_font_italic);
}

static struct cb_watch_string watch_printsetup_hf_font_name = {
	0, "printsetup/hf-font-name",
	"Default header/footer font name",
	"The default font name for headers and footers.",
	"Sans",
};

const char *
gnm_conf_get_printsetup_hf_font_name (void)
{
	if (!watch_printsetup_hf_font_name.handler)
		watch_string (&watch_printsetup_hf_font_name);
	return watch_printsetup_hf_font_name.var;
}

void
gnm_conf_set_printsetup_hf_font_name (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_printsetup_hf_font_name.handler)
		watch_string (&watch_printsetup_hf_font_name);
	set_string (&watch_printsetup_hf_font_name, x);
}

/**
 * gnm_conf_get_printsetup_hf_font_name_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_hf_font_name_node (void)
{
	return get_watch_node (&watch_printsetup_hf_font_name);
}

static struct cb_watch_double watch_printsetup_hf_font_size = {
	0, "printsetup/hf-font-size",
	"Default Header/Footer Font Size",
	"The default font size for headers and footers.",
	1, 100, 10,
};

double
gnm_conf_get_printsetup_hf_font_size (void)
{
	if (!watch_printsetup_hf_font_size.handler)
		watch_double (&watch_printsetup_hf_font_size);
	return watch_printsetup_hf_font_size.var;
}

void
gnm_conf_set_printsetup_hf_font_size (double x)
{
	if (!watch_printsetup_hf_font_size.handler)
		watch_double (&watch_printsetup_hf_font_size);
	set_double (&watch_printsetup_hf_font_size, x);
}

/**
 * gnm_conf_get_printsetup_hf_font_size_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_hf_font_size_node (void)
{
	return get_watch_node (&watch_printsetup_hf_font_size);
}

static struct cb_watch_string_list watch_printsetup_hf_left = {
	0, "printsetup/hf-left",
	"Header/Footer Format (Left Portion)",
	"Please use the Print Setup dialog to edit this value.",
};

/**
 * gnm_conf_get_printsetup_hf_left:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_printsetup_hf_left (void)
{
	if (!watch_printsetup_hf_left.handler)
		watch_string_list (&watch_printsetup_hf_left);
	return watch_printsetup_hf_left.var;
}

/**
 * gnm_conf_set_printsetup_hf_left:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_printsetup_hf_left (GSList *x)
{
	if (!watch_printsetup_hf_left.handler)
		watch_string_list (&watch_printsetup_hf_left);
	set_string_list (&watch_printsetup_hf_left, x);
}

/**
 * gnm_conf_get_printsetup_hf_left_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_hf_left_node (void)
{
	return get_watch_node (&watch_printsetup_hf_left);
}

static struct cb_watch_string_list watch_printsetup_hf_middle = {
	0, "printsetup/hf-middle",
	"Header/Footer Format (Middle Portion)",
	"Please use the Print Setup dialog to edit this value.",
};

/**
 * gnm_conf_get_printsetup_hf_middle:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_printsetup_hf_middle (void)
{
	if (!watch_printsetup_hf_middle.handler)
		watch_string_list (&watch_printsetup_hf_middle);
	return watch_printsetup_hf_middle.var;
}

/**
 * gnm_conf_set_printsetup_hf_middle:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_printsetup_hf_middle (GSList *x)
{
	if (!watch_printsetup_hf_middle.handler)
		watch_string_list (&watch_printsetup_hf_middle);
	set_string_list (&watch_printsetup_hf_middle, x);
}

/**
 * gnm_conf_get_printsetup_hf_middle_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_hf_middle_node (void)
{
	return get_watch_node (&watch_printsetup_hf_middle);
}

static struct cb_watch_string_list watch_printsetup_hf_right = {
	0, "printsetup/hf-right",
	"Header/Footer Format (Right Portion)",
	"Please use the Print Setup dialog to edit this value.",
};

/**
 * gnm_conf_get_printsetup_hf_right:
 *
 * Returns: (element-type utf8) (transfer none):
 **/
GSList *
gnm_conf_get_printsetup_hf_right (void)
{
	if (!watch_printsetup_hf_right.handler)
		watch_string_list (&watch_printsetup_hf_right);
	return watch_printsetup_hf_right.var;
}

/**
 * gnm_conf_set_printsetup_hf_right:
 * @x: (element-type utf8): list of strings
 *
 **/
void
gnm_conf_set_printsetup_hf_right (GSList *x)
{
	if (!watch_printsetup_hf_right.handler)
		watch_string_list (&watch_printsetup_hf_right);
	set_string_list (&watch_printsetup_hf_right, x);
}

/**
 * gnm_conf_get_printsetup_hf_right_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_hf_right_node (void)
{
	return get_watch_node (&watch_printsetup_hf_right);
}

static struct cb_watch_double watch_printsetup_margin_bottom = {
	0, "printsetup/margin-bottom",
	"Default Bottom Margin",
	"This value gives the default number of points from the bottom of a page to the end of the body. Please use the Print Setup dialog to edit this value.",
	0, 10000, 120,
};

double
gnm_conf_get_printsetup_margin_bottom (void)
{
	if (!watch_printsetup_margin_bottom.handler)
		watch_double (&watch_printsetup_margin_bottom);
	return watch_printsetup_margin_bottom.var;
}

void
gnm_conf_set_printsetup_margin_bottom (double x)
{
	if (!watch_printsetup_margin_bottom.handler)
		watch_double (&watch_printsetup_margin_bottom);
	set_double (&watch_printsetup_margin_bottom, x);
}

/**
 * gnm_conf_get_printsetup_margin_bottom_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_margin_bottom_node (void)
{
	return get_watch_node (&watch_printsetup_margin_bottom);
}

static struct cb_watch_double watch_printsetup_margin_gtk_bottom = {
	0, "printsetup/margin-gtk-bottom",
	"Default Bottom Outside Margin",
	"This value gives the default number of points from the bottom of a page to the end of the footer. Please use the Print Setup dialog to edit this value.",
	0, 720, 72,
};

double
gnm_conf_get_printsetup_margin_gtk_bottom (void)
{
	if (!watch_printsetup_margin_gtk_bottom.handler)
		watch_double (&watch_printsetup_margin_gtk_bottom);
	return watch_printsetup_margin_gtk_bottom.var;
}

void
gnm_conf_set_printsetup_margin_gtk_bottom (double x)
{
	if (!watch_printsetup_margin_gtk_bottom.handler)
		watch_double (&watch_printsetup_margin_gtk_bottom);
	set_double (&watch_printsetup_margin_gtk_bottom, x);
}

/**
 * gnm_conf_get_printsetup_margin_gtk_bottom_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_margin_gtk_bottom_node (void)
{
	return get_watch_node (&watch_printsetup_margin_gtk_bottom);
}

static struct cb_watch_double watch_printsetup_margin_gtk_left = {
	0, "printsetup/margin-gtk-left",
	"Default Left Margin",
	"This value gives the default number of points from the left of a page to the left of the body. Please use the Print Setup dialog to edit this value.",
	0, 720, 72,
};

double
gnm_conf_get_printsetup_margin_gtk_left (void)
{
	if (!watch_printsetup_margin_gtk_left.handler)
		watch_double (&watch_printsetup_margin_gtk_left);
	return watch_printsetup_margin_gtk_left.var;
}

void
gnm_conf_set_printsetup_margin_gtk_left (double x)
{
	if (!watch_printsetup_margin_gtk_left.handler)
		watch_double (&watch_printsetup_margin_gtk_left);
	set_double (&watch_printsetup_margin_gtk_left, x);
}

/**
 * gnm_conf_get_printsetup_margin_gtk_left_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_margin_gtk_left_node (void)
{
	return get_watch_node (&watch_printsetup_margin_gtk_left);
}

static struct cb_watch_double watch_printsetup_margin_gtk_right = {
	0, "printsetup/margin-gtk-right",
	"Default Right Margin",
	"This value gives the default number of points from the right of a page to the right of the body. Please use the Print Setup dialog to edit this value.",
	0, 720, 72,
};

double
gnm_conf_get_printsetup_margin_gtk_right (void)
{
	if (!watch_printsetup_margin_gtk_right.handler)
		watch_double (&watch_printsetup_margin_gtk_right);
	return watch_printsetup_margin_gtk_right.var;
}

void
gnm_conf_set_printsetup_margin_gtk_right (double x)
{
	if (!watch_printsetup_margin_gtk_right.handler)
		watch_double (&watch_printsetup_margin_gtk_right);
	set_double (&watch_printsetup_margin_gtk_right, x);
}

/**
 * gnm_conf_get_printsetup_margin_gtk_right_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_margin_gtk_right_node (void)
{
	return get_watch_node (&watch_printsetup_margin_gtk_right);
}

static struct cb_watch_double watch_printsetup_margin_gtk_top = {
	0, "printsetup/margin-gtk-top",
	"Default Top Outside Margin",
	"This value gives the default number of points from the top of a page to the top of the header. Please use the Print Setup dialog to edit this value.",
	0, 720, 72,
};

double
gnm_conf_get_printsetup_margin_gtk_top (void)
{
	if (!watch_printsetup_margin_gtk_top.handler)
		watch_double (&watch_printsetup_margin_gtk_top);
	return watch_printsetup_margin_gtk_top.var;
}

void
gnm_conf_set_printsetup_margin_gtk_top (double x)
{
	if (!watch_printsetup_margin_gtk_top.handler)
		watch_double (&watch_printsetup_margin_gtk_top);
	set_double (&watch_printsetup_margin_gtk_top, x);
}

/**
 * gnm_conf_get_printsetup_margin_gtk_top_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_margin_gtk_top_node (void)
{
	return get_watch_node (&watch_printsetup_margin_gtk_top);
}

static struct cb_watch_double watch_printsetup_margin_top = {
	0, "printsetup/margin-top",
	"Default Top Margin",
	"This value gives the default number of points from the top of a page to the start of the body. Please use the Print Setup dialog to edit this value.",
	0, 10000, 120,
};

double
gnm_conf_get_printsetup_margin_top (void)
{
	if (!watch_printsetup_margin_top.handler)
		watch_double (&watch_printsetup_margin_top);
	return watch_printsetup_margin_top.var;
}

void
gnm_conf_set_printsetup_margin_top (double x)
{
	if (!watch_printsetup_margin_top.handler)
		watch_double (&watch_printsetup_margin_top);
	set_double (&watch_printsetup_margin_top, x);
}

/**
 * gnm_conf_get_printsetup_margin_top_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_margin_top_node (void)
{
	return get_watch_node (&watch_printsetup_margin_top);
}

static struct cb_watch_string watch_printsetup_paper = {
	0, "printsetup/paper",
	"Paper",
	"This is the default paper specification. Please use the Print Setup dialog to edit this value.",
	"",
};

const char *
gnm_conf_get_printsetup_paper (void)
{
	if (!watch_printsetup_paper.handler)
		watch_string (&watch_printsetup_paper);
	return watch_printsetup_paper.var;
}

void
gnm_conf_set_printsetup_paper (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_printsetup_paper.handler)
		watch_string (&watch_printsetup_paper);
	set_string (&watch_printsetup_paper, x);
}

/**
 * gnm_conf_get_printsetup_paper_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_paper_node (void)
{
	return get_watch_node (&watch_printsetup_paper);
}

static struct cb_watch_int watch_printsetup_paper_orientation = {
	0, "printsetup/paper-orientation",
	"Paper orientation",
	"This is the default paper orientation. Please use the Print Setup dialog to edit this value.",
	GTK_PAGE_ORIENTATION_PORTRAIT, GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE, 0,
};

int
gnm_conf_get_printsetup_paper_orientation (void)
{
	if (!watch_printsetup_paper_orientation.handler)
		watch_int (&watch_printsetup_paper_orientation);
	return watch_printsetup_paper_orientation.var;
}

void
gnm_conf_set_printsetup_paper_orientation (int x)
{
	if (!watch_printsetup_paper_orientation.handler)
		watch_int (&watch_printsetup_paper_orientation);
	set_int (&watch_printsetup_paper_orientation, x);
}

/**
 * gnm_conf_get_printsetup_paper_orientation_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_paper_orientation_node (void)
{
	return get_watch_node (&watch_printsetup_paper_orientation);
}

static struct cb_watch_enum watch_printsetup_preferred_unit = {
	0, "printsetup/preferred-unit",
	"Preferred Display Unit",
	"This string gives the default unit to be used in the page setup dialog.",
	GTK_UNIT_MM,
};

GtkUnit
gnm_conf_get_printsetup_preferred_unit (void)
{
	if (!watch_printsetup_preferred_unit.handler)
		watch_enum (&watch_printsetup_preferred_unit, GTK_TYPE_UNIT);
	return watch_printsetup_preferred_unit.var;
}

void
gnm_conf_set_printsetup_preferred_unit (GtkUnit x)
{
	if (!watch_printsetup_preferred_unit.handler)
		watch_enum (&watch_printsetup_preferred_unit, GTK_TYPE_UNIT);
	set_enum (&watch_printsetup_preferred_unit, x);
}

/**
 * gnm_conf_get_printsetup_preferred_unit_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_preferred_unit_node (void)
{
	return get_watch_node (&watch_printsetup_preferred_unit);
}

static struct cb_watch_bool watch_printsetup_print_black_n_white = {
	0, "printsetup/print-black-n-white",
	"Default Black and White Printing",
	"This value determines the default setting in the Print Setup dialog whether to print in only black and white. Please use the Print Setup dialog to edit this value.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_print_black_n_white (void)
{
	if (!watch_printsetup_print_black_n_white.handler)
		watch_bool (&watch_printsetup_print_black_n_white);
	return watch_printsetup_print_black_n_white.var;
}

void
gnm_conf_set_printsetup_print_black_n_white (gboolean x)
{
	if (!watch_printsetup_print_black_n_white.handler)
		watch_bool (&watch_printsetup_print_black_n_white);
	set_bool (&watch_printsetup_print_black_n_white, x);
}

/**
 * gnm_conf_get_printsetup_print_black_n_white_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_print_black_n_white_node (void)
{
	return get_watch_node (&watch_printsetup_print_black_n_white);
}

static struct cb_watch_bool watch_printsetup_print_even_if_only_styles = {
	0, "printsetup/print-even-if-only-styles",
	"Default Print Cells with Only Styles",
	"This value determines the default setting in the Print Setup dialog whether to print empty but formatted cells. Please use the Print Setup dialog to edit this value.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_print_even_if_only_styles (void)
{
	if (!watch_printsetup_print_even_if_only_styles.handler)
		watch_bool (&watch_printsetup_print_even_if_only_styles);
	return watch_printsetup_print_even_if_only_styles.var;
}

void
gnm_conf_set_printsetup_print_even_if_only_styles (gboolean x)
{
	if (!watch_printsetup_print_even_if_only_styles.handler)
		watch_bool (&watch_printsetup_print_even_if_only_styles);
	set_bool (&watch_printsetup_print_even_if_only_styles, x);
}

/**
 * gnm_conf_get_printsetup_print_even_if_only_styles_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_print_even_if_only_styles_node (void)
{
	return get_watch_node (&watch_printsetup_print_even_if_only_styles);
}

static struct cb_watch_bool watch_printsetup_print_grid_lines = {
	0, "printsetup/print-grid-lines",
	"Default Grid Line Printing",
	"This value determines the default setting in the Print Setup dialog whether print grid lines. Please use the Print Setup dialog to edit this value.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_print_grid_lines (void)
{
	if (!watch_printsetup_print_grid_lines.handler)
		watch_bool (&watch_printsetup_print_grid_lines);
	return watch_printsetup_print_grid_lines.var;
}

void
gnm_conf_set_printsetup_print_grid_lines (gboolean x)
{
	if (!watch_printsetup_print_grid_lines.handler)
		watch_bool (&watch_printsetup_print_grid_lines);
	set_bool (&watch_printsetup_print_grid_lines, x);
}

/**
 * gnm_conf_get_printsetup_print_grid_lines_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_print_grid_lines_node (void)
{
	return get_watch_node (&watch_printsetup_print_grid_lines);
}

static struct cb_watch_bool watch_printsetup_print_titles = {
	0, "printsetup/print-titles",
	"Default Title Printing",
	"This value determines the default setting in the Print Setup dialog whether to print row and column headers. Please use the Print Setup dialog to edit this value.",
	FALSE,
};

gboolean
gnm_conf_get_printsetup_print_titles (void)
{
	if (!watch_printsetup_print_titles.handler)
		watch_bool (&watch_printsetup_print_titles);
	return watch_printsetup_print_titles.var;
}

void
gnm_conf_set_printsetup_print_titles (gboolean x)
{
	if (!watch_printsetup_print_titles.handler)
		watch_bool (&watch_printsetup_print_titles);
	set_bool (&watch_printsetup_print_titles, x);
}

/**
 * gnm_conf_get_printsetup_print_titles_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_print_titles_node (void)
{
	return get_watch_node (&watch_printsetup_print_titles);
}

static struct cb_watch_string watch_printsetup_repeat_left = {
	0, "printsetup/repeat-left",
	"Default Repeated Left Region",
	"This string gives the default region to be repeated at the left of each printed sheet. Please use the Print Setup dialog to edit this value.",
	"",
};

const char *
gnm_conf_get_printsetup_repeat_left (void)
{
	if (!watch_printsetup_repeat_left.handler)
		watch_string (&watch_printsetup_repeat_left);
	return watch_printsetup_repeat_left.var;
}

void
gnm_conf_set_printsetup_repeat_left (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_printsetup_repeat_left.handler)
		watch_string (&watch_printsetup_repeat_left);
	set_string (&watch_printsetup_repeat_left, x);
}

/**
 * gnm_conf_get_printsetup_repeat_left_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_repeat_left_node (void)
{
	return get_watch_node (&watch_printsetup_repeat_left);
}

static struct cb_watch_string watch_printsetup_repeat_top = {
	0, "printsetup/repeat-top",
	"Default Repeated Top Region",
	"This string gives the default region to be repeated at the top of each printed sheet. Please use the Print Setup dialog to edit this value.",
	"",
};

const char *
gnm_conf_get_printsetup_repeat_top (void)
{
	if (!watch_printsetup_repeat_top.handler)
		watch_string (&watch_printsetup_repeat_top);
	return watch_printsetup_repeat_top.var;
}

void
gnm_conf_set_printsetup_repeat_top (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_printsetup_repeat_top.handler)
		watch_string (&watch_printsetup_repeat_top);
	set_string (&watch_printsetup_repeat_top, x);
}

/**
 * gnm_conf_get_printsetup_repeat_top_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_repeat_top_node (void)
{
	return get_watch_node (&watch_printsetup_repeat_top);
}

static struct cb_watch_int watch_printsetup_scale_height = {
	0, "printsetup/scale-height",
	"Default Scaling Height",
	"This value determines the maximum number of pages that make up the height of a printout of the current sheet. The sheet will be reduced to fit within this height. This value can be changed in the Page Setup dialog.",
	0, 100, 0,
};

int
gnm_conf_get_printsetup_scale_height (void)
{
	if (!watch_printsetup_scale_height.handler)
		watch_int (&watch_printsetup_scale_height);
	return watch_printsetup_scale_height.var;
}

void
gnm_conf_set_printsetup_scale_height (int x)
{
	if (!watch_printsetup_scale_height.handler)
		watch_int (&watch_printsetup_scale_height);
	set_int (&watch_printsetup_scale_height, x);
}

/**
 * gnm_conf_get_printsetup_scale_height_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_scale_height_node (void)
{
	return get_watch_node (&watch_printsetup_scale_height);
}

static struct cb_watch_bool watch_printsetup_scale_percentage = {
	0, "printsetup/scale-percentage",
	"Default Scale Type",
	"This value determines the default setting in the Print Setup dialog whether to scale pages by a given percentage. Please use the Print Setup dialog to edit this value.",
	TRUE,
};

gboolean
gnm_conf_get_printsetup_scale_percentage (void)
{
	if (!watch_printsetup_scale_percentage.handler)
		watch_bool (&watch_printsetup_scale_percentage);
	return watch_printsetup_scale_percentage.var;
}

void
gnm_conf_set_printsetup_scale_percentage (gboolean x)
{
	if (!watch_printsetup_scale_percentage.handler)
		watch_bool (&watch_printsetup_scale_percentage);
	set_bool (&watch_printsetup_scale_percentage, x);
}

/**
 * gnm_conf_get_printsetup_scale_percentage_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_scale_percentage_node (void)
{
	return get_watch_node (&watch_printsetup_scale_percentage);
}

static struct cb_watch_double watch_printsetup_scale_percentage_value = {
	0, "printsetup/scale-percentage-value",
	"Default Scale Percentage",
	"This value gives the percentage by which to scale each printed page. Please use the Print Setup dialog to edit this value.",
	1, 500, 100,
};

double
gnm_conf_get_printsetup_scale_percentage_value (void)
{
	if (!watch_printsetup_scale_percentage_value.handler)
		watch_double (&watch_printsetup_scale_percentage_value);
	return watch_printsetup_scale_percentage_value.var;
}

void
gnm_conf_set_printsetup_scale_percentage_value (double x)
{
	if (!watch_printsetup_scale_percentage_value.handler)
		watch_double (&watch_printsetup_scale_percentage_value);
	set_double (&watch_printsetup_scale_percentage_value, x);
}

/**
 * gnm_conf_get_printsetup_scale_percentage_value_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_scale_percentage_value_node (void)
{
	return get_watch_node (&watch_printsetup_scale_percentage_value);
}

static struct cb_watch_int watch_printsetup_scale_width = {
	0, "printsetup/scale-width",
	"Default Scaling Width",
	"This value determines the maximum number of pages that make up the width of a printout of the current sheet. The sheet will be reduced to fit within this width. This value can be changed in the Page Setup dialog.",
	0, 100, 0,
};

int
gnm_conf_get_printsetup_scale_width (void)
{
	if (!watch_printsetup_scale_width.handler)
		watch_int (&watch_printsetup_scale_width);
	return watch_printsetup_scale_width.var;
}

void
gnm_conf_set_printsetup_scale_width (int x)
{
	if (!watch_printsetup_scale_width.handler)
		watch_int (&watch_printsetup_scale_width);
	set_int (&watch_printsetup_scale_width, x);
}

/**
 * gnm_conf_get_printsetup_scale_width_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_scale_width_node (void)
{
	return get_watch_node (&watch_printsetup_scale_width);
}

static struct cb_watch_bool watch_searchreplace_change_cell_expressions = {
	0, "searchreplace/change-cell-expressions",
	"Search & Replace Changes Expressions",
	"Search & Replace changes cells containing expressions as default",
	TRUE,
};

gboolean
gnm_conf_get_searchreplace_change_cell_expressions (void)
{
	if (!watch_searchreplace_change_cell_expressions.handler)
		watch_bool (&watch_searchreplace_change_cell_expressions);
	return watch_searchreplace_change_cell_expressions.var;
}

void
gnm_conf_set_searchreplace_change_cell_expressions (gboolean x)
{
	if (!watch_searchreplace_change_cell_expressions.handler)
		watch_bool (&watch_searchreplace_change_cell_expressions);
	set_bool (&watch_searchreplace_change_cell_expressions, x);
}

/**
 * gnm_conf_get_searchreplace_change_cell_expressions_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_change_cell_expressions_node (void)
{
	return get_watch_node (&watch_searchreplace_change_cell_expressions);
}

static struct cb_watch_bool watch_searchreplace_change_cell_other = {
	0, "searchreplace/change-cell-other",
	"Search & Replace Changes Other Values",
	"Search & Replace changes cells containing other values as default",
	TRUE,
};

gboolean
gnm_conf_get_searchreplace_change_cell_other (void)
{
	if (!watch_searchreplace_change_cell_other.handler)
		watch_bool (&watch_searchreplace_change_cell_other);
	return watch_searchreplace_change_cell_other.var;
}

void
gnm_conf_set_searchreplace_change_cell_other (gboolean x)
{
	if (!watch_searchreplace_change_cell_other.handler)
		watch_bool (&watch_searchreplace_change_cell_other);
	set_bool (&watch_searchreplace_change_cell_other, x);
}

/**
 * gnm_conf_get_searchreplace_change_cell_other_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_change_cell_other_node (void)
{
	return get_watch_node (&watch_searchreplace_change_cell_other);
}

static struct cb_watch_bool watch_searchreplace_change_cell_strings = {
	0, "searchreplace/change-cell-strings",
	"Search & Replace Changes Strings",
	"Search & Replace changes cells containing strings as default",
	TRUE,
};

gboolean
gnm_conf_get_searchreplace_change_cell_strings (void)
{
	if (!watch_searchreplace_change_cell_strings.handler)
		watch_bool (&watch_searchreplace_change_cell_strings);
	return watch_searchreplace_change_cell_strings.var;
}

void
gnm_conf_set_searchreplace_change_cell_strings (gboolean x)
{
	if (!watch_searchreplace_change_cell_strings.handler)
		watch_bool (&watch_searchreplace_change_cell_strings);
	set_bool (&watch_searchreplace_change_cell_strings, x);
}

/**
 * gnm_conf_get_searchreplace_change_cell_strings_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_change_cell_strings_node (void)
{
	return get_watch_node (&watch_searchreplace_change_cell_strings);
}

static struct cb_watch_bool watch_searchreplace_change_comments = {
	0, "searchreplace/change-comments",
	"Search & Replace Changes Comments",
	"Search & Replace changes comments as default",
	FALSE,
};

gboolean
gnm_conf_get_searchreplace_change_comments (void)
{
	if (!watch_searchreplace_change_comments.handler)
		watch_bool (&watch_searchreplace_change_comments);
	return watch_searchreplace_change_comments.var;
}

void
gnm_conf_set_searchreplace_change_comments (gboolean x)
{
	if (!watch_searchreplace_change_comments.handler)
		watch_bool (&watch_searchreplace_change_comments);
	set_bool (&watch_searchreplace_change_comments, x);
}

/**
 * gnm_conf_get_searchreplace_change_comments_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_change_comments_node (void)
{
	return get_watch_node (&watch_searchreplace_change_comments);
}

static struct cb_watch_bool watch_searchreplace_columnmajor = {
	0, "searchreplace/columnmajor",
	"Search & Replace Column Major",
	"Search & Replace proceeds in column major order as default",
	TRUE,
};

gboolean
gnm_conf_get_searchreplace_columnmajor (void)
{
	if (!watch_searchreplace_columnmajor.handler)
		watch_bool (&watch_searchreplace_columnmajor);
	return watch_searchreplace_columnmajor.var;
}

void
gnm_conf_set_searchreplace_columnmajor (gboolean x)
{
	if (!watch_searchreplace_columnmajor.handler)
		watch_bool (&watch_searchreplace_columnmajor);
	set_bool (&watch_searchreplace_columnmajor, x);
}

/**
 * gnm_conf_get_searchreplace_columnmajor_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_columnmajor_node (void)
{
	return get_watch_node (&watch_searchreplace_columnmajor);
}

static struct cb_watch_int watch_searchreplace_error_behaviour = {
	0, "searchreplace/error-behaviour",
	"Search & Replace Error Behavior",
	"This is the default error behavior of Search & Replace indicated by an integer from 0 to 4.",
	0, 4, 0,
};

int
gnm_conf_get_searchreplace_error_behaviour (void)
{
	if (!watch_searchreplace_error_behaviour.handler)
		watch_int (&watch_searchreplace_error_behaviour);
	return watch_searchreplace_error_behaviour.var;
}

void
gnm_conf_set_searchreplace_error_behaviour (int x)
{
	if (!watch_searchreplace_error_behaviour.handler)
		watch_int (&watch_searchreplace_error_behaviour);
	set_int (&watch_searchreplace_error_behaviour, x);
}

/**
 * gnm_conf_get_searchreplace_error_behaviour_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_error_behaviour_node (void)
{
	return get_watch_node (&watch_searchreplace_error_behaviour);
}

static struct cb_watch_bool watch_searchreplace_ignore_case = {
	0, "searchreplace/ignore-case",
	"Search & Replace Ignores Case",
	"Search & Replace ignores case as default",
	TRUE,
};

gboolean
gnm_conf_get_searchreplace_ignore_case (void)
{
	if (!watch_searchreplace_ignore_case.handler)
		watch_bool (&watch_searchreplace_ignore_case);
	return watch_searchreplace_ignore_case.var;
}

void
gnm_conf_set_searchreplace_ignore_case (gboolean x)
{
	if (!watch_searchreplace_ignore_case.handler)
		watch_bool (&watch_searchreplace_ignore_case);
	set_bool (&watch_searchreplace_ignore_case, x);
}

/**
 * gnm_conf_get_searchreplace_ignore_case_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_ignore_case_node (void)
{
	return get_watch_node (&watch_searchreplace_ignore_case);
}

static struct cb_watch_bool watch_searchreplace_keep_strings = {
	0, "searchreplace/keep-strings",
	"Search & Replace Keeps Strings as Strings",
	"Search & Replace keeps strings as strings even if they look like numbers as default",
	TRUE,
};

gboolean
gnm_conf_get_searchreplace_keep_strings (void)
{
	if (!watch_searchreplace_keep_strings.handler)
		watch_bool (&watch_searchreplace_keep_strings);
	return watch_searchreplace_keep_strings.var;
}

void
gnm_conf_set_searchreplace_keep_strings (gboolean x)
{
	if (!watch_searchreplace_keep_strings.handler)
		watch_bool (&watch_searchreplace_keep_strings);
	set_bool (&watch_searchreplace_keep_strings, x);
}

/**
 * gnm_conf_get_searchreplace_keep_strings_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_keep_strings_node (void)
{
	return get_watch_node (&watch_searchreplace_keep_strings);
}

static struct cb_watch_bool watch_searchreplace_preserve_case = {
	0, "searchreplace/preserve-case",
	"Search & Replace Preserves Case",
	"Search & Replace preserves case as default",
	FALSE,
};

gboolean
gnm_conf_get_searchreplace_preserve_case (void)
{
	if (!watch_searchreplace_preserve_case.handler)
		watch_bool (&watch_searchreplace_preserve_case);
	return watch_searchreplace_preserve_case.var;
}

void
gnm_conf_set_searchreplace_preserve_case (gboolean x)
{
	if (!watch_searchreplace_preserve_case.handler)
		watch_bool (&watch_searchreplace_preserve_case);
	set_bool (&watch_searchreplace_preserve_case, x);
}

/**
 * gnm_conf_get_searchreplace_preserve_case_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_preserve_case_node (void)
{
	return get_watch_node (&watch_searchreplace_preserve_case);
}

static struct cb_watch_bool watch_searchreplace_query = {
	0, "searchreplace/query",
	"Search & Replace Poses Query",
	"Search & Replace poses query before each change as default",
	FALSE,
};

gboolean
gnm_conf_get_searchreplace_query (void)
{
	if (!watch_searchreplace_query.handler)
		watch_bool (&watch_searchreplace_query);
	return watch_searchreplace_query.var;
}

void
gnm_conf_set_searchreplace_query (gboolean x)
{
	if (!watch_searchreplace_query.handler)
		watch_bool (&watch_searchreplace_query);
	set_bool (&watch_searchreplace_query, x);
}

/**
 * gnm_conf_get_searchreplace_query_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_query_node (void)
{
	return get_watch_node (&watch_searchreplace_query);
}

static struct cb_watch_int watch_searchreplace_regex = {
	0, "searchreplace/regex",
	"Search & Replace Search Type",
	"This value determines the input type for Search & Replace. 0: text; 1: regular expression; 2: number",
	0, 2, 0,
};

int
gnm_conf_get_searchreplace_regex (void)
{
	if (!watch_searchreplace_regex.handler)
		watch_int (&watch_searchreplace_regex);
	return watch_searchreplace_regex.var;
}

void
gnm_conf_set_searchreplace_regex (int x)
{
	if (!watch_searchreplace_regex.handler)
		watch_int (&watch_searchreplace_regex);
	set_int (&watch_searchreplace_regex, x);
}

/**
 * gnm_conf_get_searchreplace_regex_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_regex_node (void)
{
	return get_watch_node (&watch_searchreplace_regex);
}

static struct cb_watch_int watch_searchreplace_scope = {
	0, "searchreplace/scope",
	"Search & Replace Scope",
	"This is the default scope of Search & Replace. 0: entire workbook; 1: current sheet; 2: range",
	0, 2, 0,
};

int
gnm_conf_get_searchreplace_scope (void)
{
	if (!watch_searchreplace_scope.handler)
		watch_int (&watch_searchreplace_scope);
	return watch_searchreplace_scope.var;
}

void
gnm_conf_set_searchreplace_scope (int x)
{
	if (!watch_searchreplace_scope.handler)
		watch_int (&watch_searchreplace_scope);
	set_int (&watch_searchreplace_scope, x);
}

/**
 * gnm_conf_get_searchreplace_scope_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_scope_node (void)
{
	return get_watch_node (&watch_searchreplace_scope);
}

static struct cb_watch_bool watch_searchreplace_search_results = {
	0, "searchreplace/search-results",
	"Search searches in results",
	"Search searches in results as default",
	TRUE,
};

gboolean
gnm_conf_get_searchreplace_search_results (void)
{
	if (!watch_searchreplace_search_results.handler)
		watch_bool (&watch_searchreplace_search_results);
	return watch_searchreplace_search_results.var;
}

void
gnm_conf_set_searchreplace_search_results (gboolean x)
{
	if (!watch_searchreplace_search_results.handler)
		watch_bool (&watch_searchreplace_search_results);
	set_bool (&watch_searchreplace_search_results, x);
}

/**
 * gnm_conf_get_searchreplace_search_results_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_search_results_node (void)
{
	return get_watch_node (&watch_searchreplace_search_results);
}

static struct cb_watch_bool watch_searchreplace_whole_words_only = {
	0, "searchreplace/whole-words-only",
	"Search & Replace Whole Words Only",
	"Search & Replace replaces whole words only as default",
	FALSE,
};

gboolean
gnm_conf_get_searchreplace_whole_words_only (void)
{
	if (!watch_searchreplace_whole_words_only.handler)
		watch_bool (&watch_searchreplace_whole_words_only);
	return watch_searchreplace_whole_words_only.var;
}

void
gnm_conf_set_searchreplace_whole_words_only (gboolean x)
{
	if (!watch_searchreplace_whole_words_only.handler)
		watch_bool (&watch_searchreplace_whole_words_only);
	set_bool (&watch_searchreplace_whole_words_only, x);
}

/**
 * gnm_conf_get_searchreplace_whole_words_only_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_whole_words_only_node (void)
{
	return get_watch_node (&watch_searchreplace_whole_words_only);
}

static struct cb_watch_string watch_stf_export_encoding = {
	0, "stf/export/encoding",
	"Text Export Encoding",
	"Please use the Text Export dialog to edit this value.",
	"",
};

const char *
gnm_conf_get_stf_export_encoding (void)
{
	if (!watch_stf_export_encoding.handler)
		watch_string (&watch_stf_export_encoding);
	return watch_stf_export_encoding.var;
}

void
gnm_conf_set_stf_export_encoding (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_stf_export_encoding.handler)
		watch_string (&watch_stf_export_encoding);
	set_string (&watch_stf_export_encoding, x);
}

/**
 * gnm_conf_get_stf_export_encoding_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_encoding_node (void)
{
	return get_watch_node (&watch_stf_export_encoding);
}

static struct cb_watch_enum watch_stf_export_format = {
	0, "stf/export/format",
	"Text Export Formatting Rule",
	"Please use the Text Export dialog to edit this value.",
	GNM_STF_FORMAT_AUTO,
};

GnmStfFormatMode
gnm_conf_get_stf_export_format (void)
{
	if (!watch_stf_export_format.handler)
		watch_enum (&watch_stf_export_format, GNM_STF_FORMAT_MODE_TYPE);
	return watch_stf_export_format.var;
}

void
gnm_conf_set_stf_export_format (GnmStfFormatMode x)
{
	if (!watch_stf_export_format.handler)
		watch_enum (&watch_stf_export_format, GNM_STF_FORMAT_MODE_TYPE);
	set_enum (&watch_stf_export_format, x);
}

/**
 * gnm_conf_get_stf_export_format_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_format_node (void)
{
	return get_watch_node (&watch_stf_export_format);
}

static struct cb_watch_string watch_stf_export_locale = {
	0, "stf/export/locale",
	"Text Export Locale",
	"Please use the Text Export dialog to edit this value.",
	"",
};

const char *
gnm_conf_get_stf_export_locale (void)
{
	if (!watch_stf_export_locale.handler)
		watch_string (&watch_stf_export_locale);
	return watch_stf_export_locale.var;
}

void
gnm_conf_set_stf_export_locale (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_stf_export_locale.handler)
		watch_string (&watch_stf_export_locale);
	set_string (&watch_stf_export_locale, x);
}

/**
 * gnm_conf_get_stf_export_locale_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_locale_node (void)
{
	return get_watch_node (&watch_stf_export_locale);
}

static struct cb_watch_enum watch_stf_export_quoting = {
	0, "stf/export/quoting",
	"Text Export String Quoting Rule",
	"Please use the Text Export dialog to edit this value.",
	GSF_OUTPUT_CSV_QUOTING_MODE_AUTO,
};

GsfOutputCsvQuotingMode
gnm_conf_get_stf_export_quoting (void)
{
	if (!watch_stf_export_quoting.handler)
		watch_enum (&watch_stf_export_quoting, GSF_OUTPUT_CSV_QUOTING_MODE_TYPE);
	return watch_stf_export_quoting.var;
}

void
gnm_conf_set_stf_export_quoting (GsfOutputCsvQuotingMode x)
{
	if (!watch_stf_export_quoting.handler)
		watch_enum (&watch_stf_export_quoting, GSF_OUTPUT_CSV_QUOTING_MODE_TYPE);
	set_enum (&watch_stf_export_quoting, x);
}

/**
 * gnm_conf_get_stf_export_quoting_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_quoting_node (void)
{
	return get_watch_node (&watch_stf_export_quoting);
}

static struct cb_watch_string watch_stf_export_separator = {
	0, "stf/export/separator",
	"Text Export Field Separator",
	"Please use the Text Export dialog to edit this value.",
	",",
};

const char *
gnm_conf_get_stf_export_separator (void)
{
	if (!watch_stf_export_separator.handler)
		watch_string (&watch_stf_export_separator);
	return watch_stf_export_separator.var;
}

void
gnm_conf_set_stf_export_separator (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_stf_export_separator.handler)
		watch_string (&watch_stf_export_separator);
	set_string (&watch_stf_export_separator, x);
}

/**
 * gnm_conf_get_stf_export_separator_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_separator_node (void)
{
	return get_watch_node (&watch_stf_export_separator);
}

static struct cb_watch_string watch_stf_export_stringindicator = {
	0, "stf/export/stringindicator",
	"Text Export String Indicator",
	"Please use the Text Export dialog to edit this value.",
	"\"",
};

const char *
gnm_conf_get_stf_export_stringindicator (void)
{
	if (!watch_stf_export_stringindicator.handler)
		watch_string (&watch_stf_export_stringindicator);
	return watch_stf_export_stringindicator.var;
}

void
gnm_conf_set_stf_export_stringindicator (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_stf_export_stringindicator.handler)
		watch_string (&watch_stf_export_stringindicator);
	set_string (&watch_stf_export_stringindicator, x);
}

/**
 * gnm_conf_get_stf_export_stringindicator_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_stringindicator_node (void)
{
	return get_watch_node (&watch_stf_export_stringindicator);
}

static struct cb_watch_string watch_stf_export_terminator = {
	0, "stf/export/terminator",
	"Text Export Record Terminator",
	"Please use the Text Export dialog to edit this value.",
	"\n",
};

const char *
gnm_conf_get_stf_export_terminator (void)
{
	if (!watch_stf_export_terminator.handler)
		watch_string (&watch_stf_export_terminator);
	return watch_stf_export_terminator.var;
}

void
gnm_conf_set_stf_export_terminator (const char *x)
{
	g_return_if_fail (x != NULL);
	if (!watch_stf_export_terminator.handler)
		watch_string (&watch_stf_export_terminator);
	set_string (&watch_stf_export_terminator, x);
}

/**
 * gnm_conf_get_stf_export_terminator_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_terminator_node (void)
{
	return get_watch_node (&watch_stf_export_terminator);
}

static struct cb_watch_bool watch_stf_export_transliteration = {
	0, "stf/export/transliteration",
	"Text Export Unknown Character Transliteration",
	"Please use the Text Export dialog to edit this value.",
	TRUE,
};

gboolean
gnm_conf_get_stf_export_transliteration (void)
{
	if (!watch_stf_export_transliteration.handler)
		watch_bool (&watch_stf_export_transliteration);
	return watch_stf_export_transliteration.var;
}

void
gnm_conf_set_stf_export_transliteration (gboolean x)
{
	if (!watch_stf_export_transliteration.handler)
		watch_bool (&watch_stf_export_transliteration);
	set_bool (&watch_stf_export_transliteration, x);
}

/**
 * gnm_conf_get_stf_export_transliteration_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_transliteration_node (void)
{
	return get_watch_node (&watch_stf_export_transliteration);
}

static struct cb_watch_enum watch_toolbar_style = {
	0, "toolbar-style",
	"Toolbar Style",
	"Toolbar Style. Valid values are both, both_horiz, icon, and text.",
	GTK_TOOLBAR_ICONS,
};

GtkToolbarStyle
gnm_conf_get_toolbar_style (void)
{
	if (!watch_toolbar_style.handler)
		watch_enum (&watch_toolbar_style, GTK_TYPE_TOOLBAR_STYLE);
	return watch_toolbar_style.var;
}

void
gnm_conf_set_toolbar_style (GtkToolbarStyle x)
{
	if (!watch_toolbar_style.handler)
		watch_enum (&watch_toolbar_style, GTK_TYPE_TOOLBAR_STYLE);
	set_enum (&watch_toolbar_style, x);
}

static struct cb_watch_int watch_undo_max_descriptor_width = {
	0, "undo/max-descriptor-width",
	"Length of the Undo Descriptors",
	"This value is indicative of the maximum length of the command descriptors in the undo and redo chains.",
	5, 256, 40,
};

int
gnm_conf_get_undo_max_descriptor_width (void)
{
	if (!watch_undo_max_descriptor_width.handler)
		watch_int (&watch_undo_max_descriptor_width);
	return watch_undo_max_descriptor_width.var;
}

void
gnm_conf_set_undo_max_descriptor_width (int x)
{
	if (!watch_undo_max_descriptor_width.handler)
		watch_int (&watch_undo_max_descriptor_width);
	set_int (&watch_undo_max_descriptor_width, x);
}

/**
 * gnm_conf_get_undo_max_descriptor_width_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_undo_max_descriptor_width_node (void)
{
	return get_watch_node (&watch_undo_max_descriptor_width);
}

static struct cb_watch_int watch_undo_maxnum = {
	0, "undo/maxnum",
	"Number of Undo Items",
	"This value determines the maximum number of items in the undo/redo list.",
	0, 10000, 20,
};

int
gnm_conf_get_undo_maxnum (void)
{
	if (!watch_undo_maxnum.handler)
		watch_int (&watch_undo_maxnum);
	return watch_undo_maxnum.var;
}

void
gnm_conf_set_undo_maxnum (int x)
{
	if (!watch_undo_maxnum.handler)
		watch_int (&watch_undo_maxnum);
	set_int (&watch_undo_maxnum, x);
}

/**
 * gnm_conf_get_undo_maxnum_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_undo_maxnum_node (void)
{
	return get_watch_node (&watch_undo_maxnum);
}

static struct cb_watch_bool watch_undo_show_sheet_name = {
	0, "undo/show-sheet-name",
	"Show Sheet Name in Undo List",
	"This value determines whether to show the sheet names in the undo and redo lists.",
	FALSE,
};

gboolean
gnm_conf_get_undo_show_sheet_name (void)
{
	if (!watch_undo_show_sheet_name.handler)
		watch_bool (&watch_undo_show_sheet_name);
	return watch_undo_show_sheet_name.var;
}

void
gnm_conf_set_undo_show_sheet_name (gboolean x)
{
	if (!watch_undo_show_sheet_name.handler)
		watch_bool (&watch_undo_show_sheet_name);
	set_bool (&watch_undo_show_sheet_name, x);
}

/**
 * gnm_conf_get_undo_show_sheet_name_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_undo_show_sheet_name_node (void)
{
	return get_watch_node (&watch_undo_show_sheet_name);
}

static struct cb_watch_int watch_undo_size = {
	0, "undo/size",
	"Maximal Undo Size",
	"This value determines the length of the undo chain. Each editing action has a size associate with it, to compare it with the memory requirements of a simple one-cell edit (size of 1). The undo list will be truncated when its total size exceeds this configurable value.",
	1, 1000000, 100,
};

int
gnm_conf_get_undo_size (void)
{
	if (!watch_undo_size.handler)
		watch_int (&watch_undo_size);
	return watch_undo_size.var;
}

void
gnm_conf_set_undo_size (int x)
{
	if (!watch_undo_size.handler)
		watch_int (&watch_undo_size);
	set_int (&watch_undo_size, x);
}

/**
 * gnm_conf_get_undo_size_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_undo_size_node (void)
{
	return get_watch_node (&watch_undo_size);
}

/**
 * gnm_conf_get_autocorrect_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autocorrect_dir_node (void)
{
	return get_node ("autocorrect", NULL);
}

/**
 * gnm_conf_get_autoformat_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_autoformat_dir_node (void)
{
	return get_node ("autoformat", NULL);
}

/**
 * gnm_conf_get_core_defaultfont_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_defaultfont_dir_node (void)
{
	return get_node ("core/defaultfont", NULL);
}

/**
 * gnm_conf_get_core_file_save_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_file_save_dir_node (void)
{
	return get_node ("core/file/save", NULL);
}

/**
 * gnm_conf_get_core_gui_cells_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_cells_dir_node (void)
{
	return get_node ("core/gui/cells", NULL);
}

/**
 * gnm_conf_get_core_gui_editing_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_editing_dir_node (void)
{
	return get_node ("core/gui/editing", NULL);
}

/**
 * gnm_conf_get_core_gui_screen_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_screen_dir_node (void)
{
	return get_node ("core/gui/screen", NULL);
}

/**
 * gnm_conf_get_core_gui_toolbars_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_toolbars_dir_node (void)
{
	return get_node ("core/gui/toolbars", NULL);
}

/**
 * gnm_conf_get_core_gui_window_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_gui_window_dir_node (void)
{
	return get_node ("core/gui/window", NULL);
}

/**
 * gnm_conf_get_core_sort_default_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_sort_default_dir_node (void)
{
	return get_node ("core/sort/default", NULL);
}

/**
 * gnm_conf_get_core_sort_dialog_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_sort_dialog_dir_node (void)
{
	return get_node ("core/sort/dialog", NULL);
}

/**
 * gnm_conf_get_core_workbook_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_workbook_dir_node (void)
{
	return get_node ("core/workbook", NULL);
}

/**
 * gnm_conf_get_core_xml_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_core_xml_dir_node (void)
{
	return get_node ("core/xml", NULL);
}

/**
 * gnm_conf_get_cut_and_paste_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_cut_and_paste_dir_node (void)
{
	return get_node ("cut-and-paste", NULL);
}

/**
 * gnm_conf_get_dialogs_rs_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_dialogs_rs_dir_node (void)
{
	return get_node ("dialogs/rs", NULL);
}

/**
 * gnm_conf_get_functionselector_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_functionselector_dir_node (void)
{
	return get_node ("functionselector", NULL);
}

/**
 * gnm_conf_get_plugin_glpk_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugin_glpk_dir_node (void)
{
	return get_node ("plugin/glpk", NULL);
}

/**
 * gnm_conf_get_plugin_latex_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugin_latex_dir_node (void)
{
	return get_node ("plugin/latex", NULL);
}

/**
 * gnm_conf_get_plugin_lpsolve_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugin_lpsolve_dir_node (void)
{
	return get_node ("plugin/lpsolve", NULL);
}

/**
 * gnm_conf_get_plugins_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_plugins_dir_node (void)
{
	return get_node ("plugins", NULL);
}

/**
 * gnm_conf_get_printsetup_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_printsetup_dir_node (void)
{
	return get_node ("printsetup", NULL);
}

/**
 * gnm_conf_get_searchreplace_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_searchreplace_dir_node (void)
{
	return get_node ("searchreplace", NULL);
}

/**
 * gnm_conf_get_stf_export_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_stf_export_dir_node (void)
{
	return get_node ("stf/export", NULL);
}

/**
 * gnm_conf_get_toolbar_style_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_toolbar_style_dir_node (void)
{
	return get_node ("toolbar-style", NULL);
}

/**
 * gnm_conf_get_undo_dir_node:
 *
 * Returns: (transfer none): A #GOConfNode
 */
GOConfNode *
gnm_conf_get_undo_dir_node (void)
{
	return get_node ("undo", NULL);
}

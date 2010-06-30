/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-gconf.c:
 *
 * Author:
 *	Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2002-2005 Andreas J. Guelzow <aguelzow@taliesin.ca>
 * (C) Copyright 2009 Morten Welinder <terra@gnome.org>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "application.h"
#include "gnumeric-gconf.h"
#include "gutils.h"
#include "mstyle.h"
#include <goffice/goffice.h>
#include <value.h>
#include <number-match.h>
#include <string.h>
#include <sheet.h>
#include <print-info.h>

#define NO_DEBUG_GCONF
#ifndef NO_DEBUG_GCONF
#define d(code)	{ code; }
#else
#define d(code)
#endif

#define GNM_CONF_DIR "gnumeric"

static GOConfNode *root = NULL;

/*
 * Hashes to simply ownership rules.  We use this so none of the getters
 * have to return memory that the callers needs to free.
  */
static GHashTable *string_pool;
static GHashTable *string_list_pool;
static GHashTable *node_pool;

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

GOConfNode *
gnm_conf_get_root (void)
{
	return root;
}

static GOConfNode *
get_node (const char *key)
{
	GOConfNode *res = g_hash_table_lookup (node_pool, key);
	if (!res) {
		res = go_conf_get_node (root, key);
		g_hash_table_insert (node_pool, (gpointer)key, res);
	}
	return res;
}

/* -------------------------------------------------------------------------- */

static GSList *watchers;

struct cb_watch_generic {
	guint handler;
};

static void
free_watcher (struct cb_watch_generic *watcher)
{
	go_conf_remove_monitor (watcher->handler);
}

/* ---------------------------------------- */

struct cb_watch_bool {
	guint handler;
	const char *key;
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
	GOConfNode *node = get_node (watch->key);
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
	go_conf_set_bool (root, watch->key, x);
	schedule_sync ();
}

/* ---------------------------------------- */

struct cb_watch_int {
	guint handler;
	const char *key;
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
	GOConfNode *node = get_node (watch->key);
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
	go_conf_set_int (root, watch->key, x);
	schedule_sync ();
}

/* ---------------------------------------- */

struct cb_watch_double {
	guint handler;
	const char *key;
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
	GOConfNode *node = get_node (watch->key);
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
	go_conf_set_double (root, watch->key, x);
	schedule_sync ();
}

/* ---------------------------------------- */

struct cb_watch_string {
	guint handler;
	const char *key;
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
	GOConfNode *node = get_node (watch->key);
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
	go_conf_set_string (root, watch->key, xc);
	schedule_sync ();
}

/* ---------------------------------------- */

struct cb_watch_string_list {
	guint handler;
	const char *key;
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
	GOConfNode *node = get_node (watch->key);
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
	go_conf_set_str_list (root, watch->key, x);
	schedule_sync ();
}

/* ---------------------------------------- */

struct cb_watch_enum {
	guint handler;
	const char *key;
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
	GOConfNode *node = get_node (watch->key);
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
	go_conf_set_enum (root, watch->key, watch->typ, x);
	schedule_sync ();
}

/* -------------------------------------------------------------------------- */

static void
cb_free_string_list (GSList *l)
{
	go_slist_free_custom (l, g_free);
}

void
gnm_conf_init (void)
{
	string_pool = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 NULL, g_free);
	string_list_pool = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 NULL, (GDestroyNotify)cb_free_string_list);
	node_pool = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 NULL, (GDestroyNotify)go_conf_free_node);

	root = go_conf_get_node (NULL, GNM_CONF_DIR);
	g_hash_table_insert (node_pool, (gpointer)"/", root);

	debug_getters = gnm_debug_flag ("conf-get");
	debug_setters = gnm_debug_flag ("conf-set");
}

void
gnm_conf_shutdown (void)
{
	go_conf_sync (root);
	if (sync_handler) {
		g_source_remove (sync_handler);
		sync_handler = 0;
	}

	go_slist_free_custom (watchers, (GFreeFunc)free_watcher);
	watchers = NULL;

	g_hash_table_destroy (string_pool);
	string_pool = NULL;

	g_hash_table_destroy (string_list_pool);
	string_list_pool = NULL;

	g_hash_table_destroy (node_pool);
	node_pool = NULL;
	root = NULL;
}

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

#define TOOLBAR_TANGO(Object,Format,LongFormat,Standard)	\
	if (strcmp (name, "ObjectToolbar") == 0)		\
		Object						\
	else if (strcmp (name, "FormatToolbar") == 0)		\
		Format						\
	else if (strcmp (name, "LongFormatToolbar") == 0)	\
		LongFormat					\
	else if (strcmp (name, "StandardToolbar") == 0)		\
		Standard


gboolean
gnm_conf_get_toolbar_visible (const char *name)
{
	TOOLBAR_TANGO
		(return gnm_conf_get_core_gui_toolbars_ObjectToolbar ();,
		 return gnm_conf_get_core_gui_toolbars_FormatToolbar ();,
		 return gnm_conf_get_core_gui_toolbars_LongFormatToolbar ();,
		 return gnm_conf_get_core_gui_toolbars_StandardToolbar (););

	g_warning ("Unknown toolbar: %s", name);
	return FALSE;
}

void
gnm_conf_set_toolbar_visible (const char *name, gboolean x)
{
	TOOLBAR_TANGO
		(gnm_conf_set_core_gui_toolbars_ObjectToolbar (x);,
		 gnm_conf_set_core_gui_toolbars_FormatToolbar (x);,
		 gnm_conf_set_core_gui_toolbars_LongFormatToolbar (x);,
		 gnm_conf_set_core_gui_toolbars_StandardToolbar (x););
}

GtkPositionType
gnm_conf_get_toolbar_position (const char *name)
{
	TOOLBAR_TANGO
		(return gnm_conf_get_core_gui_toolbars_ObjectToolbar_position ();,
		 return gnm_conf_get_core_gui_toolbars_FormatToolbar_position ();,
		 return gnm_conf_get_core_gui_toolbars_LongFormatToolbar_position ();,
		 return gnm_conf_get_core_gui_toolbars_StandardToolbar_position (););

	g_warning ("Unknown toolbar: %s", name);
	return GTK_POS_TOP;
}

void
gnm_conf_set_toolbar_position (const char *name, GtkPositionType x)
{
	TOOLBAR_TANGO
		(gnm_conf_set_core_gui_toolbars_ObjectToolbar_position (x);,
		 gnm_conf_set_core_gui_toolbars_FormatToolbar_position (x);,
		 gnm_conf_set_core_gui_toolbars_LongFormatToolbar_position (x);,
		 gnm_conf_set_core_gui_toolbars_StandardToolbar_position (x););
}

#undef TOOLBAR_TANGO

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
	go_slist_free_custom (list, g_free);
}

gboolean
gnm_conf_get_detachable_toolbars (void)
{
#ifdef WIN32
	return FALSE;
#else
	return go_conf_get_bool
		(NULL,
		 "/desktop/gnome/interface/toolbar_detachable");
#endif
}

/* ------------------------------------------------------------------------- */
/*
 * The following code was generated by running
 *
 *     cd src
 *     perl ../tools/handle-conf-options --cfile \
 *             ../schemas/gnumeric*.schemas.in >~/xxx
 *
 * The corresponding headers were generated using "--hfile".
 */


/* ----------- AUTOMATICALLY GENERATED CODE BELOW -- DO NOT EDIT ----------- */

static struct cb_watch_enum watch_toolbar_style = {
	0, "/apps/gnome-settings/gnumeric/toolbar_style", GTK_TOOLBAR_ICONS,
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

static struct cb_watch_bool watch_autocorrect_first_letter = {
	0, "autocorrect/first-letter", TRUE,
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

GOConfNode *
gnm_conf_get_autocorrect_first_letter_node (void)
{
	return get_node (watch_autocorrect_first_letter.key);
}

static struct cb_watch_string_list watch_autocorrect_first_letter_list = {
	0, "autocorrect/first-letter-list",
};

GSList *
gnm_conf_get_autocorrect_first_letter_list (void)
{
	if (!watch_autocorrect_first_letter_list.handler)
		watch_string_list (&watch_autocorrect_first_letter_list);
	return watch_autocorrect_first_letter_list.var;
}

void
gnm_conf_set_autocorrect_first_letter_list (GSList *x)
{
	if (!watch_autocorrect_first_letter_list.handler)
		watch_string_list (&watch_autocorrect_first_letter_list);
	set_string_list (&watch_autocorrect_first_letter_list, x);
}

GOConfNode *
gnm_conf_get_autocorrect_first_letter_list_node (void)
{
	return get_node (watch_autocorrect_first_letter_list.key);
}

static struct cb_watch_bool watch_autocorrect_init_caps = {
	0, "autocorrect/init-caps", TRUE,
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

GOConfNode *
gnm_conf_get_autocorrect_init_caps_node (void)
{
	return get_node (watch_autocorrect_init_caps.key);
}

static struct cb_watch_string_list watch_autocorrect_init_caps_list = {
	0, "autocorrect/init-caps-list",
};

GSList *
gnm_conf_get_autocorrect_init_caps_list (void)
{
	if (!watch_autocorrect_init_caps_list.handler)
		watch_string_list (&watch_autocorrect_init_caps_list);
	return watch_autocorrect_init_caps_list.var;
}

void
gnm_conf_set_autocorrect_init_caps_list (GSList *x)
{
	if (!watch_autocorrect_init_caps_list.handler)
		watch_string_list (&watch_autocorrect_init_caps_list);
	set_string_list (&watch_autocorrect_init_caps_list, x);
}

GOConfNode *
gnm_conf_get_autocorrect_init_caps_list_node (void)
{
	return get_node (watch_autocorrect_init_caps_list.key);
}

static struct cb_watch_bool watch_autocorrect_names_of_days = {
	0, "autocorrect/names-of-days", TRUE,
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

GOConfNode *
gnm_conf_get_autocorrect_names_of_days_node (void)
{
	return get_node (watch_autocorrect_names_of_days.key);
}

static struct cb_watch_bool watch_autocorrect_replace = {
	0, "autocorrect/replace", TRUE,
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

GOConfNode *
gnm_conf_get_autocorrect_replace_node (void)
{
	return get_node (watch_autocorrect_replace.key);
}

static struct cb_watch_string_list watch_autoformat_extra_dirs = {
	0, "autoformat/extra-dirs",
};

GSList *
gnm_conf_get_autoformat_extra_dirs (void)
{
	if (!watch_autoformat_extra_dirs.handler)
		watch_string_list (&watch_autoformat_extra_dirs);
	return watch_autoformat_extra_dirs.var;
}

void
gnm_conf_set_autoformat_extra_dirs (GSList *x)
{
	if (!watch_autoformat_extra_dirs.handler)
		watch_string_list (&watch_autoformat_extra_dirs);
	set_string_list (&watch_autoformat_extra_dirs, x);
}

GOConfNode *
gnm_conf_get_autoformat_extra_dirs_node (void)
{
	return get_node (watch_autoformat_extra_dirs.key);
}

static struct cb_watch_string watch_autoformat_sys_dir = {
	0, "autoformat/sys-dir", "autoformat-templates",
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

GOConfNode *
gnm_conf_get_autoformat_sys_dir_node (void)
{
	return get_node (watch_autoformat_sys_dir.key);
}

static struct cb_watch_string watch_autoformat_usr_dir = {
	0, "autoformat/usr-dir", "autoformat-templates",
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

GOConfNode *
gnm_conf_get_autoformat_usr_dir_node (void)
{
	return get_node (watch_autoformat_usr_dir.key);
}

static struct cb_watch_bool watch_core_defaultfont_bold = {
	0, "core/defaultfont/bold", FALSE,
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

GOConfNode *
gnm_conf_get_core_defaultfont_bold_node (void)
{
	return get_node (watch_core_defaultfont_bold.key);
}

static struct cb_watch_bool watch_core_defaultfont_italic = {
	0, "core/defaultfont/italic", FALSE,
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

GOConfNode *
gnm_conf_get_core_defaultfont_italic_node (void)
{
	return get_node (watch_core_defaultfont_italic.key);
}

static struct cb_watch_string watch_core_defaultfont_name = {
	0, "core/defaultfont/name", "Sans",
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

GOConfNode *
gnm_conf_get_core_defaultfont_name_node (void)
{
	return get_node (watch_core_defaultfont_name.key);
}

static struct cb_watch_double watch_core_defaultfont_size = {
	0, "core/defaultfont/size", 1, 100, 10,
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

GOConfNode *
gnm_conf_get_core_defaultfont_size_node (void)
{
	return get_node (watch_core_defaultfont_size.key);
}

static struct cb_watch_bool watch_core_file_save_def_overwrite = {
	0, "core/file/save/def-overwrite", FALSE,
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

GOConfNode *
gnm_conf_get_core_file_save_def_overwrite_node (void)
{
	return get_node (watch_core_file_save_def_overwrite.key);
}

static struct cb_watch_bool watch_core_file_save_single_sheet = {
	0, "core/file/save/single_sheet", TRUE,
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

GOConfNode *
gnm_conf_get_core_file_save_single_sheet_node (void)
{
	return get_node (watch_core_file_save_single_sheet.key);
}

static struct cb_watch_bool watch_core_gui_editing_autocomplete = {
	0, "core/gui/editing/autocomplete", TRUE,
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

GOConfNode *
gnm_conf_get_core_gui_editing_autocomplete_node (void)
{
	return get_node (watch_core_gui_editing_autocomplete.key);
}

static struct cb_watch_enum watch_core_gui_editing_enter_moves_dir = {
	0, "core/gui/editing/enter_moves_dir", GO_DIRECTION_DOWN,
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

GOConfNode *
gnm_conf_get_core_gui_editing_enter_moves_dir_node (void)
{
	return get_node (watch_core_gui_editing_enter_moves_dir.key);
}

static struct cb_watch_bool watch_core_gui_editing_livescrolling = {
	0, "core/gui/editing/livescrolling", TRUE,
};

gboolean
gnm_conf_get_core_gui_editing_livescrolling (void)
{
	if (!watch_core_gui_editing_livescrolling.handler)
		watch_bool (&watch_core_gui_editing_livescrolling);
	return watch_core_gui_editing_livescrolling.var;
}

void
gnm_conf_set_core_gui_editing_livescrolling (gboolean x)
{
	if (!watch_core_gui_editing_livescrolling.handler)
		watch_bool (&watch_core_gui_editing_livescrolling);
	set_bool (&watch_core_gui_editing_livescrolling, x);
}

GOConfNode *
gnm_conf_get_core_gui_editing_livescrolling_node (void)
{
	return get_node (watch_core_gui_editing_livescrolling.key);
}

static struct cb_watch_int watch_core_gui_editing_recalclag = {
	0, "core/gui/editing/recalclag", -5000, 5000, 200,
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

GOConfNode *
gnm_conf_get_core_gui_editing_recalclag_node (void)
{
	return get_node (watch_core_gui_editing_recalclag.key);
}

static struct cb_watch_bool watch_core_gui_editing_transitionkeys = {
	0, "core/gui/editing/transitionkeys", FALSE,
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

GOConfNode *
gnm_conf_get_core_gui_editing_transitionkeys_node (void)
{
	return get_node (watch_core_gui_editing_transitionkeys.key);
}

static struct cb_watch_double watch_core_gui_screen_horizontaldpi = {
	0, "core/gui/screen/horizontaldpi", 10, 1000, 96,
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

GOConfNode *
gnm_conf_get_core_gui_screen_horizontaldpi_node (void)
{
	return get_node (watch_core_gui_screen_horizontaldpi.key);
}

static struct cb_watch_double watch_core_gui_screen_verticaldpi = {
	0, "core/gui/screen/verticaldpi", 10, 1000, 96,
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

GOConfNode *
gnm_conf_get_core_gui_screen_verticaldpi_node (void)
{
	return get_node (watch_core_gui_screen_verticaldpi.key);
}

static struct cb_watch_bool watch_core_gui_toolbars_FormatToolbar = {
	0, "core/gui/toolbars/FormatToolbar", TRUE,
};

gboolean
gnm_conf_get_core_gui_toolbars_FormatToolbar (void)
{
	if (!watch_core_gui_toolbars_FormatToolbar.handler)
		watch_bool (&watch_core_gui_toolbars_FormatToolbar);
	return watch_core_gui_toolbars_FormatToolbar.var;
}

void
gnm_conf_set_core_gui_toolbars_FormatToolbar (gboolean x)
{
	if (!watch_core_gui_toolbars_FormatToolbar.handler)
		watch_bool (&watch_core_gui_toolbars_FormatToolbar);
	set_bool (&watch_core_gui_toolbars_FormatToolbar, x);
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_FormatToolbar_node (void)
{
	return get_node (watch_core_gui_toolbars_FormatToolbar.key);
}

static struct cb_watch_int watch_core_gui_toolbars_FormatToolbar_position = {
	0, "core/gui/toolbars/FormatToolbar-position", 0, 3, 2,
};

GtkPositionType
gnm_conf_get_core_gui_toolbars_FormatToolbar_position (void)
{
	if (!watch_core_gui_toolbars_FormatToolbar_position.handler)
		watch_int (&watch_core_gui_toolbars_FormatToolbar_position);
	return watch_core_gui_toolbars_FormatToolbar_position.var;
}

void
gnm_conf_set_core_gui_toolbars_FormatToolbar_position (GtkPositionType x)
{
	if (!watch_core_gui_toolbars_FormatToolbar_position.handler)
		watch_int (&watch_core_gui_toolbars_FormatToolbar_position);
	set_int (&watch_core_gui_toolbars_FormatToolbar_position, x);
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_FormatToolbar_position_node (void)
{
	return get_node (watch_core_gui_toolbars_FormatToolbar_position.key);
}

static struct cb_watch_bool watch_core_gui_toolbars_LongFormatToolbar = {
	0, "core/gui/toolbars/LongFormatToolbar", FALSE,
};

gboolean
gnm_conf_get_core_gui_toolbars_LongFormatToolbar (void)
{
	if (!watch_core_gui_toolbars_LongFormatToolbar.handler)
		watch_bool (&watch_core_gui_toolbars_LongFormatToolbar);
	return watch_core_gui_toolbars_LongFormatToolbar.var;
}

void
gnm_conf_set_core_gui_toolbars_LongFormatToolbar (gboolean x)
{
	if (!watch_core_gui_toolbars_LongFormatToolbar.handler)
		watch_bool (&watch_core_gui_toolbars_LongFormatToolbar);
	set_bool (&watch_core_gui_toolbars_LongFormatToolbar, x);
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_LongFormatToolbar_node (void)
{
	return get_node (watch_core_gui_toolbars_LongFormatToolbar.key);
}

static struct cb_watch_int watch_core_gui_toolbars_LongFormatToolbar_position = {
	0, "core/gui/toolbars/LongFormatToolbar-position", 0, 3, 2,
};

GtkPositionType
gnm_conf_get_core_gui_toolbars_LongFormatToolbar_position (void)
{
	if (!watch_core_gui_toolbars_LongFormatToolbar_position.handler)
		watch_int (&watch_core_gui_toolbars_LongFormatToolbar_position);
	return watch_core_gui_toolbars_LongFormatToolbar_position.var;
}

void
gnm_conf_set_core_gui_toolbars_LongFormatToolbar_position (GtkPositionType x)
{
	if (!watch_core_gui_toolbars_LongFormatToolbar_position.handler)
		watch_int (&watch_core_gui_toolbars_LongFormatToolbar_position);
	set_int (&watch_core_gui_toolbars_LongFormatToolbar_position, x);
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_LongFormatToolbar_position_node (void)
{
	return get_node (watch_core_gui_toolbars_LongFormatToolbar_position.key);
}

static struct cb_watch_bool watch_core_gui_toolbars_ObjectToolbar = {
	0, "core/gui/toolbars/ObjectToolbar", FALSE,
};

gboolean
gnm_conf_get_core_gui_toolbars_ObjectToolbar (void)
{
	if (!watch_core_gui_toolbars_ObjectToolbar.handler)
		watch_bool (&watch_core_gui_toolbars_ObjectToolbar);
	return watch_core_gui_toolbars_ObjectToolbar.var;
}

void
gnm_conf_set_core_gui_toolbars_ObjectToolbar (gboolean x)
{
	if (!watch_core_gui_toolbars_ObjectToolbar.handler)
		watch_bool (&watch_core_gui_toolbars_ObjectToolbar);
	set_bool (&watch_core_gui_toolbars_ObjectToolbar, x);
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_ObjectToolbar_node (void)
{
	return get_node (watch_core_gui_toolbars_ObjectToolbar.key);
}

static struct cb_watch_int watch_core_gui_toolbars_ObjectToolbar_position = {
	0, "core/gui/toolbars/ObjectToolbar-position", 0, 3, 2,
};

GtkPositionType
gnm_conf_get_core_gui_toolbars_ObjectToolbar_position (void)
{
	if (!watch_core_gui_toolbars_ObjectToolbar_position.handler)
		watch_int (&watch_core_gui_toolbars_ObjectToolbar_position);
	return watch_core_gui_toolbars_ObjectToolbar_position.var;
}

void
gnm_conf_set_core_gui_toolbars_ObjectToolbar_position (GtkPositionType x)
{
	if (!watch_core_gui_toolbars_ObjectToolbar_position.handler)
		watch_int (&watch_core_gui_toolbars_ObjectToolbar_position);
	set_int (&watch_core_gui_toolbars_ObjectToolbar_position, x);
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_ObjectToolbar_position_node (void)
{
	return get_node (watch_core_gui_toolbars_ObjectToolbar_position.key);
}

static struct cb_watch_bool watch_core_gui_toolbars_StandardToolbar = {
	0, "core/gui/toolbars/StandardToolbar", TRUE,
};

gboolean
gnm_conf_get_core_gui_toolbars_StandardToolbar (void)
{
	if (!watch_core_gui_toolbars_StandardToolbar.handler)
		watch_bool (&watch_core_gui_toolbars_StandardToolbar);
	return watch_core_gui_toolbars_StandardToolbar.var;
}

void
gnm_conf_set_core_gui_toolbars_StandardToolbar (gboolean x)
{
	if (!watch_core_gui_toolbars_StandardToolbar.handler)
		watch_bool (&watch_core_gui_toolbars_StandardToolbar);
	set_bool (&watch_core_gui_toolbars_StandardToolbar, x);
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_StandardToolbar_node (void)
{
	return get_node (watch_core_gui_toolbars_StandardToolbar.key);
}

static struct cb_watch_int watch_core_gui_toolbars_StandardToolbar_position = {
	0, "core/gui/toolbars/StandardToolbar-position", 0, 3, 2,
};

GtkPositionType
gnm_conf_get_core_gui_toolbars_StandardToolbar_position (void)
{
	if (!watch_core_gui_toolbars_StandardToolbar_position.handler)
		watch_int (&watch_core_gui_toolbars_StandardToolbar_position);
	return watch_core_gui_toolbars_StandardToolbar_position.var;
}

void
gnm_conf_set_core_gui_toolbars_StandardToolbar_position (GtkPositionType x)
{
	if (!watch_core_gui_toolbars_StandardToolbar_position.handler)
		watch_int (&watch_core_gui_toolbars_StandardToolbar_position);
	set_int (&watch_core_gui_toolbars_StandardToolbar_position, x);
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_StandardToolbar_position_node (void)
{
	return get_node (watch_core_gui_toolbars_StandardToolbar_position.key);
}

static struct cb_watch_double watch_core_gui_window_x = {
	0, "core/gui/window/x", 0.1, 1, 0.75,
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

GOConfNode *
gnm_conf_get_core_gui_window_x_node (void)
{
	return get_node (watch_core_gui_window_x.key);
}

static struct cb_watch_double watch_core_gui_window_y = {
	0, "core/gui/window/y", 0.1, 1, 0.75,
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

GOConfNode *
gnm_conf_get_core_gui_window_y_node (void)
{
	return get_node (watch_core_gui_window_y.key);
}

static struct cb_watch_double watch_core_gui_window_zoom = {
	0, "core/gui/window/zoom", 0.1, 5, 1,
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

GOConfNode *
gnm_conf_get_core_gui_window_zoom_node (void)
{
	return get_node (watch_core_gui_window_zoom.key);
}

static struct cb_watch_bool watch_core_sort_default_ascending = {
	0, "core/sort/default/ascending", TRUE,
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

GOConfNode *
gnm_conf_get_core_sort_default_ascending_node (void)
{
	return get_node (watch_core_sort_default_ascending.key);
}

static struct cb_watch_bool watch_core_sort_default_by_case = {
	0, "core/sort/default/by-case", FALSE,
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

GOConfNode *
gnm_conf_get_core_sort_default_by_case_node (void)
{
	return get_node (watch_core_sort_default_by_case.key);
}

static struct cb_watch_bool watch_core_sort_default_retain_formats = {
	0, "core/sort/default/retain-formats", TRUE,
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

GOConfNode *
gnm_conf_get_core_sort_default_retain_formats_node (void)
{
	return get_node (watch_core_sort_default_retain_formats.key);
}

static struct cb_watch_int watch_core_sort_dialog_max_initial_clauses = {
	0, "core/sort/dialog/max-initial-clauses", 0, 256, 10,
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

GOConfNode *
gnm_conf_get_core_sort_dialog_max_initial_clauses_node (void)
{
	return get_node (watch_core_sort_dialog_max_initial_clauses.key);
}

static struct cb_watch_int watch_core_workbook_autosave_time = {
	0, "core/workbook/autosave_time", 0, 365 * 24 * 60 * 60, 0,
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

GOConfNode *
gnm_conf_get_core_workbook_autosave_time_node (void)
{
	return get_node (watch_core_workbook_autosave_time.key);
}

static struct cb_watch_int watch_core_workbook_n_cols = {
	0, "core/workbook/n-cols", GNM_MIN_COLS, GNM_MAX_COLS, 256,
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

GOConfNode *
gnm_conf_get_core_workbook_n_cols_node (void)
{
	return get_node (watch_core_workbook_n_cols.key);
}

static struct cb_watch_int watch_core_workbook_n_rows = {
	0, "core/workbook/n-rows", GNM_MIN_ROWS, GNM_MAX_ROWS, 65536,
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

GOConfNode *
gnm_conf_get_core_workbook_n_rows_node (void)
{
	return get_node (watch_core_workbook_n_rows.key);
}

static struct cb_watch_int watch_core_workbook_n_sheet = {
	0, "core/workbook/n-sheet", 1, 64, 3,
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

GOConfNode *
gnm_conf_get_core_workbook_n_sheet_node (void)
{
	return get_node (watch_core_workbook_n_sheet.key);
}

static struct cb_watch_int watch_core_xml_compression_level = {
	0, "core/xml/compression-level", 0, 9, 9,
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

GOConfNode *
gnm_conf_get_core_xml_compression_level_node (void)
{
	return get_node (watch_core_xml_compression_level.key);
}

static struct cb_watch_bool watch_cut_and_paste_prefer_clipboard = {
	0, "cut-and-paste/prefer-clipboard", TRUE,
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

GOConfNode *
gnm_conf_get_cut_and_paste_prefer_clipboard_node (void)
{
	return get_node (watch_cut_and_paste_prefer_clipboard.key);
}

static struct cb_watch_bool watch_dialogs_rs_unfocused = {
	0, "dialogs/rs/unfocused", FALSE,
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

GOConfNode *
gnm_conf_get_dialogs_rs_unfocused_node (void)
{
	return get_node (watch_dialogs_rs_unfocused.key);
}

static struct cb_watch_int watch_functionselector_num_of_recent = {
	0, "functionselector/num-of-recent", 0, 40, 12,
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

GOConfNode *
gnm_conf_get_functionselector_num_of_recent_node (void)
{
	return get_node (watch_functionselector_num_of_recent.key);
}

static struct cb_watch_string_list watch_functionselector_recentfunctions = {
	0, "functionselector/recentfunctions",
};

GSList *
gnm_conf_get_functionselector_recentfunctions (void)
{
	if (!watch_functionselector_recentfunctions.handler)
		watch_string_list (&watch_functionselector_recentfunctions);
	return watch_functionselector_recentfunctions.var;
}

void
gnm_conf_set_functionselector_recentfunctions (GSList *x)
{
	if (!watch_functionselector_recentfunctions.handler)
		watch_string_list (&watch_functionselector_recentfunctions);
	set_string_list (&watch_functionselector_recentfunctions, x);
}

GOConfNode *
gnm_conf_get_functionselector_recentfunctions_node (void)
{
	return get_node (watch_functionselector_recentfunctions.key);
}

static struct cb_watch_bool watch_plugin_latex_use_utf8 = {
	0, "plugin/latex/use-utf8", FALSE,
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

GOConfNode *
gnm_conf_get_plugin_latex_use_utf8_node (void)
{
	return get_node (watch_plugin_latex_use_utf8.key);
}

static struct cb_watch_bool watch_plugins_activate_new = {
	0, "plugins/activate-new", TRUE,
};

gboolean
gnm_conf_get_plugins_activate_new (void)
{
	if (!watch_plugins_activate_new.handler)
		watch_bool (&watch_plugins_activate_new);
	return watch_plugins_activate_new.var;
}

void
gnm_conf_set_plugins_activate_new (gboolean x)
{
	if (!watch_plugins_activate_new.handler)
		watch_bool (&watch_plugins_activate_new);
	set_bool (&watch_plugins_activate_new, x);
}

GOConfNode *
gnm_conf_get_plugins_activate_new_node (void)
{
	return get_node (watch_plugins_activate_new.key);
}

static struct cb_watch_string_list watch_plugins_active = {
	0, "plugins/active",
};

GSList *
gnm_conf_get_plugins_active (void)
{
	if (!watch_plugins_active.handler)
		watch_string_list (&watch_plugins_active);
	return watch_plugins_active.var;
}

void
gnm_conf_set_plugins_active (GSList *x)
{
	if (!watch_plugins_active.handler)
		watch_string_list (&watch_plugins_active);
	set_string_list (&watch_plugins_active, x);
}

GOConfNode *
gnm_conf_get_plugins_active_node (void)
{
	return get_node (watch_plugins_active.key);
}

static struct cb_watch_string_list watch_plugins_extra_dirs = {
	0, "plugins/extra-dirs",
};

GSList *
gnm_conf_get_plugins_extra_dirs (void)
{
	if (!watch_plugins_extra_dirs.handler)
		watch_string_list (&watch_plugins_extra_dirs);
	return watch_plugins_extra_dirs.var;
}

void
gnm_conf_set_plugins_extra_dirs (GSList *x)
{
	if (!watch_plugins_extra_dirs.handler)
		watch_string_list (&watch_plugins_extra_dirs);
	set_string_list (&watch_plugins_extra_dirs, x);
}

GOConfNode *
gnm_conf_get_plugins_extra_dirs_node (void)
{
	return get_node (watch_plugins_extra_dirs.key);
}

static struct cb_watch_string_list watch_plugins_file_states = {
	0, "plugins/file-states",
};

GSList *
gnm_conf_get_plugins_file_states (void)
{
	if (!watch_plugins_file_states.handler)
		watch_string_list (&watch_plugins_file_states);
	return watch_plugins_file_states.var;
}

void
gnm_conf_set_plugins_file_states (GSList *x)
{
	if (!watch_plugins_file_states.handler)
		watch_string_list (&watch_plugins_file_states);
	set_string_list (&watch_plugins_file_states, x);
}

GOConfNode *
gnm_conf_get_plugins_file_states_node (void)
{
	return get_node (watch_plugins_file_states.key);
}

static struct cb_watch_string_list watch_plugins_known = {
	0, "plugins/known",
};

GSList *
gnm_conf_get_plugins_known (void)
{
	if (!watch_plugins_known.handler)
		watch_string_list (&watch_plugins_known);
	return watch_plugins_known.var;
}

void
gnm_conf_set_plugins_known (GSList *x)
{
	if (!watch_plugins_known.handler)
		watch_string_list (&watch_plugins_known);
	set_string_list (&watch_plugins_known, x);
}

GOConfNode *
gnm_conf_get_plugins_known_node (void)
{
	return get_node (watch_plugins_known.key);
}

static struct cb_watch_bool watch_printsetup_across_then_down = {
	0, "printsetup/across-then-down", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_across_then_down_node (void)
{
	return get_node (watch_printsetup_across_then_down.key);
}

static struct cb_watch_bool watch_printsetup_all_sheets = {
	0, "printsetup/all-sheets", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_all_sheets_node (void)
{
	return get_node (watch_printsetup_all_sheets.key);
}

static struct cb_watch_bool watch_printsetup_center_horizontally = {
	0, "printsetup/center-horizontally", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_center_horizontally_node (void)
{
	return get_node (watch_printsetup_center_horizontally.key);
}

static struct cb_watch_bool watch_printsetup_center_vertically = {
	0, "printsetup/center-vertically", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_center_vertically_node (void)
{
	return get_node (watch_printsetup_center_vertically.key);
}

static struct cb_watch_string_list watch_printsetup_footer = {
	0, "printsetup/footer",
};

GSList *
gnm_conf_get_printsetup_footer (void)
{
	if (!watch_printsetup_footer.handler)
		watch_string_list (&watch_printsetup_footer);
	return watch_printsetup_footer.var;
}

void
gnm_conf_set_printsetup_footer (GSList *x)
{
	if (!watch_printsetup_footer.handler)
		watch_string_list (&watch_printsetup_footer);
	set_string_list (&watch_printsetup_footer, x);
}

GOConfNode *
gnm_conf_get_printsetup_footer_node (void)
{
	return get_node (watch_printsetup_footer.key);
}

static struct cb_watch_string_list watch_printsetup_gtk_setting = {
	0, "printsetup/gtk-setting",
};

GSList *
gnm_conf_get_printsetup_gtk_setting (void)
{
	if (!watch_printsetup_gtk_setting.handler)
		watch_string_list (&watch_printsetup_gtk_setting);
	return watch_printsetup_gtk_setting.var;
}

void
gnm_conf_set_printsetup_gtk_setting (GSList *x)
{
	if (!watch_printsetup_gtk_setting.handler)
		watch_string_list (&watch_printsetup_gtk_setting);
	set_string_list (&watch_printsetup_gtk_setting, x);
}

GOConfNode *
gnm_conf_get_printsetup_gtk_setting_node (void)
{
	return get_node (watch_printsetup_gtk_setting.key);
}

static struct cb_watch_string_list watch_printsetup_header = {
	0, "printsetup/header",
};

GSList *
gnm_conf_get_printsetup_header (void)
{
	if (!watch_printsetup_header.handler)
		watch_string_list (&watch_printsetup_header);
	return watch_printsetup_header.var;
}

void
gnm_conf_set_printsetup_header (GSList *x)
{
	if (!watch_printsetup_header.handler)
		watch_string_list (&watch_printsetup_header);
	set_string_list (&watch_printsetup_header, x);
}

GOConfNode *
gnm_conf_get_printsetup_header_node (void)
{
	return get_node (watch_printsetup_header.key);
}

static struct cb_watch_bool watch_printsetup_hf_font_bold = {
	0, "printsetup/hf-font-bold", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_hf_font_bold_node (void)
{
	return get_node (watch_printsetup_hf_font_bold.key);
}

static struct cb_watch_bool watch_printsetup_hf_font_italic = {
	0, "printsetup/hf-font-italic", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_hf_font_italic_node (void)
{
	return get_node (watch_printsetup_hf_font_italic.key);
}

static struct cb_watch_string watch_printsetup_hf_font_name = {
	0, "printsetup/hf-font-name", "Sans",
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

GOConfNode *
gnm_conf_get_printsetup_hf_font_name_node (void)
{
	return get_node (watch_printsetup_hf_font_name.key);
}

static struct cb_watch_double watch_printsetup_hf_font_size = {
	0, "printsetup/hf-font-size", 1, 100, 10,
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

GOConfNode *
gnm_conf_get_printsetup_hf_font_size_node (void)
{
	return get_node (watch_printsetup_hf_font_size.key);
}

static struct cb_watch_string_list watch_printsetup_hf_left = {
	0, "printsetup/hf-left",
};

GSList *
gnm_conf_get_printsetup_hf_left (void)
{
	if (!watch_printsetup_hf_left.handler)
		watch_string_list (&watch_printsetup_hf_left);
	return watch_printsetup_hf_left.var;
}

void
gnm_conf_set_printsetup_hf_left (GSList *x)
{
	if (!watch_printsetup_hf_left.handler)
		watch_string_list (&watch_printsetup_hf_left);
	set_string_list (&watch_printsetup_hf_left, x);
}

GOConfNode *
gnm_conf_get_printsetup_hf_left_node (void)
{
	return get_node (watch_printsetup_hf_left.key);
}

static struct cb_watch_string_list watch_printsetup_hf_middle = {
	0, "printsetup/hf-middle",
};

GSList *
gnm_conf_get_printsetup_hf_middle (void)
{
	if (!watch_printsetup_hf_middle.handler)
		watch_string_list (&watch_printsetup_hf_middle);
	return watch_printsetup_hf_middle.var;
}

void
gnm_conf_set_printsetup_hf_middle (GSList *x)
{
	if (!watch_printsetup_hf_middle.handler)
		watch_string_list (&watch_printsetup_hf_middle);
	set_string_list (&watch_printsetup_hf_middle, x);
}

GOConfNode *
gnm_conf_get_printsetup_hf_middle_node (void)
{
	return get_node (watch_printsetup_hf_middle.key);
}

static struct cb_watch_string_list watch_printsetup_hf_right = {
	0, "printsetup/hf-right",
};

GSList *
gnm_conf_get_printsetup_hf_right (void)
{
	if (!watch_printsetup_hf_right.handler)
		watch_string_list (&watch_printsetup_hf_right);
	return watch_printsetup_hf_right.var;
}

void
gnm_conf_set_printsetup_hf_right (GSList *x)
{
	if (!watch_printsetup_hf_right.handler)
		watch_string_list (&watch_printsetup_hf_right);
	set_string_list (&watch_printsetup_hf_right, x);
}

GOConfNode *
gnm_conf_get_printsetup_hf_right_node (void)
{
	return get_node (watch_printsetup_hf_right.key);
}

static struct cb_watch_double watch_printsetup_margin_bottom = {
	0, "printsetup/margin-bottom", 0, 10000, 120,
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

GOConfNode *
gnm_conf_get_printsetup_margin_bottom_node (void)
{
	return get_node (watch_printsetup_margin_bottom.key);
}

static struct cb_watch_double watch_printsetup_margin_gtk_bottom = {
	0, "printsetup/margin-gtk-bottom", 0, 720, 72,
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

GOConfNode *
gnm_conf_get_printsetup_margin_gtk_bottom_node (void)
{
	return get_node (watch_printsetup_margin_gtk_bottom.key);
}

static struct cb_watch_double watch_printsetup_margin_gtk_left = {
	0, "printsetup/margin-gtk-left", 0, 720, 72,
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

GOConfNode *
gnm_conf_get_printsetup_margin_gtk_left_node (void)
{
	return get_node (watch_printsetup_margin_gtk_left.key);
}

static struct cb_watch_double watch_printsetup_margin_gtk_right = {
	0, "printsetup/margin-gtk-right", 0, 720, 72,
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

GOConfNode *
gnm_conf_get_printsetup_margin_gtk_right_node (void)
{
	return get_node (watch_printsetup_margin_gtk_right.key);
}

static struct cb_watch_double watch_printsetup_margin_gtk_top = {
	0, "printsetup/margin-gtk-top", 0, 720, 72,
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

GOConfNode *
gnm_conf_get_printsetup_margin_gtk_top_node (void)
{
	return get_node (watch_printsetup_margin_gtk_top.key);
}

static struct cb_watch_double watch_printsetup_margin_top = {
	0, "printsetup/margin-top", 0, 10000, 120,
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

GOConfNode *
gnm_conf_get_printsetup_margin_top_node (void)
{
	return get_node (watch_printsetup_margin_top.key);
}

static struct cb_watch_string watch_printsetup_paper = {
	0, "printsetup/paper", "",
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

GOConfNode *
gnm_conf_get_printsetup_paper_node (void)
{
	return get_node (watch_printsetup_paper.key);
}

static struct cb_watch_int watch_printsetup_paper_orientation = {
	0, "printsetup/paper-orientation", GTK_PAGE_ORIENTATION_PORTRAIT, GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE, 0,
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

GOConfNode *
gnm_conf_get_printsetup_paper_orientation_node (void)
{
	return get_node (watch_printsetup_paper_orientation.key);
}

static struct cb_watch_enum watch_printsetup_preferred_unit = {
	0, "printsetup/preferred-unit", GTK_UNIT_MM,
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

GOConfNode *
gnm_conf_get_printsetup_preferred_unit_node (void)
{
	return get_node (watch_printsetup_preferred_unit.key);
}

static struct cb_watch_bool watch_printsetup_print_black_n_white = {
	0, "printsetup/print-black-n-white", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_print_black_n_white_node (void)
{
	return get_node (watch_printsetup_print_black_n_white.key);
}

static struct cb_watch_bool watch_printsetup_print_even_if_only_styles = {
	0, "printsetup/print-even-if-only-styles", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_print_even_if_only_styles_node (void)
{
	return get_node (watch_printsetup_print_even_if_only_styles.key);
}

static struct cb_watch_bool watch_printsetup_print_grid_lines = {
	0, "printsetup/print-grid-lines", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_print_grid_lines_node (void)
{
	return get_node (watch_printsetup_print_grid_lines.key);
}

static struct cb_watch_bool watch_printsetup_print_titles = {
	0, "printsetup/print-titles", FALSE,
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

GOConfNode *
gnm_conf_get_printsetup_print_titles_node (void)
{
	return get_node (watch_printsetup_print_titles.key);
}

static struct cb_watch_string watch_printsetup_repeat_left = {
	0, "printsetup/repeat-left", "",
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

GOConfNode *
gnm_conf_get_printsetup_repeat_left_node (void)
{
	return get_node (watch_printsetup_repeat_left.key);
}

static struct cb_watch_string watch_printsetup_repeat_top = {
	0, "printsetup/repeat-top", "",
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

GOConfNode *
gnm_conf_get_printsetup_repeat_top_node (void)
{
	return get_node (watch_printsetup_repeat_top.key);
}

static struct cb_watch_int watch_printsetup_scale_height = {
	0, "printsetup/scale-height", 0, 100, 0,
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

GOConfNode *
gnm_conf_get_printsetup_scale_height_node (void)
{
	return get_node (watch_printsetup_scale_height.key);
}

static struct cb_watch_bool watch_printsetup_scale_percentage = {
	0, "printsetup/scale-percentage", TRUE,
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

GOConfNode *
gnm_conf_get_printsetup_scale_percentage_node (void)
{
	return get_node (watch_printsetup_scale_percentage.key);
}

static struct cb_watch_double watch_printsetup_scale_percentage_value = {
	0, "printsetup/scale-percentage-value", 1, 500, 100,
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

GOConfNode *
gnm_conf_get_printsetup_scale_percentage_value_node (void)
{
	return get_node (watch_printsetup_scale_percentage_value.key);
}

static struct cb_watch_int watch_printsetup_scale_width = {
	0, "printsetup/scale-width", 0, 100, 0,
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

GOConfNode *
gnm_conf_get_printsetup_scale_width_node (void)
{
	return get_node (watch_printsetup_scale_width.key);
}


static struct cb_watch_bool watch_searchreplace_change_cell_expressions = {
	0, "searchreplace/change-cell-expressions", TRUE,
};

GOConfNode *
gnm_conf_get_searchreplace_change_cell_expressions_node (void)
{
	return get_node (watch_searchreplace_change_cell_expressions.key);
}

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

static struct cb_watch_bool watch_searchreplace_change_cell_other = {
	0, "searchreplace/change-cell-other", TRUE,
};

GOConfNode *
gnm_conf_get_searchreplace_change_cell_other_node (void)
{
	return get_node (watch_searchreplace_change_cell_other.key);
}

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

static struct cb_watch_bool watch_searchreplace_change_cell_strings = {
	0, "searchreplace/change-cell-strings", TRUE,
};

GOConfNode *
gnm_conf_get_searchreplace_change_cell_strings_node (void)
{
	return get_node (watch_searchreplace_change_cell_strings.key);
}

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

static struct cb_watch_bool watch_searchreplace_change_comments = {
	0, "searchreplace/change-comments", FALSE,
};

GOConfNode *
gnm_conf_get_searchreplace_change_comments_node (void)
{
	return get_node (watch_searchreplace_change_comments.key);
}

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

static struct cb_watch_bool watch_searchreplace_columnmajor = {
	0, "searchreplace/columnmajor", TRUE,
};

GOConfNode *
gnm_conf_get_searchreplace_columnmajor_node (void)
{
	return get_node (watch_searchreplace_columnmajor.key);
}

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

static struct cb_watch_int watch_searchreplace_error_behaviour = {
	0, "searchreplace/error-behaviour", 0, 4, 0,
};

GOConfNode *
gnm_conf_get_searchreplace_error_behaviour_node (void)
{
	return get_node (watch_searchreplace_error_behaviour.key);
}

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

static struct cb_watch_bool watch_searchreplace_ignore_case = {
	0, "searchreplace/ignore-case", TRUE,
};

GOConfNode *
gnm_conf_get_searchreplace_ignore_case_node (void)
{
	return get_node (watch_searchreplace_ignore_case.key);
}

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

static struct cb_watch_bool watch_searchreplace_keep_strings = {
	0, "searchreplace/keep-strings", TRUE,
};

GOConfNode *
gnm_conf_get_searchreplace_keep_strings_node (void)
{
	return get_node (watch_searchreplace_keep_strings.key);
}

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

static struct cb_watch_bool watch_searchreplace_preserve_case = {
	0, "searchreplace/preserve-case", FALSE,
};

GOConfNode *
gnm_conf_get_searchreplace_preserve_case_node (void)
{
	return get_node (watch_searchreplace_preserve_case.key);
}

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

static struct cb_watch_bool watch_searchreplace_query = {
	0, "searchreplace/query", FALSE,
};

GOConfNode *
gnm_conf_get_searchreplace_query_node (void)
{
	return get_node (watch_searchreplace_query.key);
}

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

static struct cb_watch_int watch_searchreplace_regex = {
	0, "searchreplace/regex", 0, 2, 0,
};

GOConfNode *
gnm_conf_get_searchreplace_regex_node (void)
{
	return get_node (watch_searchreplace_regex.key);
}

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

static struct cb_watch_int watch_searchreplace_scope = {
	0, "searchreplace/scope", 0, 2, 0,
};

GOConfNode *
gnm_conf_get_searchreplace_scope_node (void)
{
	return get_node (watch_searchreplace_scope.key);
}

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

static struct cb_watch_bool watch_searchreplace_whole_words_only = {
	0, "searchreplace/whole-words-only", FALSE,
};

GOConfNode *
gnm_conf_get_searchreplace_whole_words_only_node (void)
{
	return get_node (watch_searchreplace_whole_words_only.key);
}

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

static struct cb_watch_string watch_stf_export_separator = {
	0, "stf/export/separator", ",",
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

GOConfNode *
gnm_conf_get_stf_export_separator_node (void)
{
	return get_node (watch_stf_export_separator.key);
}

static struct cb_watch_string watch_stf_export_stringindicator = {
	0, "stf/export/stringindicator", "\"",
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

GOConfNode *
gnm_conf_get_stf_export_stringindicator_node (void)
{
	return get_node (watch_stf_export_stringindicator.key);
}

static struct cb_watch_string watch_stf_export_terminator = {
	0, "stf/export/terminator", "\n",
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

GOConfNode *
gnm_conf_get_stf_export_terminator_node (void)
{
	return get_node (watch_stf_export_terminator.key);
}

static struct cb_watch_int watch_undo_max_descriptor_width = {
	0, "undo/max_descriptor_width", 5, 256, 40,
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

GOConfNode *
gnm_conf_get_undo_max_descriptor_width_node (void)
{
	return get_node (watch_undo_max_descriptor_width.key);
}

static struct cb_watch_int watch_undo_maxnum = {
	0, "undo/maxnum", 0, 10000, 20,
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

GOConfNode *
gnm_conf_get_undo_maxnum_node (void)
{
	return get_node (watch_undo_maxnum.key);
}

static struct cb_watch_bool watch_undo_show_sheet_name = {
	0, "undo/show_sheet_name", FALSE,
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

GOConfNode *
gnm_conf_get_undo_show_sheet_name_node (void)
{
	return get_node (watch_undo_show_sheet_name.key);
}

static struct cb_watch_int watch_undo_size = {
	0, "undo/size", 1, 1000000, 100,
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

GOConfNode *
gnm_conf_get_undo_size_node (void)
{
	return get_node (watch_undo_size.key);
}

GOConfNode *
gnm_conf_get_autocorrect_dir_node (void)
{
	return get_node ("autocorrect");
}

GOConfNode *
gnm_conf_get_autoformat_dir_node (void)
{
	return get_node ("autoformat");
}

GOConfNode *
gnm_conf_get_core_defaultfont_dir_node (void)
{
	return get_node ("core/defaultfont");
}

GOConfNode *
gnm_conf_get_core_file_save_dir_node (void)
{
	return get_node ("core/file/save");
}

GOConfNode *
gnm_conf_get_core_gui_editing_dir_node (void)
{
	return get_node ("core/gui/editing");
}

GOConfNode *
gnm_conf_get_core_gui_screen_dir_node (void)
{
	return get_node ("core/gui/screen");
}

GOConfNode *
gnm_conf_get_core_gui_toolbars_dir_node (void)
{
	return get_node ("core/gui/toolbars");
}

GOConfNode *
gnm_conf_get_core_gui_window_dir_node (void)
{
	return get_node ("core/gui/window");
}

GOConfNode *
gnm_conf_get_core_sort_default_dir_node (void)
{
	return get_node ("core/sort/default");
}

GOConfNode *
gnm_conf_get_core_sort_dialog_dir_node (void)
{
	return get_node ("core/sort/dialog");
}

GOConfNode *
gnm_conf_get_core_workbook_dir_node (void)
{
	return get_node ("core/workbook");
}

GOConfNode *
gnm_conf_get_core_xml_dir_node (void)
{
	return get_node ("core/xml");
}

GOConfNode *
gnm_conf_get_cut_and_paste_dir_node (void)
{
	return get_node ("cut-and-paste");
}

GOConfNode *
gnm_conf_get_dialogs_rs_dir_node (void)
{
	return get_node ("dialogs/rs");
}

GOConfNode *
gnm_conf_get_functionselector_dir_node (void)
{
	return get_node ("functionselector");
}

GOConfNode *
gnm_conf_get_plugin_latex_dir_node (void)
{
	return get_node ("plugin/latex");
}

GOConfNode *
gnm_conf_get_plugins_dir_node (void)
{
	return get_node ("plugins");
}

GOConfNode *
gnm_conf_get_printsetup_dir_node (void)
{
	return get_node ("printsetup");
}

GOConfNode *
gnm_conf_get_undo_dir_node (void)
{
	return get_node ("undo");
}

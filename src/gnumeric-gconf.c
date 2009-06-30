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
#include "gnumeric-gconf-priv.h"
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

static GOConfNode *root = NULL;

/*
 * Hashes to simply ownership rules.  We use this so none of the getters
 * have to return memory that the callers needs to free.
  */
static GHashTable *string_pool;
static GHashTable *string_list_pool;

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

	root = go_conf_get_node (NULL, GNM_CONF_DIR);
}

void
gnm_conf_shutdown (void)
{
	go_conf_sync (root);
	if (sync_handler) {
		g_source_remove (sync_handler);
		sync_handler = 0;
	}

	g_hash_table_destroy (string_pool);
	string_pool = NULL;

	g_hash_table_destroy (string_list_pool);
	string_list_pool = NULL;

	go_conf_free_node (root);
	root = NULL;
}

GOConfNode *
gnm_conf_get_root (void)
{
	return root;
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

GtkToolbarStyle
gnm_conf_get_toolbar_style (void)
{
	const char *key = "/apps/gnome-settings/gnumeric/toolbar_style";
	return go_conf_load_enum (NULL, key, GTK_TYPE_TOOLBAR_STYLE, GTK_TOOLBAR_ICONS);
}

void
gnm_conf_set_toolbar_style (GtkToolbarStyle x)
{
	const char *key = "/apps/gnome-settings/gnumeric/toolbar_style";
	go_conf_set_enum (NULL, key, GTK_TYPE_TOOLBAR_STYLE, x);
	schedule_sync ();
}

gboolean
gnm_conf_get_autocorrect_first_letter (void)
{
	const char *key = "autocorrect/first-letter";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_autocorrect_first_letter (gboolean x)
{
	const char *key = "autocorrect/first-letter";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GSList *
gnm_conf_get_autocorrect_first_letter_list (void)
{
	const char *key = "autocorrect/first-letter-list";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_autocorrect_first_letter_list (GSList *x)
{
	const char *key = "autocorrect/first-letter-list";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

gboolean
gnm_conf_get_autocorrect_init_caps (void)
{
	const char *key = "autocorrect/init-caps";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_autocorrect_init_caps (gboolean x)
{
	const char *key = "autocorrect/init-caps";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GSList *
gnm_conf_get_autocorrect_init_caps_list (void)
{
	const char *key = "autocorrect/init-caps-list";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_autocorrect_init_caps_list (GSList *x)
{
	const char *key = "autocorrect/init-caps-list";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

gboolean
gnm_conf_get_autocorrect_names_of_days (void)
{
	const char *key = "autocorrect/names-of-days";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_autocorrect_names_of_days (gboolean x)
{
	const char *key = "autocorrect/names-of-days";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_autocorrect_replace (void)
{
	const char *key = "autocorrect/replace";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_autocorrect_replace (gboolean x)
{
	const char *key = "autocorrect/replace";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GSList *
gnm_conf_get_autoformat_extra_dirs (void)
{
	const char *key = "autoformat/extra-dirs";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_autoformat_extra_dirs (GSList *x)
{
	const char *key = "autoformat/extra-dirs";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

const char *
gnm_conf_get_autoformat_sys_dir (void)
{
	const char *key = "autoformat/sys-dir";
	char *res = go_conf_load_string (root, key);
	if (!res) res = g_strdup ("autoformat-templates");
	g_hash_table_replace (string_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_autoformat_sys_dir (const char *x)
{
	const char *key = "autoformat/sys-dir";
	go_conf_set_string (root, key, x);
	g_hash_table_remove (string_pool, key);
	schedule_sync ();
}

const char *
gnm_conf_get_autoformat_usr_dir (void)
{
	const char *key = "autoformat/usr-dir";
	char *res = go_conf_load_string (root, key);
	if (!res) res = g_strdup ("autoformat-templates");
	g_hash_table_replace (string_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_autoformat_usr_dir (const char *x)
{
	const char *key = "autoformat/usr-dir";
	go_conf_set_string (root, key, x);
	g_hash_table_remove (string_pool, key);
	schedule_sync ();
}

gboolean
gnm_conf_get_core_defaultfont_bold (void)
{
	const char *key = "core/defaultfont/bold";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_core_defaultfont_bold (gboolean x)
{
	const char *key = "core/defaultfont/bold";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_core_defaultfont_italic (void)
{
	const char *key = "core/defaultfont/italic";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_core_defaultfont_italic (gboolean x)
{
	const char *key = "core/defaultfont/italic";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

const char *
gnm_conf_get_core_defaultfont_name (void)
{
	const char *key = "core/defaultfont/name";
	char *res = go_conf_load_string (root, key);
	if (!res) res = g_strdup ("Sans");
	g_hash_table_replace (string_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_core_defaultfont_name (const char *x)
{
	const char *key = "core/defaultfont/name";
	go_conf_set_string (root, key, x);
	g_hash_table_remove (string_pool, key);
	schedule_sync ();
}

double
gnm_conf_get_core_defaultfont_size (void)
{
	const char *key = "core/defaultfont/size";
	return go_conf_load_double (root, key, 1, 100, 10);
}

void
gnm_conf_set_core_defaultfont_size (double x)
{
	const char *key = "core/defaultfont/size";
	go_conf_set_double (root, key, CLAMP (x, 1, 100));
	schedule_sync ();
}

gboolean
gnm_conf_get_core_file_save_def_overwrite (void)
{
	const char *key = "core/file/save/def-overwrite";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_core_file_save_def_overwrite (gboolean x)
{
	const char *key = "core/file/save/def-overwrite";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_core_file_save_single_sheet (void)
{
	const char *key = "core/file/save/single_sheet";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_core_file_save_single_sheet (gboolean x)
{
	const char *key = "core/file/save/single_sheet";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_core_gui_editing_autocomplete (void)
{
	const char *key = "core/gui/editing/autocomplete";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_core_gui_editing_autocomplete (gboolean x)
{
	const char *key = "core/gui/editing/autocomplete";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GODirection
gnm_conf_get_core_gui_editing_enter_moves_dir (void)
{
	const char *key = "core/gui/editing/enter_moves_dir";
	return go_conf_load_enum (root, key, GO_TYPE_DIRECTION, GO_DIRECTION_DOWN);
}

void
gnm_conf_set_core_gui_editing_enter_moves_dir (GODirection x)
{
	const char *key = "core/gui/editing/enter_moves_dir";
	go_conf_set_enum (root, key, GO_TYPE_DIRECTION, x);
	schedule_sync ();
}

gboolean
gnm_conf_get_core_gui_editing_livescrolling (void)
{
	const char *key = "core/gui/editing/livescrolling";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_core_gui_editing_livescrolling (gboolean x)
{
	const char *key = "core/gui/editing/livescrolling";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

int
gnm_conf_get_core_gui_editing_recalclag (void)
{
	const char *key = "core/gui/editing/recalclag";
	return go_conf_load_int (root, key, -5000, 5000, 200);
}

void
gnm_conf_set_core_gui_editing_recalclag (int x)
{
	const char *key = "core/gui/editing/recalclag";
	go_conf_set_int (root, key, CLAMP (x, -5000, 5000));
	schedule_sync ();
}

gboolean
gnm_conf_get_core_gui_editing_transitionkeys (void)
{
	const char *key = "core/gui/editing/transitionkeys";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_core_gui_editing_transitionkeys (gboolean x)
{
	const char *key = "core/gui/editing/transitionkeys";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

double
gnm_conf_get_core_gui_screen_horizontaldpi (void)
{
	const char *key = "core/gui/screen/horizontaldpi";
	return go_conf_load_double (root, key, 10, 1000, 96);
}

void
gnm_conf_set_core_gui_screen_horizontaldpi (double x)
{
	const char *key = "core/gui/screen/horizontaldpi";
	go_conf_set_double (root, key, CLAMP (x, 10, 1000));
	schedule_sync ();
}

double
gnm_conf_get_core_gui_screen_verticaldpi (void)
{
	const char *key = "core/gui/screen/verticaldpi";
	return go_conf_load_double (root, key, 10, 1000, 96);
}

void
gnm_conf_set_core_gui_screen_verticaldpi (double x)
{
	const char *key = "core/gui/screen/verticaldpi";
	go_conf_set_double (root, key, CLAMP (x, 10, 1000));
	schedule_sync ();
}

gboolean
gnm_conf_get_core_gui_toolbars_FormatToolbar (void)
{
	const char *key = "core/gui/toolbars/FormatToolbar";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_core_gui_toolbars_FormatToolbar (gboolean x)
{
	const char *key = "core/gui/toolbars/FormatToolbar";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GtkPositionType
gnm_conf_get_core_gui_toolbars_FormatToolbar_position (void)
{
	const char *key = "core/gui/toolbars/FormatToolbar-position";
	return go_conf_load_int (root, key, 0, 3, 2);
}

void
gnm_conf_set_core_gui_toolbars_FormatToolbar_position (GtkPositionType x)
{
	const char *key = "core/gui/toolbars/FormatToolbar-position";
	go_conf_set_int (root, key, CLAMP (x, 0, 3));
	schedule_sync ();
}

gboolean
gnm_conf_get_core_gui_toolbars_LongFormatToolbar (void)
{
	const char *key = "core/gui/toolbars/LongFormatToolbar";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_core_gui_toolbars_LongFormatToolbar (gboolean x)
{
	const char *key = "core/gui/toolbars/LongFormatToolbar";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GtkPositionType
gnm_conf_get_core_gui_toolbars_LongFormatToolbar_position (void)
{
	const char *key = "core/gui/toolbars/LongFormatToolbar-position";
	return go_conf_load_int (root, key, 0, 3, 2);
}

void
gnm_conf_set_core_gui_toolbars_LongFormatToolbar_position (GtkPositionType x)
{
	const char *key = "core/gui/toolbars/LongFormatToolbar-position";
	go_conf_set_int (root, key, CLAMP (x, 0, 3));
	schedule_sync ();
}

gboolean
gnm_conf_get_core_gui_toolbars_ObjectToolbar (void)
{
	const char *key = "core/gui/toolbars/ObjectToolbar";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_core_gui_toolbars_ObjectToolbar (gboolean x)
{
	const char *key = "core/gui/toolbars/ObjectToolbar";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GtkPositionType
gnm_conf_get_core_gui_toolbars_ObjectToolbar_position (void)
{
	const char *key = "core/gui/toolbars/ObjectToolbar-position";
	return go_conf_load_int (root, key, 0, 3, 2);
}

void
gnm_conf_set_core_gui_toolbars_ObjectToolbar_position (GtkPositionType x)
{
	const char *key = "core/gui/toolbars/ObjectToolbar-position";
	go_conf_set_int (root, key, CLAMP (x, 0, 3));
	schedule_sync ();
}

gboolean
gnm_conf_get_core_gui_toolbars_StandardToolbar (void)
{
	const char *key = "core/gui/toolbars/StandardToolbar";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_core_gui_toolbars_StandardToolbar (gboolean x)
{
	const char *key = "core/gui/toolbars/StandardToolbar";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GtkPositionType
gnm_conf_get_core_gui_toolbars_StandardToolbar_position (void)
{
	const char *key = "core/gui/toolbars/StandardToolbar-position";
	return go_conf_load_int (root, key, 0, 3, 2);
}

void
gnm_conf_set_core_gui_toolbars_StandardToolbar_position (GtkPositionType x)
{
	const char *key = "core/gui/toolbars/StandardToolbar-position";
	go_conf_set_int (root, key, CLAMP (x, 0, 3));
	schedule_sync ();
}

double
gnm_conf_get_core_gui_window_x (void)
{
	const char *key = "core/gui/window/x";
	return go_conf_load_double (root, key, 0.1, 1, 0.75);
}

void
gnm_conf_set_core_gui_window_x (double x)
{
	const char *key = "core/gui/window/x";
	go_conf_set_double (root, key, CLAMP (x, 0.1, 1));
	schedule_sync ();
}

double
gnm_conf_get_core_gui_window_y (void)
{
	const char *key = "core/gui/window/y";
	return go_conf_load_double (root, key, 0.1, 1, 0.75);
}

void
gnm_conf_set_core_gui_window_y (double x)
{
	const char *key = "core/gui/window/y";
	go_conf_set_double (root, key, CLAMP (x, 0.1, 1));
	schedule_sync ();
}

double
gnm_conf_get_core_gui_window_zoom (void)
{
	const char *key = "core/gui/window/zoom";
	return go_conf_load_double (root, key, 0.1, 5, 1);
}

void
gnm_conf_set_core_gui_window_zoom (double x)
{
	const char *key = "core/gui/window/zoom";
	go_conf_set_double (root, key, CLAMP (x, 0.1, 5));
	schedule_sync ();
}

gboolean
gnm_conf_get_core_sort_default_ascending (void)
{
	const char *key = "core/sort/default/ascending";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_core_sort_default_ascending (gboolean x)
{
	const char *key = "core/sort/default/ascending";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_core_sort_default_by_case (void)
{
	const char *key = "core/sort/default/by-case";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_core_sort_default_by_case (gboolean x)
{
	const char *key = "core/sort/default/by-case";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_core_sort_default_retain_formats (void)
{
	const char *key = "core/sort/default/retain-formats";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_core_sort_default_retain_formats (gboolean x)
{
	const char *key = "core/sort/default/retain-formats";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

int
gnm_conf_get_core_sort_dialog_max_initial_clauses (void)
{
	const char *key = "core/sort/dialog/max-initial-clauses";
	return go_conf_load_int (root, key, 0, 256, 10);
}

void
gnm_conf_set_core_sort_dialog_max_initial_clauses (int x)
{
	const char *key = "core/sort/dialog/max-initial-clauses";
	go_conf_set_int (root, key, CLAMP (x, 0, 256));
	schedule_sync ();
}

int
gnm_conf_get_core_workbook_autosave_time (void)
{
	const char *key = "core/workbook/autosave_time";
	return go_conf_load_int (root, key, 0, 365 * 24 * 60 * 60, 0);
}

void
gnm_conf_set_core_workbook_autosave_time (int x)
{
	const char *key = "core/workbook/autosave_time";
	go_conf_set_int (root, key, CLAMP (x, 0, 365 * 24 * 60 * 60));
	schedule_sync ();
}

int
gnm_conf_get_core_workbook_n_cols (void)
{
	const char *key = "core/workbook/n-cols";
	return go_conf_load_int (root, key, GNM_MIN_COLS, GNM_MAX_COLS, 256);
}

void
gnm_conf_set_core_workbook_n_cols (int x)
{
	const char *key = "core/workbook/n-cols";
	go_conf_set_int (root, key, CLAMP (x, GNM_MIN_COLS, GNM_MAX_COLS));
	schedule_sync ();
}

int
gnm_conf_get_core_workbook_n_rows (void)
{
	const char *key = "core/workbook/n-rows";
	return go_conf_load_int (root, key, GNM_MIN_ROWS, GNM_MAX_ROWS, 65536);
}

void
gnm_conf_set_core_workbook_n_rows (int x)
{
	const char *key = "core/workbook/n-rows";
	go_conf_set_int (root, key, CLAMP (x, GNM_MIN_ROWS, GNM_MAX_ROWS));
	schedule_sync ();
}

int
gnm_conf_get_core_workbook_n_sheet (void)
{
	const char *key = "core/workbook/n-sheet";
	return go_conf_load_int (root, key, 1, 64, 3);
}

void
gnm_conf_set_core_workbook_n_sheet (int x)
{
	const char *key = "core/workbook/n-sheet";
	go_conf_set_int (root, key, CLAMP (x, 1, 64));
	schedule_sync ();
}

int
gnm_conf_get_core_xml_compression_level (void)
{
	const char *key = "core/xml/compression-level";
	return go_conf_load_int (root, key, 0, 9, 9);
}

void
gnm_conf_set_core_xml_compression_level (int x)
{
	const char *key = "core/xml/compression-level";
	go_conf_set_int (root, key, CLAMP (x, 0, 9));
	schedule_sync ();
}

gboolean
gnm_conf_get_cut_and_paste_prefer_clipboard (void)
{
	const char *key = "cut-and-paste/prefer-clipboard";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_cut_and_paste_prefer_clipboard (gboolean x)
{
	const char *key = "cut-and-paste/prefer-clipboard";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_dialogs_rs_unfocused (void)
{
	const char *key = "dialogs/rs/unfocused";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_dialogs_rs_unfocused (gboolean x)
{
	const char *key = "dialogs/rs/unfocused";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

int
gnm_conf_get_functionselector_num_of_recent (void)
{
	const char *key = "functionselector/num-of-recent";
	return go_conf_load_int (root, key, 0, 40, 12);
}

void
gnm_conf_set_functionselector_num_of_recent (int x)
{
	const char *key = "functionselector/num-of-recent";
	go_conf_set_int (root, key, CLAMP (x, 0, 40));
	schedule_sync ();
}

GSList *
gnm_conf_get_functionselector_recentfunctions (void)
{
	const char *key = "functionselector/recentfunctions";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_functionselector_recentfunctions (GSList *x)
{
	const char *key = "functionselector/recentfunctions";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

gboolean
gnm_conf_get_plugin_latex_use_utf8 (void)
{
	const char *key = "plugin/latex/use-utf8";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_plugin_latex_use_utf8 (gboolean x)
{
	const char *key = "plugin/latex/use-utf8";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_plugins_activate_new (void)
{
	const char *key = "plugins/activate-new";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_plugins_activate_new (gboolean x)
{
	const char *key = "plugins/activate-new";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GSList *
gnm_conf_get_plugins_active (void)
{
	const char *key = "plugins/active";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_plugins_active (GSList *x)
{
	const char *key = "plugins/active";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

GSList *
gnm_conf_get_plugins_extra_dirs (void)
{
	const char *key = "plugins/extra-dirs";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_plugins_extra_dirs (GSList *x)
{
	const char *key = "plugins/extra-dirs";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

GSList *
gnm_conf_get_plugins_file_states (void)
{
	const char *key = "plugins/file-states";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_plugins_file_states (GSList *x)
{
	const char *key = "plugins/file-states";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

GSList *
gnm_conf_get_plugins_known (void)
{
	const char *key = "plugins/known";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_plugins_known (GSList *x)
{
	const char *key = "plugins/known";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_across_then_down (void)
{
	const char *key = "printsetup/across-then-down";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_across_then_down (gboolean x)
{
	const char *key = "printsetup/across-then-down";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_all_sheets (void)
{
	const char *key = "printsetup/all-sheets";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_all_sheets (gboolean x)
{
	const char *key = "printsetup/all-sheets";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_center_horizontally (void)
{
	const char *key = "printsetup/center-horizontally";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_center_horizontally (gboolean x)
{
	const char *key = "printsetup/center-horizontally";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_center_vertically (void)
{
	const char *key = "printsetup/center-vertically";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_center_vertically (gboolean x)
{
	const char *key = "printsetup/center-vertically";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

GSList *
gnm_conf_get_printsetup_footer (void)
{
	const char *key = "printsetup/footer";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_footer (GSList *x)
{
	const char *key = "printsetup/footer";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

GSList *
gnm_conf_get_printsetup_gtk_setting (void)
{
	const char *key = "printsetup/gtk-setting";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_gtk_setting (GSList *x)
{
	const char *key = "printsetup/gtk-setting";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

GSList *
gnm_conf_get_printsetup_header (void)
{
	const char *key = "printsetup/header";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_header (GSList *x)
{
	const char *key = "printsetup/header";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_hf_font_bold (void)
{
	const char *key = "printsetup/hf-font-bold";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_hf_font_bold (gboolean x)
{
	const char *key = "printsetup/hf-font-bold";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_hf_font_italic (void)
{
	const char *key = "printsetup/hf-font-italic";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_hf_font_italic (gboolean x)
{
	const char *key = "printsetup/hf-font-italic";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

const char *
gnm_conf_get_printsetup_hf_font_name (void)
{
	const char *key = "printsetup/hf-font-name";
	char *res = go_conf_load_string (root, key);
	if (!res) res = g_strdup ("Sans");
	g_hash_table_replace (string_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_hf_font_name (const char *x)
{
	const char *key = "printsetup/hf-font-name";
	go_conf_set_string (root, key, x);
	g_hash_table_remove (string_pool, key);
	schedule_sync ();
}

double
gnm_conf_get_printsetup_hf_font_size (void)
{
	const char *key = "printsetup/hf-font-size";
	return go_conf_load_double (root, key, 1, 100, 10);
}

void
gnm_conf_set_printsetup_hf_font_size (double x)
{
	const char *key = "printsetup/hf-font-size";
	go_conf_set_double (root, key, CLAMP (x, 1, 100));
	schedule_sync ();
}

GSList *
gnm_conf_get_printsetup_hf_left (void)
{
	const char *key = "printsetup/hf-left";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_hf_left (GSList *x)
{
	const char *key = "printsetup/hf-left";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

GSList *
gnm_conf_get_printsetup_hf_middle (void)
{
	const char *key = "printsetup/hf-middle";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_hf_middle (GSList *x)
{
	const char *key = "printsetup/hf-middle";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

GSList *
gnm_conf_get_printsetup_hf_right (void)
{
	const char *key = "printsetup/hf-right";
	GSList *res = go_conf_load_str_list (root, key);
	g_hash_table_replace (string_list_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_hf_right (GSList *x)
{
	const char *key = "printsetup/hf-right";
	go_conf_set_str_list (root, key, x);
	g_hash_table_remove (string_list_pool, key);
	schedule_sync ();
}

double
gnm_conf_get_printsetup_margin_bottom (void)
{
	const char *key = "printsetup/margin-bottom";
	return go_conf_load_double (root, key, 0, 10000, 120);
}

void
gnm_conf_set_printsetup_margin_bottom (double x)
{
	const char *key = "printsetup/margin-bottom";
	go_conf_set_double (root, key, CLAMP (x, 0, 10000));
	schedule_sync ();
}

double
gnm_conf_get_printsetup_margin_gtk_bottom (void)
{
	const char *key = "printsetup/margin-gtk-bottom";
	return go_conf_load_double (root, key, 0, 720, 72);
}

void
gnm_conf_set_printsetup_margin_gtk_bottom (double x)
{
	const char *key = "printsetup/margin-gtk-bottom";
	go_conf_set_double (root, key, CLAMP (x, 0, 720));
	schedule_sync ();
}

double
gnm_conf_get_printsetup_margin_gtk_left (void)
{
	const char *key = "printsetup/margin-gtk-left";
	return go_conf_load_double (root, key, 0, 720, 72);
}

void
gnm_conf_set_printsetup_margin_gtk_left (double x)
{
	const char *key = "printsetup/margin-gtk-left";
	go_conf_set_double (root, key, CLAMP (x, 0, 720));
	schedule_sync ();
}

double
gnm_conf_get_printsetup_margin_gtk_right (void)
{
	const char *key = "printsetup/margin-gtk-right";
	return go_conf_load_double (root, key, 0, 720, 72);
}

void
gnm_conf_set_printsetup_margin_gtk_right (double x)
{
	const char *key = "printsetup/margin-gtk-right";
	go_conf_set_double (root, key, CLAMP (x, 0, 720));
	schedule_sync ();
}

double
gnm_conf_get_printsetup_margin_gtk_top (void)
{
	const char *key = "printsetup/margin-gtk-top";
	return go_conf_load_double (root, key, 0, 720, 72);
}

void
gnm_conf_set_printsetup_margin_gtk_top (double x)
{
	const char *key = "printsetup/margin-gtk-top";
	go_conf_set_double (root, key, CLAMP (x, 0, 720));
	schedule_sync ();
}

double
gnm_conf_get_printsetup_margin_top (void)
{
	const char *key = "printsetup/margin-top";
	return go_conf_load_double (root, key, 0, 10000, 120);
}

void
gnm_conf_set_printsetup_margin_top (double x)
{
	const char *key = "printsetup/margin-top";
	go_conf_set_double (root, key, CLAMP (x, 0, 10000));
	schedule_sync ();
}

const char *
gnm_conf_get_printsetup_paper (void)
{
	const char *key = "printsetup/paper";
	char *res = go_conf_load_string (root, key);
	if (!res) res = g_strdup ("");
	g_hash_table_replace (string_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_paper (const char *x)
{
	const char *key = "printsetup/paper";
	go_conf_set_string (root, key, x);
	g_hash_table_remove (string_pool, key);
	schedule_sync ();
}

int
gnm_conf_get_printsetup_paper_orientation (void)
{
	const char *key = "printsetup/paper-orientation";
	return go_conf_load_int (root, key, GTK_PAGE_ORIENTATION_PORTRAIT, GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE, 0);
}

void
gnm_conf_set_printsetup_paper_orientation (int x)
{
	const char *key = "printsetup/paper-orientation";
	go_conf_set_int (root, key, CLAMP (x, GTK_PAGE_ORIENTATION_PORTRAIT, GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE));
	schedule_sync ();
}

GtkUnit
gnm_conf_get_printsetup_preferred_unit (void)
{
	const char *key = "printsetup/preferred-unit";
	return go_conf_load_enum (root, key, GTK_TYPE_UNIT, GTK_UNIT_MM);
}

void
gnm_conf_set_printsetup_preferred_unit (GtkUnit x)
{
	const char *key = "printsetup/preferred-unit";
	go_conf_set_enum (root, key, GTK_TYPE_UNIT, x);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_print_black_n_white (void)
{
	const char *key = "printsetup/print-black-n-white";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_print_black_n_white (gboolean x)
{
	const char *key = "printsetup/print-black-n-white";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_print_even_if_only_styles (void)
{
	const char *key = "printsetup/print-even-if-only-styles";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_print_even_if_only_styles (gboolean x)
{
	const char *key = "printsetup/print-even-if-only-styles";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_print_grid_lines (void)
{
	const char *key = "printsetup/print-grid-lines";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_print_grid_lines (gboolean x)
{
	const char *key = "printsetup/print-grid-lines";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_print_titles (void)
{
	const char *key = "printsetup/print-titles";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_printsetup_print_titles (gboolean x)
{
	const char *key = "printsetup/print-titles";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

const char *
gnm_conf_get_printsetup_repeat_left (void)
{
	const char *key = "printsetup/repeat-left";
	char *res = go_conf_load_string (root, key);
	if (!res) res = g_strdup ("");
	g_hash_table_replace (string_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_repeat_left (const char *x)
{
	const char *key = "printsetup/repeat-left";
	go_conf_set_string (root, key, x);
	g_hash_table_remove (string_pool, key);
	schedule_sync ();
}

const char *
gnm_conf_get_printsetup_repeat_top (void)
{
	const char *key = "printsetup/repeat-top";
	char *res = go_conf_load_string (root, key);
	if (!res) res = g_strdup ("");
	g_hash_table_replace (string_pool, (gpointer)key, res);
	return res;
}

void
gnm_conf_set_printsetup_repeat_top (const char *x)
{
	const char *key = "printsetup/repeat-top";
	go_conf_set_string (root, key, x);
	g_hash_table_remove (string_pool, key);
	schedule_sync ();
}

int
gnm_conf_get_printsetup_scale_height (void)
{
	const char *key = "printsetup/scale-height";
	return go_conf_load_int (root, key, 0, 100, 0);
}

void
gnm_conf_set_printsetup_scale_height (int x)
{
	const char *key = "printsetup/scale-height";
	go_conf_set_int (root, key, CLAMP (x, 0, 100));
	schedule_sync ();
}

gboolean
gnm_conf_get_printsetup_scale_percentage (void)
{
	const char *key = "printsetup/scale-percentage";
	return go_conf_load_bool (root, key, TRUE);
}

void
gnm_conf_set_printsetup_scale_percentage (gboolean x)
{
	const char *key = "printsetup/scale-percentage";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

double
gnm_conf_get_printsetup_scale_percentage_value (void)
{
	const char *key = "printsetup/scale-percentage-value";
	return go_conf_load_double (root, key, 1, 500, 100);
}

void
gnm_conf_set_printsetup_scale_percentage_value (double x)
{
	const char *key = "printsetup/scale-percentage-value";
	go_conf_set_double (root, key, CLAMP (x, 1, 500));
	schedule_sync ();
}

int
gnm_conf_get_printsetup_scale_width (void)
{
	const char *key = "printsetup/scale-width";
	return go_conf_load_int (root, key, 0, 100, 0);
}

void
gnm_conf_set_printsetup_scale_width (int x)
{
	const char *key = "printsetup/scale-width";
	go_conf_set_int (root, key, CLAMP (x, 0, 100));
	schedule_sync ();
}

int
gnm_conf_get_undo_max_descriptor_width (void)
{
	const char *key = "undo/max_descriptor_width";
	return go_conf_load_int (root, key, 5, 256, 40);
}

void
gnm_conf_set_undo_max_descriptor_width (int x)
{
	const char *key = "undo/max_descriptor_width";
	go_conf_set_int (root, key, CLAMP (x, 5, 256));
	schedule_sync ();
}

int
gnm_conf_get_undo_maxnum (void)
{
	const char *key = "undo/maxnum";
	return go_conf_load_int (root, key, 0, 10000, 20);
}

void
gnm_conf_set_undo_maxnum (int x)
{
	const char *key = "undo/maxnum";
	go_conf_set_int (root, key, CLAMP (x, 0, 10000));
	schedule_sync ();
}

gboolean
gnm_conf_get_undo_show_sheet_name (void)
{
	const char *key = "undo/show_sheet_name";
	return go_conf_load_bool (root, key, FALSE);
}

void
gnm_conf_set_undo_show_sheet_name (gboolean x)
{
	const char *key = "undo/show_sheet_name";
	go_conf_set_bool (root, key, x != FALSE);
	schedule_sync ();
}

int
gnm_conf_get_undo_size (void)
{
	const char *key = "undo/size";
	return go_conf_load_int (root, key, 1, 1000000, 100);
}

void
gnm_conf_set_undo_size (int x)
{
	const char *key = "undo/size";
	go_conf_set_int (root, key, CLAMP (x, 1, 1000000));
	schedule_sync ();
}

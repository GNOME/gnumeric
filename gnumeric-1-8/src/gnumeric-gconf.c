/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-gconf.c:
 *
 * Author:
 *	Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2002-2005 Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <goffice/utils/go-glib-extras.h>
#include <goffice/utils/go-format.h>
#include <goffice/utils/go-locale.h>
#include <value.h>
#include <number-match.h>

static GnmAppPrefs prefs;
GnmAppPrefs const *gnm_app_prefs = &prefs;
static GOConfNode *root = NULL;

#define NO_DEBUG_GCONF
#ifndef NO_DEBUG_GCONF
#define d(code)	{ code; }
#else
#define d(code)
#endif

#ifdef GNM_WITH_GNOME
#include "gnm-conf-gconf.c"
#elif defined G_OS_WIN32
#include "gnm-conf-win32.c"
#else
#include "gnm-conf-keyfile.c"
#endif

static void
gnm_conf_init_page_setup (GOConfNode *node)
{
	if (prefs.page_setup == NULL) {
		gchar *paper;
		double margin;
		GtkPageOrientation orient;

		prefs.page_setup = gtk_page_setup_new ();

		paper = go_conf_load_string (node, PRINTSETUP_GCONF_PAPER);

		if (paper != NULL) {
			if (*paper != 0) {
				GtkPaperSize *size
					= gtk_paper_size_new (paper);
				gtk_page_setup_set_paper_size
					(prefs.page_setup,
					 size);
				gtk_paper_size_free (size);
			}
			g_free (paper);
		}

		orient = go_conf_load_int (node, PRINTSETUP_GCONF_PAPER_ORIENTATION,
					   GTK_PAGE_ORIENTATION_PORTRAIT,
					   GTK_PAGE_ORIENTATION_REVERSE_LANDSCAPE,
					   GTK_PAGE_ORIENTATION_PORTRAIT);
		gtk_page_setup_set_orientation (prefs.page_setup, orient);

		margin = go_conf_load_double
			(node, PRINTSETUP_GCONF_MARGIN_GTK_TOP,
			 0., 720. , 72.);
		gtk_page_setup_set_top_margin (prefs.page_setup, margin,
					       GTK_UNIT_POINTS);
		margin = go_conf_load_double
			(node, PRINTSETUP_GCONF_MARGIN_GTK_BOTTOM,
			 0., 720. , 72.);
		gtk_page_setup_set_bottom_margin (prefs.page_setup, margin,
						  GTK_UNIT_POINTS);
		margin = go_conf_load_double
			(node, PRINTSETUP_GCONF_MARGIN_GTK_LEFT,
			 0., 720. , 72.);
		gtk_page_setup_set_left_margin (prefs.page_setup, margin,
						GTK_UNIT_POINTS);
		margin = go_conf_load_double
			(node, PRINTSETUP_GCONF_MARGIN_GTK_RIGHT,
			 0., 720. , 72.);
		gtk_page_setup_set_right_margin (prefs.page_setup, margin,
						 GTK_UNIT_POINTS);

	}
}

static void
gnm_conf_init_print_settings (GOConfNode *node)
{
	GSList *list, *item;
	char const *key;
	char const *value;

	prefs.print_settings =  gtk_print_settings_new ();

	item = list = go_conf_load_str_list (node, PRINTSETUP_GCONF_GTKSETTING);

	while (item) {
		value = item->data;
		item = item->next;
		if (item) {
			key = item->data;
			item = item->next;
			gtk_print_settings_set (prefs.print_settings, key, value);
		}
	}

	go_slist_free_custom (list, g_free);
}

static void
gnm_conf_init_printer_decoration_font (void)
{
	GOConfNode *node;
	gchar *name;
	if (prefs.printer_decoration_font == NULL)
		prefs.printer_decoration_font = gnm_style_new ();

	node = go_conf_get_node (root, PRINTSETUP_GCONF_DIR);
	name = go_conf_load_string (node, PRINTSETUP_GCONF_HF_FONT_NAME);
	if (name) {
		gnm_style_set_font_name (prefs.printer_decoration_font, name);
		g_free (name);
	} else
		gnm_style_set_font_name (prefs.printer_decoration_font, DEFAULT_FONT);
	gnm_style_set_font_size (prefs.printer_decoration_font,
		go_conf_load_double (node, PRINTSETUP_GCONF_HF_FONT_SIZE, 1., 100., DEFAULT_SIZE));
	gnm_style_set_font_bold (prefs.printer_decoration_font,
		go_conf_load_bool (node, PRINTSETUP_GCONF_HF_FONT_BOLD, FALSE));
	gnm_style_set_font_italic (prefs.printer_decoration_font,
		go_conf_load_bool (node, PRINTSETUP_GCONF_HF_FONT_ITALIC, FALSE));
	go_conf_free_node (node);
}

static void
gnm_conf_init_essential (void)
{
	GOConfNode *node;

	node = go_conf_get_node (root, CONF_DEFAULT_FONT_DIR);
	prefs.default_font.name = go_conf_load_string (node, CONF_DEFAULT_FONT_NAME);
	if (prefs.default_font.name == NULL)
		prefs.default_font.name = g_strdup (DEFAULT_FONT);
	prefs.default_font.size = go_conf_load_double (
		node, CONF_DEFAULT_FONT_SIZE, 1., 100., DEFAULT_SIZE);
	prefs.default_font.is_bold = go_conf_load_bool (
		node, CONF_DEFAULT_FONT_BOLD, FALSE);
	prefs.default_font.is_italic = go_conf_load_bool (
		node, CONF_DEFAULT_FONT_ITALIC, FALSE);
	go_conf_free_node (node);

	node = go_conf_get_node (root, PLUGIN_GCONF_DIR);
	prefs.plugin_file_states = go_conf_load_str_list (node, PLUGIN_GCONF_FILE_STATES);
	prefs.plugin_extra_dirs = go_conf_load_str_list (node, PLUGIN_GCONF_EXTRA_DIRS);
	prefs.active_plugins = go_conf_load_str_list (node, PLUGIN_GCONF_ACTIVE);
	prefs.activate_new_plugins = go_conf_load_bool (
		node, PLUGIN_GCONF_ACTIVATE_NEW, TRUE);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_GUI_DIR);
	prefs.horizontal_dpi = go_conf_load_double (
		node, GNM_CONF_GUI_RES_H, 10., 1000., 96.);
	prefs.vertical_dpi = go_conf_load_double (
		node, GNM_CONF_GUI_RES_V, 10., 1000., 96.);
	prefs.initial_sheet_number = go_conf_load_int (
		root, GNM_CONF_WORKBOOK_NSHEETS, 1, 64, 3);
	prefs.horizontal_window_fraction = go_conf_load_double (
		  node, GNM_CONF_GUI_WINDOW_X, .1, 1., .6);
	prefs.vertical_window_fraction = go_conf_load_double (
		  node, GNM_CONF_GUI_WINDOW_Y, .1, 1., .6);
	prefs.zoom = go_conf_load_double (
		  node, GNM_CONF_GUI_ZOOM, .1, 5., 1.);
	prefs.enter_moves_dir = go_conf_load_enum (
		  node, GNM_CONF_GUI_ED_ENTER_MOVES_DIR,
		  GO_DIRECTION_TYPE, GO_DIRECTION_DOWN);
	prefs.auto_complete = go_conf_load_bool (
		  node, GNM_CONF_GUI_ED_AUTOCOMPLETE, TRUE);
	prefs.live_scrolling = go_conf_load_bool (
		  node, GNM_CONF_GUI_ED_LIVESCROLLING, TRUE);
	prefs.toolbars = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify)g_free,
		 NULL);
	prefs.toolbar_positions = g_hash_table_new_full
		(g_str_hash, g_str_equal,
		 (GDestroyNotify)g_free,
		 NULL);
	go_conf_free_node (node);
}

void
gnm_gconf_init_printer_defaults (void)
{
	GOConfNode *node;

	if (prefs.print_settings != NULL)
		return;

	node = go_conf_get_node (root, PRINTSETUP_GCONF_DIR);

	gnm_conf_init_print_settings (node);
	gnm_conf_init_page_setup (node);

	prefs.print_center_horizontally = go_conf_load_bool
		(node, PRINTSETUP_GCONF_CENTER_HORIZONTALLY, FALSE);
	prefs.print_center_vertically = go_conf_load_bool
		(node, PRINTSETUP_GCONF_CENTER_VERTICALLY, FALSE);
	prefs.print_grid_lines = go_conf_load_bool
		(node, PRINTSETUP_GCONF_PRINT_GRID_LINES, FALSE);
	prefs.print_even_if_only_styles = go_conf_load_bool
		(node, PRINTSETUP_GCONF_EVEN_IF_ONLY_STYLES, FALSE);
	prefs.print_black_and_white = go_conf_load_bool
		(node, PRINTSETUP_GCONF_PRINT_BLACK_AND_WHITE, FALSE);
	prefs.print_titles = go_conf_load_bool
		(node, PRINTSETUP_GCONF_PRINT_TITLES, FALSE);
	prefs.print_order_across_then_down = go_conf_load_bool
		(node, PRINTSETUP_GCONF_ACROSS_THEN_DOWN, FALSE);
	prefs.print_scale_percentage = go_conf_load_bool
		(node, PRINTSETUP_GCONF_SCALE_PERCENTAGE, TRUE);
	prefs.print_scale_percentage_value = go_conf_load_double
		(node, PRINTSETUP_GCONF_SCALE_PERCENTAGE_VALUE, 1, 500, 100);
	prefs.print_scale_width = go_conf_load_int
		(node, PRINTSETUP_GCONF_SCALE_WIDTH, 0, 100, 1);
        prefs.print_scale_height = go_conf_load_int
		(node, PRINTSETUP_GCONF_SCALE_HEIGHT, 0, 100, 1);
	prefs.print_repeat_top = go_conf_load_string (node, PRINTSETUP_GCONF_REPEAT_TOP);
	prefs.print_repeat_left = go_conf_load_string (node, PRINTSETUP_GCONF_REPEAT_LEFT);
	prefs.print_margin_top = go_conf_load_double
		(node, PRINTSETUP_GCONF_MARGIN_TOP, 0.0, 10000.0, 120.0);
	prefs.print_margin_bottom = go_conf_load_double
		(node, PRINTSETUP_GCONF_MARGIN_BOTTOM, 0.0, 10000.0, 120.0);
	{
		char *str;
		str = go_conf_load_string
			(node,PRINTSETUP_GCONF_PREFERRED_UNIT);
		if (str != NULL) {
			prefs.desired_display = unit_name_to_unit (str);
			g_free (str);
		} else
			prefs.desired_display = GTK_UNIT_MM;
	}
	prefs.print_all_sheets = go_conf_load_bool (
		node, PRINTSETUP_GCONF_ALL_SHEETS, TRUE);
	prefs.printer_header = go_conf_load_str_list (node, PRINTSETUP_GCONF_HEADER);
	prefs.printer_footer = go_conf_load_str_list (node, PRINTSETUP_GCONF_FOOTER);
	prefs.printer_header_formats_left = go_conf_load_str_list (node, PRINTSETUP_GCONF_HEADER_FORMAT_LEFT);
	prefs.printer_header_formats_middle = go_conf_load_str_list (node, PRINTSETUP_GCONF_HEADER_FORMAT_MIDDLE);
	prefs.printer_header_formats_right = go_conf_load_str_list (node, PRINTSETUP_GCONF_HEADER_FORMAT_RIGHT);
	go_conf_free_node (node);
}


static gboolean
gnm_conf_init_extras (void)
{
	char *tmp;
	GOConfNode *node;

	node = go_conf_get_node (root, FUNCTION_SELECT_GCONF_DIR);
	prefs.num_of_recent_funcs = go_conf_load_int (
		node, FUNCTION_SELECT_GCONF_NUM_OF_RECENT, 0, 40, 10);
	prefs.recent_funcs = go_conf_load_str_list (node, FUNCTION_SELECT_GCONF_RECENT);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_GUI_DIR);
	prefs.transition_keys = go_conf_load_bool (
		node, GNM_CONF_GUI_ED_TRANSITION_KEYS, FALSE);
	prefs.recalc_lag = go_conf_load_int (
		node, GNM_CONF_GUI_ED_RECALC_LAG, -5000, 5000, 200);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_UNDO_DIR);
	prefs.show_sheet_name = go_conf_load_bool (
		node, GNM_CONF_UNDO_SHOW_SHEET_NAME, TRUE);
	prefs.max_descriptor_width = go_conf_load_int (
		node, GNM_CONF_UNDO_MAX_DESCRIPTOR_WIDTH, 5, 256, 15);
	prefs.undo_size = go_conf_load_int (
		node, GNM_CONF_UNDO_SIZE, 1, 1000000, 100000);
	prefs.undo_max_number = go_conf_load_int (
		node, GNM_CONF_UNDO_MAXNUM, 0, 10000, 100);
	go_conf_free_node (node);

	node = go_conf_get_node (root, AUTOFORMAT_GCONF_DIR);
	prefs.autoformat.extra_dirs = go_conf_load_str_list (node, AUTOFORMAT_GCONF_EXTRA_DIRS);

	tmp = go_conf_load_string (node, AUTOFORMAT_GCONF_SYS_DIR);
	if (tmp == NULL)
		tmp = g_strdup ("autoformat-templates");
	prefs.autoformat.sys_dir = g_build_filename (gnm_sys_data_dir (), tmp, NULL);
	g_free (tmp);

	if (gnm_usr_dir () != NULL) {
		tmp = go_conf_load_string (node, AUTOFORMAT_GCONF_USR_DIR);
		if (tmp == NULL)
			tmp = g_strdup ("autoformat-templates");
		prefs.autoformat.usr_dir = g_build_filename (gnm_usr_dir (), tmp, NULL);
		g_free (tmp);
	}
	go_conf_free_node (node);

	prefs.xml_compression_level = go_conf_load_int (
		root, GNM_CONF_XML_COMPRESSION, 0, 9, 9);

	node = go_conf_get_node (root, GNM_CONF_FILE_DIR);
	prefs.file_overwrite_default_answer = go_conf_load_bool (
		node, GNM_CONF_FILE_OVERWRITE_DEFAULT, FALSE);
	prefs.file_ask_single_sheet_save = go_conf_load_bool (
		node, GNM_CONF_FILE_SINGLE_SHEET_SAVE, TRUE);
	go_conf_free_node (node);

	node = go_conf_get_node (root, GNM_CONF_SORT_DIR);
	prefs.sort_default_by_case = go_conf_load_bool (
		node, GNM_CONF_SORT_DEFAULT_BY_CASE, FALSE);
	prefs.sort_default_has_header = go_conf_load_bool (
		node, GNM_CONF_SORT_DEFAULT_HAS_HEADER, FALSE);
	prefs.sort_default_retain_formats = go_conf_load_bool (
		node, GNM_CONF_SORT_DEFAULT_RETAIN_FORM, TRUE);
	prefs.sort_default_ascending = go_conf_load_bool (
		node, GNM_CONF_SORT_DEFAULT_ASCENDING, TRUE);
	prefs.sort_max_initial_clauses = go_conf_load_int (
		node, GNM_CONF_SORT_DIALOG_MAX_INITIAL, 0, 256, 10);
	go_conf_free_node (node);

	prefs.unfocused_range_selection = go_conf_load_bool (
		root, DIALOGS_GCONF_DIR "/" DIALOGS_GCONF_UNFOCUSED_RS, TRUE);
	prefs.prefer_clipboard_selection = go_conf_load_bool (
		root, GNM_CONF_CUTANDPASTE_DIR "/" GNM_CONF_CUTANDPASTE_PREFER_CLIPBOARD, TRUE);
	prefs.latex_use_utf8 = go_conf_load_bool (
		root, PLUGIN_GCONF_LATEX "/" PLUGIN_GCONF_LATEX_USE_UTF8, TRUE);

	gnm_conf_init_printer_decoration_font ();

	gnm_gconf_init_printer_defaults ();

	return FALSE;
}

/**
 * gnm_conf_init
 *
 * @fast : Load non-essential prefs in an idle handler
 **/
void
gnm_conf_init (gboolean fast)
{
	go_conf_init ();
	root = go_conf_get_node (NULL, GNM_CONF_DIR);
	gnm_conf_init_essential ();
	if (fast)
		g_timeout_add (1000, (GSourceFunc) gnm_conf_init_extras, NULL);
	else
		gnm_conf_init_extras ();
}

void
gnm_conf_shutdown (void)
{
	if (prefs.printer_decoration_font) {
		gnm_style_unref (prefs.printer_decoration_font);
		prefs.printer_decoration_font = NULL;
	}
	g_hash_table_destroy (prefs.toolbars);
	g_hash_table_destroy (prefs.toolbar_positions);

	go_slist_free_custom ((GSList *)prefs.plugin_file_states,
			      (GFreeFunc)g_free);
	prefs.plugin_file_states = NULL;

	if (prefs.print_settings != NULL) {
		g_object_unref (prefs.print_settings);
		prefs.print_settings = NULL;
	}
	if (prefs.page_setup != NULL) {
		g_object_unref (prefs.page_setup);
		prefs.page_setup = NULL;
	}


	go_conf_free_node (root);
	go_conf_shutdown ();
}

GOConfNode *
gnm_conf_get_root (void)
{
	return root;
}

static void
gnm_gconf_set_print_settings_cb (const gchar *key, const gchar *value, gpointer user_data)
{
	GSList **list = user_data;

	*list = g_slist_prepend (*list, g_strdup (key));
	*list = g_slist_prepend (*list, g_strdup (value));
}

GtkPrintSettings *
gnm_gconf_get_print_settings (void) {
	gnm_gconf_init_printer_defaults ();
	return prefs.print_settings;
}

GtkPageSetup *
gnm_gconf_get_page_setup (void) {
	gnm_gconf_init_printer_defaults ();
	return prefs.page_setup;
}

void
gnm_gconf_set_print_settings (GtkPrintSettings *settings)
{
	GSList *list = NULL;

	if (prefs.print_settings != NULL)
		g_object_unref (prefs.print_settings);
	prefs.print_settings = g_object_ref (settings);

	gtk_print_settings_foreach (settings, gnm_gconf_set_print_settings_cb, &list);
	go_conf_set_str_list (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_GTKSETTING, list);
	go_slist_free_custom (list, g_free);
}

void
gnm_gconf_set_page_setup (GtkPageSetup *setup)
{
	char * paper;

	g_return_if_fail (setup != NULL);

	if (prefs.page_setup != NULL)
		g_object_unref (prefs.page_setup);
	prefs.page_setup = gtk_page_setup_copy (setup);

	paper = page_setup_get_paper (setup);
	go_conf_set_string (root,
			    PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PAPER,
			    paper);
	g_free (paper);

	go_conf_set_int
		(root,
		 PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PAPER_ORIENTATION,
		 gtk_page_setup_get_orientation (setup));

	go_conf_set_double
		(root,
		 PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_MARGIN_GTK_TOP,
		 gtk_page_setup_get_top_margin (setup, GTK_UNIT_POINTS));
	go_conf_set_double
		(root,
		 PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_MARGIN_GTK_BOTTOM,
		 gtk_page_setup_get_bottom_margin (setup, GTK_UNIT_POINTS));
	go_conf_set_double
		(root,
		 PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_MARGIN_GTK_LEFT,
		 gtk_page_setup_get_left_margin (setup, GTK_UNIT_POINTS));
	go_conf_set_double
		(root,
		 PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_MARGIN_GTK_RIGHT,
		 gtk_page_setup_get_right_margin (setup, GTK_UNIT_POINTS));
}

void
gnm_gconf_set_plugin_file_states (GSList *list)
{
	g_return_if_fail (prefs.plugin_file_states != list);

	/* the const_casts are ok, the const in the header is just to keep
	 * people for doing stupid things */
	go_slist_free_custom ((GSList *)prefs.plugin_file_states,
			      (GFreeFunc)g_free);
	prefs.plugin_file_states = list;

	go_conf_set_str_list (root, PLUGIN_GCONF_DIR "/" PLUGIN_GCONF_FILE_STATES, list);
}

void
gnm_gconf_set_plugin_extra_dirs (GSList *list)
{
	g_return_if_fail (prefs.plugin_extra_dirs != list);

	/* the const_cast is ok, the const in the header is just to keep
	 * people for doing stupid things */
	go_slist_free_custom ((GSList *)prefs.plugin_extra_dirs, g_free);
	prefs.plugin_extra_dirs = list;

	go_conf_set_str_list (root, PLUGIN_GCONF_DIR "/" PLUGIN_GCONF_EXTRA_DIRS, list);
}

void
gnm_gconf_set_active_plugins (GSList *list)
{
	go_conf_set_str_list (root, PLUGIN_GCONF_DIR "/" PLUGIN_GCONF_ACTIVE, list);
}

void
gnm_gconf_set_activate_new_plugins (gboolean val)
{
	go_conf_set_bool (root, PLUGIN_GCONF_DIR "/" PLUGIN_GCONF_ACTIVATE_NEW, val);
}

void
gnm_gconf_set_recent_funcs (GSList *list)
{
	go_conf_set_str_list (root, FUNCTION_SELECT_GCONF_DIR "/" FUNCTION_SELECT_GCONF_RECENT, list);

	/* the const_cast is ok, the const in the header is just to keep
	 * people for doing stupid things */
	go_slist_free_custom ((GSList *)prefs.recent_funcs, g_free);

	prefs.recent_funcs = list;
}

void
gnm_gconf_set_num_recent_functions (gint val)
{
	if (val < 0)
		val = 0;
	prefs.num_of_recent_funcs = val;
	go_conf_set_int (root, FUNCTION_SELECT_GCONF_DIR "/" FUNCTION_SELECT_GCONF_NUM_OF_RECENT, val);
}

void
gnm_gconf_set_undo_size (gint val)
{
	if (val < 1)
		val = 1;
	prefs.undo_size = val;
	go_conf_set_int (root, GNM_CONF_UNDO_DIR "/" GNM_CONF_UNDO_SIZE, val);
}


void
gnm_gconf_set_undo_max_number (gint val)
{
	if (val < 1)
		val = 1;
	prefs.undo_max_number = val;
	go_conf_set_int (root, GNM_CONF_UNDO_DIR "/" GNM_CONF_UNDO_MAXNUM, val);
}

void
gnm_gconf_set_autoformat_sys_dirs (char const * string)
{
	go_conf_set_string (root, AUTOFORMAT_GCONF_DIR "/" AUTOFORMAT_GCONF_SYS_DIR, string);
}

void
gnm_gconf_set_autoformat_usr_dirs (char const * string)
{
	go_conf_set_string (root, AUTOFORMAT_GCONF_DIR "/" AUTOFORMAT_GCONF_USR_DIR, string);
}

void
gnm_gconf_set_all_sheets (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_ALL_SHEETS, val);
}

void
gnm_gconf_set_printer_header (gchar const *left, gchar const *middle,
			      gchar const *right)
{
	GSList *list = NULL;
	list = g_slist_prepend (list, g_strdup (right ? right : ""));
	list = g_slist_prepend (list, g_strdup (middle ? middle : ""));
	list = g_slist_prepend (list, g_strdup (left ? left : ""));
	go_conf_set_str_list (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HEADER, list);
	go_slist_free_custom ((GSList *)prefs.printer_header, g_free);
	prefs.printer_header = list;
}

void
gnm_gconf_set_printer_footer (gchar const *left, gchar const *middle,
			      gchar const *right)
{
	GSList *list = NULL;
	list = g_slist_prepend (list, g_strdup (right ? right : ""));
	list = g_slist_prepend (list, g_strdup (middle ? middle : ""));
	list = g_slist_prepend (list, g_strdup (left ? left : ""));
	go_conf_set_str_list (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_FOOTER, list);
	go_slist_free_custom ((GSList *)prefs.printer_footer, g_free);
	prefs.printer_footer = list;
}

void
gnm_gconf_set_print_center_horizontally (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_CENTER_HORIZONTALLY, val);
}

void
gnm_gconf_set_print_center_vertically (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_CENTER_VERTICALLY, val);
}

void
gnm_gconf_set_print_grid_lines (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PRINT_GRID_LINES, val);
}

void
gnm_gconf_set_print_even_if_only_styles (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_EVEN_IF_ONLY_STYLES, val);
}

void
gnm_gconf_set_print_black_and_white (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PRINT_BLACK_AND_WHITE, val);
}

void
gnm_gconf_set_print_titles (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PRINT_TITLES, val);
}

void
gnm_gconf_set_print_order_across_then_down (gboolean val)
{
	go_conf_set_bool (root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_ACROSS_THEN_DOWN, val);
}

void
gnm_gconf_set_print_scale_percentage (gboolean val)
{
	go_conf_set_bool (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_SCALE_PERCENTAGE, val);
}

void
gnm_gconf_set_print_scale_percentage_value (gnm_float val)
{
	go_conf_set_double (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_SCALE_PERCENTAGE_VALUE, val);
}

void
gnm_gconf_set_print_tb_margins (double edge_to_header,
				double edge_to_footer,
				GtkUnit unit)
{
	go_conf_set_double (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_MARGIN_TOP,
		edge_to_header);
	go_conf_set_double (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_MARGIN_BOTTOM,
		edge_to_footer);
	go_conf_set_string (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_PREFERRED_UNIT, unit_to_unit_name (unit));
}

void
gnm_gconf_set_print_header_formats (GSList *left, GSList *middle,
				    GSList *right)
{
	go_conf_set_str_list (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HEADER_FORMAT_LEFT, left);
	go_slist_free_custom (left, g_free);
	go_conf_set_str_list (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HEADER_FORMAT_MIDDLE, middle);
	go_slist_free_custom (middle, g_free);
	go_conf_set_str_list (
		root, PRINTSETUP_GCONF_DIR "/" PRINTSETUP_GCONF_HEADER_FORMAT_RIGHT, right);
	go_slist_free_custom (right, g_free);
}

void
gnm_gconf_set_gui_window_x (gnm_float val)
{
	prefs.horizontal_window_fraction = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_WINDOW_X, val);
}

void
gnm_gconf_set_gui_window_y (gnm_float val)
{
	prefs.vertical_window_fraction = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_WINDOW_Y, val);
}

void
gnm_gconf_set_gui_zoom (gnm_float val)
{
	prefs.zoom = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ZOOM, val);
}

void
gnm_gconf_set_default_font_size (gnm_float val)
{
	prefs.default_font.size = val;
	go_conf_set_double (
		root, GNM_CONF_FONT_DIR "/" GNM_CONF_FONT_SIZE, val);
}

void
gnm_gconf_set_default_font_name (char const *str)
{
	go_conf_set_string (root, GNM_CONF_FONT_DIR "/" GNM_CONF_FONT_NAME, str);
	if (prefs.default_font.name != str) {
		/* the const in the header is just a safety net */
		g_free ((char *) prefs.default_font.name);
		prefs.default_font.name = g_strdup (str);
	}
}

void
gnm_gconf_set_default_font_bold (gboolean val)
{
	prefs.default_font.is_bold = val;
	go_conf_set_bool (
		root, GNM_CONF_FONT_DIR "/" GNM_CONF_FONT_BOLD, val);
}

void
gnm_gconf_set_default_font_italic (gboolean val)
{
	prefs.default_font.is_italic = val;
	go_conf_set_bool (
		root, GNM_CONF_FONT_DIR "/" GNM_CONF_FONT_ITALIC, val);
}

void
gnm_gconf_set_hf_font (GnmStyle const *mstyle)
{
	GOConfNode *node;
	GnmStyle *old_style = (prefs.printer_decoration_font != NULL) ?
		prefs.printer_decoration_font :
		gnm_style_new_default ();

	prefs.printer_decoration_font = gnm_style_new_merged (old_style, mstyle);
	gnm_style_unref (old_style);

	node = go_conf_get_node (root, PRINTSETUP_GCONF_DIR);
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_SIZE))
		go_conf_set_double (node, PRINTSETUP_GCONF_HF_FONT_SIZE,
			gnm_style_get_font_size (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_NAME))
		go_conf_set_string (node, PRINTSETUP_GCONF_HF_FONT_NAME,
			gnm_style_get_font_name (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_BOLD))
		go_conf_set_bool (node, PRINTSETUP_GCONF_HF_FONT_BOLD,
			gnm_style_get_font_bold (mstyle));
	if (gnm_style_is_element_set (mstyle, MSTYLE_FONT_ITALIC))
		go_conf_set_bool (node, PRINTSETUP_GCONF_HF_FONT_ITALIC,
			gnm_style_get_font_italic (mstyle));
	go_conf_free_node (node);
}


void
gnm_gconf_set_max_descriptor_width (gint val)
{
	if (val < 1)
		val = 1;
	prefs.max_descriptor_width = val;
	go_conf_set_int (
		root, GNM_CONF_UNDO_DIR "/" GNM_CONF_UNDO_MAX_DESCRIPTOR_WIDTH, val);
}

void
gnm_gconf_set_sort_dialog_max_initial (gint val)
{
	if (val < 1)
		val = 1;
	prefs.sort_max_initial_clauses = val;
	go_conf_set_int (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DIALOG_MAX_INITIAL, val);
}

void
gnm_gconf_set_workbook_nsheets (gint val)
{
	if (val < 1)
		val = 1;
	prefs.initial_sheet_number = val;
	go_conf_set_int (root, GNM_CONF_WORKBOOK_NSHEETS, val);
}

void
gnm_gconf_set_xml_compression (gint val)
{
	if (val < 0)
		val = 0;
	prefs.xml_compression_level = val;
	go_conf_set_int (root, GNM_CONF_XML_COMPRESSION, val);
}

void
gnm_gconf_set_show_sheet_name (gboolean val)
{
	prefs.show_sheet_name = val;
	go_conf_set_bool (
		root, GNM_CONF_UNDO_DIR "/" GNM_CONF_UNDO_SHOW_SHEET_NAME,val != FALSE);
}

void
gnm_gconf_set_latex_use_utf8 (gboolean val)
{
	prefs.latex_use_utf8 = val;
	go_conf_set_bool (
		root, PLUGIN_GCONF_LATEX "/" PLUGIN_GCONF_LATEX_USE_UTF8, val != FALSE);
}

void
gnm_gconf_set_sort_retain_form (gboolean val)
{
	prefs.sort_default_retain_formats = val;
	go_conf_set_bool (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DEFAULT_RETAIN_FORM, val != FALSE);
}

void
gnm_gconf_set_sort_by_case (gboolean val)
{
	prefs.sort_default_by_case = val;
	go_conf_set_bool (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DEFAULT_BY_CASE, val != FALSE);
}

void
gnm_gconf_set_sort_has_header (gboolean val)
{
	prefs.sort_default_has_header = val;
	go_conf_set_bool (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DEFAULT_HAS_HEADER, val != FALSE);
}

void
gnm_gconf_set_sort_ascending (gboolean val)
{
	prefs.sort_default_ascending = val;
	go_conf_set_bool (
		root, GNM_CONF_SORT_DIR "/" GNM_CONF_SORT_DEFAULT_ASCENDING, val != FALSE);
}

void
gnm_gconf_set_gui_transition_keys (gboolean val)
{
	prefs.transition_keys = val;
	go_conf_set_bool (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_TRANSITION_KEYS, val != FALSE);
}

void
gnm_gconf_set_gui_livescrolling (gboolean val)
{
	prefs.live_scrolling = val;
	go_conf_set_bool (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_LIVESCROLLING, val != FALSE);
}

void
gnm_gconf_set_file_overwrite (gboolean val)
{
	prefs.file_overwrite_default_answer = val;
	go_conf_set_bool (
		root, GNM_CONF_FILE_DIR "/" GNM_CONF_FILE_OVERWRITE_DEFAULT, val != FALSE);
}

void
gnm_gconf_set_file_single_sheet_save (gboolean val)
{
	prefs.file_ask_single_sheet_save = val;
	go_conf_set_bool (
		root, GNM_CONF_FILE_DIR "/" GNM_CONF_FILE_SINGLE_SHEET_SAVE, val != FALSE);
}

void
gnm_gconf_set_gui_resolution_h (gnm_float val)
{
	if (val < 50)
		val = 50;
	if (val > 250)
		val = 250;
	prefs.horizontal_dpi = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_RES_H, val);
}

void
gnm_gconf_set_gui_resolution_v (gnm_float val)
{
	if (val < 50)
		val = 50;
	if (val > 250)
		val = 250;
	prefs.vertical_dpi = val;
	go_conf_set_double (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_RES_V, val);
}

gboolean
gnm_gconf_get_toolbar_visible (char const *name)
{
	gpointer pval;
	char *key = g_strconcat (GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_TOOLBARS "/",
				 name,
				 NULL);
	gboolean found, vis;

	found = g_hash_table_lookup_extended (prefs.toolbars,
					      key,
					      NULL, &pval);
	if (found) {
		vis = GPOINTER_TO_INT (pval);
	} else {
		vis = go_conf_load_bool (root, key, TRUE);
		g_hash_table_insert (prefs.toolbars,
				     g_strdup (name),
				     GINT_TO_POINTER (vis));
	}

	g_free (key);
	return vis;
}

void
gnm_gconf_set_toolbar_visible (char const *name, gboolean vis)
{
	char *key = g_strconcat (GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_TOOLBARS "/",
				 name,
				 NULL);
	vis = !!vis;
	g_hash_table_replace (prefs.toolbars,
			      g_strdup (name),
			      GINT_TO_POINTER (vis));
	go_conf_set_bool (root, key, vis);
	g_free (key);
}

/*
 * Actually returns a GtkPositionType.
 */
int
gnm_gconf_get_toolbar_position (char const *name)
{
	gpointer pval;
	char *key = g_strconcat (GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_TOOLBARS "/",
				 name, "-position",
				 NULL);
	gboolean found;
	int pos;
	static const int TOP = 2;

	found = g_hash_table_lookup_extended (prefs.toolbar_positions,
					      key,
					      NULL, &pval);
	if (found) {
		pos = GPOINTER_TO_INT (pval);
	} else {
		pos = go_conf_load_int (root, key, 0, 3, TOP);
		g_hash_table_insert (prefs.toolbar_positions,
				     g_strdup (name),
				     GINT_TO_POINTER (pos));
	}

	g_free (key);
	return pos;
}

void
gnm_gconf_set_toolbar_position (char const *name, int pos)
{
	char *key;

	g_return_if_fail (pos >= 0 && pos <= 3);

	key = g_strconcat (GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_TOOLBARS "/",
				 name, "-position",
				 NULL);
	g_hash_table_replace (prefs.toolbar_positions,
			      g_strdup (name),
			      GINT_TO_POINTER (pos));
	go_conf_set_int (root, key, pos);
	g_free (key);
}

void
gnm_gconf_set_unfocused_rs (gboolean val)
{
	prefs.unfocused_range_selection = val;
	go_conf_set_bool (
		root, DIALOGS_GCONF_DIR "/" DIALOGS_GCONF_UNFOCUSED_RS, val != FALSE);
}

void
gnm_gconf_set_enter_moves_dir (GODirection val)
{
	prefs.enter_moves_dir = val;
	go_conf_set_enum (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_ENTER_MOVES_DIR, GO_DIRECTION_TYPE, val);
}

void
gnm_gconf_set_autocomplete (gboolean val)
{
	prefs.auto_complete = val;
	go_conf_set_bool (
		root, GNM_CONF_GUI_DIR "/" GNM_CONF_GUI_ED_AUTOCOMPLETE, val != FALSE);
}
void
gnm_gconf_set_prefer_clipboard  (gboolean val)
{
	prefs.prefer_clipboard_selection = val;
	go_conf_set_bool (
		root, GNM_CONF_CUTANDPASTE_DIR "/" GNM_CONF_CUTANDPASTE_PREFER_CLIPBOARD, val != FALSE);
}

/***************************************************************************/

gchar *
go_conf_get_enum_as_str (GOConfNode *node, gchar const *key)
{
	return go_conf_get_string (node, key);
}
int
go_conf_load_enum (GOConfNode *node, gchar const *key, GType t, int default_val)
{
	int	 res;
	gchar   *val_str = go_conf_load_string (node, key);
	gboolean use_default = TRUE;

	if (NULL != val_str) {
		GEnumClass *enum_class = G_ENUM_CLASS (g_type_class_ref (t));
		GEnumValue *enum_value = g_enum_get_value_by_nick (enum_class, val_str);
		if (NULL == enum_value)
			enum_value = g_enum_get_value_by_name (enum_class, val_str);

		if (NULL != enum_value) {
			use_default = FALSE;
			res = enum_value->value;
		} else {
			g_warning ("Unknown value '%s' for %s", val_str, key);
		}

		g_type_class_unref (enum_class);
		g_free (val_str);

	}

	if (use_default) {
		d (g_warning ("Using default value '%d'", default_val));
		return default_val;
	}
	return res;
}

void
go_conf_set_enum (GOConfNode *node, gchar const *key, GType t, gint val)
{
	GEnumClass *enum_class = G_ENUM_CLASS (g_type_class_ref (t));
	GEnumValue *enum_value = g_enum_get_value (enum_class, val);
	go_conf_set_string (node, key, enum_value->value_nick);
	g_type_class_unref (enum_class);
}


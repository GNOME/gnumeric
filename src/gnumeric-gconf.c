/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * gnumeric-gconf.c:
 *
 *
 * Author:
 * 	Andreas J. Guelzow <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2002 Andreas J. Guelzow <aguelzow@taliesin.ca>
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
#include <gconf/gconf-client.h>
#include "gutils.h"

static GnmAppPrefs prefs;
GnmAppPrefs const *gnm_app_prefs = &prefs;

static GConfValue *
gnm_gconf_get (GConfClient *client, char const *key, GConfValueType t)
{
	GError *err = NULL;
	GConfValue *val = gconf_client_get (client, key, &err);

	if (err != NULL) {
		g_warning ("Unable to load key '%s' : because %s",
			   key, err->message);
		g_error_free (err);
		return NULL;
	}
	if (val == NULL) {
		g_warning ("Unable to load key '%s'", key);
		return NULL;
	}

	if (val->type != t) {
#if 1 /* gconf_value_type_to_string is internal */
		g_warning ("Expected `%d' got `%d' for key %s",
			t, val->type, key);
#else
		g_warning ("Expected `%s' got `%s' for key %s",
			gconf_value_type_to_string (t),
			gconf_value_type_to_string (val->type),
			key);
#endif
		gconf_value_free (val);
		return NULL;
	}

	return val;
}
static gboolean
gnm_gconf_get_bool (GConfClient *client, char const *key, gboolean default_val)
{
	gboolean res;
	GConfValue *val = gnm_gconf_get (client, key, GCONF_VALUE_BOOL);

	if (val != NULL) {
		res = gconf_value_get_bool (val);
		gconf_value_free (val);
	} else {
		g_warning ("Using default value '%s'", default_val ? "true" : "false");
		return default_val;
	}
	return res;
}

static int
gnm_gconf_get_int (GConfClient *client, char const *key,
		   int minima, int maxima, int default_val)
{
	int res = -1;
	GConfValue *val = gnm_gconf_get (client, key, GCONF_VALUE_INT);

	if (val != NULL) {
		res = gconf_value_get_int (val);
		gconf_value_free (val);
		if (res < minima || maxima < res) {
			g_warning ("Invalid value '%d' for %s.  If should be >= %d and <= %d",
				   res, key, minima, maxima);
			val = NULL;
		}
	}
	if (val == NULL) {
		g_warning ("Using default value '%d'", default_val);
		return default_val;
	}
	return res;
}

static double
gnm_gconf_get_float (GConfClient *client, char const *key,
		     double minima, double maxima, double default_val)
{
	double res = -1;
	GConfValue *val = gnm_gconf_get (client, key, GCONF_VALUE_FLOAT);

	if (val != NULL) {
		res = gconf_value_get_float (val);
		gconf_value_free (val);
		if (res < minima || maxima < res) {
			g_warning ("Invalid value '%g' for %s.  If should be >= %g and <= %g",
				   res, key, minima, maxima);
			val = NULL;
		}
	}
	if (val == NULL) {
		g_warning ("Using default value '%g'", default_val);
		return default_val;
	}
	return res;
}

static void
gnm_conf_init_essential (void)
{
	GConfClient *client = gnm_app_get_gconf_client ();

	prefs.file_history_max = gnm_gconf_get_int (client,
		GNUMERIC_GCONF_FILE_HISTORY_N, 0, 20, 4);
	prefs.file_history_files = gconf_client_get_list (client,
		GNUMERIC_GCONF_FILE_HISTORY_FILES, GCONF_VALUE_STRING, NULL);
	prefs.plugin_file_states = gconf_client_get_list (client,
		PLUGIN_GCONF_FILE_STATES, GCONF_VALUE_STRING, NULL);
	prefs.plugin_extra_dirs = gconf_client_get_list (client,
		PLUGIN_GCONF_EXTRA_DIRS, GCONF_VALUE_STRING, NULL);
	prefs.active_plugins = gconf_client_get_list (client,
		PLUGIN_GCONF_ACTIVE, GCONF_VALUE_STRING, NULL);
	prefs.activate_new_plugins = gnm_gconf_get_bool (client,
		PLUGIN_GCONF_ACTIVATE_NEW, TRUE);

	prefs.horizontal_dpi = gnm_gconf_get_float (client,
		GNUMERIC_GCONF_GUI_RES_H, 10., 1000., 96.);
	prefs.vertical_dpi = gnm_gconf_get_float (client,
		GNUMERIC_GCONF_GUI_RES_V, 10., 1000., 96.);
	prefs.initial_sheet_number = gnm_gconf_get_int (client,
		GNUMERIC_GCONF_WORKBOOK_NSHEETS, 1, 64, 3);
	prefs.horizontal_window_fraction = gnm_gconf_get_float (client,
		  GNUMERIC_GCONF_GUI_WINDOW_X, .1, 1., .6);
	prefs.vertical_window_fraction = gnm_gconf_get_float (client,
		  GNUMERIC_GCONF_GUI_WINDOW_Y, .1, 1., .6);
	prefs.zoom = gnm_gconf_get_float (client,
		  GNUMERIC_GCONF_GUI_ZOOM, .1, 5., 1.);
}

static gboolean
gnm_conf_init_extras (void)
{
	char *tmp;
	GConfClient *client = gnm_app_get_gconf_client ();

	prefs.num_of_recent_funcs = gnm_gconf_get_int (client,
		FUNCTION_SELECT_GCONF_NUM_OF_RECENT, 0, 40, 10);
	prefs.recent_funcs = gconf_client_get_list (client,
		FUNCTION_SELECT_GCONF_RECENT, GCONF_VALUE_STRING, NULL);

	prefs.auto_complete = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE, TRUE);
	prefs.transition_keys = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_GUI_ED_TRANSITION_KEYS, FALSE);
	prefs.live_scrolling = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_GUI_ED_LIVESCROLLING, TRUE);
	prefs.recalc_lag = gnm_gconf_get_int (client,
		GNUMERIC_GCONF_GUI_ED_RECALC_LAG, -5000, 5000, 200);
	prefs.show_sheet_name = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME, TRUE);
	prefs.max_descriptor_width = gnm_gconf_get_int (client,
		GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH, 5, 256, 15);
	prefs.undo_size = gnm_gconf_get_int (client,
		GNUMERIC_GCONF_UNDO_SIZE, 1, 1000000, 100000);
	prefs.undo_max_number = gnm_gconf_get_int (client,
		GNUMERIC_GCONF_UNDO_MAXNUM, 0, 10000, 100);

	prefs.autoformat.extra_dirs = gconf_client_get_list (client,
		AUTOFORMAT_GCONF_EXTRA_DIRS, GCONF_VALUE_STRING, NULL);
	tmp = gconf_client_get_string (client,
		AUTOFORMAT_GCONF_SYS_DIR, NULL);
	if (tmp == NULL)
		tmp = g_strdup ("autoformat-template");
	prefs.autoformat.sys_dir = gnumeric_sys_data_dir (tmp);
	g_free (tmp);
	tmp = gconf_client_get_string (client,
		AUTOFORMAT_GCONF_USR_DIR, NULL);
	if (tmp == NULL)
		tmp = g_strdup ("autoformat-template");
	prefs.autoformat.usr_dir = gnumeric_usr_dir (tmp);
	g_free (tmp);

	prefs.xml_compression_level = gnm_gconf_get_int (client,
		GNUMERIC_GCONF_XML_COMPRESSION, 0, 9, 9);
	prefs.file_overwrite_default_answer = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT, FALSE);
	prefs.file_ask_single_sheet_save = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE, TRUE);
	prefs.sort_default_by_case = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE, FALSE);
	prefs.sort_default_retain_formats = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM, TRUE);
	prefs.sort_default_ascending = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING, TRUE);
	prefs.sort_max_initial_clauses = gnm_gconf_get_int (client,
		GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL, 0, 256, 10);
	prefs.print_all_sheets = gnm_gconf_get_bool (client,
		PRINTSETUP_GCONF_ALL_SHEETS, TRUE);
	prefs.printer_config = gconf_client_get_string (client,
		PRINTSETUP_GCONF_PRINTER_CONFIG, NULL);
	prefs.printer_header = gconf_client_get_list (client,
		PRINTSETUP_GCONF_HEADER, GCONF_VALUE_STRING, NULL);
	prefs.printer_footer = gconf_client_get_list (client,
		PRINTSETUP_GCONF_FOOTER, GCONF_VALUE_STRING, NULL);
	prefs.printer_decoration_font_name = gconf_client_get_string (client,
		PRINTSETUP_GCONF_HF_FONT_NAME, NULL);
	if (prefs.printer_decoration_font_name == NULL)
		prefs.printer_decoration_font_name = g_strdup ("Sans");
	prefs.printer_decoration_font_size = gnm_gconf_get_float (client,
		PRINTSETUP_GCONF_HF_FONT_SIZE, 1., 100., 10.);
	prefs.printer_decoration_font_weight = ( gnm_gconf_get_bool
						(client,
						 PRINTSETUP_GCONF_HF_FONT_BOLD,
						 FALSE) 
						? GNOME_FONT_BOLD 
						: GNOME_FONT_REGULAR);
	prefs.printer_decoration_font_italic = gnm_gconf_get_bool (client,
		PRINTSETUP_GCONF_HF_FONT_ITALIC, FALSE);
	prefs.unfocused_range_selection = gnm_gconf_get_bool (client,
		DIALOGS_GCONF_UNFOCUSED_RS, TRUE);
	prefs.prefer_clipboard_selection = gnm_gconf_get_bool (client,
		GNUMERIC_GCONF_CUTANDPASTE_PREFER_CLIPBOARD, TRUE);
	prefs.latex_use_utf8 = gnm_gconf_get_bool (client,
		PLUGIN_GCONF_LATEX_USE_UTF8, TRUE); 
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
	gnm_conf_init_essential ();
	if (fast)
		g_timeout_add (1000, (GSourceFunc) gnm_conf_init_extras, NULL);
	else
		gnm_conf_init_extras ();
}

void
gnm_conf_sync (void)
{
	gconf_client_suggest_sync (gnm_app_get_gconf_client (), NULL);
}

void
gnm_gconf_set_plugin_file_states (GSList *list)
{
	g_return_if_fail (prefs.plugin_file_states != list);

	/* the const_casts are ok, the const in the header is just to keep
	 * people for doing stupid things */
	g_slist_foreach ((GSList *)prefs.plugin_file_states, (GFunc)g_free, NULL);
	g_slist_free ((GSList *)prefs.plugin_file_states);
	prefs.plugin_file_states = list;

	gconf_client_set_list (gnm_app_get_gconf_client (),
			       PLUGIN_GCONF_FILE_STATES,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_plugin_extra_dirs (GSList *list)
{
	g_return_if_fail (prefs.plugin_extra_dirs != list);

	/* the const_casts are ok, the const in the header is just to keep
	 * people for doing stupid things */
	g_slist_foreach ((GSList *)prefs.plugin_extra_dirs, (GFunc)g_free, NULL);
	g_slist_free ((GSList *)prefs.plugin_extra_dirs);
	prefs.plugin_extra_dirs = list;

	gconf_client_set_list (gnm_app_get_gconf_client (),
			       PLUGIN_GCONF_EXTRA_DIRS,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_active_plugins (GSList *list)
{
	gconf_client_set_list (gnm_app_get_gconf_client (),
			       PLUGIN_GCONF_ACTIVE,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_activate_new_plugins (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       PLUGIN_GCONF_ACTIVATE_NEW,
			       val, NULL);
}

void
gnm_gconf_set_recent_funcs (GSList *list)
{
	gconf_client_set_list (gnm_app_get_gconf_client (),
			       FUNCTION_SELECT_GCONF_RECENT,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_num_of_recent_funcs (guint val)
{
	gconf_client_set_int (gnm_app_get_gconf_client (),
			      FUNCTION_SELECT_GCONF_NUM_OF_RECENT,
			      (gint) val, NULL);
}


void
gnm_gconf_set_horizontal_dpi  (gnm_float val)
{
	gconf_client_set_float (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_GUI_RES_H,
			       val, NULL);
}

void
gnm_gconf_set_vertical_dpi  (gnm_float val)
{
	gconf_client_set_float (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_GUI_RES_V,
			       val, NULL);
}

void
gnm_gconf_set_auto_complete (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE,
			       val, NULL);
}

void
gnm_gconf_set_transition_keys (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_GUI_ED_TRANSITION_KEYS,
			       val, NULL);
}

void
gnm_gconf_set_live_scrolling (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_GUI_ED_LIVESCROLLING,
			       val, NULL);
}

void
gnm_gconf_set_recalc_lag (gint val)
{
	gconf_client_set_int (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_GUI_ED_RECALC_LAG,
			       val, NULL);
}

void
gnm_gconf_set_file_history_max (gint val)
{
	gconf_client_set_int (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_FILE_HISTORY_N,
			       val, NULL);
}

void
gnm_gconf_set_file_history_files (GSList *list)
{
	g_return_if_fail (prefs.file_history_files != list);

	/* the const_casts are ok, the const in the header is just to keep
	 * people for doing stupid things */
	g_slist_foreach ((GSList *)prefs.file_history_files, (GFunc)g_free, NULL);
	g_slist_free ((GSList *)prefs.file_history_files);
	prefs.file_history_files = list;
	gconf_client_set_list (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_FILE_HISTORY_FILES,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_initial_sheet_number (gint val)
{
	gconf_client_set_int (gnm_app_get_gconf_client (),
			      GNUMERIC_GCONF_WORKBOOK_NSHEETS,
			      val, NULL);
}

void
gnm_gconf_set_show_sheet_name (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
			       val, NULL);
}

void
gnm_gconf_set_max_descriptor_width (guint val)
{
	gconf_client_set_int (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
			       (gint) val, NULL);
}

void
gnm_gconf_set_undo_size (gint val)
{
	if (val < 1)
		val = 1;
	gconf_client_set_int (gnm_app_get_gconf_client (),
			      GNUMERIC_GCONF_UNDO_SIZE,
			      val, NULL);
}


void
gnm_gconf_set_undo_max_number (gint val)
{
	if (val < 1)
		val = 1;
	gconf_client_set_int (gnm_app_get_gconf_client (),
			      GNUMERIC_GCONF_UNDO_MAXNUM,
			      val, NULL);
}

void
gnm_gconf_set_autoformat_sys_dirs (char const * string)
{
	gconf_client_set_string (gnm_app_get_gconf_client (),
			       AUTOFORMAT_GCONF_SYS_DIR,
			       string, NULL);
}

void
gnm_gconf_set_autoformat_usr_dirs (char const * string)
{
	gconf_client_set_string (gnm_app_get_gconf_client (),
			       AUTOFORMAT_GCONF_USR_DIR,
			       string, NULL);
}

void
gnm_gconf_set_horizontal_window_fraction  (gnm_float val)
{
	gconf_client_set_float (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_GUI_WINDOW_X,
			       val, NULL);
}

void
gnm_gconf_set_vertical_window_fraction  (gnm_float val)
{
	gconf_client_set_float (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_GUI_WINDOW_Y,
			       val, NULL);
}

void
gnm_gconf_set_xml_compression_level (gint val)
{
	gconf_client_set_int (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_XML_COMPRESSION,
			       val, NULL);
}

void
gnm_gconf_set_file_overwrite_default_answer (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT,
			       val, NULL);
}

void
gnm_gconf_set_file_ask_single_sheet_save (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE,
			       val, NULL);
}

void
gnm_gconf_set_sort_default_by_case (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE,
			       val, NULL);
}


void
gnm_gconf_set_sort_default_retain_formats (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM,
			       val, NULL);
}

void
gnm_gconf_set_sort_default_ascending (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING,
			       val, NULL);
}

void
gnm_gconf_set_sort_max_initial_clauses (gint val)
{
	gconf_client_set_int (gnm_app_get_gconf_client (),
			      GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL,
			      val, NULL);
}

void
gnm_gconf_set_zoom  (gnm_float val)
{
	gconf_client_set_float (gnm_app_get_gconf_client (),
				GNUMERIC_GCONF_GUI_ZOOM,
				val, NULL);
}

void
gnm_gconf_set_unfocused_range_selection (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       DIALOGS_GCONF_UNFOCUSED_RS,
			       val, NULL);
}

void
gnm_gconf_set_prefer_clipboard_selection (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       GNUMERIC_GCONF_CUTANDPASTE_PREFER_CLIPBOARD,
			       val, NULL);
}


/*  PRINTSETUP  */
void
gnm_gconf_set_all_sheets (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       PRINTSETUP_GCONF_ALL_SHEETS,
			       val, NULL);
}

void
gnm_gconf_set_printer_config (gchar *str)
{
	gconf_client_set_string  (gnm_app_get_gconf_client (),
				  PRINTSETUP_GCONF_PRINTER_CONFIG,
				  str, NULL);
	g_free (prefs.printer_config);
	prefs.printer_config = str;
}

void
gnm_gconf_set_printer_header (gchar const *left, gchar const *middle, 
			      gchar const *right)
{
	GSList *list = NULL;
	list = g_slist_prepend (list, g_strdup (right));
	list = g_slist_prepend (list, g_strdup (middle));
	list = g_slist_prepend (list, g_strdup (left));
	gconf_client_set_list (gnm_app_get_gconf_client (),
			       PRINTSETUP_GCONF_HEADER,
			       GCONF_VALUE_STRING, list, NULL);
	g_slist_free_custom ((GSList *)prefs.printer_header, g_free);
	prefs.printer_header = list;
}

void
gnm_gconf_set_printer_footer (gchar const *left, gchar const *middle, 
			      gchar const *right)
{
	GSList *list = NULL;
	list = g_slist_prepend (list, g_strdup (right));
	list = g_slist_prepend (list, g_strdup (middle));
	list = g_slist_prepend (list, g_strdup (left));
	gconf_client_set_list (gnm_app_get_gconf_client (),
			       PRINTSETUP_GCONF_FOOTER,
			       GCONF_VALUE_STRING, list, NULL);
	g_slist_free_custom ((GSList *)prefs.printer_footer, g_free);
	prefs.printer_footer = list;
}

/*  LATEX  */
void
gnm_gconf_set_latex_use_utf8 (gboolean val)
{
	gconf_client_set_bool (gnm_app_get_gconf_client (),
			       PLUGIN_GCONF_LATEX_USE_UTF8,
			       val, NULL);
	prefs.latex_use_utf8 = val;
}


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

void
gnm_conf_init (void)
{
	char *tmp;
	GConfClient *client = application_get_gconf_client ();

	/* history */
	prefs.file_history_max = gconf_client_get_int (client,
		GNUMERIC_GCONF_FILE_HISTORY_N, NULL);
	prefs.file_history_files = gconf_client_get_list (client,
		GNUMERIC_GCONF_FILE_HISTORY_FILES, GCONF_VALUE_STRING, NULL);
	prefs.num_of_recent_funcs = gconf_client_get_int (client,
		FUNCTION_SELECT_GCONF_NUM_OF_RECENT, NULL);
	prefs.recent_funcs = gconf_client_get_list (client,
		FUNCTION_SELECT_GCONF_RECENT, GCONF_VALUE_STRING, NULL);
	if (prefs.file_history_max < 0 || 20 < prefs.file_history_max)
		prefs.file_history_max = 4;
	if (prefs.num_of_recent_funcs < 0 || 40  < prefs.num_of_recent_funcs)
		prefs.num_of_recent_funcs = 10;

	prefs.plugin_file_states = gconf_client_get_list (client,
		PLUGIN_GCONF_FILE_STATES, GCONF_VALUE_STRING, NULL);
	prefs.plugin_extra_dirs = gconf_client_get_list (client,
		PLUGIN_GCONF_EXTRA_DIRS, GCONF_VALUE_STRING, NULL);
	prefs.active_plugins = gconf_client_get_list (client,
		PLUGIN_GCONF_ACTIVE, GCONF_VALUE_STRING, NULL);
	prefs.activate_new_plugins = gconf_client_get_bool (client,
		PLUGIN_GCONF_ACTIVATE_NEW, NULL);

	prefs.horizontal_dpi = gconf_client_get_float (client,
		GNUMERIC_GCONF_GUI_RES_H, NULL);
	prefs.vertical_dpi = gconf_client_get_float (client,
		GNUMERIC_GCONF_GUI_RES_V, NULL);
	prefs.auto_complete = gconf_client_get_bool (client,
		GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE, NULL);
	prefs.live_scrolling = gconf_client_get_bool (client,
		GNUMERIC_GCONF_GUI_ED_LIVESCROLLING, NULL);
	prefs.recalc_lag = gconf_client_get_int (client,
		GNUMERIC_GCONF_GUI_ED_RECALC_LAG, NULL);
	prefs.initial_sheet_number = gconf_client_get_int (client,
		GNUMERIC_GCONF_WORKBOOK_NSHEETS, NULL);
	prefs.show_sheet_name = gconf_client_get_bool (client,
		GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME, NULL);
	prefs.max_descriptor_width = gconf_client_get_int (client,
		GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH, NULL);
	prefs.undo_size = gconf_client_get_int (client,
		GNUMERIC_GCONF_UNDO_SIZE, NULL);
	prefs.undo_max_number = gconf_client_get_int (client,
		GNUMERIC_GCONF_UNDO_MAXNUM, NULL);

	if (prefs.horizontal_dpi < 10 || 1000 < prefs.horizontal_dpi)
		prefs.horizontal_dpi = 96;
	if (prefs.vertical_dpi < 10 || 1000 < prefs.vertical_dpi)
		prefs.vertical_dpi = 96;
	if (prefs.initial_sheet_number < 1 || 64 < prefs.initial_sheet_number)
		prefs.initial_sheet_number = 3;
	if (prefs.max_descriptor_width < 5 || 256 < prefs.max_descriptor_width)
		prefs.max_descriptor_width = 15;
	if (prefs.undo_size  < 1 || 10000000 < prefs.undo_size)
		prefs.undo_size = 1000000;
	if (prefs.undo_max_number < 0 || 100000 < prefs.undo_max_number)
		prefs.undo_max_number = 100;

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

	prefs.horizontal_window_fraction = gconf_client_get_float (client,
		  GNUMERIC_GCONF_GUI_WINDOW_X, NULL);
	prefs.vertical_window_fraction = gconf_client_get_float (client,
		  GNUMERIC_GCONF_GUI_WINDOW_Y, NULL);
	prefs.zoom = gconf_client_get_float (client,
		  GNUMERIC_GCONF_GUI_ZOOM, NULL);
	if (prefs.horizontal_window_fraction < .1 || 1 < prefs.horizontal_window_fraction)
		prefs.horizontal_window_fraction = .6;
	if (prefs.vertical_window_fraction < .1 || 1 < prefs.vertical_window_fraction)
		prefs.vertical_window_fraction = .6;
	if (prefs.zoom < .1 || 5 < prefs.zoom)
		prefs.zoom = 1;

	prefs.xml_compression_level = gconf_client_get_int (client,
		GNUMERIC_GCONF_XML_COMPRESSION, NULL);
	prefs.import_uses_all_openers = gconf_client_get_bool (client,
		GNUMERIC_GCONF_FILE_IMPORT_USES_ALL_OP, NULL);
	prefs.file_overwrite_default_answer = gconf_client_get_bool (client,
		GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT, NULL);
	prefs.file_ask_single_sheet_save = gconf_client_get_bool (client,
		GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE, NULL);
	if (prefs.xml_compression_level < 0 || 9 < prefs.xml_compression_level)
		prefs.xml_compression_level = 9;

	prefs.sort_default_by_case = gconf_client_get_bool (client,
		GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE, NULL);
	prefs.sort_default_retain_formats = gconf_client_get_bool (client,
		GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM, NULL);
	prefs.sort_default_ascending = gconf_client_get_bool (client,
		GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING, NULL);
	prefs.sort_max_initial_clauses = gconf_client_get_int (client,
		GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL, NULL);

	prefs.print_all_sheets = gconf_client_get_bool (client,
		PRINTSETUP_GCONF_ALL_SHEETS, NULL);
	prefs.unfocused_range_selection = gconf_client_get_bool (client,
		DIALOGS_GCONF_UNFOCUSED_RS, NULL);
	prefs.printer = gconf_client_get_string (client,
		PRINTING_GCONF_PRINTER, NULL);
	prefs.printer_backend = gconf_client_get_string (client,
		PRINTING_GCONF_BACKEND, NULL);
	prefs.printer_filename = gconf_client_get_string (client,
		PRINTING_GCONF_FILENAME, NULL);
	prefs.printer_lpr_P = gconf_client_get_string (client,
		PRINTING_GCONF_BACKEND_PRINTER, NULL);
}

void
gnm_conf_sync (void)
{
	gconf_client_suggest_sync (application_get_gconf_client (), NULL);
}

guint
gnm_gconf_rm_notification (guint id)
{
	gconf_client_notify_remove (application_get_gconf_client (), id);
	return 0;
}

void
gnm_gconf_set_plugin_file_states (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       PLUGIN_GCONF_FILE_STATES,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_plugin_extra_dirs (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       PLUGIN_GCONF_EXTRA_DIRS,
			       GCONF_VALUE_STRING, list, NULL);
}

guint
gnm_gconf_add_notification_plugin_directories (GConfClientNotifyFunc func, gpointer data)
{
	return gconf_client_notify_add (application_get_gconf_client (), PLUGIN_GCONF_EXTRA_DIRS,
					func, data, NULL, NULL);
}

void
gnm_gconf_set_active_plugins (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       PLUGIN_GCONF_ACTIVE,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_activate_new_plugins (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       PLUGIN_GCONF_ACTIVATE_NEW,
			       val, NULL);
}

void
gnm_gconf_set_recent_funcs (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       FUNCTION_SELECT_GCONF_RECENT,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_num_of_recent_funcs (guint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			      FUNCTION_SELECT_GCONF_NUM_OF_RECENT,
			      (gint) val, NULL);
}


void
gnm_gconf_set_horizontal_dpi  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_RES_H,
			       val, NULL);
}

void
gnm_gconf_set_vertical_dpi  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_RES_V,
			       val, NULL);
}

void
gnm_gconf_set_auto_complete (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE,
			       val, NULL);
}

void
gnm_gconf_set_live_scrolling (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_ED_LIVESCROLLING,
			       val, NULL);
}

void
gnm_gconf_set_recalc_lag (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_ED_RECALC_LAG,
			       val, NULL);
}

void
gnm_gconf_set_file_history_max (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_HISTORY_N,
			       val, NULL);
}

void
gnm_gconf_set_file_history_files (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_HISTORY_FILES,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_initial_sheet_number (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			      GNUMERIC_GCONF_WORKBOOK_NSHEETS,
			      val, NULL);
}

void
gnm_gconf_set_show_sheet_name (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
			       val, NULL);
}

void
gnm_gconf_set_max_descriptor_width (guint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			       GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
			       (gint) val, NULL);
}

void
gnm_gconf_set_undo_size (gint val)
{
	if (val < 1)
		val = 1;
	gconf_client_set_int (application_get_gconf_client (), 
			      GNUMERIC_GCONF_UNDO_SIZE,
			      val, NULL);
}


void
gnm_gconf_set_undo_max_number (gint val)
{
	if (val < 1)
		val = 1;
	gconf_client_set_int (application_get_gconf_client (), 
			      GNUMERIC_GCONF_UNDO_MAXNUM,
			      val, NULL);
}

void
gnm_gconf_set_autoformat_extra_dirs (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       AUTOFORMAT_GCONF_EXTRA_DIRS,
			       GCONF_VALUE_STRING, list, NULL);
}

void
gnm_gconf_set_autoformat_sys_dirs (char const * string)
{
	gconf_client_set_string (application_get_gconf_client (), 
			       AUTOFORMAT_GCONF_SYS_DIR,
			       string, NULL);
}

void
gnm_gconf_set_autoformat_usr_dirs (char const * string)
{
	gconf_client_set_string (application_get_gconf_client (), 
			       AUTOFORMAT_GCONF_USR_DIR,
			       string, NULL);
}

void
gnm_gconf_set_horizontal_window_fraction  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_WINDOW_X,
			       val, NULL);
}

void
gnm_gconf_set_vertical_window_fraction  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_WINDOW_Y,
			       val, NULL);
}

void
gnm_gconf_set_xml_compression_level (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			       GNUMERIC_GCONF_XML_COMPRESSION,
			       val, NULL);
}

void
gnm_gconf_set_import_uses_all_openers (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_IMPORT_USES_ALL_OP,
			       val, NULL);
}

void
gnm_gconf_set_file_overwrite_default_answer (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT,
			       val, NULL);
}

void
gnm_gconf_set_file_ask_single_sheet_save (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE,
			       val, NULL);
}

void
gnm_gconf_set_sort_default_by_case (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE,
			       val, NULL);
}


void
gnm_gconf_set_sort_default_retain_formats (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM,
			       val, NULL);
}

void
gnm_gconf_set_sort_default_ascending (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING,
			       val, NULL);
}

void
gnm_gconf_set_sort_max_initial_clauses (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			      GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL,
			      val, NULL);
}

void
gnm_gconf_set_zoom  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
				GNUMERIC_GCONF_GUI_ZOOM,
				val, NULL);
}

void
gnm_gconf_set_all_sheets (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       PRINTSETUP_GCONF_ALL_SHEETS,
			       val, NULL);
}

void
gnm_gconf_set_unfocused_range_selection (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       DIALOGS_GCONF_UNFOCUSED_RS,
			       val, NULL);
}
void     gnm_gconf_set_printer (gchar *str)
{
	if (str == NULL)
		str = g_strdup ("");
	gconf_client_set_string  (application_get_gconf_client (), 
				  PRINTING_GCONF_PRINTER,
                                  str, NULL);
	g_free (str);
}
void     gnm_gconf_set_printer_backend (gchar *str)
{
	if (str == NULL)
		str = g_strdup ("");
	gconf_client_set_string  (application_get_gconf_client (), 
				  PRINTING_GCONF_BACKEND,
                                  str, NULL);
	g_free (str);
}
void     gnm_gconf_set_printer_filename (gchar *str)
{
	if (str == NULL)
		str = g_strdup ("");
	gconf_client_set_string  (application_get_gconf_client (), 
				  PRINTING_GCONF_FILENAME,
                                  str, NULL);
	g_free (str);
}

gchar   *gnm_gconf_get_printer_command (void)
{
	return gconf_client_get_string (application_get_gconf_client (), 
					PRINTING_GCONF_COMMAND,
					NULL);
}

void     gnm_gconf_set_printer_command (gchar *str)
{
	if (str == NULL)
		str = g_strdup ("");
	gconf_client_set_string  (application_get_gconf_client (), 
				  PRINTING_GCONF_COMMAND,
                                  str, NULL);
	g_free (str);
}
void     gnm_gconf_set_printer_lpr_P (gchar *str)
{
	if (str == NULL)
		str = g_strdup ("");
	gconf_client_set_string  (application_get_gconf_client (), 
				  PRINTING_GCONF_BACKEND_PRINTER,
                                  str, NULL);
	g_free (str);
}


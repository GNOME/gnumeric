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

void
gnm_conf_sync (void)
{
	gconf_client_suggest_sync (application_get_gconf_client (), NULL);
}

GSList *
gnm_gconf_get_plugin_file_states (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      PLUGIN_GCONF_FILE_STATES,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_plugin_file_states (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       PLUGIN_GCONF_FILE_STATES,
			       GCONF_VALUE_STRING, list, NULL);
}

GSList *
gnm_gconf_get_plugin_extra_dirs (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      PLUGIN_GCONF_EXTRA_DIRS,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_plugin_extra_dirs (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       PLUGIN_GCONF_EXTRA_DIRS,
			       GCONF_VALUE_STRING, list, NULL);
}

GSList *
gnm_gconf_get_active_plugins (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      PLUGIN_GCONF_ACTIVE,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_active_plugins (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       PLUGIN_GCONF_ACTIVE,
			       GCONF_VALUE_STRING, list, NULL);
}

GSList *
gnm_gconf_get_known_plugins (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      PLUGIN_GCONF_KNOWN,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_known_plugins (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       PLUGIN_GCONF_KNOWN,
			       GCONF_VALUE_STRING, list, NULL);
}

gboolean
gnm_gconf_get_activate_new_plugins (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      PLUGIN_GCONF_ACTIVATE_NEW,
				      NULL);
}

void
gnm_gconf_set_activate_new_plugins (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       PLUGIN_GCONF_ACTIVATE_NEW,
			       val, NULL);
}

GSList *
gnm_gconf_get_recent_funcs (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      FUNCTION_SELECT_GCONF_RECENT,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_recent_funcs (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       FUNCTION_SELECT_GCONF_RECENT,
			       GCONF_VALUE_STRING, list, NULL);
}

guint
gnm_gconf_get_num_of_recent_funcs (void)
{
	gint num = gconf_client_get_int (application_get_gconf_client (), 
				      FUNCTION_SELECT_GCONF_NUM_OF_RECENT,
				      NULL);
	if (num < 0)
		return 0;
	return (guint) num;
}

void
gnm_gconf_set_num_of_recent_funcs (guint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			      FUNCTION_SELECT_GCONF_NUM_OF_RECENT,
			      (gint) val, NULL);
}


gboolean
gnm_gconf_get_autocorrect_init_caps (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      AUTOCORRECT_INIT_CAPS,
				      NULL);
}

void
gnm_gconf_set_autocorrect_init_caps (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       AUTOCORRECT_INIT_CAPS,
			       val, NULL);
}

gboolean
gnm_gconf_get_autocorrect_first_letter (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      AUTOCORRECT_FIRST_LETTER,
				      NULL);
}

void
gnm_gconf_set_autocorrect_first_letter (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       AUTOCORRECT_FIRST_LETTER,
			       val, NULL);
}

gboolean
gnm_gconf_get_autocorrect_names_of_days (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      AUTOCORRECT_NAMES_OF_DAYS,
				      NULL);
}

void
gnm_gconf_set_autocorrect_names_of_days (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       AUTOCORRECT_NAMES_OF_DAYS,
			       val, NULL);
}

gboolean
gnm_gconf_get_autocorrect_replace (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      AUTOCORRECT_NAMES_OF_DAYS,
				      NULL);
}

void
gnm_gconf_set_autocorrect_replace (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       AUTOCORRECT_NAMES_OF_DAYS,
			       val, NULL);
}

GSList *
gnm_gconf_get_autocorrect_init_caps_exceptions (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      AUTOCORRECT_INIT_CAPS_LIST,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_autocorrect_init_caps_exceptions (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       AUTOCORRECT_INIT_CAPS_LIST,
			       GCONF_VALUE_STRING, list, NULL);
}

GSList *
gnm_gconf_get_autocorrect_first_letter_exceptions (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      AUTOCORRECT_FIRST_LETTER_LIST,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_autocorrect_first_letter_exceptions (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       AUTOCORRECT_FIRST_LETTER_LIST,
			       GCONF_VALUE_STRING, list, NULL);
}

guint
gnm_gconf_add_notification_autocorrect (GConfClientNotifyFunc func)
{
	return gconf_client_notify_add (application_get_gconf_client (), AUTOCORRECT_DIRECTORY,
					func, NULL, NULL, NULL);
}

guint
gnm_gconf_rm_notification_autocorrect (guint id)
{
	gconf_client_notify_remove (application_get_gconf_client (), id);
	return 0;
}

gnum_float
gnm_gconf_get_horizontal_dpi (void)
{
	gnum_float val =  gconf_client_get_float (application_get_gconf_client (), 
						  GNUMERIC_GCONF_GUI_RES_H,
						  NULL);
	return ((val == 0.0) ? 96.0 : val);
}

void
gnm_gconf_set_horizontal_dpi  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_RES_H,
			       val, NULL);
}

gnum_float
gnm_gconf_get_vertical_dpi (void)
{
	gnum_float val =  gconf_client_get_float (application_get_gconf_client (), 
						  GNUMERIC_GCONF_GUI_RES_V,
						  NULL);
	return ((val == 0.0) ? 96.0 : val);
}

void
gnm_gconf_set_vertical_dpi  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_RES_V,
			       val, NULL);
}

gboolean
gnm_gconf_get_auto_complete (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE,
				      NULL);
}

void
gnm_gconf_set_auto_complete (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_ED_AUTOCOMPLETE,
			       val, NULL);
}

gboolean
gnm_gconf_get_live_scrolling (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_GUI_ED_LIVESCROLLING,
				      NULL);
}

void
gnm_gconf_set_live_scrolling (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_ED_LIVESCROLLING,
			       val, NULL);
}

gint
gnm_gconf_get_recalc_lag (void)
{
	gint val = gconf_client_get_int (application_get_gconf_client (), 
					 GNUMERIC_GCONF_GUI_ED_RECALC_LAG,
					 NULL);
	return ((val == 0) ? 200 : val);
}

void
gnm_gconf_set_recalc_lag (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_ED_RECALC_LAG,
			       val, NULL);
}

gint
gnm_gconf_get_file_history_max (void)
{
	gint val = gconf_client_get_int (application_get_gconf_client (), 
					 GNUMERIC_GCONF_FILE_HISTORY_N,
					 NULL);
	return ((val < 0) ? 4 : val);
}

void
gnm_gconf_set_file_history_max (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_HISTORY_N,
			       val, NULL);
}

GSList *
gnm_gconf_get_file_history_files (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      GNUMERIC_GCONF_FILE_HISTORY_FILES,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_file_history_files (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_HISTORY_FILES,
			       GCONF_VALUE_STRING, list, NULL);
}

gint
gnm_gconf_get_initial_sheet_number (void)
{
	gint val = gconf_client_get_int (application_get_gconf_client (), 
					 GNUMERIC_GCONF_WORKBOOK_NSHEETS,
					 NULL);
	return ((val <= 0) ? 1 : val);
}

void
gnm_gconf_set_initial_sheet_number (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			      GNUMERIC_GCONF_WORKBOOK_NSHEETS,
			      val, NULL);
}

gboolean
gnm_gconf_get_show_sheet_name (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
				      NULL);
}

void
gnm_gconf_set_show_sheet_name (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_UNDO_SHOW_SHEET_NAME,
			       val, NULL);
}

guint
gnm_gconf_get_max_descriptor_width (void)
{
	gint val = gconf_client_get_int (application_get_gconf_client (), 
					 GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
					 NULL);
	return ((val < 3) ? 10 : (guint) val);
}

void
gnm_gconf_set_max_descriptor_width (guint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			       GNUMERIC_GCONF_UNDO_MAX_DESCRIPTOR_WIDTH,
			       (gint) val, NULL);
}

gint
gnm_gconf_get_undo_size (void)
{
	gint val = gconf_client_get_int (application_get_gconf_client (), 
					 GNUMERIC_GCONF_UNDO_SIZE,
					 NULL);
	return ((val < 0) ? 0 : val);
}

void
gnm_gconf_set_undo_size (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			      GNUMERIC_GCONF_UNDO_SIZE,
			      val, NULL);
}


gint
gnm_gconf_get_undo_max_number (void)
{
	gint val = gconf_client_get_int (application_get_gconf_client (), 
					 GNUMERIC_GCONF_UNDO_MAXNUM,
					 NULL);
	return ((val <= 0) ? 1 : val);
}

void
gnm_gconf_set_undo_max_number (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			      GNUMERIC_GCONF_UNDO_MAXNUM,
			      val, NULL);
}

GSList *
gnm_gconf_get_autoformat_extra_dirs (void)
{
	return gconf_client_get_list (application_get_gconf_client (), 
				      AUTOFORMAT_GCONF_EXTRA_DIRS,
				      GCONF_VALUE_STRING, NULL);
}

void
gnm_gconf_set_autoformat_extra_dirs (GSList *list)
{
	gconf_client_set_list (application_get_gconf_client (), 
			       AUTOFORMAT_GCONF_EXTRA_DIRS,
			       GCONF_VALUE_STRING, list, NULL);
}

char *
gnm_gconf_get_autoformat_sys_dirs (void)
{
	char *directory;
	char *conf_value = gconf_client_get_string (application_get_gconf_client (), 
						    AUTOFORMAT_GCONF_SYS_DIR,
						    NULL);
	if (conf_value) {
		directory = gnumeric_sys_data_dir (conf_value);
		g_free (conf_value);
		return directory;
	} else 
		return gnumeric_sys_data_dir ("autoformat-template");
}

void
gnm_gconf_set_autoformat_sys_dirs (char const * string)
{
	gconf_client_set_string (application_get_gconf_client (), 
			       AUTOFORMAT_GCONF_SYS_DIR,
			       string, NULL);
}

char *
gnm_gconf_get_autoformat_usr_dirs (void)
{
	char *directory;
	char *conf_value = gconf_client_get_string (application_get_gconf_client (), 
						    AUTOFORMAT_GCONF_USR_DIR,
						    NULL);
	if (conf_value) {
		directory = gnumeric_usr_dir (conf_value);
		g_free (conf_value);
		return directory;
	} else 
		return gnumeric_usr_dir ("autoformat-template");
}

void
gnm_gconf_set_autoformat_usr_dirs (char const * string)
{
	gconf_client_set_string (application_get_gconf_client (), 
			       AUTOFORMAT_GCONF_USR_DIR,
			       string, NULL);
}

gnum_float
gnm_gconf_get_horizontal_window_fraction (void)
{
	gnum_float val =  gconf_client_get_float (application_get_gconf_client (), 
						  GNUMERIC_GCONF_GUI_WINDOW_X,
						  NULL);
	val = MIN (val, 1.0);
	val = MAX (0.25, val);
	return val;
}

void
gnm_gconf_set_horizontal_window_fraction  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_WINDOW_X,
			       val, NULL);
}

gnum_float
gnm_gconf_get_vertical_window_fraction (void)
{
	gnum_float val =  gconf_client_get_float (application_get_gconf_client (), 
						  GNUMERIC_GCONF_GUI_WINDOW_Y,
						  NULL);
	val = MIN (val, 1.0);
	val = MAX (0.25, val);
	return val;
}

void
gnm_gconf_set_vertical_window_fraction  (gnum_float val)
{
	gconf_client_set_float (application_get_gconf_client (), 
			       GNUMERIC_GCONF_GUI_WINDOW_Y,
			       val, NULL);
}

gint
gnm_gconf_get_xml_compression_level (void)
{
	gint val = gconf_client_get_int (application_get_gconf_client (), 
					 GNUMERIC_GCONF_XML_COMPRESSION,
					 NULL);
	val = MIN (val, 10);
	val = MAX (0, val);
	return val;
}

void
gnm_gconf_set_xml_compression_level (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			       GNUMERIC_GCONF_XML_COMPRESSION,
			       val, NULL);
}

gboolean
gnm_gconf_get_import_uses_all_openers (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_FILE_IMPORT_USES_ALL_OP,
				      NULL);
}

void
gnm_gconf_set_import_uses_all_openers (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_IMPORT_USES_ALL_OP,
			       val, NULL);
}

gboolean
gnm_gconf_get_file_overwrite_default_answer (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT,
				      NULL);
}

void
gnm_gconf_set_file_overwrite_default_answer (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_OVERWRITE_DEFAULT,
			       val, NULL);
}

gboolean
gnm_gconf_get_file_ask_single_sheet_save (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE,
				      NULL);
}

void
gnm_gconf_set_file_ask_single_sheet_save (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_FILE_SINGLE_SHEET_SAVE,
			       val, NULL);
}

gboolean
gnm_gconf_get_sort_default_by_case (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE,
				      NULL);
}

void
gnm_gconf_set_sort_default_by_case (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_SORT_DEFAULT_BY_CASE,
			       val, NULL);
}


gboolean
gnm_gconf_get_sort_default_retain_formats (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM,
				      NULL);
}

void
gnm_gconf_set_sort_default_retain_formats (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_SORT_DEFAULT_RETAIN_FORM,
			       val, NULL);
}

gboolean
gnm_gconf_get_sort_default_ascending (void)
{
	return gconf_client_get_bool (application_get_gconf_client (), 
				      GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING,
				      NULL);
}

void
gnm_gconf_set_sort_default_ascending (gboolean val)
{
	gconf_client_set_bool (application_get_gconf_client (), 
			       GNUMERIC_GCONF_SORT_DEFAULT_ASCENDING,
			       val, NULL);
}

gint
gnm_gconf_get_sort_max_initial_clauses (void)
{
	gint val = gconf_client_get_int (application_get_gconf_client (), 
					 GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL,
					 NULL);
	val = MIN (val, 50);
	val = MAX (0, val);
	return val;
}

void
gnm_gconf_set_sort_max_initial_clauses (gint val)
{
	gconf_client_set_int (application_get_gconf_client (), 
			      GNUMERIC_GCONF_SORT_DIALOG_MAX_INITIAL,
			      val, NULL);
}


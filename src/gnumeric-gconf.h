#ifndef GNUMERIC_GCONF_H
#define GNUMERIC_GCONF_H

#include <gconf/gconf-client.h>
#include <numbers.h>

void     gnm_conf_sync (void);

/* autocorrect */
guint    gnm_gconf_add_notification_autocorrect (GConfClientNotifyFunc func);
guint    gnm_gconf_rm_notification_autocorrect (guint id);

gboolean gnm_gconf_get_autocorrect_init_caps (void);
void     gnm_gconf_set_autocorrect_init_caps (gboolean val);

gboolean gnm_gconf_get_autocorrect_first_letter (void);
void     gnm_gconf_set_autocorrect_first_letter (gboolean val);

gboolean gnm_gconf_get_autocorrect_names_of_days (void);
void     gnm_gconf_set_autocorrect_names_of_days (gboolean val);

gboolean gnm_gconf_get_autocorrect_replace (void);
void     gnm_gconf_set_autocorrect_replace (gboolean val);

GSList * gnm_gconf_get_autocorrect_init_caps_exceptions (void);
void     gnm_gconf_set_autocorrect_init_caps_exceptions (GSList *list);

GSList * gnm_gconf_get_autocorrect_first_letter_exceptions (void);
void     gnm_gconf_set_autocorrect_first_letter_exceptions (GSList *list);

/* autoformat */
GSList * gnm_gconf_get_autoformat_extra_dirs (void);
void     gnm_gconf_set_autoformat_extra_dirs (GSList *list);

char *   gnm_gconf_get_autoformat_sys_dirs (void);
void     gnm_gconf_set_autoformat_sys_dirs (char const * string);

char *   gnm_gconf_get_autoformat_usr_dirs (void);
void     gnm_gconf_set_autoformat_usr_dirs (char const * string);

/* file history */
gint     gnm_gconf_get_file_history_max (void);
void     gnm_gconf_set_file_history_max (gint val);

GSList * gnm_gconf_get_file_history_files (void);
void     gnm_gconf_set_file_history_files (GSList *list);

/* plugins */
GSList * gnm_gconf_get_plugin_file_states (void);
void     gnm_gconf_set_plugin_file_states (GSList *list);

GSList * gnm_gconf_get_plugin_extra_dirs (void);
void     gnm_gconf_set_plugin_extra_dirs (GSList *list);

GSList * gnm_gconf_get_active_plugins (void);
void     gnm_gconf_set_active_plugins (GSList *list);

GSList * gnm_gconf_get_known_plugins (void);
void     gnm_gconf_set_known_plugins (GSList *list);

gboolean gnm_gconf_get_activate_new_plugins (void);
void     gnm_gconf_set_activate_new_plugins (gboolean val);

/* undo */
gboolean gnm_gconf_get_show_sheet_name (void);
void     gnm_gconf_set_show_sheet_name (gboolean val);

guint    gnm_gconf_get_max_descriptor_width (void);
void     gnm_gconf_set_max_descriptor_width (guint val);

gint     gnm_gconf_get_undo_size (void);
void     gnm_gconf_set_undo_size (gint val);

gint     gnm_gconf_get_undo_max_number (void);
void     gnm_gconf_set_undo_max_number (gint val);

/* new workbooks */
gint     gnm_gconf_get_initial_sheet_number (void);
void     gnm_gconf_set_initial_sheet_number (gint val);

gnum_float gnm_gconf_get_horizontal_window_fraction (void);
void     gnm_gconf_set_horizontal_window_fraction  (gnum_float val);

gnum_float gnm_gconf_get_vertical_window_fraction (void);
void     gnm_gconf_set_vertical_window_fraction  (gnum_float val);

gnum_float gnm_gconf_get_zoom (void);
void     gnm_gconf_set_zoom  (gnum_float val);

/* xml/files */
gint     gnm_gconf_get_xml_compression_level (void);
void     gnm_gconf_set_xml_compression_level (gint val);

GSList * gnm_gconf_get_recent_funcs (void);
void     gnm_gconf_set_recent_funcs (GSList *list);

guint    gnm_gconf_get_num_of_recent_funcs (void);
void     gnm_gconf_set_num_of_recent_funcs (guint val);

gboolean gnm_gconf_get_import_uses_all_openers (void);
void     gnm_gconf_set_import_uses_all_openers (gboolean val);

gboolean gnm_gconf_get_file_overwrite_default_answer (void);
void     gnm_gconf_set_file_overwrite_default_answer (gboolean val);

gboolean gnm_gconf_get_file_ask_single_sheet_save (void);
void     gnm_gconf_set_file_ask_single_sheet_save (gboolean val);

/* sort */

gboolean gnm_gconf_get_sort_default_by_case (void);
void     gnm_gconf_set_sort_default_by_case (gboolean val);

gboolean gnm_gconf_get_sort_default_retain_formats (void);
void     gnm_gconf_set_sort_default_retain_formats (gboolean val);

gboolean gnm_gconf_get_sort_default_ascending (void);
void     gnm_gconf_set_sort_default_ascending (gboolean val);

gint     gnm_gconf_get_sort_max_initial_clauses (void);
void     gnm_gconf_set_sort_max_initial_clauses (gint val);

/* others */
gnum_float gnm_gconf_get_horizontal_dpi (void);
void     gnm_gconf_set_horizontal_dpi  (gnum_float val);

gnum_float gnm_gconf_get_vertical_dpi (void);
void     gnm_gconf_set_vertical_dpi  (gnum_float val);

gboolean gnm_gconf_get_auto_complete (void);
void     gnm_gconf_set_auto_complete (gboolean val);

gboolean gnm_gconf_get_live_scrolling (void);
void     gnm_gconf_set_live_scrolling (gboolean val);

gint     gnm_gconf_get_recalc_lag (void);
void     gnm_gconf_set_recalc_lag (gint val);

#endif /* GNUMERIC_GRAPH_H */
